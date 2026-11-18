#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "frame_generation_model.h"
#include "frame_generation_telemetry.h"

// Este backend NO es el OFA casero de Dayaah. NVIDIA VFG es el modelo neural
// oficial del Video Effects SDK. Lo cargamos dinamicamente para que el EXE
// normal siga abriendo aunque la usuaria no tenga el runtime opcional.
//
// Tampoco copiamos los headers propietarios en el repo: las funciones usan
// handles opacos y los numeros publicos documentados por NvCVImage.
class NvidiaVfgFrameGenerator {
public:
    ~NvidiaVfgFrameGenerator() { Shutdown(); }

    HRESULT Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
                       UINT width, UINT height, FrameGenerationMode mode) {
        Shutdown();
        if (!device || !context || width < 16 || height < 16 ||
            !FrameGenerationIsVfg(mode)) return E_INVALIDARG;

        device_ = device;
        context_ = context;
        device_->AddRef();
        context_->AddRef();
        width_ = width;
        height_ = height;
        requestedMode_ = mode;

        HRESULT hr = FindRuntime();
        if (FAILED(hr)) return Abort(hr);
        hr = LoadRuntime();
        if (FAILED(hr)) return Abort(hr);
        hr = CreateTextures();
        if (FAILED(hr)) return Abort(hr);
        hr = CreateImages();
        if (FAILED(hr)) return Abort(hr);
        hr = CreateEffect();
        if (FAILED(hr)) return Abort(hr);
        hr = CreateDrawPipeline();
        if (FAILED(hr)) return Abort(hr);

        surfaceSummary_ = L"D3D11 RGBA8 <-> CUDA RGBA8; VFG " + ModelName();
        active_ = true;
        error_.clear();
        return S_OK;
    }

    void Shutdown() {
        active_ = false;
        BeforeResize();
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

        DestroyImage(transferScratch_);
        DestroyImage(cudaOutput_);
        for (void*& image : cudaSources_) DestroyImage(image);
        DestroyImage(d3dOutput_);
        for (void*& image : d3dSources_) DestroyImage(image);

        if (effect_ && api_.destroyEffect) api_.destroyEffect(effect_);
        effect_ = nullptr;
        if (cudaStream_ && api_.cudaStreamDestroy) api_.cudaStreamDestroy(cudaStream_);
        cudaStream_ = nullptr;

        api_ = {};
        for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) {
            if (*it) FreeLibrary(*it);
        }
        modules_.clear();
        RestorePath();

        ReleaseOne(context_);
        ReleaseOne(device_);
        width_ = height_ = targetWidth_ = targetHeight_ = 0;
        previousIndex_ = currentIndex_ = pendingIndex_ = 0;
        hasCurrent_ = false;
        lastFlowMs_ = 0.0;
        warpStats_ = {};
        requestedMode_ = FrameGenerationMode::Off;
        runtimeDirectory_.clear();
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
    DXGI_FORMAT InputSurfaceFormat() const { return DXGI_FORMAT_R8G8B8A8_UNORM; }
    FrameGenerationMode Mode() const { return requestedMode_; }

    ID3D11Texture2D* PendingTexture() const {
        return active_ ? sourceTextures_[pendingIndex_] : nullptr;
    }
    ID3D11Texture2D* SourceTexture(int index) const {
        return active_ && index >= 0 && index < 2 ? sourceTextures_[index] : nullptr;
    }
    int PendingIndex() const { return pendingIndex_; }

    HRESULT PrepareForConvertedFrame() { return active_ ? S_OK : E_UNEXPECTED; }

    // Video Processor ya dejo el frame nuevo en RGBA. Lo pasamos a CUDA, VFG
    // inventa exactamente el punto medio (t=0.5) y regresamos el resultado a
    // una textura D3D11. NvVFX_Run(..., 0) es sincrono a proposito: asi no se
    // reutiliza una superficie mientras el modelo todavia la anda viendo.
    HRESULT CommitConvertedFrame() {
        if (!active_) return E_UNEXPECTED;
        const int oldCurrent = currentIndex_;
        currentIndex_ = pendingIndex_;
        pendingIndex_ = (pendingIndex_ + 1) % 2;
        if (!hasCurrent_) {
            hasCurrent_ = true;
            return S_FALSE;
        }
        previousIndex_ = oldCurrent;

        int status = TransferD3DToCuda(d3dSources_[previousIndex_], cudaSources_[0]);
        if (status == 0) status = TransferD3DToCuda(d3dSources_[currentIndex_], cudaSources_[1]);
        if (status != 0) return StatusFailure(status, L"NvCVImage_Transfer entrada");

        status = api_.setImage(effect_, "SrcImage0", cudaSources_[0]);
        if (status == 0) status = api_.setImage(effect_, "SrcImage1", cudaSources_[1]);
        if (status == 0) status = api_.setU32(effect_, "ShotChange", 0);
        if (status == 0) status = api_.setU32(effect_, "FrameIndex", 1);
        if (status != 0) return StatusFailure(status, L"NvVFX_SetImage/SetU32");

        LARGE_INTEGER frequency{};
        LARGE_INTEGER started{};
        LARGE_INTEGER finished{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&started);
        status = api_.run(effect_, 0);
        QueryPerformanceCounter(&finished);
        if (frequency.QuadPart > 0) {
            lastFlowMs_ = static_cast<double>(finished.QuadPart - started.QuadPart) * 1000.0 /
                          static_cast<double>(frequency.QuadPart);
        }
        if (status != 0) return StatusFailure(status, L"NvVFX_Run");

        status = TransferCudaToD3D(cudaOutput_, d3dOutput_);
        if (status != 0) return StatusFailure(status, L"NvCVImage_Transfer salida");

        wchar_t line[160]{};
        _snwprintf_s(line, _countof(line), _TRUNCATE,
                     L"VFGTEL modelo=%s run=%.3f ms multiplier=2 t=0.5",
                     ModelName().c_str(), lastFlowMs_);
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
        if (!active_ || !hasCurrent_ || previousIndex_ == currentIndex_) return E_UNEXPECTED;
        return Draw(generatedView_, contentRect);
    }

