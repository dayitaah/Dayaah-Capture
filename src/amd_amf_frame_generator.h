#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#include "frame_generation_model.h"
#include "frame_generation_telemetry.h"
#include "third_party/amd/amf/include/core/Factory.h"
#include "third_party/amd/amf/include/components/FRC.h"

// AMD ya trae en su driver un componente dedicado a interpolar VIDEO. Eso nos
// cae como anillo al dedo: le damos texturas BGRA de D3D11, pedimos solamente
// el cuadro inventado y Dayaah conserva su propio orden generado -> real.
//
// amfrt64.dll se carga al vuelo. Asi el mismo EXE abre en NVIDIA, Intel o en una
// Radeon sin AMF FRC; si el componente no existe, la captura normal sigue viva.
class AmdAmfFrameGenerator {
public:
    ~AmdAmfFrameGenerator() { Shutdown(); }

    HRESULT Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
                       UINT width, UINT height, FrameGenerationMode mode) {
        Shutdown();
        if (!device || !context || width < 16 || height < 16 ||
            !FrameGenerationIsAmdFrc(mode)) return E_INVALIDARG;

        device_ = device;
        context_ = context;
        device_->AddRef();
        context_->AddRef();
        width_ = width;
        height_ = height;
        requestedMode_ = mode;

        HRESULT hr = LoadRuntime();
        if (SUCCEEDED(hr)) hr = InitializeAmf();
        if (SUCCEEDED(hr)) hr = CreateTextures();
        if (SUCCEEDED(hr)) hr = CreateDrawPipeline();
        if (FAILED(hr)) return Abort(hr);

        surfaceSummary_ = L"D3D11 BGRA8 <-> AMD AMF FRC " + ProfileName() +
                          L" (runtime " + RuntimeVersionName() + L")";
        active_ = true;
        error_.clear();
        return S_OK;
    }

    void Shutdown() {
        active_ = false;
        BeforeResize();
        lastOutput_.Release();
        if (frc_ != nullptr) {
            frc_->Terminate();
            frc_.Release();
        }
        if (amfContext_ != nullptr) {
            amfContext_->Terminate();
            amfContext_.Release();
        }
        factory_ = nullptr;

        ReleaseOne(depth_);
        ReleaseOne(raster_);
        ReleaseOne(sampler_);
        ReleaseOne(constants_);
        ReleaseOne(pixelShader_);
        ReleaseOne(vertexShader_);
        ReleaseOne(generatedView_);
        ReleaseOne(generatedTexture_);
        for (ID3D11ShaderResourceView*& view : sourceViews_) ReleaseOne(view);
        for (ID3D11Texture2D*& texture : sourceTextures_) ReleaseOne(texture);

        if (runtime_) FreeLibrary(runtime_);
        runtime_ = nullptr;
        init_ = nullptr;
        queryVersion_ = nullptr;
        ReleaseOne(context_);
        ReleaseOne(device_);

        width_ = height_ = targetWidth_ = targetHeight_ = 0;
        currentIndex_ = pendingIndex_ = 0;
        hasCurrent_ = false;
        submittedFrames_ = 0;
        lastFlowMs_ = 0.0;
        runtimeVersion_ = 0;
        warpStats_ = {};
        requestedMode_ = FrameGenerationMode::Off;
        surfaceSummary_.clear();
        flowDiagnosticLine_.clear();
    }

    bool Active() const { return active_; }
    bool HasCurrent() const { return hasCurrent_; }
    int GridSize() const { return 0; }
    bool UsesCostMap() const { return false; }
    int LineCleanerLevel() const { return 0; }
    int BypassMode() const { return 0; }
    int SourceTextureCount() const { return 2; }
    double LastFlowMs() const { return lastFlowMs_; }
    double GpuOpticalMs() const { return lastFlowMs_; }
    double GpuWarpMs() const { return 0.0; }
    double SyncVideoToOfaMs() const { return 0.0; }
    double SyncOfaToShaderMs() const { return 0.0; }
    double SyncBeforeReuseMs() const { return 0.0; }
    const FrameGenWarpStats& WarpStats() const { return warpStats_; }
    const std::wstring& Error() const { return error_; }
    const std::wstring& SurfaceSummary() const { return surfaceSummary_; }
    const std::wstring& FlowDiagnosticLine() const { return flowDiagnosticLine_; }
    DXGI_FORMAT InputSurfaceFormat() const { return DXGI_FORMAT_B8G8R8A8_UNORM; }
    FrameGenerationMode Mode() const { return requestedMode_; }

    ID3D11Texture2D* PendingTexture() const {
        return active_ ? sourceTextures_[pendingIndex_] : nullptr;
    }
    ID3D11Texture2D* SourceTexture(int index) const {
        return active_ && index >= 0 && index < 2 ? sourceTextures_[index] : nullptr;
    }
    int PendingIndex() const { return pendingIndex_; }

    HRESULT PrepareForConvertedFrame() { return active_ ? S_OK : E_UNEXPECTED; }

    HRESULT CommitConvertedFrame() {
        if (!active_) return E_UNEXPECTED;
        currentIndex_ = pendingIndex_;
        pendingIndex_ = (pendingIndex_ + 1) % 2;

        // A diferencia del OFA, AMF guarda su historia dentro del componente.
        // Por eso hasta el primer cuadro se envia: todavía no produce salida,
        // pero queda listo para interpolarlo cuando llegue el segundo.
        const bool firstFrame = !hasCurrent_;
        hasCurrent_ = true;
        const HRESULT submitHr = SubmitTexture(sourceTextures_[currentIndex_]);
        if (FAILED(submitHr)) return submitHr;
        ++submittedFrames_;
        if (firstFrame) return S_FALSE;

        LARGE_INTEGER frequency{};
        LARGE_INTEGER started{};
        LARGE_INTEGER finished{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&started);
        const HRESULT outputHr = WaitForGeneratedFrame();
        QueryPerformanceCounter(&finished);
        if (frequency.QuadPart > 0) {
            lastFlowMs_ = static_cast<double>(finished.QuadPart - started.QuadPart) * 1000.0 /
                          static_cast<double>(frequency.QuadPart);
        }
        if (FAILED(outputHr)) return outputHr;

        wchar_t line[192]{};
        _snwprintf_s(line, _countof(line), _TRUNCATE,
                     L"AMFTEL perfil=%s FRC=%.3f ms future=0 fallback=blend",
                     ProfileName().c_str(), lastFlowMs_);
        flowDiagnosticLine_ = line;
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
        return hr;
    }

    void BeforeResize() {
        ReleaseOne(target_);
        targetWidth_ = targetHeight_ = 0;
    }

    HRESULT DrawCurrent(const RECT& contentRect) {
        if (!active_ || !hasCurrent_) return E_UNEXPECTED;
        return Draw(sourceViews_[currentIndex_], contentRect);
    }

    HRESULT DrawGenerated(const RECT& contentRect) {
        if (!active_ || submittedFrames_ < 2) return E_UNEXPECTED;
        return Draw(generatedView_, contentRect);
    }

