#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <initializer_list>
#include <string>
#include <vector>

#include "frame_generation_model.h"
#include "frame_generation_telemetry.h"
#include "third_party/nvidia/nvOpticalFlowD3D11.h"

// No es DLSS ni pretende hacerse pasar por el. Esta clase usa el motor OFA que
// NVIDIA instala con el driver, calcula flujo hacia adelante y hacia atras, y
// reconstruye el punto medio con un shader pequeño. Asi no necesitamos colar
// DLLs cerradas en el ZIP ni pedirle profundidad al HDMI (porque no existe).
class NvidiaFrameGenerator {
public:
    ~NvidiaFrameGenerator() { Shutdown(); }

    HRESULT Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
                       UINT width, UINT height, FrameGenerationMode mode) {
        Shutdown();
        if (!device || !context || width < 16 || height < 16 ||
            mode == FrameGenerationMode::Off) return E_INVALIDARG;

        device_ = device;
        context_ = context;
        device_->AddRef();
        context_->AddRef();
        width_ = width;
        height_ = height;
        requestedMode_ = mode;
        bypassMode_ = FrameGenBypassMode();
        sourceTextureCount_ = bypassMode_ == 3 ? 3 : 2;
        lineCleanerLevel_ = FrameGenEnvDefaultOn(L"DAYAAH_FG_LINE_CLEANER")
            ? FrameGenerationLineCleanerLevel(mode) : 0;
        tuning_ = LoadFrameGenTuning();
        debugMode_ = FrameGenDebugMode();
        telemetry_ = FrameGenEnvFlag(L"DAYAAH_FG_TELEMETRY");

        HRESULT hr = LoadApi();
        if (FAILED(hr)) return Abort(hr);

        NV_OF_STATUS status = api_.nvCreateOpticalFlowD3D11(device_, context_, &ofHandle_);
        if (status != NV_OF_SUCCESS || !ofHandle_) return Abort(StatusFailure(status, L"nvCreateOpticalFlowD3D11"));

        std::vector<int> grids;
        uint32_t gridCount = 0;
        status = api_.nvOFGetCaps(ofHandle_, NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES,
                                  nullptr, &gridCount);
        if (status == NV_OF_SUCCESS && gridCount > 0 && gridCount <= 16) {
            std::vector<uint32_t> values(gridCount);
            status = api_.nvOFGetCaps(ofHandle_, NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES,
                                      values.data(), &gridCount);
            if (status == NV_OF_SUCCESS) {
                for (uint32_t i = 0; i < gridCount; ++i) grids.push_back(static_cast<int>(values[i]));
            }
        }
        if (status != NV_OF_SUCCESS) return Abort(StatusFailure(status, L"nvOFGetCaps"));

        gridSize_ = SelectFrameGenerationGrid(FrameGenerationRequestedGrid(mode), grids);
        if (!gridSize_) {
            error_ = L"La GPU no anuncio una rejilla Optical Flow compatible (1x1, 2x2 o 4x4).";
            return Abort(E_NOTIMPL);
        }
        flowWidth_ = (width_ + static_cast<UINT>(gridSize_) - 1) / static_cast<UINT>(gridSize_);
        flowHeight_ = (height_ + static_cast<UINT>(gridSize_) - 1) / static_cast<UINT>(gridSize_);

        const std::vector<DXGI_FORMAT> inputFormats = SurfaceFormats(NV_OF_BUFFER_USAGE_INPUT);
        const std::vector<DXGI_FORMAT> outputFormats = SurfaceFormats(NV_OF_BUFFER_USAGE_OUTPUT);
        const std::vector<DXGI_FORMAT> costFormats = SurfaceFormats(NV_OF_BUFFER_USAGE_COST);
        inputSurfaceFormat_ = FirstSupported(
            inputFormats, {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM});
        outputSurfaceFormat_ = FirstSupported(outputFormats, {DXGI_FORMAT_R16G16_SINT});
        surfaceSummary_ = L"input=" + DescribeFormats(inputFormats) +
                          L", output=" + DescribeFormats(outputFormats) +
                          L", cost=" + DescribeFormats(costFormats);
        if (inputSurfaceFormat_ == DXGI_FORMAT_UNKNOWN ||
            outputSurfaceFormat_ == DXGI_FORMAT_UNKNOWN) {
            error_ = L"El driver Optical Flow no anunció una pareja RGB/SHORT2 compatible (" +
                     surfaceSummary_ + L").";
            return Abort(E_NOTIMPL);
        }
        useCostMap_ = FrameGenerationUsesCostMap(mode) &&
                      std::find(costFormats.begin(), costFormats.end(), DXGI_FORMAT_R8_UINT) !=
                          costFormats.end();

        NV_OF_INIT_PARAMS init{};
        init.width = width_;
        init.height = height_;
        init.outGridSize = static_cast<NV_OF_OUTPUT_VECTOR_GRID_SIZE>(gridSize_);
        init.hintGridSize = NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;
        init.mode = NV_OF_MODE_OPTICALFLOW;
        init.perfLevel = static_cast<NV_OF_PERF_LEVEL>(FrameGenerationPerformanceCode(mode));
        init.enableExternalHints = NV_OF_FALSE;
        init.enableOutputCost = useCostMap_ ? NV_OF_TRUE : NV_OF_FALSE;
        init.hPrivData = nullptr;
        init.disparityRange = NV_OF_STEREO_DISPARITY_RANGE_UNDEFINED;
        init.enableRoi = NV_OF_FALSE;
        init.predDirection = NV_OF_PRED_DIRECTION_BOTH;
        init.enableGlobalFlow = NV_OF_FALSE;
        init.inputBufferFormat = NV_OF_BUFFER_FORMAT_ABGR8;
        status = api_.nvOFInit(ofHandle_, &init);
        if (status != NV_OF_SUCCESS) return Abort(StatusFailure(status, L"nvOFInit"));

        hr = CreateTextures();
        if (FAILED(hr)) return Abort(hr);
        hr = RegisterTextures();
        if (FAILED(hr)) return Abort(hr);
        if (FrameGenBypassUsesStableFlow(bypassMode_)) {
            hr = CreateDiagnosticSynchronization();
            if (FAILED(hr)) return Abort(hr);
        }
        hr = CreateDrawPipeline();
        if (FAILED(hr)) return Abort(hr);
        // La telemetria es opcional: si no se pueden crear sus queries, FG sigue
        // funcionando y simplemente no reporta tiempos de GPU.
        if (telemetry_ && FAILED(CreateGpuQueries())) ReleaseGpuQueries();

        active_ = true;
        error_.clear();
        return S_OK;
    }

    void Shutdown() {
        active_ = false;
        targetWidth_ = targetHeight_ = 0;
        ReleaseStatsTarget();
        ReleaseGpuQueries();
        ReleaseDiagnosticSynchronization();
        SafeRelease(target_);
        SafeRelease(depth_);
        SafeRelease(raster_);
        SafeRelease(sampler_);
        SafeRelease(constants_);
        SafeRelease(pixelShader_);
        SafeRelease(vertexShader_);

        if (api_.nvOFUnregisterResourceD3D11) {
            for (NvOFGPUBufferHandle& handle : registered_) {
                if (handle) api_.nvOFUnregisterResourceD3D11(handle);
                handle = nullptr;
            }
        } else {
            registered_.fill(nullptr);
        }
        for (ID3D11ShaderResourceView*& view : costViews_) SafeRelease(view);
        for (ID3D11Texture2D*& texture : costTextures_) SafeRelease(texture);
        for (ID3D11ShaderResourceView*& view : flowViews_) SafeRelease(view);
        for (ID3D11Texture2D*& texture : flowTextures_) SafeRelease(texture);
        for (ID3D11ShaderResourceView*& view : sourceViews_) SafeRelease(view);
        for (ID3D11Texture2D*& texture : sourceTextures_) SafeRelease(texture);

        if (ofHandle_ && api_.nvOFDestroy) api_.nvOFDestroy(ofHandle_);
        ofHandle_ = nullptr;
        api_ = {};
        if (module_) FreeLibrary(module_);
        module_ = nullptr;
        SafeRelease(context_);
        SafeRelease(device_);
        width_ = height_ = flowWidth_ = flowHeight_ = 0;
        gridSize_ = 0;
        sourceTextureCount_ = 2;
        bypassMode_ = 0;
        useCostMap_ = false;
        lineCleanerLevel_ = 0;
        hasCurrent_ = false;
        currentIndex_ = 0;
        pendingIndex_ = 0;
        resetTemporalHints_ = true;
        lastFlowMs_ = 0.0;
        requestedMode_ = FrameGenerationMode::Off;
        inputSurfaceFormat_ = DXGI_FORMAT_UNKNOWN;
        outputSurfaceFormat_ = DXGI_FORMAT_UNKNOWN;
        telemetry_ = false;
        debugMode_ = 0;
        gpuOpticalMs_ = gpuWarpMs_ = -1.0;
        warpStats_ = FrameGenWarpStats{};
        flowDiagnosticLine_.clear();
        lastFlowDiagnosticTick_ = 0;
    }

    bool Active() const { return active_; }
    bool HasCurrent() const { return hasCurrent_; }
    int GridSize() const { return gridSize_; }
    bool UsesCostMap() const { return useCostMap_; }
    int LineCleanerLevel() const { return lineCleanerLevel_; }
    int BypassMode() const { return bypassMode_; }
    int SourceTextureCount() const { return sourceTextureCount_; }
    // OJO: nvOFExecute es asincrono (el header de NVIDIA dice "kicks off ... and
    // returns after submitting"). LastFlowMs() mide solo el costo de ENVIAR el
    // trabajo, no lo que tarda el motor OFA. Los tiempos reales de GPU salen de
    // GpuOpticalMs()/GpuWarpMs() (-1 si la telemetria esta apagada o aun no hay dato).
    double LastFlowMs() const { return lastFlowMs_; }
    double GpuOpticalMs() const { return gpuOpticalMs_; }
    double GpuWarpMs() const { return gpuWarpMs_; }
    double SyncVideoToOfaMs() const { return syncVideoToOfaMs_; }
    double SyncOfaToShaderMs() const { return syncOfaToShaderMs_; }
    double SyncBeforeReuseMs() const { return syncBeforeReuseMs_; }
    const FrameGenWarpStats& WarpStats() const { return warpStats_; }
    const std::wstring& Error() const { return error_; }
    const std::wstring& SurfaceSummary() const { return surfaceSummary_; }
    const std::wstring& FlowDiagnosticLine() const { return flowDiagnosticLine_; }
    DXGI_FORMAT InputSurfaceFormat() const { return inputSurfaceFormat_; }
    FrameGenerationMode Mode() const { return requestedMode_; }

    ID3D11Texture2D* PendingTexture() const {
        return active_ ? sourceTextures_[pendingIndex_] : nullptr;
    }
    ID3D11Texture2D* SourceTexture(int index) const {
        return active_ && index >= 0 && index < sourceTextureCount_
            ? sourceTextures_[static_cast<size_t>(index)] : nullptr;
    }
    int PendingIndex() const { return pendingIndex_; }

    // BYPASS 4 espera aqui el draw generado anterior ANTES de que el Video
    // Processor vuelva a escribir una fuente o que OFA reutilice sus salidas.
    HRESULT PrepareForConvertedFrame() {
        syncBeforeReuseMs_ = 0.0;
        if (!FrameGenBypassUsesStableFlow(bypassMode_) || !diagnosticDrawPending_) return S_OK;
        const HRESULT hr = WaitForEvent(diagnosticDrawDone_, syncBeforeReuseMs_);
        if (SUCCEEDED(hr)) diagnosticDrawPending_ = false;
        return hr;
    }

    // Se llama despues de que el Video Processor termino de convertir el frame
    // entrante a BGRA. S_FALSE significa "primer frame: aun no hay pareja".
    HRESULT CommitConvertedFrame() {
        if (!active_) return E_UNEXPECTED;
        const int previousIndex = currentIndex_;
        currentIndex_ = pendingIndex_;
        pendingIndex_ = (pendingIndex_ + 1) % sourceTextureCount_;
        if (!hasCurrent_) {
            hasCurrent_ = true;
            resetTemporalHints_ = true;
            return S_FALSE;
        }

        previousIndex_ = previousIndex;
        if (bypassMode_ == 2) {
            // La prueba 2 conserva exactamente la rotacion de fuentes, pero no
            // deja que OFA toque el contexto ni sus recursos.
            resetTemporalHints_ = true;
            lastFlowMs_ = 0.0;
            gpuOpticalMs_ = gpuWarpMs_ = -1.0;
            return S_OK;
        }

        syncVideoToOfaMs_ = 0.0;
        syncOfaToShaderMs_ = 0.0;
        if (FrameGenBypassUsesStableFlow(bypassMode_)) {
            // Este evento esta despues del VideoProcessorBlt que acaba de llenar
            // currentIndex_. OFA no ve una conversion a medio cocinar.
            const HRESULT syncHr = SignalAndWait(diagnosticVideoReady_, syncVideoToOfaMs_);
            if (FAILED(syncHr)) return syncHr;
        }

        BeginGpuTiming();
        NV_OF_EXECUTE_INPUT_PARAMS input{};
        input.inputFrame = registered_[previousIndex];
        input.referenceFrame = registered_[currentIndex_];
        input.disableTemporalHints = resetTemporalHints_ ? NV_OF_TRUE : NV_OF_FALSE;

        NV_OF_EXECUTE_OUTPUT_PARAMS output{};
        output.outputBuffer = registered_[kForwardFlowSlot];
        output.bwdOutputBuffer = registered_[kBackwardFlowSlot];
        if (useCostMap_) {
            output.outputCostBuffer = registered_[kForwardCostSlot];
            output.bwdOutputCostBuffer = registered_[kBackwardCostSlot];
        }

        LARGE_INTEGER frequency{};
        LARGE_INTEGER started{};
        LARGE_INTEGER finished{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&started);
        const NV_OF_STATUS status = api_.nvOFExecute(ofHandle_, &input, &output);
        QueryPerformanceCounter(&finished);
        MarkAfterOptical();
        if (frequency.QuadPart > 0) {
            lastFlowMs_ = static_cast<double>(finished.QuadPart - started.QuadPart) * 1000.0 /
                          static_cast<double>(frequency.QuadPart);
        }
        if (status != NV_OF_SUCCESS) {
            EndGpuTiming();
            return StatusFailure(status, L"nvOFExecute");
        }
        if (FrameGenBypassUsesStableFlow(bypassMode_)) {
            // Consumimos TODAS las salidas en comandos D3D11 y esperamos. El
            // shader lee estas copias estables, no buffers que el siguiente
            // nvOFExecute pueda empezar a pisar por cuadritos.
            for (size_t i = 0; i < flowTextures_.size(); ++i) {
                context_->CopyResource(diagnosticFlowTextures_[i], flowTextures_[i]);
            }
            if (useCostMap_) {
                for (size_t i = 0; i < costTextures_.size(); ++i) {
                    context_->CopyResource(diagnosticCostTextures_[i], costTextures_[i]);
                }
            }
            const HRESULT syncHr = SignalAndWait(diagnosticFlowReady_, syncOfaToShaderMs_);
            if (FAILED(syncHr)) {
                EndGpuTiming();
                return syncHr;
            }
            CaptureFlowDiagnostics();
        }
        resetTemporalHints_ = false;
        return S_OK;
    }

    HRESULT SetOutputTarget(IDXGISwapChain1* swapChain) {
        BeforeResize();
        if (!active_ || !swapChain) return E_INVALIDARG;
        ID3D11Texture2D* backBuffer = nullptr;
        HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr)) return hr;
        D3D11_TEXTURE2D_DESC desc{};
        backBuffer->GetDesc(&desc);
        targetWidth_ = desc.Width;
        targetHeight_ = desc.Height;
        hr = device_->CreateRenderTargetView(backBuffer, nullptr, &target_);
        backBuffer->Release();
        if (SUCCEEDED(hr)) CreateStatsTarget();
        return hr;
    }

    void BeforeResize() {
        ReleaseStatsTarget();
        SafeRelease(target_);
        targetWidth_ = targetHeight_ = 0;
    }

    HRESULT DrawCurrent(const RECT& contentRect) {
        if (!active_ || !hasCurrent_) return E_UNEXPECTED;
        return Draw(contentRect, true);
    }

    HRESULT DrawGenerated(const RECT& contentRect) {
        if (!active_ || !hasCurrent_ || previousIndex_ == currentIndex_) return E_UNEXPECTED;
        const HRESULT hr = Draw(contentRect, false);
        if (SUCCEEDED(hr)) {
            if (FrameGenBypassUsesStableFlow(bypassMode_) && diagnosticDrawDone_) {
                context_->End(diagnosticDrawDone_);
                context_->Flush();
                diagnosticDrawPending_ = true;
            }
            EndGpuTiming();      // cierra la medicion ANTES de gastar GPU en estadisticas
            CollectWarpStats();
        }
        return hr;
    }