private:
    using NvStatus = int;
    using CreateEffectFn = NvStatus(__cdecl*)(const char*, void**);
    using DestroyEffectFn = NvStatus(__cdecl*)(void*);
    using SetU32Fn = NvStatus(__cdecl*)(void*, const char*, unsigned);
    using SetImageFn = NvStatus(__cdecl*)(void*, const char*, void*);
    using SetCudaStreamFn = NvStatus(__cdecl*)(void*, const char*, void*);
    using LoadEffectFn = NvStatus(__cdecl*)(void*);
    using RunEffectFn = NvStatus(__cdecl*)(void*, int);
    using CudaStreamCreateFn = NvStatus(__cdecl*)(void**);
    using CudaStreamDestroyFn = NvStatus(__cdecl*)(void*);
    using ImageCreateFn = NvStatus(__cdecl*)(unsigned, unsigned, int, int,
                                             unsigned, unsigned, unsigned, void**);
    using ImageDeallocFn = NvStatus(__cdecl*)(void*);
    using ImageDestroyFn = NvStatus(__cdecl*)(void*);
    using ImageFromD3DFn = NvStatus(__cdecl*)(void*, ID3D11Texture2D*);
    using ImageMapFn = NvStatus(__cdecl*)(void*, void*);
    using ImageUnmapFn = NvStatus(__cdecl*)(void*, void*);
    using ImageTransferFn = NvStatus(__cdecl*)(const void*, void*, float, void*, void*);
    using ErrorStringFn = const char*(__cdecl*)(NvStatus);

    struct Api {
        CreateEffectFn createEffect = nullptr;
        DestroyEffectFn destroyEffect = nullptr;
        SetU32Fn setU32 = nullptr;
        SetImageFn setImage = nullptr;
        SetCudaStreamFn setCudaStream = nullptr;
        LoadEffectFn load = nullptr;
        RunEffectFn run = nullptr;
        CudaStreamCreateFn cudaStreamCreate = nullptr;
        CudaStreamDestroyFn cudaStreamDestroy = nullptr;
        ImageCreateFn imageCreate = nullptr;
        ImageDeallocFn imageDealloc = nullptr;
        ImageDestroyFn imageDestroy = nullptr;
        ImageFromD3DFn imageFromD3D = nullptr;
        ImageMapFn imageMap = nullptr;
        ImageUnmapFn imageUnmap = nullptr;
        ImageTransferFn imageTransfer = nullptr;
        ErrorStringFn errorString = nullptr;
    };

    static constexpr int kNvCvRgba = 6;
    static constexpr int kNvCvU8 = 1;
    static constexpr unsigned kNvCvInterleaved = 0;
    static constexpr unsigned kNvCvCpu = 0;
    static constexpr unsigned kNvCvGpu = 1;

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

    static std::wstring Join(const std::wstring& base, const wchar_t* child) {
        if (base.empty()) return child;
        if (base.back() == L'\\' || base.back() == L'/') return base + child;
        return base + L"\\" + child;
    }

    static bool FileExists(const std::wstring& path) {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
               !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    static std::wstring Environment(const wchar_t* name) {
        const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
        if (!needed) return {};
        std::wstring value(needed, L'\0');
        const DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
        if (!written || written >= needed) return {};
        value.resize(written);
        return value;
    }

    static std::wstring ExecutableDirectory() {
        std::wstring path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                               static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) return {};
        path.resize(length);
        const size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
    }

    static std::wstring Utf8ToWide(const char* text) {
        if (!text || !*text) return {};
        const int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        if (needed <= 0) return {};
        std::wstring result(static_cast<size_t>(needed), L'\0');
        if (!MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), needed)) return {};
        if (!result.empty() && result.back() == L'\0') result.pop_back();
        return result;
    }

    bool RuntimeComplete(const std::wstring& directory) const {
        static constexpr const wchar_t* files[] = {
            L"cudart64_12.dll", L"NVCVImage.dll", L"nvngxruntime.dll",
            L"NVVideoEffects.dll", L"nvVFXVideoFrameGeneration.dll"
        };
        for (const wchar_t* file : files) {
            if (!FileExists(Join(directory, file))) return false;
        }
        return true;
    }

    HRESULT FindRuntime() {
        std::vector<std::wstring> candidates;
        const std::wstring overridePath = Environment(L"DAYAAH_VFG_RUNTIME");
        if (!overridePath.empty()) candidates.push_back(overridePath);
        const std::wstring executable = ExecutableDirectory();
        if (!executable.empty()) {
            candidates.push_back(Join(executable, L"runtime\\nvidia-vfg"));
            candidates.push_back(executable);
        }
        const std::wstring sdkPath = Environment(L"NV_VIDEO_EFFECTS_PATH");
        if (!sdkPath.empty()) {
            candidates.push_back(sdkPath);
            candidates.push_back(Join(sdkPath, L"bin"));
            candidates.push_back(Join(sdkPath, L"bin\\x64"));
        }
        const std::wstring programFiles = Environment(L"ProgramFiles");
        if (!programFiles.empty()) {
            candidates.push_back(Join(programFiles,
                L"NVIDIA Corporation\\NVIDIA Video Effects"));
            candidates.push_back(Join(programFiles,
                L"NVIDIA Corporation\\NVIDIA Video Effects\\bin"));
        }
        for (const std::wstring& candidate : candidates) {
            if (RuntimeComplete(candidate)) {
                runtimeDirectory_ = candidate;
                return S_OK;
            }
        }
        error_ = L"Falta el runtime NVIDIA VFG. Usa el ZIP NVIDIA-VFG o coloca "
                 L"sus cinco DLL en runtime\\nvidia-vfg.";
        return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    }

    void PrependRuntimeToPath() {
        oldPath_ = Environment(L"PATH");
        std::wstring combined = runtimeDirectory_;
        if (!oldPath_.empty()) combined += L";" + oldPath_;
        pathChanged_ = SetEnvironmentVariableW(L"PATH", combined.c_str()) == TRUE;
    }

    void RestorePath() {
        if (pathChanged_) {
            SetEnvironmentVariableW(L"PATH", oldPath_.empty() ? nullptr : oldPath_.c_str());
        }
        pathChanged_ = false;
        oldPath_.clear();
    }

    HMODULE LoadModule(const wchar_t* name) {
        const std::wstring path = Join(runtimeDirectory_, name);
        HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module) modules_.push_back(module);
        return module;
    }

    HRESULT LoadRuntime() {
        PrependRuntimeToPath();
        if (!LoadModule(L"cudart64_12.dll")) return ModuleFailure(L"cudart64_12.dll");
        HMODULE image = LoadModule(L"NVCVImage.dll");
        if (!image) return ModuleFailure(L"NVCVImage.dll");
        if (!LoadModule(L"nvngxruntime.dll")) return ModuleFailure(L"nvngxruntime.dll");
        HMODULE effects = LoadModule(L"NVVideoEffects.dll");
        if (!effects) return ModuleFailure(L"NVVideoEffects.dll");
        if (!LoadModule(L"nvVFXVideoFrameGeneration.dll")) {
            return ModuleFailure(L"nvVFXVideoFrameGeneration.dll");
        }

        const bool effectApi =
            Resolve(effects, "NvVFX_CreateEffect", api_.createEffect) &&
            Resolve(effects, "NvVFX_DestroyEffect", api_.destroyEffect) &&
            Resolve(effects, "NvVFX_SetU32", api_.setU32) &&
            Resolve(effects, "NvVFX_SetImage", api_.setImage) &&
            Resolve(effects, "NvVFX_SetCudaStream", api_.setCudaStream) &&
            Resolve(effects, "NvVFX_Load", api_.load) &&
            Resolve(effects, "NvVFX_Run", api_.run) &&
            Resolve(effects, "NvVFX_CudaStreamCreate", api_.cudaStreamCreate) &&
            Resolve(effects, "NvVFX_CudaStreamDestroy", api_.cudaStreamDestroy);
        const bool imageApi =
            Resolve(image, "NvCVImage_Create", api_.imageCreate) &&
            Resolve(image, "NvCVImage_Dealloc", api_.imageDealloc) &&
            Resolve(image, "NvCVImage_Destroy", api_.imageDestroy) &&
            Resolve(image, "NvCVImage_InitFromD3D11Texture", api_.imageFromD3D) &&
            Resolve(image, "NvCVImage_MapResource", api_.imageMap) &&
            Resolve(image, "NvCVImage_UnmapResource", api_.imageUnmap) &&
            Resolve(image, "NvCVImage_Transfer", api_.imageTransfer) &&
            Resolve(image, "NvCV_GetErrorStringFromCode", api_.errorString);
        if (!effectApi || !imageApi) {
            error_ = L"El runtime NVIDIA VFG no expone la API esperada.";
            return E_NOINTERFACE;
        }
        return S_OK;
    }

    HRESULT ModuleFailure(const wchar_t* name) {
        const DWORD code = GetLastError();
        error_ = L"No pude cargar " + std::wstring(name) +
                 L" desde el runtime NVIDIA VFG (Windows " + std::to_wstring(code) + L").";
        return HRESULT_FROM_WIN32(code ? code : ERROR_MOD_NOT_FOUND);
    }

    HRESULT CreateTextures() {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width_;
        desc.Height = height_;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        // NvCVImage_Transfer no acepta en todos los drivers el cruce
        // CUDA RGBA8 -> textura D3D11 BGRA8. Mantener RGBA de punta a punta
        // evita el -9 "pixel format is not accommodated" visto en la RTX 5070.
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
        if (FAILED(hr)) error_ = L"No pude crear las texturas RGBA para NVIDIA VFG.";
        return hr;
    }

    int CreateCudaImage(void** image) {
        return api_.imageCreate(width_, height_, kNvCvRgba, kNvCvU8,
                                kNvCvInterleaved, kNvCvGpu, 32, image);
    }

    int WrapD3DTexture(ID3D11Texture2D* texture, void** image) {
        int status = api_.imageCreate(1, 1, kNvCvRgba, kNvCvU8,
                                      kNvCvInterleaved, kNvCvCpu, 1, image);
        if (status != 0) return status;
        status = api_.imageDealloc(*image);
        if (status == 0) status = api_.imageFromD3D(*image, texture);
        if (status != 0) DestroyImage(*image);
        return status;
    }

    HRESULT CreateImages() {
        // Primero registramos la textura D3D. Asi CUDA aprende qué adaptador
        // eligio Dayaah antes de crear el stream (importa en equipos con dos RTX).
        int status = 0;
        for (size_t i = 0; i < d3dSources_.size() && status == 0; ++i) {
            status = WrapD3DTexture(sourceTextures_[i], &d3dSources_[i]);
        }
        if (status == 0) status = WrapD3DTexture(generatedTexture_, &d3dOutput_);
        if (status == 0) status = api_.cudaStreamCreate(&cudaStream_);
        for (size_t i = 0; i < cudaSources_.size() && status == 0; ++i) {
            status = CreateCudaImage(&cudaSources_[i]);
        }
        if (status == 0) status = CreateCudaImage(&cudaOutput_);
        if (status == 0) status = CreateCudaImage(&transferScratch_);
        return status == 0 ? S_OK : StatusFailure(status, L"NvCVImage_Create/InitFromD3D11Texture");
    }

    HRESULT CreateEffect() {
        int status = api_.createEffect("VideoFrameGeneration", &effect_);
        if (status == 0) status = api_.setCudaStream(effect_, "CudaStream", cudaStream_);
        if (status == 0) status = api_.setU32(effect_, "InputWidth", width_);
        if (status == 0) status = api_.setU32(effect_, "InputHeight", height_);
        if (status == 0) {
            status = api_.setU32(effect_, "Mode", FrameGenerationVfgModel(requestedMode_));
        }
        if (status == 0) {
            status = api_.setU32(effect_, "AutomaticShotChangeDetectionEnabled", 1);
        }
        if (status == 0) status = api_.load(effect_);
        if (status == 0) status = api_.setImage(effect_, "DstImage0", cudaOutput_);
        if (status == 0) status = api_.setU32(effect_, "FrameMultiplier", 2);
        return status == 0 ? S_OK : StatusFailure(status, L"Inicializacion NvVFX VFG");
    }

    int TransferD3DToCuda(void* source, void* destination) {
        int status = api_.imageMap(source, cudaStream_);
        if (status != 0) return status;
        status = api_.imageTransfer(source, destination, 1.0f,
                                    cudaStream_, transferScratch_);
        const int unmap = api_.imageUnmap(source, cudaStream_);
        return status != 0 ? status : unmap;
    }

    int TransferCudaToD3D(void* source, void* destination) {
        int status = api_.imageMap(destination, cudaStream_);
        if (status != 0) return status;
        status = api_.imageTransfer(source, destination, 1.0f,
                                    cudaStream_, transferScratch_);
        const int unmap = api_.imageUnmap(destination, cudaStream_);
        return status != 0 ? status : unmap;
    }

    void DestroyImage(void*& image) {
        if (image && api_.imageDestroy) api_.imageDestroy(image);
        image = nullptr;
    }

    std::wstring ModelName() const {
        if (requestedMode_ == FrameGenerationMode::VfgHigh) return L"High";
        if (requestedMode_ == FrameGenerationMode::VfgMedium) return L"Medium";
        return L"Low";
    }

    HRESULT StatusFailure(int status, const wchar_t* operation) {
        std::wstring detail;
        if (api_.errorString) detail = Utf8ToWide(api_.errorString(status));
        error_ = operation + std::wstring(L" fallo (NVIDIA ") +
                 std::to_wstring(status) + L")";
        if (!detail.empty()) error_ += L": " + detail;
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF,
                            0x600 + (static_cast<unsigned>(status) & 0xff));
    }

    HRESULT Abort(HRESULT hr) {
        const std::wstring saved = error_;
        Shutdown();
        error_ = saved;
        return hr;
    }

    struct DrawConstants { float outputRect[4]; };
    static_assert(sizeof(DrawConstants) == 16);

    static constexpr const char* kShader = R"DAYAAH_VFG(
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
)DAYAAH_VFG";

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
            const HRESULT result = compile(kShader, std::strlen(kShader), "DayaahNvidiaVFG",
                nullptr, nullptr, entry, profile,
                D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
                0, blob, &errors);
            if (errors) {
                if (FAILED(result)) {
                    const char* bytes = static_cast<const char*>(errors->GetBufferPointer());
                    error_ = Utf8ToWide(std::string(bytes, errors->GetBufferSize()).c_str());
                }
                errors->Release();
            }
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
        if (FAILED(hr) && error_.empty()) error_ = L"No pude crear el shader de salida NVIDIA VFG.";
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

    Api api_{};
    std::vector<HMODULE> modules_;
    std::wstring runtimeDirectory_;
    std::wstring oldPath_;
    bool pathChanged_ = false;
    void* effect_ = nullptr;
    void* cudaStream_ = nullptr;
    std::array<void*, 2> d3dSources_{};
    void* d3dOutput_ = nullptr;
    std::array<void*, 2> cudaSources_{};
    void* cudaOutput_ = nullptr;
    void* transferScratch_ = nullptr;
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
    int previousIndex_ = 0;
    int currentIndex_ = 0;
    int pendingIndex_ = 0;
    bool active_ = false;
    bool hasCurrent_ = false;
    double lastFlowMs_ = 0.0;
    FrameGenWarpStats warpStats_{};
    FrameGenerationMode requestedMode_ = FrameGenerationMode::Off;
    std::wstring error_;
    std::wstring surfaceSummary_;
    std::wstring flowDiagnosticLine_;
};