private:
    template <typename T>
    static void ReleaseOne(T*& value) {
        if (value) {
            value->Release();
            value = nullptr;
        }
    }

    template <typename T>
    static bool Resolve(HMODULE module, const char* name, T& value) {
        const FARPROC address = module ? GetProcAddress(module, name) : nullptr;
        value = nullptr;
        static_assert(sizeof(value) == sizeof(address));
        std::memcpy(&value, &address, sizeof(value));
        return value != nullptr;
    }

    HRESULT LoadRuntime() {
        runtime_ = LoadLibraryExW(L"amfrt64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!runtime_) {
            const DWORD code = GetLastError();
            error_ = L"AMD AMF no esta instalado (amfrt64.dll, Windows " +
                     std::to_wstring(code) + L"). Actualiza el driver Radeon.";
            return HRESULT_FROM_WIN32(code ? code : ERROR_MOD_NOT_FOUND);
        }
        if (!Resolve(runtime_, AMF_INIT_FUNCTION_NAME, init_) ||
            !Resolve(runtime_, AMF_QUERY_VERSION_FUNCTION_NAME, queryVersion_)) {
            error_ = L"El runtime AMD AMF no expone la API esperada.";
            return E_NOINTERFACE;
        }
        const AMF_RESULT versionResult = queryVersion_(&runtimeVersion_);
        if (versionResult != AMF_OK) runtimeVersion_ = 0;
        return S_OK;
    }

    HRESULT InitializeAmf() {
        AMF_RESULT result = init_(AMF_FULL_VERSION, &factory_);
        if (result != AMF_OK || !factory_) return AmfFailure(result, L"AMFInit");
        result = factory_->CreateContext(&amfContext_);
        if (result != AMF_OK || amfContext_ == nullptr) {
            return AmfFailure(result, L"AMFFactory::CreateContext");
        }
        result = amfContext_->InitDX11(device_);
        if (result != AMF_OK) return AmfFailure(result, L"AMFContext::InitDX11");
        result = factory_->CreateComponent(amfContext_, AMFFRC, &frc_);
        if (result != AMF_OK || frc_ == nullptr) {
            return AmfFailure(result, L"Crear componente AMFFRC");
        }

        result = frc_->SetProperty(AMF_FRC_ENGINE_TYPE,
                                   static_cast<amf_int64>(FRC_ENGINE_DX11));
        if (result == AMF_OK) {
            result = frc_->SetProperty(AMF_FRC_MODE,
                                       static_cast<amf_int64>(FRC_ONLY_INTERPOLATED));
        }
        if (result == AMF_OK) result = frc_->SetProperty(AMF_FRC_ENABLE_FALLBACK, true);
        if (result == AMF_OK) result = frc_->SetProperty(AMF_FRC_INDICATOR, false);
        if (result == AMF_OK) {
            result = frc_->SetProperty(AMF_FRC_PROFILE,
                static_cast<amf_int64>(FrameGenerationAmdFrcProfile(requestedMode_)));
        }
        if (result == AMF_OK) {
            result = frc_->SetProperty(AMF_FRC_MV_SEARCH_MODE,
                static_cast<amf_int64>(FrameGenerationAmdFrcPerformanceSearch(requestedMode_)
                    ? FRC_MV_SEARCH_PERFORMANCE : FRC_MV_SEARCH_NATIVE));
        }
        // FutureFrame usa un TERCER cuadro y añade otro intervalo entero. Para
        // jugar por capturadora eso es veneno; la pareja anterior/actual basta.
        if (result == AMF_OK) result = frc_->SetProperty(AMF_FRC_USE_FUTURE_FRAME, false);
        if (result != AMF_OK) return AmfFailure(result, L"Configurar AMD AMF FRC");

        result = frc_->Init(amf::AMF_SURFACE_BGRA,
                            static_cast<amf_int32>(width_),
                            static_cast<amf_int32>(height_));
        return result == AMF_OK ? S_OK : AmfFailure(result, L"AMFFRC::Init");
    }

    HRESULT CreateTextures() {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width_;
        desc.Height = height_;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = S_OK;
        for (size_t i = 0; i < sourceTextures_.size() && SUCCEEDED(hr); ++i) {
            hr = device_->CreateTexture2D(&desc, nullptr, &sourceTextures_[i]);
            if (SUCCEEDED(hr)) {
                hr = device_->CreateShaderResourceView(sourceTextures_[i], nullptr,
                                                       &sourceViews_[i]);
            }
        }
        if (SUCCEEDED(hr)) hr = device_->CreateTexture2D(&desc, nullptr, &generatedTexture_);
        if (SUCCEEDED(hr)) {
            hr = device_->CreateShaderResourceView(generatedTexture_, nullptr, &generatedView_);
        }
        if (FAILED(hr)) error_ = L"No pude crear las texturas BGRA para AMD AMF FRC.";
        return hr;
    }

    HRESULT SubmitTexture(ID3D11Texture2D* texture) {
        if (!texture || amfContext_ == nullptr || frc_ == nullptr) return E_POINTER;
        amf::AMFSurfacePtr surface;
        AMF_RESULT result = amfContext_->CreateSurfaceFromDX11Native(texture, &surface, nullptr);
        if (result != AMF_OK || surface == nullptr) {
            return AmfFailure(result, L"AMF CreateSurfaceFromDX11Native");
        }

        const ULONGLONG deadline = GetTickCount64() + 100;
        do {
            result = frc_->SubmitInput(surface);
            if (result == AMF_OK || result == AMF_NEED_MORE_INPUT) return S_OK;
            if (result != AMF_INPUT_FULL) return AmfFailure(result, L"AMFFRC::SubmitInput");

            // Si la cola se lleno, sacamos cualquier salida pendiente antes de
            // reintentar. No dejamos que AMF convierta esto en una fila del OXXO.
            amf::AMFDataPtr pending;
            const AMF_RESULT query = frc_->QueryOutput(&pending);
            if (query != AMF_OK && query != AMF_REPEAT && query != AMF_NEED_MORE_INPUT) {
                return AmfFailure(query, L"AMFFRC::QueryOutput durante SubmitInput");
            }
            SwitchToThread();
        } while (GetTickCount64() < deadline);

        error_ = L"AMD AMF FRC mantuvo llena su cola por mas de 100 ms.";
        return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    }

    HRESULT WaitForGeneratedFrame() {
        if (frc_ == nullptr || !generatedTexture_) return E_UNEXPECTED;
        const ULONGLONG deadline = GetTickCount64() + 100;
        for (;;) {
            amf::AMFDataPtr output;
            const AMF_RESULT result = frc_->QueryOutput(&output);
            if ((result == AMF_OK || result == AMF_REPEAT) && output != nullptr) {
                amf::AMFSurfacePtr surface(output);
                if (surface == nullptr || surface->GetFormat() != amf::AMF_SURFACE_BGRA ||
                    surface->GetPlanesCount() < 1 || !surface->GetPlaneAt(0)) {
                    error_ = L"AMD AMF FRC devolvio una superficie que no es BGRA.";
                    return E_UNEXPECTED;
                }
                auto* native = static_cast<ID3D11Texture2D*>(surface->GetPlaneAt(0)->GetNative());
                if (!native) {
                    error_ = L"AMD AMF FRC devolvio una superficie D3D11 vacia.";
                    return E_POINTER;
                }
                D3D11_TEXTURE2D_DESC desc{};
                native->GetDesc(&desc);
                if (desc.Width != width_ || desc.Height != height_ ||
                    desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
                    error_ = L"AMD AMF FRC cambio el tamaño o formato de su salida.";
                    return E_UNEXPECTED;
                }
                context_->CopyResource(generatedTexture_, native);
                // Conservamos la superficie hasta recibir la siguiente. Asi AMF
                // no recicla su textura mientras el CopyResource sigue en la GPU.
                lastOutput_ = surface;
                return S_OK;
            }
            if (result != AMF_OK && result != AMF_REPEAT && result != AMF_NEED_MORE_INPUT) {
                return AmfFailure(result, L"AMFFRC::QueryOutput");
            }
            if (GetTickCount64() >= deadline) {
                error_ = L"AMD AMF FRC no entrego el cuadro interpolado en 100 ms.";
                return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
            }
            SwitchToThread();
        }
    }

    std::wstring ProfileName() const {
        if (requestedMode_ == FrameGenerationMode::AmdFrcMaximum) return L"Super";
        if (requestedMode_ == FrameGenerationMode::AmdFrcQuality) return L"High";
        return L"Low/Performance";
    }

    std::wstring RuntimeVersionName() const {
        if (!runtimeVersion_) return L"desconocido";
        return std::to_wstring(AMF_GET_MAJOR_VERSION(runtimeVersion_)) + L"." +
               std::to_wstring(AMF_GET_MINOR_VERSION(runtimeVersion_)) + L"." +
               std::to_wstring(AMF_GET_SUBMINOR_VERSION(runtimeVersion_)) + L"." +
               std::to_wstring(AMF_GET_BUILD_VERSION(runtimeVersion_));
    }

    static const wchar_t* AmfResultName(AMF_RESULT result) {
        switch (result) {
        case AMF_OK: return L"AMF_OK";
        case AMF_FAIL: return L"AMF_FAIL";
        case AMF_NOT_SUPPORTED: return L"AMF_NOT_SUPPORTED";
        case AMF_NO_DEVICE: return L"AMF_NO_DEVICE";
        case AMF_DIRECTX_FAILED: return L"AMF_DIRECTX_FAILED";
        case AMF_INVALID_FORMAT: return L"AMF_INVALID_FORMAT";
        case AMF_WRONG_STATE: return L"AMF_WRONG_STATE";
        case AMF_INPUT_FULL: return L"AMF_INPUT_FULL";
        case AMF_NEED_MORE_INPUT: return L"AMF_NEED_MORE_INPUT";
        default: return L"AMF_ERROR";
        }
    }

    HRESULT AmfFailure(AMF_RESULT result, const wchar_t* operation) {
        error_ = operation + std::wstring(L" fallo (") + AmfResultName(result) +
                 L", " + std::to_wstring(static_cast<int>(result)) + L").";
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF,
                            0x700 + (static_cast<unsigned>(result) & 0xff));
    }

    HRESULT Abort(HRESULT hr) {
        const std::wstring saved = error_;
        Shutdown();
        error_ = saved;
        return hr;
    }

    struct DrawConstants { float outputRect[4]; };
    static_assert(sizeof(DrawConstants) == 16);

    static constexpr const char* kShader = R"DAYAAH_AMF(