private:
    static constexpr int kMaximumSourceTextures = 3;
    static constexpr int kForwardFlowSlot = 3;
    static constexpr int kBackwardFlowSlot = 4;
    static constexpr int kForwardCostSlot = 5;
    static constexpr int kBackwardCostSlot = 6;

    static constexpr const char* kShader = R"DAYAAH_FG(
cbuffer FrameGenConstants : register(b0) {
    float4 OutputRect;
    float4 Dimensions;
    float4 Control;
    float4 Tuning0;  // x,y: inicio de tolerancia |ida+vuelta| (base, relativa); z,w: fin (base, relativa)
    float4 Tuning1;  // x,y: rango del exceso fotometrico; z: rodilla del costo OFA; w: umbral de decision
    float4 Debug;    // x: 0 normal, 1 mapa de calor, 2 mapa por causa
    float4 Cleaner;  // x: activo; y: nivel; z,w: umbral de borde base y relativo al movimiento
};
Texture2D<float4> PreviousFrame : register(t0);
Texture2D<float4> CurrentFrame : register(t1);
Texture2D<int2> ForwardFlow : register(t2);
Texture2D<int2> BackwardFlow : register(t3);
Texture2D<uint> ForwardCost : register(t4);
Texture2D<uint> BackwardCost : register(t5);
SamplerState LinearClamp : register(s0);

struct VOut { float4 position : SV_Position; float2 uv : TEXCOORD0; };