cbuffer DrawConstants : register(b0) { float4 OutputRect; };
Texture2D<float4> Frame : register(t0);
SamplerState LinearClamp : register(s0);
struct VOut { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VOut VSMain(uint id : SV_VertexID) {
    VOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.position = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    o.uv = p;
    return o;
}
float4 PSMain(VOut input) : SV_Target {
    if (input.uv.x < OutputRect.x || input.uv.y < OutputRect.y ||
        input.uv.x > OutputRect.z || input.uv.y > OutputRect.w) return float4(0, 0, 0, 1);
    float2 size = max(OutputRect.zw - OutputRect.xy, float2(0.000001, 0.000001));
    float2 uv = saturate((input.uv - OutputRect.xy) / size);
    return Frame.SampleLevel(LinearClamp, uv, 0);
}
)DAYAAH_AMF";

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
        CompileFn compile = nullptr;
        const FARPROC address = GetProcAddress(compiler, "D3DCompile");
        static_assert(sizeof(compile) == sizeof(address));
        std::memcpy(&compile, &address, sizeof(compile));
        if (!compile) { FreeLibrary(compiler); return E_NOINTERFACE; }

        auto build = [&](const char* entry, const char* profile, ID3DBlob** blob) {
            ID3DBlob* errors = nullptr;
            const HRESULT result = compile(kShader, std::strlen(kShader), "DayaahAmdAMFFRC",
                nullptr, nullptr, entry, profile,
                D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
                0, blob, &errors);
            if (errors) errors->Release();
            return result;
        };
        ID3DBlob* vertex = nullptr;
        ID3DBlob* pixel = nullptr;
        HRESULT hr = build("VSMain", "vs_4_0", &vertex);
        if (SUCCEEDED(hr)) hr = build("PSMain", "ps_4_0", &pixel);
        if (SUCCEEDED(hr)) {
            hr = device_->CreateVertexShader(vertex->GetBufferPointer(),
                                              vertex->GetBufferSize(), nullptr, &vertexShader_);
        }
        if (SUCCEEDED(hr)) {
            hr = device_->CreatePixelShader(pixel->GetBufferPointer(),
                                             pixel->GetBufferSize(), nullptr, &pixelShader_);
        }
        ReleaseOne(vertex);
        ReleaseOne(pixel);
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
        if (FAILED(hr)) error_ = L"No pude crear el shader de salida AMD AMF FRC.";
        return hr;
    }

    HRESULT Draw(ID3D11ShaderResourceView* frame, const RECT& contentRect) {
        if (!frame || !target_ || !targetWidth_ || !targetHeight_) return E_UNEXPECTED;
        DrawConstants values{};
        values.outputRect[0] = static_cast<float>(contentRect.left) / targetWidth_;
        values.outputRect[1] = static_cast<float>(contentRect.top) / targetHeight_;
        values.outputRect[2] = static_cast<float>(contentRect.right) / targetWidth_;
        values.outputRect[3] = static_cast<float>(contentRect.bottom) / targetHeight_;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context_->Map(constants_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) return hr;
        std::memcpy(mapped.pData, &values, sizeof(values));
        context_->Unmap(constants_, 0);

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
        context_->OMSetRenderTargets(1, &target_, nullptr);
        context_->PSSetConstantBuffers(0, 1, &constants_);
        context_->PSSetSamplers(0, 1, &sampler_);
        context_->PSSetShaderResources(0, 1, &frame);
        context_->Draw(3, 0);
        ID3D11ShaderResourceView* empty = nullptr;
        context_->PSSetShaderResources(0, 1, &empty);
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        return S_OK;
    }

    using AMFInitFn = AMF_RESULT(AMF_CDECL_CALL*)(amf_uint64, amf::AMFFactory**);
    using AMFQueryVersionFn = AMF_RESULT(AMF_CDECL_CALL*)(amf_uint64*);

    HMODULE runtime_ = nullptr;
    AMFInitFn init_ = nullptr;
    AMFQueryVersionFn queryVersion_ = nullptr;
    amf_uint64 runtimeVersion_ = 0;
    amf::AMFFactory* factory_ = nullptr;
    amf::AMFContextPtr amfContext_;
    amf::AMFComponentPtr frc_;
    amf::AMFSurfacePtr lastOutput_;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    std::array<ID3D11Texture2D*, 2> sourceTextures_{};
    std::array<ID3D11ShaderResourceView*, 2> sourceViews_{};
    ID3D11Texture2D* generatedTexture_ = nullptr;
    ID3D11ShaderResourceView* generatedView_ = nullptr;
    ID3D11VertexShader* vertexShader_ = nullptr;
    ID3D11PixelShader* pixelShader_ = nullptr;
    ID3D11Buffer* constants_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11RasterizerState* raster_ = nullptr;
    ID3D11DepthStencilState* depth_ = nullptr;
    ID3D11RenderTargetView* target_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
    UINT targetWidth_ = 0;
    UINT targetHeight_ = 0;
    int currentIndex_ = 0;
    int pendingIndex_ = 0;
    std::uint64_t submittedFrames_ = 0;
    bool active_ = false;
    bool hasCurrent_ = false;
    double lastFlowMs_ = 0.0;
    FrameGenWarpStats warpStats_{};
    FrameGenerationMode requestedMode_ = FrameGenerationMode::Off;
    std::wstring error_;
    std::wstring surfaceSummary_;
    std::wstring flowDiagnosticLine_;
};