VOut VSMain(uint vertexId : SV_VertexID) {
    VOut value;
    float2 position = float2((vertexId << 1) & 2, vertexId & 2);
    value.uv = position;
    value.position = float4(position * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return value;
}

int2 ClampFlowCell(int2 cell) {
    return clamp(cell, int2(0, 0), int2(Dimensions.zw) - 1);
}

float2 ReadForwardFlow(float2 pixel) {
    float grid = max(1.0, Control.w);
    float2 position = pixel / grid - 0.5;
    int2 base = int2(floor(position));
    float2 fraction = frac(position);
    float2 top = lerp(
        float2(ForwardFlow.Load(int3(ClampFlowCell(base), 0))),
        float2(ForwardFlow.Load(int3(ClampFlowCell(base + int2(1, 0)), 0))),
        fraction.x);
    float2 bottom = lerp(
        float2(ForwardFlow.Load(int3(ClampFlowCell(base + int2(0, 1)), 0))),
        float2(ForwardFlow.Load(int3(ClampFlowCell(base + int2(1, 1)), 0))),
        fraction.x);
    return lerp(top, bottom, fraction.y) / 32.0;
}

float2 ReadBackwardFlow(float2 pixel) {
    float grid = max(1.0, Control.w);
    float2 position = pixel / grid - 0.5;
    int2 base = int2(floor(position));
    float2 fraction = frac(position);
    float2 top = lerp(
        float2(BackwardFlow.Load(int3(ClampFlowCell(base), 0))),
        float2(BackwardFlow.Load(int3(ClampFlowCell(base + int2(1, 0)), 0))),
        fraction.x);
    float2 bottom = lerp(
        float2(BackwardFlow.Load(int3(ClampFlowCell(base + int2(0, 1)), 0))),
        float2(BackwardFlow.Load(int3(ClampFlowCell(base + int2(1, 1)), 0))),
        fraction.x);
    return lerp(top, bottom, fraction.y) / 32.0;
}

float2 LoadForwardCell(int2 cell) {
    return float2(ForwardFlow.Load(int3(ClampFlowCell(cell), 0))) / 32.0;
}

float2 LoadBackwardCell(int2 cell) {
    return float2(BackwardFlow.Load(int3(ClampFlowCell(cell), 0))) / 32.0;
}

float LoadForwardCostCell(int2 cell) {
    return (float)ForwardCost.Load(int3(ClampFlowCell(cell), 0));
}

float LoadBackwardCostCell(int2 cell) {
    return (float)BackwardCost.Load(int3(ClampFlowCell(cell), 0));
}

float2 LimitFlow(float2 flow) {
    float magnitude = length(flow);
    return magnitude > 128.0 ? flow * (128.0 / magnitude) : flow;
}

// El atlas no intenta verse bonito: direccion = color y magnitud = brillo.
// Cero movimiento queda negro, asi que un menu quieto lleno de rectangulos
// brillantes delata de inmediato que el problema ya viene en el campo OFA.
float3 FlowDiagnosticColor(float2 flow) {
    float magnitude = length(flow);
    if (magnitude < 0.03125) return float3(0.0, 0.0, 0.0);
    float2 direction = flow / magnitude;
    float3 directionColor = 0.5 + 0.5 * float3(direction.x, direction.y, -direction.x);
    float brightness = sqrt(saturate(magnitude / 32.0));
    return directionColor * brightness;
}

float ColorDiff(float4 a, float4 b) {
    float3 d = abs(a.rgb - b.rgb);
    return max(d.r, max(d.g, d.b));
}

// Dayaah Line Cleaner no copia una cuadricula completa ni difumina vectores de
// objeto y fondo. Cuando detecta un borde de movimiento prueba los vectores de
// las celdas vecinas y se queda con el que mejor alinea ambos frames, castigando
// tambien desacuerdo ida/vuelta y costo OFA. En una zona descubierta puede tomar
// un candidato un poco mas lejano antes de rendirse al fallback.
void TryLineCleanerCandidate(
    float2 pixel, int2 cell, float backwardSeed, float tieBias,
    inout float bestScore, inout float2 bestFlow,
    inout float bestMismatch, inout float bestCost) {
    float grid = max(1.0, Control.w);
    float blend = Control.x;
    int2 forwardCell;
    int2 backwardCell;
    float2 forward;
    float2 backward;

    // Probamos semillas de los DOS campos. En el borde delantero de un objeto
    // rapido el vector util puede existir solo en backward; quedarnos unicamente
    // con forward hacia que ese contorno no tuviera candidato bueno que elegir.
    if (backwardSeed > 0.5) {
        backwardCell = ClampFlowCell(cell);
        backward = LimitFlow(LoadBackwardCell(backwardCell));
        float2 previousGuess = pixel + backward * blend;
        forwardCell = ClampFlowCell(int2(previousGuess / grid));
        forward = LimitFlow(LoadForwardCell(forwardCell));
    } else {
        forwardCell = ClampFlowCell(cell);
        forward = LimitFlow(LoadForwardCell(forwardCell));
        float2 currentGuess = pixel + forward * (1.0 - blend);
        backwardCell = ClampFlowCell(int2(currentGuess / grid));
        backward = LimitFlow(LoadBackwardCell(backwardCell));
    }

    float motion = max(length(forward), length(backward));
    float mismatch = length(forward + backward);
    float toleranceStart = Tuning0.x + Tuning0.y * motion;
    float toleranceEnd = max(Tuning0.z + Tuning0.w * motion, toleranceStart + 0.05);
    float consistencyPenalty = smoothstep(toleranceStart, toleranceEnd, mismatch);

    float forwardCost = 0.0;
    float backwardCost = 0.0;
    float cost = 0.0;
    float costPenalty = 0.0;
    if (Control.z > 0.5) {
        forwardCost = LoadForwardCostCell(forwardCell);
        backwardCost = LoadBackwardCostCell(backwardCell);
        cost = max(forwardCost, backwardCost);
    }

    // Si ida y vuelta describen superficies distintas, su promedio no describe
    // NINGUNA y dibuja una linea de goma entre objeto y fondo. En ese caso gana
    // el campo de menor costo; sin mapa de costos respetamos la semilla que se
    // esta evaluando y dejamos que la fotometria decida entre ambos candidatos.
    float2 flow = LimitFlow((forward - backward) * 0.5);
    if (mismatch > toleranceEnd) {
        if (Control.z > 0.5) {
            if (forwardCost <= backwardCost) {
                flow = forward;
                cost = forwardCost;
            } else {
                flow = -backward;
                cost = backwardCost;
            }
        } else {
            flow = backwardSeed > 0.5 ? -backward : forward;
        }
    }

    float2 previousPixel = pixel - flow * blend;
    float2 currentPixel = pixel + flow * (1.0 - blend);
    bool outside = any(previousPixel < float2(0.0, 0.0)) ||
                   any(currentPixel < float2(0.0, 0.0)) ||
                   any(previousPixel > Dimensions.xy - 1.0) ||
                   any(currentPixel > Dimensions.xy - 1.0);
    float2 previousUv = saturate(previousPixel / Dimensions.xy);
    float2 currentUv = saturate(currentPixel / Dimensions.xy);
    float photo = ColorDiff(PreviousFrame.SampleLevel(LinearClamp, previousUv, 0.0),
                            CurrentFrame.SampleLevel(LinearClamp, currentUv, 0.0));

    if (Control.z > 0.5) {
        costPenalty = smoothstep(min(Tuning1.z, 250.0), 255.0, cost);
    }

    // Fotometria manda; consistencia y costo desempatan sin volver a multiplicar
    // tres confianzas y matar movimiento valido. El bias hace ganar a la celda
    // propia si dos opciones son practicamente iguales.
    float score = photo + consistencyPenalty * 0.12 + costPenalty * 0.04 + tieBias;
    if (outside) score += 2.0;
    if (score < bestScore) {
        bestScore = score;
        bestFlow = flow;
        bestMismatch = mismatch;
        bestCost = cost;
    }
}

void TryLineCleanerPair(
    float2 pixel, int2 cell, float tieBias,
    inout float bestScore, inout float2 bestFlow,
    inout float bestMismatch, inout float bestCost) {
    TryLineCleanerCandidate(pixel, cell, 0.0, tieBias,
                            bestScore, bestFlow, bestMismatch, bestCost);
    TryLineCleanerCandidate(pixel, cell, 1.0, tieBias,
                            bestScore, bestFlow, bestMismatch, bestCost);
}

float2 SelectLineCleanerFlow(float2 pixel, out float mismatch, out float cost, out float used) {
    float grid = max(1.0, Control.w);
    // Los offsets son celdas OFA, no pixeles. Ajustarlos a unos cuatro pixeles
    // fisicos evita que Calidad (rejilla 1x1) mire cuatro veces menos lejos que
    // Equilibrado (2x2) justo cuando mas detalle de movimiento tenemos.
    int step = max(1, (int)(4.0 / grid + 0.5));
    int2 base = ClampFlowCell(int2(pixel / grid));
    float2 center = LimitFlow(LoadForwardCell(base));
    float2 left = LimitFlow(LoadForwardCell(base + int2(-step, 0)));
    float2 right = LimitFlow(LoadForwardCell(base + int2(step, 0)));
    float2 up = LimitFlow(LoadForwardCell(base + int2(0, -step)));
    float2 down = LimitFlow(LoadForwardCell(base + int2(0, step)));
    float2 backwardCenter = -LimitFlow(LoadBackwardCell(base));

    // En un paneo uniforme todos estos vectores se parecen y no vale la pena
    // pagar muestras de color. Solo limpiamos donde realmente hay una linea de
    // movimiento, una oclusion o ruido fuerte entre celdas.
    float boundary = max(max(length(center - left), length(center - right)),
                         max(length(center - up), length(center - down)));
    // Una sola lectura backward tambien abre el gate donde forward parece plano
    // pero el borde delantero del objeto solo existe en el campo de regreso.
    boundary = max(boundary, length(center - backwardCenter));
    float boundaryStart = Cleaner.z + Cleaner.w * length(center);
    if (boundary < boundaryStart) {
        mismatch = 0.0;
        cost = 0.0;
        used = 0.0;
        return center;
    }

    used = 1.0;
    float bestScore = 1000.0;
    float2 bestFlow = center;
    mismatch = 1000.0;
    cost = 255.0;
    TryLineCleanerPair(pixel, base, 0.0, bestScore, bestFlow, mismatch, cost);
    TryLineCleanerPair(pixel, base + int2(-step, 0), 0.010, bestScore, bestFlow, mismatch, cost);
    TryLineCleanerPair(pixel, base + int2(step, 0), 0.010, bestScore, bestFlow, mismatch, cost);
    TryLineCleanerPair(pixel, base + int2(0, -step), 0.010, bestScore, bestFlow, mismatch, cost);
    TryLineCleanerPair(pixel, base + int2(0, step), 0.010, bestScore, bestFlow, mismatch, cost);

    // Calidad revisa tambien diagonales. Equilibrado se queda con la cruz para
    // que la limpieza no se convierta en otro filtro tragadero de GPU.
    if (Cleaner.y > 1.5) {
        TryLineCleanerPair(pixel, base + int2(-step, -step), 0.015, bestScore, bestFlow, mismatch, cost);
        TryLineCleanerPair(pixel, base + int2(step, -step), 0.015, bestScore, bestFlow, mismatch, cost);
        TryLineCleanerPair(pixel, base + int2(-step, step), 0.015, bestScore, bestFlow, mismatch, cost);
        TryLineCleanerPair(pixel, base + int2(step, step), 0.015, bestScore, bestFlow, mismatch, cost);
    }

    // Si ninguna celda inmediata alinea bien, probablemente estamos en fondo
    // recien descubierto. Una sola cruz mas lejana busca movimiento de fondo
    // parecido; no hacemos una busqueda enorme de 64 px en cada pixel.
    if (bestScore > 0.14) {
        int reach = max(2 * step,
                        min((int)(length(center) * 0.5 / grid + 0.5),
                            (int)(32.0 / grid)));
        TryLineCleanerPair(pixel, base + int2(-reach, 0), 0.030, bestScore, bestFlow, mismatch, cost);
        TryLineCleanerPair(pixel, base + int2(reach, 0), 0.030, bestScore, bestFlow, mismatch, cost);
        TryLineCleanerPair(pixel, base + int2(0, -reach), 0.030, bestScore, bestFlow, mismatch, cost);
        TryLineCleanerPair(pixel, base + int2(0, reach), 0.030, bestScore, bestFlow, mismatch, cost);
    }
    return bestFlow;
}

struct POut {
    float4 color : SV_Target0;
    // Solo se escribe cuando la telemetria esta activa y hay un segundo RTV
    // enlazado: (usa warp, fallback por consistencia, por fotometria, por costo).
    float4 stats : SV_Target1;
};

POut PSMain(VOut input) {
    POut o;
    o.color = float4(0.0, 0.0, 0.0, 1.0);
    o.stats = float4(0.0, 0.0, 0.0, 0.0);
    float2 uv = input.uv;
    if (uv.x < OutputRect.x || uv.y < OutputRect.y ||
        uv.x > OutputRect.z || uv.y > OutputRect.w) return o;
    float2 sourceUv = saturate((uv - OutputRect.xy) / max(OutputRect.zw - OutputRect.xy, 0.00001));
    int bypass = (int)(Debug.y + 0.5);
    // Los diagnosticos 7/8 se dibujan tambien en el Present "real". Si solo
    // aparecieran en el generado, alternarian video/mapa a 60 Hz y seria una
    // licuadora imposible de grabar con el telefono o la Game Bar.
    if (Control.y > 0.5 && bypass < 7) {
        o.color = CurrentFrame.Sample(LinearClamp, sourceUv);
        return o;
    }

    // Pruebas de bisturi. Todas salen ANTES de tocar flujo, costos o Line
    // Cleaner: asi un resultado negro no puede echarle la culpa al warp.
    if (bypass == 5) {
        o.color = PreviousFrame.Sample(LinearClamp, sourceUv);
        return o;
    }
    if (bypass == 6) {
        o.color = CurrentFrame.Sample(LinearClamp, sourceUv);
        return o;
    }
    if (bypass == 1 || bypass == 2 || bypass == 3) {
        float4 previousStaticEarly = PreviousFrame.Sample(LinearClamp, sourceUv);
        float4 currentStaticEarly = CurrentFrame.Sample(LinearClamp, sourceUv);
        o.color = lerp(previousStaticEarly, currentStaticEarly, Control.x);
        return o;
    }

    float2 frameSize = Dimensions.xy;
    float2 pixel = sourceUv * frameSize;
    float blend = Control.x;

    if (bypass == 7) {
        // Cuatro vistas completas en un solo comando:
        //   arriba izq.  forward       arriba der.  backward
        //   abajo izq.   costo forward abajo der.   costo backward
        bool right = sourceUv.x >= 0.5;
        bool bottom = sourceUv.y >= 0.5;
        float2 atlasUv = frac(sourceUv * 2.0);
        float2 atlasPixel = atlasUv * frameSize;
        if (!bottom) {
            float2 flow = right ? ReadBackwardFlow(atlasPixel)
                                : ReadForwardFlow(atlasPixel);
            o.color = float4(FlowDiagnosticColor(flow), 1.0);
        } else {
            int grid = max(1, (int)Control.w);
            int2 cell = ClampFlowCell(int2(atlasPixel) / grid);
            float cost = 0.0;
            if (Control.z > 0.5) {
                cost = right ? (float)BackwardCost.Load(int3(cell, 0))
                             : (float)ForwardCost.Load(int3(cell, 0));
            }
            float normalized = saturate(cost / 255.0);
            o.color = float4(normalized, normalized * 0.35, 0.0, 1.0);
        }
        return o;
    }

    if (bypass == 8) {
        // La mitad izquierda reconstruye TODO con forward. La derecha hace lo
        // mismo con -backward. Sin segunda consulta, estabilizador ni Cleaner:
        // si solo una mitad se corta, ya sabemos que campo/signo esta mal.
        float2 oneWayFlow = sourceUv.x < 0.5
            ? LimitFlow(ReadForwardFlow(pixel))
            : -LimitFlow(ReadBackwardFlow(pixel));
        float2 previousPixelOneWay = pixel - oneWayFlow * blend;
        float2 currentPixelOneWay = pixel + oneWayFlow * (1.0 - blend);
        float4 previousOneWay = PreviousFrame.SampleLevel(
            LinearClamp, saturate(previousPixelOneWay / frameSize), 0.0);
        float4 currentOneWay = CurrentFrame.SampleLevel(
            LinearClamp, saturate(currentPixelOneWay / frameSize), 0.0);
        o.color = lerp(previousOneWay, currentOneWay, blend);
        if (abs(sourceUv.x - 0.5) < 1.5 / max(frameSize.x, 1.0)) {
            o.color = float4(1.0, 1.0, 1.0, 1.0);
        }
        return o;
    }

    float cleanerMismatch = 0.0;
    float cleanerCost = 0.0;
    float cleanerUsed = 0.0;
    float2 cleanerFlow = float2(0.0, 0.0);
    if (Cleaner.x > 0.5) {
        cleanerFlow = SelectLineCleanerFlow(pixel, cleanerMismatch, cleanerCost, cleanerUsed);
    }

    float2 forward;
    float2 backward;
    if (cleanerUsed > 0.5) {
        // Un solo vector consensuado mantiene ambos lados del contorno pegados.
        forward = cleanerFlow;
        backward = -cleanerFlow;
    } else {
        forward = LimitFlow(ReadForwardFlow(pixel));
        backward = LimitFlow(ReadBackwardFlow(pixel));

        // No volvemos a consultar el flujo en la posicion ya desplazada. En
        // TestUFO esa segunda lectura saltaba de una celda del ovni a otra del
        // fondo (20/40/80 px segun la fila) y convertia un vector correcto en
        // las rebanadas rectangulares que vimos. Forward y backward ya llegan
        // con escala, signo y costo correctos desde esta primera lectura.
    }

    // OFA puede devolver movimiento subpixel aun en una imagen quieta. Esa
    // respiracion falsa es justo el temblor de cuadro completo que queremos matar.
    float motion = max(length(forward), length(backward));
    float motionWeight = smoothstep(0.35, 1.10, motion);
    forward *= motionWeight;
    backward *= motionWeight;

    // 1) Consistencia ida/vuelta. La tolerancia CRECE con la magnitud del
    //    movimiento: con 40 px/frame una discrepancia de 3 px es normal en OFA y
    //    con 2 px/frame ya seria sospechosa. Un umbral absoluto fijo rechaza
    //    flujo valido justo en los paneos rapidos.
    float mismatch = cleanerUsed > 0.5
        ? cleanerMismatch * motionWeight
        : length(forward + backward);
    float toleranceStart = Tuning0.x + Tuning0.y * motion;
    float toleranceEnd = max(Tuning0.z + Tuning0.w * motion, toleranceStart + 0.05);
    float consistency = 1.0 - smoothstep(toleranceStart, toleranceEnd, mismatch);

    // Leemos los costos en la misma celda donde obtuvimos cada flujo. Cuando
    // ida y vuelta describen superficies distintas no las promediamos: gana
    // una trayectoria completa. En empate preferimos backward de forma estable
    // porque reconstruye desde el frame actual y evita parpadeo por ruido OFA.
    float forwardCost = 0.0;
    float backwardCost = 0.0;
    if (Control.z > 0.5) {
        if (cleanerUsed > 0.5) {
            forwardCost = cleanerCost;
            backwardCost = cleanerCost;
        } else {
            int grid = max(1, (int)Control.w);
            int2 costCell = ClampFlowCell(int2(pixel) / grid);
            forwardCost = (float)ForwardCost.Load(int3(costCell, 0));
            backwardCost = (float)BackwardCost.Load(int3(costCell, 0));
        }
    }

    float2 resolvedFlow = cleanerUsed > 0.5
        ? forward
        : LimitFlow((forward - backward) * 0.5);
    if (cleanerUsed <= 0.5 && mismatch > toleranceEnd) {
        resolvedFlow = Control.z > 0.5 && forwardCost + 6.0 < backwardCost
            ? forward
            : -backward;
    }

    // Los dos frames se proyectan SIEMPRE con la misma trayectoria simetrica.
    // El diagnostico 8 demostro que forward y backward por separado eran
    // completos; los bloques aparecian al combinar campos o fuentes por pixel.
    float2 previousPixel = pixel - resolvedFlow * blend;
    float2 currentPixel = pixel + resolvedFlow * (1.0 - blend);
    float2 previousUv = saturate(previousPixel / frameSize);
    float2 currentUv = saturate(currentPixel / frameSize);
    float4 previousWarp = PreviousFrame.Sample(LinearClamp, previousUv);
    float4 currentWarp = CurrentFrame.Sample(LinearClamp, currentUv);
    float4 previousStatic = PreviousFrame.Sample(LinearClamp, sourceUv);
    float4 currentStatic = CurrentFrame.Sample(LinearClamp, sourceUv);

    // 2) Fotometria: el warp solo es creible si las dos muestras que va a mezclar
    //    se parecen MAS (o casi igual) que las muestras sin mover. Si el flujo
    //    esta mal, previousWarp y currentWarp dejan de coincidir y se nota aqui.
    //    Tambien delata un signo de flujo invertido: el warp seria peor en todos lados.
    float warpDisagreement = ColorDiff(previousWarp, currentWarp);
    float excess = warpDisagreement - ColorDiff(previousStatic, currentStatic);
    float photo = 1.0 - smoothstep(Tuning1.x, max(Tuning1.y, Tuning1.x + 0.01), excess);
    // Si Line Cleaner encontro un warp que coincide fotometricamente, no dejamos
    // que el desacuerdo original objeto/fondo lo mande a crossfade otra vez.
    // Costo OFA y la propia prueba fotometrica siguen pudiendo vetarlo.
    if (cleanerUsed > 0.5) consistency = max(consistency, photo);

    // 3) Costo OFA, con rodilla: por debajo no se castiga. Antes se restaba
    //    cost/255 linealmente, asi que hasta un match bueno perdia confianza.
    float costTerm = 1.0;
    if (Control.z > 0.5) {
        float cost = mismatch > toleranceEnd
            ? min(forwardCost, backwardCost)
            : max(forwardCost, backwardCost);
        costTerm = 1.0 - smoothstep(min(Tuning1.z, 250.0), 255.0, cost);
    }

    // La confianza ya no elige entre pedazos de fuentes distintas. Atenua el
    // MOVIMIENTO de una sola trayectoria coherente; asi una frontera dudosa
    // puede perder interpolacion gradualmente, pero no revelar rectangulos de
    // CurrentFrame ni fabricar cuatro contornos con un crossfade de colores.
    float limiting = min(consistency, min(photo, costTerm));
    float center = Tuning1.w;
    float confidence = smoothstep(center - 0.25, center + 0.25, limiting);
    bool moving = motionWeight > 0.05;
    bool usesWarp = confidence >= 0.5;

    float2 stableFlow = resolvedFlow * confidence;
    float2 stablePreviousUv = saturate((pixel - stableFlow * blend) / frameSize);
    float2 stableCurrentUv = saturate((pixel + stableFlow * (1.0 - blend)) / frameSize);
    float4 stablePrevious = PreviousFrame.Sample(LinearClamp, stablePreviousUv);
    float4 stableCurrent = CurrentFrame.Sample(LinearClamp, stableCurrentUv);
    o.color = lerp(stablePrevious, stableCurrent, blend);

    bool consistencyLimits = consistency <= photo && consistency <= costTerm;
    bool photoLimits = !consistencyLimits && photo <= costTerm;
    if (moving) {
        o.stats = float4(usesWarp ? 1.0 : 0.0,
                         (!usesWarp && consistencyLimits) ? 1.0 : 0.0,
                         (!usesWarp && photoLimits) ? 1.0 : 0.0,
                         (!usesWarp && !consistencyLimits && !photoLimits) ? 1.0 : 0.0);
    }

    if (Debug.x > 0.5) {
        float3 tint = float3(0.0, 0.25, 1.0);
        if (Debug.x > 2.5) {
            tint = cleanerUsed > 0.5 ? float3(0.0, 1.0, 1.0) : float3(0.05, 0.08, 0.10);
        } else if (moving) {
            if (Debug.x < 1.5) tint = lerp(float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), confidence);
            else if (usesWarp) tint = float3(0.0, 1.0, 0.0);
            else if (consistencyLimits) tint = float3(1.0, 0.0, 0.0);
            else if (photoLimits) tint = float3(1.0, 1.0, 0.0);
            else tint = float3(1.0, 0.0, 1.0);
        }
        o.color = float4(lerp(o.color.rgb, tint, 0.55), 1.0);
    }
    return o;
}
)DAYAAH_FG";

    template<class T> static void SafeRelease(T*& object) {
        if (object) { object->Release(); object = nullptr; }
    }

    HRESULT LoadApi() {
        wchar_t systemPath[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(systemPath, MAX_PATH);
        if (!length || length + 17 >= MAX_PATH) {
            error_ = L"No pude resolver la carpeta System32.";
            return E_FAIL;
        }
        std::wstring path(systemPath);
        path += L"\\nvofapi64.dll";
        module_ = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module_) {
            error_ = L"nvofapi64.dll no esta disponible. Se necesita un driver NVIDIA compatible.";
            return HRESULT_FROM_WIN32(GetLastError());
        }
        using CreateApiFn = NV_OF_STATUS(NVOFAPI*)(uint32_t, NV_OF_D3D11_API_FUNCTION_LIST*);
        const FARPROC address = GetProcAddress(module_, "NvOFAPICreateInstanceD3D11");
        CreateApiFn createApi = nullptr;
        static_assert(sizeof(createApi) == sizeof(address));
        std::memcpy(&createApi, &address, sizeof(createApi));
        if (!createApi) {
            error_ = L"El driver NVIDIA no exporta NvOFAPICreateInstanceD3D11.";
            return E_NOINTERFACE;
        }
        const NV_OF_STATUS status = createApi(NV_OF_API_VERSION, &api_);
        if (status != NV_OF_SUCCESS) return StatusFailure(status, L"NvOFAPICreateInstanceD3D11");
        return S_OK;
    }

    std::vector<DXGI_FORMAT> SurfaceFormats(NV_OF_BUFFER_USAGE usage) {
        uint32_t count = 0;
        NV_OF_STATUS status = api_.nvOFGetSurfaceFormatCountD3D11(
            ofHandle_, usage, NV_OF_MODE_OPTICALFLOW, &count);
        if (status != NV_OF_SUCCESS || !count || count > 64) return {};
        std::vector<DXGI_FORMAT> formats(count);
        status = api_.nvOFGetSurfaceFormatD3D11(
            ofHandle_, usage, NV_OF_MODE_OPTICALFLOW, formats.data());
        if (status != NV_OF_SUCCESS) return {};
        return formats;
    }

    static DXGI_FORMAT FirstSupported(const std::vector<DXGI_FORMAT>& available,
                                      std::initializer_list<DXGI_FORMAT> preferred) {
        for (DXGI_FORMAT format : preferred) {
            if (std::find(available.begin(), available.end(), format) != available.end()) {
                return format;
            }
        }
        return DXGI_FORMAT_UNKNOWN;
    }

    static const wchar_t* FormatName(DXGI_FORMAT format) {
        if (format == DXGI_FORMAT_B8G8R8A8_UNORM) return L"B8G8R8A8";
        if (format == DXGI_FORMAT_R8G8B8A8_UNORM) return L"R8G8B8A8";
        if (format == DXGI_FORMAT_NV12) return L"NV12";
        if (format == DXGI_FORMAT_R16G16_SINT) return L"R16G16_SINT";
        if (format == DXGI_FORMAT_R8_UINT) return L"R8_UINT";
        if (format == DXGI_FORMAT_R32_UINT) return L"R32_UINT";
        return nullptr;
    }

    static std::wstring DescribeFormats(const std::vector<DXGI_FORMAT>& formats) {
        if (formats.empty()) return L"ninguno";
        std::wstring result;
        for (DXGI_FORMAT format : formats) {
            if (!result.empty()) result += L"/";
            const wchar_t* name = FormatName(format);
            result += name ? name : (L"DXGI#" + std::to_wstring(static_cast<unsigned>(format)));
        }
        return result;
    }

    HRESULT CreateTextures() {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width_;
        desc.Height = height_;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        // No adivinamos si el driver llama ABGR a B8G8R8A8 o R8G8B8A8: usamos
        // exactamente el DXGI_FORMAT que anunció para esta sesión.
        desc.Format = inputSurfaceFormat_;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = S_OK;
        for (int i = 0; i < sourceTextureCount_ && SUCCEEDED(hr); ++i) {
            hr = device_->CreateTexture2D(&desc, nullptr,
                                           &sourceTextures_[static_cast<size_t>(i)]);
            if (SUCCEEDED(hr)) {
                hr = device_->CreateShaderResourceView(
                    sourceTextures_[static_cast<size_t>(i)], nullptr,
                    &sourceViews_[static_cast<size_t>(i)]);
            }
        }

        desc.Width = flowWidth_;
        desc.Height = flowHeight_;
        desc.Format = outputSurfaceFormat_;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        for (int i = 0; i < 2 && SUCCEEDED(hr); ++i) {
            hr = device_->CreateTexture2D(&desc, nullptr, &flowTextures_[i]);
            if (SUCCEEDED(hr)) hr = device_->CreateShaderResourceView(flowTextures_[i], nullptr,
                                                                      &flowViews_[i]);
        }
        if (useCostMap_) {
            desc.Format = DXGI_FORMAT_R8_UINT;
            for (int i = 0; i < 2 && SUCCEEDED(hr); ++i) {
                hr = device_->CreateTexture2D(&desc, nullptr, &costTextures_[i]);
                if (SUCCEEDED(hr)) hr = device_->CreateShaderResourceView(costTextures_[i], nullptr,
                                                                          &costViews_[i]);
            }
        }
        if (FAILED(hr)) error_ = L"No pude crear las texturas de NVIDIA Optical Flow.";
        return hr;
    }

    HRESULT RegisterOne(int slot, ID3D11Resource* resource) {
        const NV_OF_STATUS status = api_.nvOFRegisterResourceD3D11(
            ofHandle_, resource, &registered_[slot]);
        return status == NV_OF_SUCCESS ? S_OK : StatusFailure(status, L"nvOFRegisterResourceD3D11");
    }

    HRESULT RegisterTextures() {
        HRESULT hr = S_OK;
        for (int i = 0; i < sourceTextureCount_ && SUCCEEDED(hr); ++i) {
            hr = RegisterOne(i, sourceTextures_[static_cast<size_t>(i)]);
        }
        if (SUCCEEDED(hr)) hr = RegisterOne(kForwardFlowSlot, flowTextures_[0]);
        if (SUCCEEDED(hr)) hr = RegisterOne(kBackwardFlowSlot, flowTextures_[1]);
        if (useCostMap_ && SUCCEEDED(hr)) hr = RegisterOne(kForwardCostSlot, costTextures_[0]);
        if (useCostMap_ && SUCCEEDED(hr)) hr = RegisterOne(kBackwardCostSlot, costTextures_[1]);
        return hr;
    }

    HRESULT CreateDiagnosticSynchronization() {
        ReleaseDiagnosticSynchronization();
        D3D11_TEXTURE2D_DESC desc{};
        flowTextures_[0]->GetDesc(&desc);
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = 0;
        HRESULT hr = S_OK;
        for (size_t i = 0; i < diagnosticFlowTextures_.size() && SUCCEEDED(hr); ++i) {
            hr = device_->CreateTexture2D(&desc, nullptr, &diagnosticFlowTextures_[i]);
            if (SUCCEEDED(hr)) {
                hr = device_->CreateShaderResourceView(diagnosticFlowTextures_[i], nullptr,
                                                        &diagnosticFlowViews_[i]);
            }
        }
        D3D11_TEXTURE2D_DESC staging = desc;
        staging.Usage = D3D11_USAGE_STAGING;
        staging.BindFlags = 0;
        staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging.MiscFlags = 0;
        for (size_t i = 0; i < diagnosticFlowStaging_.size() && SUCCEEDED(hr); ++i) {
            hr = device_->CreateTexture2D(&staging, nullptr, &diagnosticFlowStaging_[i]);
        }
        if (useCostMap_ && SUCCEEDED(hr)) {
            costTextures_[0]->GetDesc(&desc);
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = 0;
            for (size_t i = 0; i < diagnosticCostTextures_.size() && SUCCEEDED(hr); ++i) {
                hr = device_->CreateTexture2D(&desc, nullptr, &diagnosticCostTextures_[i]);
                if (SUCCEEDED(hr)) {
                    hr = device_->CreateShaderResourceView(diagnosticCostTextures_[i], nullptr,
                                                            &diagnosticCostViews_[i]);
                }
            }
            staging = desc;
            staging.Usage = D3D11_USAGE_STAGING;
            staging.BindFlags = 0;
            staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            staging.MiscFlags = 0;
            for (size_t i = 0; i < diagnosticCostStaging_.size() && SUCCEEDED(hr); ++i) {
                hr = device_->CreateTexture2D(&staging, nullptr, &diagnosticCostStaging_[i]);
            }
        }

        D3D11_QUERY_DESC query{};
        query.Query = D3D11_QUERY_EVENT;
        if (SUCCEEDED(hr)) hr = device_->CreateQuery(&query, &diagnosticVideoReady_);
        if (SUCCEEDED(hr)) hr = device_->CreateQuery(&query, &diagnosticFlowReady_);
        if (SUCCEEDED(hr)) hr = device_->CreateQuery(&query, &diagnosticDrawDone_);
        if (SUCCEEDED(hr)) hr = device_->CreateQuery(&query, &diagnosticReadbackReady_);
        if (FAILED(hr)) {
            error_ = L"No pude crear la sincronizacion diagnostica de OFA.";
            ReleaseDiagnosticSynchronization();
        }
        return hr;
    }

    void ReleaseDiagnosticSynchronization() {
        diagnosticDrawPending_ = false;
        SafeRelease(diagnosticReadbackReady_);
        SafeRelease(diagnosticDrawDone_);
        SafeRelease(diagnosticFlowReady_);
        SafeRelease(diagnosticVideoReady_);
        for (ID3D11Texture2D*& texture : diagnosticCostStaging_) SafeRelease(texture);
        for (ID3D11Texture2D*& texture : diagnosticFlowStaging_) SafeRelease(texture);
        for (ID3D11ShaderResourceView*& view : diagnosticCostViews_) SafeRelease(view);
        for (ID3D11Texture2D*& texture : diagnosticCostTextures_) SafeRelease(texture);
        for (ID3D11ShaderResourceView*& view : diagnosticFlowViews_) SafeRelease(view);
        for (ID3D11Texture2D*& texture : diagnosticFlowTextures_) SafeRelease(texture);
        syncVideoToOfaMs_ = syncOfaToShaderMs_ = syncBeforeReuseMs_ = 0.0;
    }

    void CaptureFlowDiagnostics() {
        const ULONGLONG now = GetTickCount64();
        if (now - lastFlowDiagnosticTick_ < 1000ULL || !diagnosticReadbackReady_) return;
        lastFlowDiagnosticTick_ = now;

        for (size_t i = 0; i < diagnosticFlowTextures_.size(); ++i) {
            context_->CopyResource(diagnosticFlowStaging_[i], diagnosticFlowTextures_[i]);
        }
        if (useCostMap_) {
            for (size_t i = 0; i < diagnosticCostTextures_.size(); ++i) {
                context_->CopyResource(diagnosticCostStaging_[i], diagnosticCostTextures_[i]);
            }
        }
        double waitedMs = 0.0;
        if (FAILED(SignalAndWait(diagnosticReadbackReady_, waitedMs))) return;

        struct FlowSummary {
            double minX = std::numeric_limits<double>::max();
            double maxX = -std::numeric_limits<double>::max();
            double minY = std::numeric_limits<double>::max();
            double maxY = -std::numeric_limits<double>::max();
            double magnitudeSum = 0.0;
            double magnitudeMax = 0.0;
            std::uint64_t count = 0;
            std::uint64_t moving = 0;
            std::uint64_t extreme = 0;
        } flow[2];

        for (size_t index = 0; index < diagnosticFlowStaging_.size(); ++index) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context_->Map(diagnosticFlowStaging_[index], 0, D3D11_MAP_READ, 0,
                                     &mapped))) continue;
            FlowSummary& summary = flow[index];
            for (UINT y = 0; y < flowHeight_; ++y) {
                const auto* row = reinterpret_cast<const std::int16_t*>(
                    static_cast<const unsigned char*>(mapped.pData) +
                    static_cast<size_t>(y) * mapped.RowPitch);
                for (UINT x = 0; x < flowWidth_; ++x) {
                    const double fx = static_cast<double>(row[x * 2]) / 32.0;
                    const double fy = static_cast<double>(row[x * 2 + 1]) / 32.0;
                    const double magnitude = std::sqrt(fx * fx + fy * fy);
                    summary.minX = std::min(summary.minX, fx);
                    summary.maxX = std::max(summary.maxX, fx);
                    summary.minY = std::min(summary.minY, fy);
                    summary.maxY = std::max(summary.maxY, fy);
                    summary.magnitudeSum += magnitude;
                    summary.magnitudeMax = std::max(summary.magnitudeMax, magnitude);
                    ++summary.count;
                    if (magnitude > 1.0) ++summary.moving;
                    if (magnitude > 128.0) ++summary.extreme;
                }
            }
            context_->Unmap(diagnosticFlowStaging_[index], 0);
        }

        double costMean[2]{};
        unsigned costMaximum[2]{};
        if (useCostMap_) {
            for (size_t index = 0; index < diagnosticCostStaging_.size(); ++index) {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (FAILED(context_->Map(diagnosticCostStaging_[index], 0, D3D11_MAP_READ, 0,
                                         &mapped))) continue;
                std::uint64_t sum = 0;
                std::uint64_t count = 0;
                unsigned maximum = 0;
                for (UINT y = 0; y < flowHeight_; ++y) {
                    const auto* row = static_cast<const unsigned char*>(mapped.pData) +
                                      static_cast<size_t>(y) * mapped.RowPitch;
                    for (UINT x = 0; x < flowWidth_; ++x) {
                        sum += row[x];
                        maximum = std::max(maximum, static_cast<unsigned>(row[x]));
                        ++count;
                    }
                }
                context_->Unmap(diagnosticCostStaging_[index], 0);
                costMean[index] = count ? static_cast<double>(sum) / static_cast<double>(count) : 0.0;
                costMaximum[index] = maximum;
            }
        }

        auto meanMagnitude = [](const FlowSummary& value) {
            return value.count ? value.magnitudeSum / static_cast<double>(value.count) : 0.0;
        };
        auto percentage = [](std::uint64_t part, std::uint64_t total) {
            return total ? 100.0 * static_cast<double>(part) / static_cast<double>(total) : 0.0;
        };
        wchar_t line[1024]{};
        _snwprintf_s(line, _countof(line), _TRUNCATE,
            L"FGFLOW mode=%d grid=%d espera=%.2f ms | F x=%.1f..%.1f y=%.1f..%.1f mag med/max=%.2f/%.1f >1px=%.1f%% >128px=%.3f%% | "
            L"B x=%.1f..%.1f y=%.1f..%.1f mag med/max=%.2f/%.1f >1px=%.1f%% >128px=%.3f%% | costo F/B med=%.1f/%.1f max=%u/%u",
            bypassMode_, gridSize_, waitedMs,
            flow[0].minX, flow[0].maxX, flow[0].minY, flow[0].maxY,
            meanMagnitude(flow[0]), flow[0].magnitudeMax,
            percentage(flow[0].moving, flow[0].count),
            percentage(flow[0].extreme, flow[0].count),
            flow[1].minX, flow[1].maxX, flow[1].minY, flow[1].maxY,
            meanMagnitude(flow[1]), flow[1].magnitudeMax,
            percentage(flow[1].moving, flow[1].count),
            percentage(flow[1].extreme, flow[1].count),
            costMean[0], costMean[1], costMaximum[0], costMaximum[1]);
        flowDiagnosticLine_ = line;
    }

    HRESULT SignalAndWait(ID3D11Query* query, double& waitedMs) {
        if (!query || !context_) return E_POINTER;
        context_->End(query);
        context_->Flush();
        return WaitForEvent(query, waitedMs);
    }

    HRESULT WaitForEvent(ID3D11Query* query, double& waitedMs) {
        if (!query || !context_) return E_POINTER;
        LARGE_INTEGER frequency{};
        LARGE_INTEGER started{};
        LARGE_INTEGER now{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&started);
        for (;;) {
            BOOL complete = FALSE;
            const HRESULT hr = context_->GetData(query, &complete, sizeof(complete), 0);
            if (hr == S_OK && complete) {
                QueryPerformanceCounter(&now);
                waitedMs = frequency.QuadPart > 0
                    ? static_cast<double>(now.QuadPart - started.QuadPart) * 1000.0 /
                          static_cast<double>(frequency.QuadPart)
                    : 0.0;
                return S_OK;
            }
            if (FAILED(hr)) return hr;
            QueryPerformanceCounter(&now);
            if (frequency.QuadPart > 0 &&
                static_cast<double>(now.QuadPart - started.QuadPart) * 1000.0 /
                    static_cast<double>(frequency.QuadPart) > 2000.0) {
                return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
            }
            SwitchToThread();
        }
    }

    HRESULT CreateDrawPipeline() {
        wchar_t compilerPath[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(compilerPath, MAX_PATH);
        if (!length || length + 20 >= MAX_PATH) return E_FAIL;
        std::wstring path(compilerPath);
        path += L"\\d3dcompiler_47.dll";
        HMODULE compiler = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!compiler) return HRESULT_FROM_WIN32(GetLastError());
        using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
            ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
        const FARPROC address = GetProcAddress(compiler, "D3DCompile");
        CompileFn compile = nullptr;
        static_assert(sizeof(compile) == sizeof(address));
        std::memcpy(&compile, &address, sizeof(compile));
        if (!compile) { FreeLibrary(compiler); return E_NOINTERFACE; }

        auto build = [&](const char* entry, const char* profile, ID3DBlob** blob) {
            ID3DBlob* errors = nullptr;
            const HRESULT result = compile(kShader, std::strlen(kShader), "DayaahNvidiaFrameGen",
                nullptr, nullptr, entry, profile,
                D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
                0, blob, &errors);
            if (errors) {
                if (FAILED(result)) {
                    const char* bytes = static_cast<const char*>(errors->GetBufferPointer());
                    std::string message(bytes, errors->GetBufferSize());
                    error_.assign(message.begin(), message.end());
                }
                errors->Release();
            }
            return result;
        };
        ID3DBlob* vertex = nullptr;
        ID3DBlob* pixel = nullptr;
        HRESULT hr = build("VSMain", "vs_4_0", &vertex);
        if (SUCCEEDED(hr)) hr = build("PSMain", "ps_4_0", &pixel);
        if (SUCCEEDED(hr)) hr = device_->CreateVertexShader(vertex->GetBufferPointer(),
                                                            vertex->GetBufferSize(), nullptr,
                                                            &vertexShader_);
        if (SUCCEEDED(hr)) hr = device_->CreatePixelShader(pixel->GetBufferPointer(),
                                                           pixel->GetBufferSize(), nullptr,
                                                           &pixelShader_);
        SafeRelease(vertex);
        SafeRelease(pixel);
        FreeLibrary(compiler);

        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(DrawConstants);
        buffer.Usage = D3D11_USAGE_DYNAMIC;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (SUCCEEDED(hr)) hr = device_->CreateBuffer(&buffer, nullptr, &constants_);

        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (SUCCEEDED(hr)) hr = device_->CreateSamplerState(&sampler, &sampler_);

        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        if (SUCCEEDED(hr)) hr = device_->CreateRasterizerState(&raster, &raster_);

        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = FALSE;
        depth.StencilEnable = FALSE;
        if (SUCCEEDED(hr)) hr = device_->CreateDepthStencilState(&depth, &depth_);
        if (FAILED(hr) && error_.empty()) error_ = L"No pude crear el shader de interpolacion.";
        return hr;
    }

    HRESULT Draw(const RECT& contentRect, bool currentOnly) {
        if (!target_ || !targetWidth_ || !targetHeight_ || !vertexShader_ || !pixelShader_) {
            return E_UNEXPECTED;
        }
        DrawConstants values{};
        values.outputRect[0] = static_cast<float>(contentRect.left) / targetWidth_;
        values.outputRect[1] = static_cast<float>(contentRect.top) / targetHeight_;
        values.outputRect[2] = static_cast<float>(contentRect.right) / targetWidth_;
        values.outputRect[3] = static_cast<float>(contentRect.bottom) / targetHeight_;
        values.dimensions[0] = static_cast<float>(width_);
        values.dimensions[1] = static_cast<float>(height_);
        values.dimensions[2] = static_cast<float>(flowWidth_);
        values.dimensions[3] = static_cast<float>(flowHeight_);
        values.control[0] = 0.5f;
        values.control[1] = currentOnly ? 1.0f : 0.0f;
        values.control[2] = useCostMap_ ? 1.0f : 0.0f;
        values.control[3] = static_cast<float>(gridSize_);
        for (int i = 0; i < 4; ++i) {
            values.tuning0[i] = tuning_.v[i];
            values.tuning1[i] = tuning_.v[4 + i];
        }
        values.debug[0] = static_cast<float>(debugMode_);
        const int drawBypass = currentOnly && previousIndex_ == currentIndex_
            ? 0 : bypassMode_;
        values.debug[1] = static_cast<float>(drawBypass);
        values.cleaner[0] = lineCleanerLevel_ > 0 ? 1.0f : 0.0f;
        values.cleaner[1] = static_cast<float>(lineCleanerLevel_);
        values.cleaner[2] = tuning_.v[8];
        values.cleaner[3] = tuning_.v[9];
        const double contentArea = static_cast<double>(std::max<LONG>(contentRect.right - contentRect.left, 0)) *
                                   static_cast<double>(std::max<LONG>(contentRect.bottom - contentRect.top, 0));
        contentAreaFraction_ = contentArea / (static_cast<double>(targetWidth_) * static_cast<double>(targetHeight_));

        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context_->Map(constants_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) return hr;
        std::memcpy(mapped.pData, &values, sizeof(values));
        context_->Unmap(constants_, 0);

        const bool stableFlow = FrameGenBypassUsesStableFlow(bypassMode_);
        ID3D11ShaderResourceView* flowForward = stableFlow
            ? diagnosticFlowViews_[0] : flowViews_[0];
        ID3D11ShaderResourceView* flowBackward = stableFlow
            ? diagnosticFlowViews_[1] : flowViews_[1];
        ID3D11ShaderResourceView* costForward = stableFlow
            ? diagnosticCostViews_[0] : costViews_[0];
        ID3D11ShaderResourceView* costBackward = stableFlow
            ? diagnosticCostViews_[1] : costViews_[1];
        ID3D11ShaderResourceView* resources[6] = {
            sourceViews_[previousIndex_], sourceViews_[currentIndex_],
            flowForward, flowBackward, costForward, costBackward
        };
        if (currentOnly && bypassMode_ < 7) resources[0] = sourceViews_[currentIndex_];
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertexShader_, nullptr, 0);
        context_->PSSetShader(pixelShader_, nullptr, 0);
        context_->GSSetShader(nullptr, nullptr, 0);
        context_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        context_->OMSetDepthStencilState(depth_, 0);
        context_->RSSetState(raster_);
        D3D11_VIEWPORT viewport{0, 0, static_cast<float>(targetWidth_),
                                static_cast<float>(targetHeight_), 0, 1};
        context_->RSSetViewports(1, &viewport);
        // Solo el frame generado escribe estadisticas (SV_Target1) y solo si la
        // telemetria las pidio. Sin segundo RTV la escritura se descarta gratis.
        ID3D11RenderTargetView* targets[2] = {target_, currentOnly ? nullptr : statsRtv_};
        context_->OMSetRenderTargets(targets[1] ? 2 : 1, targets, nullptr);
        context_->PSSetConstantBuffers(0, 1, &constants_);
        context_->PSSetSamplers(0, 1, &sampler_);
        context_->PSSetShaderResources(0, 6, resources);
        context_->Draw(3, 0);
        ID3D11ShaderResourceView* empty[6]{};
        context_->PSSetShaderResources(0, 6, empty);
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        return S_OK;
    }

    // ---- Telemetria de GPU (solo si DAYAAH_FG_TELEMETRY=1) ----------------------
    //
    // nvOFExecute es asincrono, asi que medirlo con QueryPerformanceCounter solo
    // mide el costo de encolarlo. Aqui se ponen timestamps en la cola de GPU:
    //   start      justo antes de enviar nvOFExecute
    //   afterOfa   justo despues de enviarlo
    //   end        despues del draw del frame generado
    // Si el driver espera al OFA al encolar, el tiempo aparece en start->afterOfa;
    // si lo espera en el draw (porque el pixel shader lee el flujo), aparece en
    // afterOfa->end. La SUMA es el costo real flujo + warp en la GPU.
    HRESULT CreateGpuQueries() {
        D3D11_QUERY_DESC disjoint{};
        disjoint.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        D3D11_QUERY_DESC stamp{};
        stamp.Query = D3D11_QUERY_TIMESTAMP;
        for (GpuSlot& slot : gpuSlots_) {
            HRESULT hr = device_->CreateQuery(&disjoint, &slot.disjoint);
            if (SUCCEEDED(hr)) hr = device_->CreateQuery(&stamp, &slot.start);
            if (SUCCEEDED(hr)) hr = device_->CreateQuery(&stamp, &slot.afterOfa);
            if (SUCCEEDED(hr)) hr = device_->CreateQuery(&stamp, &slot.end);
            if (FAILED(hr)) return hr;
        }
        return S_OK;
    }

    void ReleaseGpuQueries() {
        for (GpuSlot& slot : gpuSlots_) {
            SafeRelease(slot.end);
            SafeRelease(slot.afterOfa);
            SafeRelease(slot.start);
            SafeRelease(slot.disjoint);
            slot.busy = false;
        }
        gpuSlotNext_ = 0;
        gpuSlotActive_ = -1;
    }

    void BeginGpuTiming() {
        gpuSlotActive_ = -1;
        if (!telemetry_ || !gpuSlots_[0].disjoint) return;
        PollGpuTiming();
        GpuSlot& slot = gpuSlots_[static_cast<size_t>(gpuSlotNext_)];
        if (slot.busy) return;
        context_->Begin(slot.disjoint);
        context_->End(slot.start);
        gpuSlotActive_ = gpuSlotNext_;
    }

    void MarkAfterOptical() {
        if (gpuSlotActive_ < 0) return;
        context_->End(gpuSlots_[static_cast<size_t>(gpuSlotActive_)].afterOfa);
    }

    void EndGpuTiming() {
        if (gpuSlotActive_ < 0) return;
        GpuSlot& slot = gpuSlots_[static_cast<size_t>(gpuSlotActive_)];
        context_->End(slot.end);
        context_->End(slot.disjoint);
        slot.busy = true;
        gpuSlotNext_ = (gpuSlotNext_ + 1) % static_cast<int>(gpuSlots_.size());
        gpuSlotActive_ = -1;
    }

    void PollGpuTiming() {
        for (GpuSlot& slot : gpuSlots_) {
            if (!slot.busy) continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            if (context_->GetData(slot.disjoint, &disjoint, sizeof(disjoint),
                                  D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
            UINT64 start = 0;
            UINT64 afterOfa = 0;
            UINT64 end = 0;
            const bool ready =
                context_->GetData(slot.start, &start, sizeof(start),
                                  D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                context_->GetData(slot.afterOfa, &afterOfa, sizeof(afterOfa),
                                  D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                context_->GetData(slot.end, &end, sizeof(end),
                                  D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            if (!ready) continue;
            slot.busy = false;
            if (disjoint.Disjoint || disjoint.Frequency == 0 || afterOfa < start || end < afterOfa) continue;
            const double toMs = 1000.0 / static_cast<double>(disjoint.Frequency);
            gpuOpticalMs_ = static_cast<double>(afterOfa - start) * toMs;
            gpuWarpMs_ = static_cast<double>(end - afterOfa) * toMs;
        }
    }

    // ---- Porcentaje de warp vs fallback medido en la GPU -------------------------
    //
    // El pixel shader escribe en un segundo render target (RGBA8) un indicador por
    // pixel: R = uso warp, G/B/A = cayo a crossfade por consistencia / fotometria /
    // costo. GenerateMips reduce eso a 1x1 (el promedio) y se copia a una textura
    // staging que se lee uno o dos frames despues SIN bloquear (DO_NOT_WAIT).
    void ReleaseStatsTarget() {
        for (ID3D11Texture2D*& staging : statsStaging_) SafeRelease(staging);
        statsStagingBusy_.fill(false);
        statsStagingNext_ = 0;
        SafeRelease(statsSrv_);
        SafeRelease(statsRtv_);
        SafeRelease(statsTexture_);
        statsLastMip_ = 0;
    }

    void CreateStatsTarget() {
        ReleaseStatsTarget();
        if (!telemetry_ || !targetWidth_ || !targetHeight_) return;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = targetWidth_;
        desc.Height = targetHeight_;
        desc.MipLevels = 0;  // cadena completa hasta 1x1
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &statsTexture_);
        if (SUCCEEDED(hr)) {
            D3D11_TEXTURE2D_DESC actual{};
            statsTexture_->GetDesc(&actual);
            statsLastMip_ = actual.MipLevels > 0 ? actual.MipLevels - 1 : 0;
            D3D11_RENDER_TARGET_VIEW_DESC rtv{};
            rtv.Format = desc.Format;
            rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            rtv.Texture2D.MipSlice = 0;
            hr = device_->CreateRenderTargetView(statsTexture_, &rtv, &statsRtv_);
        }
        if (SUCCEEDED(hr)) hr = device_->CreateShaderResourceView(statsTexture_, nullptr, &statsSrv_);
        D3D11_TEXTURE2D_DESC staging{};
        staging.Width = 1;
        staging.Height = 1;
        staging.MipLevels = 1;
        staging.ArraySize = 1;
        staging.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        staging.SampleDesc.Count = 1;
        staging.Usage = D3D11_USAGE_STAGING;
        staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (ID3D11Texture2D*& texture : statsStaging_) {
            if (SUCCEEDED(hr)) hr = device_->CreateTexture2D(&staging, nullptr, &texture);
        }
        // La telemetria nunca debe tumbar FG: si algo falla, se apaga solo ella.
        if (FAILED(hr)) ReleaseStatsTarget();
    }

    void CollectWarpStats() {
        if (!statsTexture_ || !statsSrv_) return;
        PollWarpStats();
        const size_t slot = static_cast<size_t>(statsStagingNext_);
        if (statsStagingBusy_[slot]) return;
        context_->GenerateMips(statsSrv_);
        const D3D11_BOX box{0, 0, 0, 1, 1, 1};
        context_->CopySubresourceRegion(statsStaging_[slot], 0, 0, 0, 0,
                                        statsTexture_, statsLastMip_, &box);
        statsStagingBusy_[slot] = true;
        statsStagingNext_ = 1 - statsStagingNext_;
    }

    void PollWarpStats() {
        for (size_t i = 0; i < statsStaging_.size(); ++i) {
            if (!statsStagingBusy_[i]) continue;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const HRESULT hr = context_->Map(statsStaging_[i], 0, D3D11_MAP_READ,
                                             D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
            if (hr == DXGI_ERROR_WAS_STILL_DRAWING) continue;
            statsStagingBusy_[i] = false;
            if (FAILED(hr)) continue;
            const unsigned char* texel = static_cast<const unsigned char*>(mapped.pData);
            const double inside = std::max(contentAreaFraction_, 0.001);
            const double warp = texel[0] / 255.0 / inside;
            const double consistency = texel[1] / 255.0 / inside;
            const double photo = texel[2] / 255.0 / inside;
            const double cost = texel[3] / 255.0 / inside;
            context_->Unmap(statsStaging_[i], 0);
            warpStats_.valid = true;
            warpStats_.warp = static_cast<float>(std::min(warp, 1.0));
            warpStats_.fallbackConsistency = static_cast<float>(std::min(consistency, 1.0));
            warpStats_.fallbackPhoto = static_cast<float>(std::min(photo, 1.0));
            warpStats_.fallbackCost = static_cast<float>(std::min(cost, 1.0));
            warpStats_.quiet = static_cast<float>(
                std::max(0.0, 1.0 - warp - consistency - photo - cost));
        }
    }

    HRESULT StatusFailure(NV_OF_STATUS status, const wchar_t* operation) {
        wchar_t text[320]{};
        char driverText[192]{};
        uint32_t driverTextSize = static_cast<uint32_t>(sizeof(driverText));
        if (ofHandle_ && api_.nvOFGetLastError &&
            api_.nvOFGetLastError(ofHandle_, driverText, &driverTextSize) == NV_OF_SUCCESS &&
            driverText[0]) {
            std::wstring detail;
            for (char value : driverText) {
                if (!value) break;
                detail.push_back(static_cast<unsigned char>(value));
            }
            _snwprintf_s(text, _countof(text), _TRUNCATE, L"%s fallo (%d): %s",
                         operation, static_cast<int>(status), detail.c_str());
        } else {
            _snwprintf_s(text, _countof(text), _TRUNCATE, L"%s fallo con estado NVIDIA %d",
                         operation, static_cast<int>(status));
        }
        error_ = text;
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x500 + static_cast<int>(status));
    }

    HRESULT Abort(HRESULT hr) {
        const std::wstring saved = error_;
        Shutdown();
        error_ = saved;
        return hr;
    }

    struct DrawConstants {
        float outputRect[4];
        float dimensions[4];
        float control[4];
        float tuning0[4];
        float tuning1[4];
        float debug[4];
        float cleaner[4];
    };
    static_assert(sizeof(DrawConstants) == 112);

    HMODULE module_ = nullptr;
    NV_OF_D3D11_API_FUNCTION_LIST api_{};
    NvOFHandle ofHandle_ = nullptr;
    std::array<NvOFGPUBufferHandle, 7> registered_{};
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    std::array<ID3D11Texture2D*, kMaximumSourceTextures> sourceTextures_{};
    std::array<ID3D11ShaderResourceView*, kMaximumSourceTextures> sourceViews_{};
    std::array<ID3D11Texture2D*, 2> flowTextures_{};
    std::array<ID3D11ShaderResourceView*, 2> flowViews_{};
    std::array<ID3D11Texture2D*, 2> costTextures_{};
    std::array<ID3D11ShaderResourceView*, 2> costViews_{};
    std::array<ID3D11Texture2D*, 2> diagnosticFlowTextures_{};
    std::array<ID3D11ShaderResourceView*, 2> diagnosticFlowViews_{};
    std::array<ID3D11Texture2D*, 2> diagnosticCostTextures_{};
    std::array<ID3D11ShaderResourceView*, 2> diagnosticCostViews_{};
    std::array<ID3D11Texture2D*, 2> diagnosticFlowStaging_{};
    std::array<ID3D11Texture2D*, 2> diagnosticCostStaging_{};
    ID3D11Query* diagnosticVideoReady_ = nullptr;
    ID3D11Query* diagnosticFlowReady_ = nullptr;
    ID3D11Query* diagnosticDrawDone_ = nullptr;
    ID3D11Query* diagnosticReadbackReady_ = nullptr;
    ID3D11VertexShader* vertexShader_ = nullptr;
    ID3D11PixelShader* pixelShader_ = nullptr;
    ID3D11Buffer* constants_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11RasterizerState* raster_ = nullptr;
    ID3D11DepthStencilState* depth_ = nullptr;
    ID3D11RenderTargetView* target_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
    UINT flowWidth_ = 0;
    UINT flowHeight_ = 0;
    UINT targetWidth_ = 0;
    UINT targetHeight_ = 0;
    int gridSize_ = 0;
    DXGI_FORMAT inputSurfaceFormat_ = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT outputSurfaceFormat_ = DXGI_FORMAT_UNKNOWN;
    int previousIndex_ = 0;
    int currentIndex_ = 0;
    int pendingIndex_ = 0;
    int sourceTextureCount_ = 2;
    int bypassMode_ = 0;
    bool active_ = false;
    bool useCostMap_ = false;
    int lineCleanerLevel_ = 0;
    bool hasCurrent_ = false;
    bool resetTemporalHints_ = true;
    bool diagnosticDrawPending_ = false;
    double lastFlowMs_ = 0.0;
    double syncVideoToOfaMs_ = 0.0;
    double syncOfaToShaderMs_ = 0.0;
    double syncBeforeReuseMs_ = 0.0;
    ULONGLONG lastFlowDiagnosticTick_ = 0;
    FrameGenerationMode requestedMode_ = FrameGenerationMode::Off;
    std::wstring error_;
    std::wstring surfaceSummary_;
    std::wstring flowDiagnosticLine_;

    struct GpuSlot {
        ID3D11Query* disjoint = nullptr;
        ID3D11Query* start = nullptr;
        ID3D11Query* afterOfa = nullptr;
        ID3D11Query* end = nullptr;
        bool busy = false;
    };
    FrameGenTuning tuning_;
    int debugMode_ = 0;
    bool telemetry_ = false;
    std::array<GpuSlot, 3> gpuSlots_{};
    int gpuSlotNext_ = 0;
    int gpuSlotActive_ = -1;
    double gpuOpticalMs_ = -1.0;
    double gpuWarpMs_ = -1.0;
    ID3D11Texture2D* statsTexture_ = nullptr;
    ID3D11RenderTargetView* statsRtv_ = nullptr;
    ID3D11ShaderResourceView* statsSrv_ = nullptr;
    std::array<ID3D11Texture2D*, 2> statsStaging_{};
    std::array<bool, 2> statsStagingBusy_{};
    int statsStagingNext_ = 0;
    UINT statsLastMip_ = 0;
    double contentAreaFraction_ = 1.0;
    FrameGenWarpStats warpStats_;
};

