#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shlwapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "disco_effect.h"
#include "disco_secret.h"
#include "latency_estimator.h"
#include "localization.h"
#include "machine_identity.h"
#include "mode_selection.h"
#include "overlay_editor.h"
#include "overlay_renderer.h"
#include "performance_telemetry.h"
#include "window_behavior.h"
#include "video_effects_editor.h"
#include "video_effects_renderer.h"
#include "video_effects_settings.h"

// ============================================================================
// Dayaah Capture, explicado sin hablar como manual de licuadora
// ----------------------------------------------------------------------------
// El viaje del frame es mas o menos asi:
// capturadora -> Media Foundation -> guardamos SOLO el frame mas nuevo ->
// D3D11 lo convierte/dibuja -> overlays -> Present() -> tu monitor.
//
// Lo de guardar solo el mas nuevo es intencional: esto es un viewer de baja
// latencia, no una grabadora. Si un frame ya llego tarde, enseñarlo nomas
// aumentaria la cola y terminariamos jugando en el pasado jajaja.
// ============================================================================

constexpr UINT WM_DAYAAH_EFFECTS_ERROR = WM_APP + 19;
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

inline constexpr const wchar_t* kAppTitle = L"Dayaah Capture 2.2.0";
inline constexpr const wchar_t* kGithubUrl = L"https://github.com/dayitaah/Dayaah-Capture";
inline constexpr const wchar_t* kKofiUrl = L"https://ko-fi.com/dayaah";

static const PROPERTYKEY kPkeyDeviceFriendlyName = {
    {0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14
};

static const GUID kWaveSubtypeFloat = {
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}
};

// Algunos headers de MinGW se hacen mensos y no traen MF_LOW_LATENCY aunque
// Windows si lo soporta. Este es el GUID oficial puesto aqui para que el build
// portable no pierda justamente la funcion que mas nos importa.
static const GUID kMfLowLatency = {
    0x9c27891a, 0xed7a, 0x40e1, {0x88, 0xe8, 0xb2, 0x27, 0x27, 0xa0, 0x24, 0xee}
};

template <typename T> static void SafeRelease(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

// SRWLOCK ya viene con Windows y no necesita Initialize/Delete. Estas dos
// guarditas evitan que un return olvidado deje el candado puesto para siempre.
class SharedSrwGuard {
public:
    explicit SharedSrwGuard(SRWLOCK& lock) : lock_(lock) { AcquireSRWLockShared(&lock_); }
    ~SharedSrwGuard() { ReleaseSRWLockShared(&lock_); }
    SharedSrwGuard(const SharedSrwGuard&) = delete;
    SharedSrwGuard& operator=(const SharedSrwGuard&) = delete;
private:
    SRWLOCK& lock_;
};

class ExclusiveSrwGuard {
public:
    explicit ExclusiveSrwGuard(SRWLOCK& lock) : lock_(lock) { AcquireSRWLockExclusive(&lock_); }
    ~ExclusiveSrwGuard() { ReleaseSRWLockExclusive(&lock_); }
    ExclusiveSrwGuard(const ExclusiveSrwGuard&) = delete;
    ExclusiveSrwGuard& operator=(const ExclusiveSrwGuard&) = delete;
private:
    SRWLOCK& lock_;
};

// Estas ayuditas viven aqui porque casi todo el programa necesita saber donde
// esta el EXE, escribir el log o convertir un HRESULT feo en algo entendible.
static std::wstring ModuleDirectory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return path;
}

static std::wstring JoinPath(const std::wstring& left, const wchar_t* right) {
    wchar_t path[MAX_PATH]{};
    lstrcpynW(path, left.c_str(), MAX_PATH);
    PathAppendW(path, right);
    return path;
}

static void LogLine(const wchar_t* format, ...) {
    static CRITICAL_SECTION lock;
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, [](PINIT_ONCE, PVOID, PVOID*) -> BOOL {
        InitializeCriticalSection(&lock);
        return TRUE;
    }, nullptr, nullptr);

    wchar_t message[2048]{};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, _countof(message), _TRUNCATE, format, args);
    va_end(args);

    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t line[2300]{};
    _snwprintf_s(line, _countof(line), _TRUNCATE,
                 L"%02u:%02u:%02u.%03u  %s\r\n",
                 time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, message);

    int bytesNeeded = WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
    if (bytesNeeded <= 0) return;
    std::vector<char> utf8(static_cast<size_t>(bytesNeeded));
    if (WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8.data(), bytesNeeded,
                            nullptr, nullptr) <= 0) return;

    EnterCriticalSection(&lock);
    std::wstring path = JoinPath(ModuleDirectory(), L"DayaahCapture.log");
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size() - 1), &written, nullptr);
        CloseHandle(file);
    }
    LeaveCriticalSection(&lock);
}

static std::wstring HrText(HRESULT hr) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, static_cast<DWORD>(hr), 0,
                   reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = buffer ? buffer : L"Error desconocido";
    if (buffer) LocalFree(buffer);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    wchar_t code[32]{};
    _snwprintf_s(code, _countof(code), _TRUNCATE, L" (0x%08lX)", static_cast<unsigned long>(hr));
    result += code;
    return result;
}

static std::wstring GuidFormatName(const GUID& subtype) {
    if (subtype == MFVideoFormat_NV12) return L"NV12";
    if (subtype == MFVideoFormat_YUY2) return L"YUY2";
    if (subtype == MFVideoFormat_I420) return L"I420";
    if (subtype == MFVideoFormat_P010) return L"P010";
    if (subtype == MFVideoFormat_MJPG) return L"MJPEG";
    if (subtype == MFVideoFormat_RGB32) return L"XRGB";
    return L"Otro";
}

static bool SupportedRawSubtype(const GUID& subtype) {
    return subtype == MFVideoFormat_NV12 || subtype == MFVideoFormat_YUY2 ||
           subtype == MFVideoFormat_I420 || subtype == MFVideoFormat_P010 ||
           subtype == MFVideoFormat_RGB32;
}

static DXGI_FORMAT RendererFormat(const GUID& subtype) {
    if (subtype == MFVideoFormat_NV12 || subtype == MFVideoFormat_I420) return DXGI_FORMAT_NV12;
    if (subtype == MFVideoFormat_YUY2) return DXGI_FORMAT_YUY2;
    if (subtype == MFVideoFormat_P010) return DXGI_FORMAT_P010;
    if (subtype == MFVideoFormat_RGB32) return DXGI_FORMAT_B8G8R8A8_UNORM;
    return DXGI_FORMAT_UNKNOWN;
}

static UINT TightRowPitch(const GUID& subtype, UINT width) {
    if (subtype == MFVideoFormat_YUY2 || subtype == MFVideoFormat_P010) return width * 2;
    if (subtype == MFVideoFormat_RGB32) return width * 4;
    return width;
}

static size_t TightFrameSize(const GUID& subtype, UINT width, UINT height) {
    if (subtype == MFVideoFormat_NV12 || subtype == MFVideoFormat_I420) {
        return static_cast<size_t>(width) * height * 3 / 2;
    }
    if (subtype == MFVideoFormat_P010) return static_cast<size_t>(width) * height * 3;
    if (subtype == MFVideoFormat_RGB32) return static_cast<size_t>(width) * height * 4;
    return static_cast<size_t>(width) * height * 2;
}

static const wchar_t* FormatNotice(const GUID& subtype, AppLanguage language) {
    if (subtype == MFVideoFormat_NV12) return Tr(language,
        L"Recomendado: bajo ancho de banda y ruta directa por GPU.",
        L"Recommended: low bandwidth and a direct GPU path.");
    if (subtype == MFVideoFormat_YUY2) return Tr(language,
        L"Penalización media: usa más ancho de banda que NV12.",
        L"Medium penalty: uses more bandwidth than NV12.");
    if (subtype == MFVideoFormat_RGB32) return Tr(language,
        L"Penalización alta: 4 bytes por píxel y mucho ancho de banda.",
        L"High penalty: 4 bytes per pixel and high bandwidth use.");
    if (subtype == MFVideoFormat_I420) return Tr(language,
        L"Penalización posible: conversión CPU a NV12 antes del render.",
        L"Possible penalty: CPU conversion to NV12 before rendering.");
    if (subtype == MFVideoFormat_P010) return Tr(language,
        L"10 bits: más ancho de banda y compatibilidad limitada; no activa HDR por sí solo.",
        L"10-bit: higher bandwidth and limited compatibility; it does not enable HDR by itself.");
    return L"";
}

// Un VideoMode es una combinacion REAL anunciada por la capturadora. No
// inventamos 1080p120 porque se vea bonito en el menu: si el driver no lo
// reporta, no existe para nosotros.
struct VideoMode {
    UINT32 width = 0;
    UINT32 height = 0;
    UINT32 fpsNum = 0;
    UINT32 fpsDen = 1;
    GUID subtype{};
    DWORD nativeIndex = 0;

    double Fps() const {
        return fpsDen ? static_cast<double>(fpsNum) / static_cast<double>(fpsDen) : 0.0;
    }

    std::wstring Key() const {
        wchar_t value[160]{};
        _snwprintf_s(value, _countof(value), _TRUNCATE, L"%ux%u|%u/%u|%s",
                     width, height, fpsNum, fpsDen, GuidFormatName(subtype).c_str());
        return value;
    }

    std::wstring Label() const {
        wchar_t value[200]{};
        const double fps = Fps();
        if (std::fabs(fps - std::round(fps)) < 0.01) {
            _snwprintf_s(value, _countof(value), _TRUNCATE, L"%u x %u  -  %.0f fps  -  %s",
                         width, height, fps, GuidFormatName(subtype).c_str());
        } else {
            _snwprintf_s(value, _countof(value), _TRUNCATE, L"%u x %u  -  %.2f fps  -  %s",
                         width, height, fps, GuidFormatName(subtype).c_str());
        }
        return value;
    }
};

struct VideoDeviceInfo {
    std::wstring name;
    IMFActivate* activate = nullptr;
    std::vector<VideoMode> modes;
};

struct AudioDeviceInfo {
    std::wstring name;
    IMMDevice* device = nullptr;
};

struct RenderTiming {
    double processingMs = 0.0;
    double presentMs = 0.0;
};

struct ViewerNotice {
    std::wstring text;
    ULONGLONG startedAt = 0;
    ULONGLONG durationMs = 0;
};

// Aqui vive la parte que convierte un frame crudo de la capturadora en pixeles
// visibles. D3D11 Video Processor hace el trabajo pesado en la GPU y la swap
// chain se encarga de entregarlo al monitor.
class D3DVideoRenderer {
public:
    ~D3DVideoRenderer() { Shutdown(); }

    HRESULT Initialize(HWND window, UINT32 inputWidth, UINT32 inputHeight,
                       const GUID& subtype, bool lowLatency, bool inputFullRange) {
        Shutdown();
        window_ = window;
        inputWidth_ = inputWidth;
        inputHeight_ = inputHeight;
        inputFormat_ = RendererFormat(subtype);
        inputIsRgb_ = subtype == MFVideoFormat_RGB32;
        if (inputFormat_ == DXGI_FORMAT_UNKNOWN) return MF_E_UNSUPPORTED_D3D_TYPE;
        lowLatency_.store(lowLatency);
        inputFullRange_.store(inputFullRange);

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
        D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL obtained{};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                       levels, _countof(levels), D3D11_SDK_VERSION,
                                       &device_, &obtained, &context_);
        if (hr == E_INVALIDARG) {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   levels + 1, _countof(levels) - 1, D3D11_SDK_VERSION,
                                   &device_, &obtained, &context_);
        }
        if (FAILED(hr)) return hr;

        ID3D10Multithread* multithread = nullptr;
        if (SUCCEEDED(context_->QueryInterface(IID_PPV_ARGS(&multithread)))) {
            multithread->SetMultithreadProtected(TRUE);
            multithread->Release();
        }

        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory2* factory = nullptr;
        hr = device_->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
        if (SUCCEEDED(hr)) hr = dxgiDevice->GetAdapter(&adapter);
        if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(&factory));
        SafeRelease(dxgiDevice);
        SafeRelease(adapter);
        if (FAILED(hr)) {
            SafeRelease(factory);
            return hr;
        }

        // Preguntamos antes de usar tearing. Cuando Windows y el monitor lo
        // permiten, esta es tambien la ruta que deja trabajar al VRR.
        allowTearing_ = false;
        IDXGIFactory5* factory5 = nullptr;
        if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory5)))) {
            BOOL supported = FALSE;
            if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                        &supported, sizeof(supported)))) {
                allowTearing_ = supported == TRUE;
            }
            factory5->Release();
        }

        RECT client{};
        GetClientRect(window_, &client);
        UINT width = std::max<LONG>(client.right - client.left, 16);
        UINT height = std::max<LONG>(client.bottom - client.top, 16);

        // Flip discard + dos buffers + latencia maxima de un frame. En pocas
        // palabras: una swap chain moderna sin hacer una fila del OXXO.
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        desc.Flags = allowTearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        hr = factory->CreateSwapChainForHwnd(device_, window_, &desc, nullptr, nullptr, &swapChain_);
        if (SUCCEEDED(hr)) factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
        SafeRelease(factory);
        if (FAILED(hr)) return hr;

        IDXGISwapChain2* swap2 = nullptr;
        if (SUCCEEDED(swapChain_->QueryInterface(IID_PPV_ARGS(&swap2)))) {
            swap2->SetMaximumFrameLatency(1);
            swap2->Release();
        }

        hr = device_->QueryInterface(IID_PPV_ARGS(&videoDevice_));
        if (SUCCEEDED(hr)) hr = context_->QueryInterface(IID_PPV_ARGS(&videoContext_));
        if (SUCCEEDED(hr)) hr = overlayRenderer_.Initialize();
        if (FAILED(hr)) return hr;

        // Esta textura recibe exactamente el formato nativo elegido. Evitamos
        // que Media Foundation meta conversiones sorpresa por CPU.
        D3D11_TEXTURE2D_DESC inputDesc{};
        inputDesc.Width = inputWidth_;
        inputDesc.Height = inputHeight_;
        inputDesc.MipLevels = 1;
        inputDesc.ArraySize = 1;
        inputDesc.Format = inputFormat_;
        inputDesc.SampleDesc.Count = 1;
        inputDesc.Usage = D3D11_USAGE_DEFAULT;
        inputDesc.BindFlags = D3D11_BIND_DECODER;
        hr = device_->CreateTexture2D(&inputDesc, nullptr, &inputTexture_);
        if (FAILED(hr)) return hr;

        const HRESULT effectsHr = effectsRenderer_.Initialize(device_);
        if (FAILED(effectsHr)) {
            LogLine(L"Shaders no disponibles: %s; %hs", HrText(effectsHr).c_str(),
                    effectsRenderer_.Error().c_str());
            effectsRenderer_.Shutdown();
        }
        hr = RebuildVideoProcessor(width, height);
        if (SUCCEEDED(hr)) {
            LogLine(L"D3D11 listo: entrada %ux%u %s, tearing=%s, disco RGB=%s",
                    inputWidth_, inputHeight_, GuidFormatName(subtype).c_str(),
                    allowTearing_ ? L"si" : L"no",
                    hueFilterSupported_.load() ? L"si" : L"no");
        }
        return hr;
    }

    HRESULT Resize(UINT width, UINT height) {
        if (!swapChain_ || width < 16 || height < 16) return S_OK;
        overlayRenderer_.BeforeResize();
        effectsRenderer_.BeforeResize();
        SafeRelease(outputView_);
        SafeRelease(inputView_);
        SafeRelease(processor_);
        SafeRelease(processorEnum_);
        context_->ClearState();
        HRESULT hr = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN,
                                               allowTearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
        if (FAILED(hr)) return hr;
        return RebuildVideoProcessor(width, height);
    }

    HRESULT Render(const BYTE* data, size_t size, LONG sourceStride,
                   const std::shared_ptr<const OverlayConfig>& overlayConfig,
                   const OverlayStats& overlayStats, const VideoEffects& effects,
                   bool latencyHud, RenderTiming* timing) {
        if (!data || !inputTexture_ || !processor_ || !outputView_) return E_POINTER;
        LARGE_INTEGER frequency{};
        LARGE_INTEGER processingStarted{};
        LARGE_INTEGER processingFinished{};
        LARGE_INTEGER presentFinished{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&processingStarted);
        UINT rowPitch = inputWidth_;
        size_t expected = static_cast<size_t>(inputWidth_) * inputHeight_ * 3 / 2;
        if (inputFormat_ == DXGI_FORMAT_YUY2) {
            rowPitch = inputWidth_ * 2;
            expected = static_cast<size_t>(rowPitch) * inputHeight_;
        } else if (inputFormat_ == DXGI_FORMAT_P010) {
            rowPitch = inputWidth_ * 2;
            expected = static_cast<size_t>(inputWidth_) * inputHeight_ * 3;
        } else if (inputFormat_ == DXGI_FORMAT_B8G8R8A8_UNORM) {
            rowPitch = inputWidth_ * 4;
            expected = static_cast<size_t>(rowPitch) * inputHeight_;
        }
        if (size < expected || sourceStride != static_cast<LONG>(rowPitch)) return MF_E_BUFFERTOOSMALL;

        // Subimos el frame a la textura. De aqui en adelante el trabajo queda
        // en la GPU: conversion de color, escalado, efectos y overlays.
        context_->UpdateSubresource(inputTexture_, 0, nullptr, data, rowPitch,
                                    static_cast<UINT>(expected));

        ApplyColorSpace();
        ApplyDiscoEffect();

        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = inputView_;
        if (!effects.HasWork()) effectsUnavailableReported_ = false;
        const bool useEffects = effects.HasWork() && effectsRenderer_.Ready();
        HRESULT hr = videoContext_->VideoProcessorBlt(processor_,
            useEffects ? effectsRenderer_.InputView() : outputView_, 0, 1, &stream);
        if (SUCCEEDED(hr) && useEffects) hr = effectsRenderer_.Draw(context_, effects, contentRect_);
        if (FAILED(hr) && useEffects) {
            LogLine(L"Efectos desactivados tras error: %s", HrText(hr).c_str());
            effectsRenderer_.BeforeResize();
            effectsUnavailableReported_ = true;
            PostMessageW(window_, WM_DAYAAH_EFFECTS_ERROR, 0, static_cast<LPARAM>(hr));
            // Si un shader truena, reintentamos ESTE MISMO frame por la ruta
            // directa. Nada de tumbar audio y captura por un efecto coqueto.
            hr = videoContext_->VideoProcessorBlt(processor_, outputView_, 0, 1, &stream);
        }
        if (effects.HasWork() && !effectsRenderer_.Ready() && !effectsUnavailableReported_) {
            effectsUnavailableReported_ = true;
            PostMessageW(window_, WM_DAYAAH_EFFECTS_ERROR, 0, static_cast<LPARAM>(E_FAIL));
        }
        if (FAILED(hr)) return hr;
        hr = overlayRenderer_.Draw(overlayConfig, overlayStats, contentRect_, latencyHud);
        if (FAILED(hr)) return hr;
        const bool lowLatency = lowLatency_.load();
        QueryPerformanceCounter(&processingFinished);
        // Present(0) = no esperar VSync. Present(1) = sincronizar. El flag de
        // tearing solo se manda cuando DXGI confirmo que se puede usar.
        const HRESULT presentHr = swapChain_->Present(lowLatency ? 0 : 1,
            lowLatency && allowTearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
        QueryPerformanceCounter(&presentFinished);
        if (timing && frequency.QuadPart > 0) {
            timing->processingMs = static_cast<double>(processingFinished.QuadPart -
                                                        processingStarted.QuadPart) *
                                   1000.0 / static_cast<double>(frequency.QuadPart);
            timing->presentMs = static_cast<double>(presentFinished.QuadPart -
                                                     processingFinished.QuadPart) *
                                1000.0 / static_cast<double>(frequency.QuadPart);
        }
        return presentHr;
    }

    void SetLowLatency(bool enabled) { lowLatency_.store(enabled); }
    void SetInputFullRange(bool enabled) { inputFullRange_.store(enabled); }
    void SetDiscoMode(bool enabled) {
        const bool previous = discoMode_.exchange(enabled);
        if (enabled && !previous) discoStartTick_.store(GetTickCount64());
    }
    bool SupportsDiscoMode() const { return hueFilterSupported_.load(); }

    void Shutdown() {
        overlayRenderer_.Shutdown();
        effectsRenderer_.Shutdown();
        SafeRelease(outputView_);
        SafeRelease(inputView_);
        SafeRelease(processor_);
        SafeRelease(processorEnum_);
        SafeRelease(inputTexture_);
        SafeRelease(videoContext_);
        SafeRelease(videoDevice_);
        SafeRelease(swapChain_);
        SafeRelease(context_);
        SafeRelease(device_);
        colorSpaceApplied_ = false;
        inputIsRgb_ = false;
        hueFilterSupported_.store(false);
        saturationFilterSupported_ = false;
        discoFilterApplied_ = false;
        window_ = nullptr;
    }

private:
    void ApplyColorSpace() {
        if (!videoContext_ || !processor_) return;
        const bool fullRange = inputFullRange_.load();
        if (colorSpaceApplied_ && appliedInputFullRange_ == fullRange) return;

        // Peritas y manzanas: muchas capturadoras mandan negro=16 y blanco=235.
        // Si el usuario dice que la señal es rango completo usamos 0..255 para
        // que los negros no se vean lavados ni nos comamos detalle.
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputColor{};
        inputColor.YCbCr_Matrix = inputIsRgb_ ? 0 : 1;
        inputColor.RGB_Range = 0;
        inputColor.Nominal_Range = inputIsRgb_ || fullRange
            ? D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255
            : D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputColor{};
        outputColor.RGB_Range = 0;
        outputColor.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
        videoContext_->VideoProcessorSetStreamColorSpace(processor_, 0, &inputColor);
        videoContext_->VideoProcessorSetOutputColorSpace(processor_, &outputColor);
        appliedInputFullRange_ = fullRange;
        colorSpaceApplied_ = true;
    }

    void ApplyDiscoEffect() {
        if (!videoContext_ || !processor_) return;
        const bool enabled = discoMode_.load() && hueFilterSupported_.load();
        if (!enabled) {
            if (discoFilterApplied_) {
                videoContext_->VideoProcessorSetStreamFilter(
                    processor_, 0, D3D11_VIDEO_PROCESSOR_FILTER_HUE,
                    FALSE, hueRange_.Default);
                if (saturationFilterSupported_) {
                    videoContext_->VideoProcessorSetStreamFilter(
                        processor_, 0, D3D11_VIDEO_PROCESSOR_FILTER_SATURATION,
                        FALSE, saturationRange_.Default);
                }
                discoFilterApplied_ = false;
            }
            return;
        }

        const ULONGLONG start = discoStartTick_.load();
        const ULONGLONG elapsed = GetTickCount64() - start;
        const int hue = ComputeDiscoHueLevel(
            hueRange_.Minimum, hueRange_.Maximum, elapsed);
        videoContext_->VideoProcessorSetStreamFilter(
            processor_, 0, D3D11_VIDEO_PROCESSOR_FILTER_HUE, TRUE, hue);

        if (!discoFilterApplied_ && saturationFilterSupported_) {
            const int saturation = ComputeDiscoSaturationLevel(
                saturationRange_.Default, saturationRange_.Maximum);
            videoContext_->VideoProcessorSetStreamFilter(
                processor_, 0, D3D11_VIDEO_PROCESSOR_FILTER_SATURATION,
                TRUE, saturation);
        }
        discoFilterApplied_ = true;
    }

    HRESULT RebuildVideoProcessor(UINT outputWidth, UINT outputHeight) {
        colorSpaceApplied_ = false;
        hueFilterSupported_.store(false);
        saturationFilterSupported_ = false;
        discoFilterApplied_ = false;
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
        content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputFrameRate.Numerator = 60;
        content.InputFrameRate.Denominator = 1;
        content.InputWidth = inputWidth_;
        content.InputHeight = inputHeight_;
        content.OutputFrameRate.Numerator = 60;
        content.OutputFrameRate.Denominator = 1;
        content.OutputWidth = outputWidth;
        content.OutputHeight = outputHeight;
        content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

        HRESULT hr = videoDevice_->CreateVideoProcessorEnumerator(&content, &processorEnum_);
        UINT formatSupport = 0;
        if (SUCCEEDED(hr)) {
            hr = processorEnum_->CheckVideoProcessorFormat(inputFormat_, &formatSupport);
            if (SUCCEEDED(hr) && !(formatSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)) {
                hr = MF_E_UNSUPPORTED_D3D_TYPE;
            }
        }
        if (SUCCEEDED(hr)) hr = videoDevice_->CreateVideoProcessor(processorEnum_, 0, &processor_);
        if (FAILED(hr)) return hr;

        D3D11_VIDEO_PROCESSOR_CAPS processorCaps{};
        if (SUCCEEDED(processorEnum_->GetVideoProcessorCaps(&processorCaps))) {
            if ((processorCaps.FilterCaps & D3D11_VIDEO_PROCESSOR_FILTER_CAPS_HUE) &&
                SUCCEEDED(processorEnum_->GetVideoProcessorFilterRange(
                    D3D11_VIDEO_PROCESSOR_FILTER_HUE, &hueRange_))) {
                hueFilterSupported_.store(true);
            }
            if ((processorCaps.FilterCaps & D3D11_VIDEO_PROCESSOR_FILTER_CAPS_SATURATION) &&
                SUCCEEDED(processorEnum_->GetVideoProcessorFilterRange(
                    D3D11_VIDEO_PROCESSOR_FILTER_SATURATION, &saturationRange_))) {
                saturationFilterSupported_ = true;
            }
        }

        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc{};
        inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        inputViewDesc.Texture2D.MipSlice = 0;
        inputViewDesc.Texture2D.ArraySlice = 0;
        hr = videoDevice_->CreateVideoProcessorInputView(inputTexture_, processorEnum_,
                                                         &inputViewDesc, &inputView_);
        if (FAILED(hr)) return hr;

        ID3D11Texture2D* backBuffer = nullptr;
        hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr)) return hr;
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc{};
        outputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        outputViewDesc.Texture2D.MipSlice = 0;
        hr = videoDevice_->CreateVideoProcessorOutputView(backBuffer, processorEnum_,
                                                          &outputViewDesc, &outputView_);
        backBuffer->Release();
        if (FAILED(hr)) return hr;

        // Conservamos la proporcion y dejamos barras negras si hace falta. No
        // vamos a estirar a Cloud para que parezca tamal nomas por llenar todo.
        RECT source{0, 0, static_cast<LONG>(inputWidth_), static_cast<LONG>(inputHeight_)};
        RECT target{0, 0, static_cast<LONG>(outputWidth), static_cast<LONG>(outputHeight)};
        const double inputAspect = static_cast<double>(inputWidth_) / inputHeight_;
        const double outputAspect = static_cast<double>(outputWidth) / outputHeight;
        if (outputAspect > inputAspect) {
            LONG fitted = static_cast<LONG>(outputHeight * inputAspect);
            target.left = (static_cast<LONG>(outputWidth) - fitted) / 2;
            target.right = target.left + fitted;
        } else {
            LONG fitted = static_cast<LONG>(outputWidth / inputAspect);
            target.top = (static_cast<LONG>(outputHeight) - fitted) / 2;
            target.bottom = target.top + fitted;
        }
        videoContext_->VideoProcessorSetStreamSourceRect(processor_, 0, TRUE, &source);
        videoContext_->VideoProcessorSetStreamDestRect(processor_, 0, TRUE, &target);
        videoContext_->VideoProcessorSetOutputTargetRect(processor_, TRUE, &target);
        contentRect_ = target;

        ApplyColorSpace();

        D3D11_VIDEO_COLOR clear{};
        clear.RGBA.A = 1.0f;
        videoContext_->VideoProcessorSetOutputBackgroundColor(processor_, FALSE, &clear);
        const HRESULT effectsHr = effectsRenderer_.CreateTarget(device_, videoDevice_, processorEnum_, swapChain_);
        if (FAILED(effectsHr)) LogLine(L"Ruta directa sin shaders: %s", HrText(effectsHr).c_str());
        effectsUnavailableReported_ = false;
        return overlayRenderer_.CreateTarget(swapChain_);
    }

    HWND window_ = nullptr;
    UINT32 inputWidth_ = 0;
    UINT32 inputHeight_ = 0;
    DXGI_FORMAT inputFormat_ = DXGI_FORMAT_UNKNOWN;
    bool inputIsRgb_ = false;
    std::atomic<bool> lowLatency_{true};
    std::atomic<bool> inputFullRange_{false};
    std::atomic<bool> discoMode_{false};
    std::atomic<ULONGLONG> discoStartTick_{0};
    std::atomic<bool> hueFilterSupported_{false};
    D3D11_VIDEO_PROCESSOR_FILTER_RANGE hueRange_{};
    D3D11_VIDEO_PROCESSOR_FILTER_RANGE saturationRange_{};
    bool colorSpaceApplied_ = false;
    bool appliedInputFullRange_ = false;
    bool saturationFilterSupported_ = false;
    bool discoFilterApplied_ = false;
    bool allowTearing_ = false;
    RECT contentRect_{};
    OverlayRenderer overlayRenderer_;
    VideoEffectsRenderer effectsRenderer_;
    bool effectsUnavailableReported_ = false;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain1* swapChain_ = nullptr;
    ID3D11VideoDevice* videoDevice_ = nullptr;
    ID3D11VideoContext* videoContext_ = nullptr;
    ID3D11Texture2D* inputTexture_ = nullptr;
    ID3D11VideoProcessorEnumerator* processorEnum_ = nullptr;
    ID3D11VideoProcessor* processor_ = nullptr;
    ID3D11VideoProcessorInputView* inputView_ = nullptr;
    ID3D11VideoProcessorOutputView* outputView_ = nullptr;
};

// WASAPI puede darnos 8/16/24/32 bits o float. Estas dos funciones traducen
// todo a float para procesarlo igual y luego lo regresan al formato de salida.
static bool WaveFormatIsFloat(const WAVEFORMATEX* format) {
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return ext->SubFormat == kWaveSubtypeFloat;
    }
    return false;
}

static float ReadWaveSample(const BYTE* data, UINT32 sampleIndex, const WAVEFORMATEX* format) {
    if (WaveFormatIsFloat(format) && format->wBitsPerSample == 32) {
        return reinterpret_cast<const float*>(data)[sampleIndex];
    }
    if (format->wBitsPerSample == 16) {
        return reinterpret_cast<const int16_t*>(data)[sampleIndex] / 32768.0f;
    }
    if (format->wBitsPerSample == 24) {
        const BYTE* value = data + sampleIndex * 3;
        int32_t sample = value[0] | (value[1] << 8) | (value[2] << 16);
        if (sample & 0x800000) sample |= ~0xFFFFFF;
        return sample / 8388608.0f;
    }
    if (format->wBitsPerSample == 32) {
        return reinterpret_cast<const int32_t*>(data)[sampleIndex] / 2147483648.0f;
    }
    if (format->wBitsPerSample == 8) {
        return (static_cast<int>(data[sampleIndex]) - 128) / 128.0f;
    }
    return 0.0f;
}

static void WriteWaveSample(BYTE* data, UINT32 sampleIndex,
                            const WAVEFORMATEX* format, float value) {
    value = std::max(-1.0f, std::min(1.0f, value));
    if (WaveFormatIsFloat(format) && format->wBitsPerSample == 32) {
        reinterpret_cast<float*>(data)[sampleIndex] = value;
    } else if (format->wBitsPerSample == 16) {
        reinterpret_cast<int16_t*>(data)[sampleIndex] = static_cast<int16_t>(value * 32767.0f);
    } else if (format->wBitsPerSample == 24) {
        int32_t sample = static_cast<int32_t>(value * 8388607.0f);
        BYTE* destination = data + sampleIndex * 3;
        destination[0] = static_cast<BYTE>(sample);
        destination[1] = static_cast<BYTE>(sample >> 8);
        destination[2] = static_cast<BYTE>(sample >> 16);
    } else if (format->wBitsPerSample == 32) {
        reinterpret_cast<int32_t*>(data)[sampleIndex] = static_cast<int32_t>(value * 2147483647.0f);
    } else if (format->wBitsPerSample == 8) {
        data[sampleIndex] = static_cast<BYTE>((value * 127.0f) + 128.0f);
    }
}

// Puente de audio: toma la entrada de la capturadora y la manda al dispositivo
// de reproduccion predeterminado. Lleva su propio hilo para no amarrar el video.
class AudioBridge {
public:
    ~AudioBridge() { Stop(); }

    HRESULT Start(IMMDevice* captureDevice, int gainDb) {
        Stop();
        if (!captureDevice) return S_FALSE;
        InterlockedExchange(&gainTenthsDb_, gainDb * 10);
        InterlockedExchange(&muted_, 0);

        HRESULT hr = captureDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                             reinterpret_cast<void**>(&captureClient_));
        if (FAILED(hr)) return hr;

        IMMDeviceEnumerator* enumerator = nullptr;
        IMMDevice* renderDevice = nullptr;
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&enumerator));
        if (SUCCEEDED(hr)) hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &renderDevice);
        SafeRelease(enumerator);
        if (SUCCEEDED(hr)) {
            hr = renderDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                        reinterpret_cast<void**>(&renderClient_));
        }
        SafeRelease(renderDevice);
        if (FAILED(hr)) return hr;

        hr = captureClient_->GetMixFormat(&captureFormat_);
        if (SUCCEEDED(hr)) hr = renderClient_->GetMixFormat(&renderFormat_);
        if (FAILED(hr)) return hr;

        captureEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        renderEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!captureEvent_ || !renderEvent_ || !stopEvent_) return HRESULT_FROM_WIN32(GetLastError());

        DWORD streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST;
        hr = captureClient_->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, 0, 0,
                                        captureFormat_, nullptr);
        if (SUCCEEDED(hr)) {
            hr = renderClient_->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, 0, 0,
                                            renderFormat_, nullptr);
        }
        if (SUCCEEDED(hr)) hr = captureClient_->SetEventHandle(captureEvent_);
        if (SUCCEEDED(hr)) hr = renderClient_->SetEventHandle(renderEvent_);
        if (SUCCEEDED(hr)) hr = captureClient_->GetService(IID_PPV_ARGS(&captureService_));
        if (SUCCEEDED(hr)) hr = renderClient_->GetService(IID_PPV_ARGS(&renderService_));
        if (FAILED(hr)) return hr;

        UINT32 renderFrames = 0;
        hr = renderClient_->GetBufferSize(&renderFrames);
        if (SUCCEEDED(hr)) {
            BYTE* initial = nullptr;
            hr = renderService_->GetBuffer(renderFrames, &initial);
            if (SUCCEEDED(hr)) renderService_->ReleaseBuffer(renderFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        }
        if (FAILED(hr)) return hr;

        running_.store(true);
        thread_ = CreateThread(nullptr, 0, ThreadEntry, this, 0, nullptr);
        if (!thread_) return HRESULT_FROM_WIN32(GetLastError());
        LogLine(L"Audio listo: %u Hz/%u ch -> %u Hz/%u ch, ganancia %+d dB",
                captureFormat_->nSamplesPerSec, captureFormat_->nChannels,
                renderFormat_->nSamplesPerSec, renderFormat_->nChannels, gainDb);
        return S_OK;
    }

    void Stop() {
        if (stopEvent_) SetEvent(stopEvent_);
        running_.store(false);
        if (thread_) {
            WaitForSingleObject(thread_, 1500);
            CloseHandle(thread_);
            thread_ = nullptr;
        }
        if (captureClient_) captureClient_->Stop();
        if (renderClient_) renderClient_->Stop();
        SafeRelease(captureService_);
        SafeRelease(renderService_);
        SafeRelease(captureClient_);
        SafeRelease(renderClient_);
        if (captureFormat_) CoTaskMemFree(captureFormat_);
        if (renderFormat_) CoTaskMemFree(renderFormat_);
        captureFormat_ = nullptr;
        renderFormat_ = nullptr;
        if (captureEvent_) CloseHandle(captureEvent_);
        if (renderEvent_) CloseHandle(renderEvent_);
        if (stopEvent_) CloseHandle(stopEvent_);
        captureEvent_ = renderEvent_ = stopEvent_ = nullptr;
        sourceStereo_.clear();
        outputStereo_.clear();
        sourceCursor_ = 0.0;
        outputRead_ = 0;
    }

    void SetGain(int db) { InterlockedExchange(&gainTenthsDb_, db * 10); }
    void ToggleMute() { InterlockedXor(&muted_, 1); }
    bool IsMuted() const { return InterlockedCompareExchange(const_cast<LONG*>(&muted_), 0, 0) != 0; }

private:
    static DWORD WINAPI ThreadEntry(void* context) {
        return static_cast<AudioBridge*>(context)->ThreadMain();
    }

    DWORD ThreadMain() {
        HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        DWORD taskIndex = 0;
        HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        renderClient_->Start();
        captureClient_->Start();

        // WASAPI nos despierta solo cuando hay audio que leer o espacio que
        // llenar. Menos vueltas inutiles, menos latencia y menos CPU.
        HANDLE events[] = {stopEvent_, captureEvent_, renderEvent_};
        while (running_.load()) {
            DWORD wait = WaitForMultipleObjects(_countof(events), events, FALSE, 1000);
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_OBJECT_0 + 1) DrainCapture();
            if (wait == WAIT_OBJECT_0 + 2) FillRender();
        }

        captureClient_->Stop();
        renderClient_->Stop();
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 0;
    }

    void DrainCapture() {
        UINT32 packetFrames = 0;
        while (SUCCEEDED(captureService_->GetNextPacketSize(&packetFrames)) && packetFrames) {
            BYTE* data = nullptr;
            DWORD flags = 0;
            UINT64 devicePosition = 0;
            UINT64 qpc = 0;
            if (FAILED(captureService_->GetBuffer(&data, &packetFrames, &flags,
                                                  &devicePosition, &qpc))) return;
            const UINT32 channels = captureFormat_->nChannels;
            for (UINT32 frame = 0; frame < packetFrames; ++frame) {
                float left = 0.0f;
                float right = 0.0f;
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                    left = ReadWaveSample(data, frame * channels, captureFormat_);
                    right = channels > 1 ? ReadWaveSample(data, frame * channels + 1, captureFormat_) : left;
                }
                sourceStereo_.push_back(left);
                sourceStereo_.push_back(right);
            }
            captureService_->ReleaseBuffer(packetFrames);
            ResampleAvailable();
        }
    }

    void ResampleAvailable() {
        // Interpolacion lineal sencilla para casar la frecuencia de la entrada
        // con la salida de Windows. Para un monitor de capturadora es rapida,
        // estable y no mete una biblioteca enorme al EXE.
        const size_t sourceFrames = sourceStereo_.size() / 2;
        const double step = static_cast<double>(captureFormat_->nSamplesPerSec) /
                            static_cast<double>(renderFormat_->nSamplesPerSec);
        while (sourceCursor_ + 1.0 < sourceFrames) {
            const size_t first = static_cast<size_t>(sourceCursor_);
            const double fraction = sourceCursor_ - first;
            const size_t second = first + 1;
            float left = static_cast<float>(sourceStereo_[first * 2] * (1.0 - fraction) +
                                            sourceStereo_[second * 2] * fraction);
            float right = static_cast<float>(sourceStereo_[first * 2 + 1] * (1.0 - fraction) +
                                             sourceStereo_[second * 2 + 1] * fraction);
            outputStereo_.push_back(left);
            outputStereo_.push_back(right);
            sourceCursor_ += step;
        }
        const size_t consumed = static_cast<size_t>(sourceCursor_);
        if (consumed) {
            sourceStereo_.erase(sourceStereo_.begin(), sourceStereo_.begin() + consumed * 2);
            sourceCursor_ -= consumed;
        }

        const size_t availableFrames = (outputStereo_.size() - outputRead_) / 2;
        const size_t maximumFrames = renderFormat_->nSamplesPerSec / 25;
        const size_t targetFrames = renderFormat_->nSamplesPerSec / 100;
        // Si el audio se atraso, tiramos lo viejo y volvemos al presente. Dejar
        // crecer la cola conservaria cada muestra, si, pero desincronizada.
        if (availableFrames > maximumFrames) {
            outputRead_ += (availableFrames - targetFrames) * 2;
            LogLine(L"Audio: cola vieja descartada para volver al directo");
        }
    }

    void FillRender() {
        UINT32 bufferFrames = 0;
        UINT32 padding = 0;
        if (FAILED(renderClient_->GetBufferSize(&bufferFrames)) ||
            FAILED(renderClient_->GetCurrentPadding(&padding)) || padding >= bufferFrames) return;
        const UINT32 frames = bufferFrames - padding;
        BYTE* destination = nullptr;
        if (FAILED(renderService_->GetBuffer(frames, &destination))) return;

        const UINT32 channels = renderFormat_->nChannels;
        const float gain = InterlockedCompareExchange(&muted_, 0, 0)
                               ? 0.0f
                               : std::pow(10.0f,
                                          InterlockedCompareExchange(&gainTenthsDb_, 0, 0) / 200.0f);
        for (UINT32 frame = 0; frame < frames; ++frame) {
            float left = 0.0f;
            float right = 0.0f;
            if (outputRead_ + 1 < outputStereo_.size()) {
                left = outputStereo_[outputRead_++] * gain;
                right = outputStereo_[outputRead_++] * gain;
            }
            for (UINT32 channel = 0; channel < channels; ++channel) {
                float sample = 0.0f;
                if (channels == 1) sample = (left + right) * 0.5f;
                else if (channel == 0) sample = left;
                else if (channel == 1) sample = right;
                WriteWaveSample(destination, frame * channels + channel, renderFormat_, sample);
            }
        }
        renderService_->ReleaseBuffer(frames, 0);

        if (outputRead_ > 8192 && outputRead_ > outputStereo_.size() / 2) {
            outputStereo_.erase(outputStereo_.begin(), outputStereo_.begin() + outputRead_);
            outputRead_ = 0;
        }
    }

    std::atomic<bool> running_{false};
    HANDLE thread_ = nullptr;
    HANDLE captureEvent_ = nullptr;
    HANDLE renderEvent_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    IAudioClient* captureClient_ = nullptr;
    IAudioClient* renderClient_ = nullptr;
    IAudioCaptureClient* captureService_ = nullptr;
    IAudioRenderClient* renderService_ = nullptr;
    WAVEFORMATEX* captureFormat_ = nullptr;
    WAVEFORMATEX* renderFormat_ = nullptr;
    std::vector<float> sourceStereo_;
    std::vector<float> outputStereo_;
    double sourceCursor_ = 0.0;
    size_t outputRead_ = 0;
    LONG gainTenthsDb_ = 150;
    LONG muted_ = 0;
};

class DayaahApp;

// Media Foundation llama este objeto desde su propio hilo cada vez que llega
// un frame. El callback se mantiene chiquito: entrega el frame a DayaahApp y
// pide el siguiente. Nada de pintar ventanas desde aqui.
class SourceReaderCallback final : public IMFSourceReaderCallback {
public:
    SourceReaderCallback(DayaahApp* app, std::uint64_t sessionId)
        : app_(app), sessionId_(sessionId) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG refs = static_cast<ULONG>(InterlockedDecrement(&refs_));
        if (!refs) delete this;
        return refs;
    }
    STDMETHODIMP OnReadSample(HRESULT status, DWORD streamIndex, DWORD streamFlags,
                              LONGLONG timestamp, IMFSample* sample) override;
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }
    STDMETHODIMP OnFlush(DWORD) override;
    void Detach() {
        // Exclusivo espera a que terminen todos los callbacks que ya traian el
        // lock compartido. Despues de esto nadie vuelve a tocar DayaahApp.
        ExclusiveSrwGuard guard(appLock_);
        app_ = nullptr;
    }

private:
    ~SourceReaderCallback() = default;
    LONG refs_ = 1;
    SRWLOCK appLock_ = SRWLOCK_INIT;
    DayaahApp* app_ = nullptr;
    const std::uint64_t sessionId_ = 0;
};

enum : int {
    IDC_VIDEO = 1001,
    IDC_RESOLUTION_SETUP = 1002,
    IDC_FPS_SETUP = 1003,
    IDC_FORMAT_SETUP = 1004,
    IDC_FORMAT_NOTICE = 1005,
    IDC_AUDIO = 1006,
    IDC_GAIN = 1007,
    IDC_SYNC = 1008,
    IDC_LANGUAGE = 1009,
    IDC_START = 1010,
    IDC_STATUS = 1011,
    IDC_KOFI = 1012,
    IDC_GITHUB = 1013,
    IDC_AUTOTUNE = 1014,
    IDM_FULLSCREEN = 2000,
    IDM_MUTE,
    IDM_VSYNC,
    IDM_COLOR_LIMITED,
    IDM_COLOR_FULL,
    IDM_OVERLAY_TOGGLE,
    IDM_OVERLAY_EDITOR,
    IDM_EFFECTS_TOGGLE,
    IDM_EFFECTS_EDITOR,
    IDM_LATENCY_HUD,
    IDM_PACING_SCOPE,
    IDM_AUTOTUNE,
    IDM_PICTURE_IN_PICTURE,
    IDM_SETTINGS,
    IDM_GAIN = 2500,
    IDM_RESOLUTION = 2600,
    IDM_FPS = 2700,
    IDM_FORMAT = 2800
};

constexpr UINT WM_DAYAAH_CAPTURE_ERROR = WM_APP + 18;

// Esta es la mera mera: crea la ventana, arranca/detiene la captura, coordina
// los hilos y guarda la configuracion. No procesa todo ella sola; mas bien es
// la directora de orquesta para que video, audio y UI no se agarren a golpes.
class DayaahApp {
public:
    DayaahApp() {
        InitializeCriticalSection(&frameLock_);
        auto initial = std::make_shared<OverlayConfig>();
        std::atomic_store(&overlayConfig_, std::shared_ptr<const OverlayConfig>(initial));
    }
    ~DayaahApp() {
        effectsEditor_.Close();
        FlushEffectsSettings(false);
        overlayEditor_.Close();
        FlushOverlaySettings(false);
        SetCaptureCursorHidden(false);
        StopCapture();
        ReleaseDevices();
        if (hiddenCursor_) DestroyCursor(hiddenCursor_);
        DeleteCriticalSection(&frameLock_);
        SafeRelease(audioEnumerator_);
        MFShutdown();
        CoUninitialize();
    }

    HRESULT Initialize(HINSTANCE instance, bool forceSettings, HRESULT phoenixFailure) {
        // COM y Media Foundation van primero porque casi todo lo demas depende
        // de ellos. Si algo falla aqui, mejor parar limpio que abrir media app.
        instance_ = instance;
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) return hr;
        hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(hr)) return hr;
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&audioEnumerator_));
        if (FAILED(hr)) return hr;

        SetProcessDPIAware();
        INITCOMMONCONTROLSEX controls{sizeof(controls),
                                      ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LINK_CLASS};
        InitCommonControlsEx(&controls);

        visibleCursor_ = LoadCursorW(nullptr, IDC_ARROW);
        BYTE andMask[32 * 32 / 8];
        BYTE xorMask[32 * 32 / 8]{};
        memset(andMask, 0xFF, sizeof(andMask));
        hiddenCursor_ = CreateCursor(instance_, 0, 0, 32, 32, andMask, xorMask);
        if (!visibleCursor_ || !hiddenCursor_) {
            const DWORD error = GetLastError();
            return HRESULT_FROM_WIN32(error ? error : ERROR_INVALID_HANDLE);
        }

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance_;
        windowClass.hCursor = visibleCursor_;
        windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
        windowClass.hbrBackground = CreateSolidBrush(RGB(10, 13, 18));
        windowClass.lpszClassName = L"DayaahCaptureWindow";
        windowClass.hIconSm = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(101),
                                                            IMAGE_ICON,
                                                            GetSystemMetrics(SM_CXSMICON),
                                                            GetSystemMetrics(SM_CYSMICON),
                                                            LR_DEFAULTCOLOR));
        if (!RegisterClassExW(&windowClass)) return HRESULT_FROM_WIN32(GetLastError());

        window_ = CreateWindowExW(0, windowClass.lpszClassName,
                                  kAppTitle,
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  900, 790, nullptr, nullptr, instance_, this);
        if (!window_) return HRESULT_FROM_WIN32(GetLastError());

        BOOL dark = TRUE;
        DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
        language_ = ReadSetting(L"language", L"es") == L"en"
            ? AppLanguage::English : AppLanguage::Spanish;
        deviceFingerprint_ = DeviceFingerprint();
        CreateControls();
        SetTimer(window_, 1, 1000, nullptr);
        SetTimer(window_, 2, 100, nullptr);
        SetTimer(window_, 5, 250, nullptr);

        EnumerateDevices();
        PopulateControls();
        if (FAILED(phoenixFailure)) {
            SetWindowTextW(statusText_, Tr(language_,
                L"Phoenix no pudo recuperar la captura. Revisa las conexiones y vuelve a iniciar.",
                L"Phoenix could not recover capture. Check the connections and start again."));
        }
        RestoreOverlaySettings();
        effectsSettings_ = video_effects_settings::Load(IniPath());
        effectsPacked_.store(effectsSettings_.Pack());
        latencyHudEnabled_.store(GetPrivateProfileIntW(
            L"capture", L"latency_hud", 0, IniPath().c_str()) == 1);
        pacingScopeEnabled_.store(GetPrivateProfileIntW(
            L"capture", L"pacing_scope", 0, IniPath().c_str()) == 1);
        // El primer arranque se guarda junto con una huella de la PC. Asi, si
        // alguien copia el ZIP a otra maquina, Configuracion vuelve a aparecer
        // y no intenta arrancar a ciegas con dispositivos que ya no existen.
        const std::wstring savedFingerprint = ReadFirstRunSetting(L"device", L"");
        const std::wstring firstRunMarker = ReadFirstRunSetting(L"Firstrun", L"false");
        const bool completed = firstRunMarker == L"true" || GetPrivateProfileIntW(
            L"first_run", L"complete", 0, IniPath().c_str()) == 1;
        firstRun_ = forceSettings || !completed || savedFingerprint != deviceFingerprint_;
        if (!firstRun_ && !videoDevices_.empty()) {
            const HRESULT startHr = StartCapture();
            if (FAILED(startHr)) {
                ShowSetup(true);
                LogLine(L"Inicio automatico fallo: %s", HrText(startHr).c_str());
            }
        }
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
        return S_OK;
    }

    int Run() {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (effectsEditor_.HandleDialog(message)) continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

    bool IsActiveSession(std::uint64_t sessionId) const {
        return running_.load() && activeSession_.load() == sessionId;
    }

    void OnVideoSample(std::uint64_t sessionId, IMFSample* sample,
                       LONGLONG sourceTimestamp) {
        // Media Foundation puede entregar buffers 2D con padding o un buffer
        // plano. Copiamos directo a un buffer reciclable; asi no apartamos y
        // copiamos otros 12 MB dos veces por cada frame 4K nomas porque si.
        if (!IsActiveSession(sessionId) || !sample) return;
        ExclusiveSrwGuard producerGuard(sampleLock_);
        if (!IsActiveSession(sessionId)) return;
        IMFMediaBuffer* buffer = nullptr;
        IMF2DBuffer* buffer2d = nullptr;
        BYTE* flat = nullptr;
        DWORD maximum = 0;
        DWORD current = 0;

        HRESULT hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) return;
        const size_t tightSize = TightFrameSize(activeMode_.subtype,
                                                activeMode_.width, activeMode_.height);
        if (!tightSize) {
            SafeRelease(buffer);
            return;
        }

        const bool i420 = activeMode_.subtype == MFVideoFormat_I420;
        std::vector<BYTE>& copied = i420 ? captureI420Native_ : captureFrame_;
        hr = buffer->QueryInterface(IID_PPV_ARGS(&buffer2d));

        // Lock2D deja copiar fila por fila y se come el padding del driver sin
        // fabricar otro vector intermedio. I420 se queda en la ruta contigua
        // porque sus planos U/V usan otro stride y luego hay que intercalarlos.
        if (SUCCEEDED(hr) && !i420) {
            BYTE* scanline = nullptr;
            LONG pitch = 0;
            hr = buffer2d->Lock2D(&scanline, &pitch);
            if (SUCCEEDED(hr)) {
                const size_t sourcePitch = pitch < 0
                    ? static_cast<size_t>(-static_cast<LONGLONG>(pitch))
                    : static_cast<size_t>(pitch);
                const size_t rowBytes = TightRowPitch(activeMode_.subtype,
                                                       activeMode_.width);
                const bool planar = activeMode_.subtype == MFVideoFormat_NV12 ||
                                    activeMode_.subtype == MFVideoFormat_P010;
                if (!scanline || sourcePitch < rowBytes || (planar && pitch < 0)) {
                    hr = MF_E_BUFFERTOOSMALL;
                } else {
                    copied.resize(tightSize);
                    BYTE* destination = copied.data();
                    for (UINT32 row = 0; row < activeMode_.height; ++row) {
                        memcpy(destination + static_cast<size_t>(row) * rowBytes,
                               scanline + static_cast<LONG_PTR>(row) * pitch, rowBytes);
                    }
                    if (planar) {
                        const BYTE* sourceUv = scanline +
                            static_cast<LONG_PTR>(activeMode_.height) * pitch;
                        BYTE* destinationUv = destination +
                            static_cast<size_t>(activeMode_.height) * rowBytes;
                        for (UINT32 row = 0; row < activeMode_.height / 2; ++row) {
                            memcpy(destinationUv + static_cast<size_t>(row) * rowBytes,
                                   sourceUv + static_cast<LONG_PTR>(row) * pitch, rowBytes);
                        }
                    }
                }
                buffer2d->Unlock2D();
            }
        } else if (SUCCEEDED(hr)) {
            // I420: pedimos la vista contigua una vez y convertimos desde este
            // buffer persistente; no hay assign ni relleno nuevo cada frame.
            DWORD contiguous = 0;
            hr = buffer2d->GetContiguousLength(&contiguous);
            if (SUCCEEDED(hr) && contiguous >= tightSize) {
                copied.resize(contiguous);
                hr = buffer2d->ContiguousCopyTo(copied.data(), contiguous);
            } else if (SUCCEEDED(hr)) {
                hr = MF_E_BUFFERTOOSMALL;
            }
        }

        // Algunos drivers anuncian IMF2DBuffer pero fallan Lock2D. Conservamos
        // la salida contigua y, si tambien se hace mensa, el Lock plano de toda
        // la vida para no perder compatibilidad con capturadoras raritas.
        if (FAILED(hr) && buffer2d) {
            DWORD contiguous = 0;
            hr = buffer2d->GetContiguousLength(&contiguous);
            if (SUCCEEDED(hr) && contiguous >= tightSize) {
                copied.resize(contiguous);
                hr = buffer2d->ContiguousCopyTo(copied.data(), contiguous);
            } else if (SUCCEEDED(hr)) {
                hr = MF_E_BUFFERTOOSMALL;
            }
        }
        if (FAILED(hr)) {
            hr = buffer->Lock(&flat, &maximum, &current);
            if (SUCCEEDED(hr)) {
                if (current >= tightSize) {
                    copied.resize(tightSize);
                    memcpy(copied.data(), flat, tightSize);
                } else {
                    hr = MF_E_BUFFERTOOSMALL;
                }
                buffer->Unlock();
            }
        }
        SafeRelease(buffer2d);
        SafeRelease(buffer);
        if (FAILED(hr)) return;

        LONG incomingStride = 0;
        if (i420) {
            // I420 trae U y V en dos planos separados. D3D11 entiende mejor
            // NV12, asi que aqui los intercalamos como UVUVUV. Es la posible
            // penalizacion que avisamos al lado del selector de formato.
            const size_t ySize = static_cast<size_t>(activeMode_.width) * activeMode_.height;
            const size_t chromaSize = ySize / 4;
            if (captureI420Native_.size() < ySize + chromaSize * 2) return;
            captureFrame_.resize(ySize + chromaSize * 2);
            memcpy(captureFrame_.data(), captureI420Native_.data(), ySize);
            const BYTE* u = captureI420Native_.data() + ySize;
            const BYTE* v = u + chromaSize;
            BYTE* uv = captureFrame_.data() + ySize;
            for (size_t i = 0; i < chromaSize; ++i) {
                uv[i * 2] = u[i];
                uv[i * 2 + 1] = v[i];
            }
            incomingStride = static_cast<LONG>(activeMode_.width);
        } else {
            incomingStride = static_cast<LONG>(TightRowPitch(activeMode_.subtype,
                                                              activeMode_.width));
        }

        if (!IsActiveSession(sessionId)) return;

        const ULONGLONG arrival = GetTickCount64();
        LARGE_INTEGER counter{};
        LARGE_INTEGER frequency{};
        QueryPerformanceCounter(&counter);
        QueryPerformanceFrequency(&frequency);
        // No hacemos una cola infinita. Si habia un frame pendiente lo
        // reemplazamos con este porque el nuevo esta mas cerquita del presente.
        EnterCriticalSection(&frameLock_);
        framePacing_.Add(sourceTimestamp, counter.QuadPart, frequency.QuadPart);
        if (framePending_) {
            discardedFrames_++;
            governorDiscardedFrames_++;
        }
        latestFrame_.swap(captureFrame_);
        latestFrameArrivalTick_ = arrival;
        latestFrameStride_ = incomingStride;
        framePending_ = true;
        LeaveCriticalSection(&frameLock_);
        receivedFrames_++;
        lastFrameArrivalTick_.store(arrival);
        if (frameReadyEvent_) SetEvent(frameReadyEvent_);
    }

    HRESULT ContinueReading(std::uint64_t sessionId) {
        IMFSourceReader* reader = nullptr;
        {
            SharedSrwGuard guard(readerLock_);
            if (running_.load() && activeSession_.load() == sessionId && reader_) {
                reader = reader_;
                reader->AddRef();
            }
        }
        if (!reader) return MF_E_SHUTDOWN;
        const HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                              nullptr, nullptr, nullptr, nullptr);
        reader->Release();
        return hr;
    }

    void ReaderFlushed(std::uint64_t sessionId) {
        if (activeSession_.load() == sessionId && flushEvent_) SetEvent(flushEvent_);
    }

    void ReaderError(std::uint64_t sessionId, HRESULT hr) {
        if (!IsActiveSession(sessionId)) return;
        captureError_.store(hr);
        PostMessageW(window_, WM_DAYAAH_CAPTURE_ERROR, 0, 0);
    }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        // Windows solo conoce una funcion estatica. Guardamos el puntero real
        // de DayaahApp dentro de la ventana para reenviar cada mensaje a su casa.
        DayaahApp* app = reinterpret_cast<DayaahApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<DayaahApp*>(create->lpCreateParams);
            app->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        return app ? app->HandleMessage(message, wParam, lParam)
                   : DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        // El famoso switch de Win32: aqui caen botones, timers, mouse, hotkeys,
        // resize y el cierre. Feito pero honesto, y no mete frameworks pesados.
        switch (message) {
        case WM_COMMAND:
            return OnCommand(LOWORD(wParam), HIWORD(wParam));
        case WM_CONTEXTMENU:
            if (capturingUi_ && running_.load()) ShowContextMenu(lParam);
            return 0;
        case WM_ENTERMENULOOP:
            menuOpen_ = true;
            SetCaptureCursorHidden(false);
            return 0;
        case WM_EXITMENULOOP:
            menuOpen_ = false;
            lastMouseMove_ = GetTickCount64();
            hasLastCursorPosition_ = false;
            return 0;
        case WM_ENTERIDLE:
            if (wParam == MSGF_MENU) {
                menuOpen_ = true;
                SetCaptureCursorHidden(false);
            }
            return 0;
        case WM_DAYAAH_EFFECTS_ERROR:
            if (effectsSettings_.enabled && !pictureInPicture_) {
                effectsSettings_.enabled = false;
                PublishEffects(effectsSettings_);
                MessageBoxW(window_, Tr(language_,
                    L"No se pudieron aplicar los efectos. El video sigue con la imagen original.\n\nRevisa DayaahCapture.log para ver el error de la GPU o del compilador de shaders.",
                    L"Video effects could not be applied. The original image remains active.\n\nCheck DayaahCapture.log for the GPU or shader compiler error."),
                    Tr(language_, L"Efectos de video", L"Video effects"), MB_OK | MB_ICONWARNING);
            }
            return 0;
        case WM_DAYAAH_CAPTURE_ERROR: {
            HRESULT hr = captureError_.load();
            ScheduleRecovery(hr);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMinTrackSize.x = pictureInPicture_ ? 320 : (capturingUi_ ? 320 : 900);
            limits->ptMinTrackSize.y = pictureInPicture_ ? 180 : (capturingUi_ ? 240 : 790);
            return 0;
        }
        case WM_SIZE:
            if (capturingUi_ && renderer_ && resizeEvent_) {
                pendingWidth_.store(LOWORD(lParam));
                pendingHeight_.store(HIWORD(lParam));
                SetEvent(resizeEvent_);
            }
            return 0;
        case WM_TIMER:
            if (wParam == 1) { UpdateFpsTitle(); UpdateAutoTune(); MaybeShowCadenceWarning(); }
            else if (wParam == 2) UpdateCursorVisibility();
            else if (wParam == 3) FlushOverlaySettings();
            else if (wParam == 4) FlushEffectsSettings();
            else if (wParam == 5) UpdateRecoveryAndHealth();
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                const bool hide = !menuOpen_ && running_.load() && cursorHidden_ &&
                                  GetForegroundWindow() == window_;
                SetCursor(hide ? hiddenCursor_ : visibleCursor_);
                return TRUE;
            }
            return DefWindowProcW(window_, message, wParam, lParam);
        case WM_MOUSEMOVE:
            if (running_.load()) {
                RecordRealCursorMovement();
                if (!mouseTracking_) {
                    TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window_, 0};
                    mouseTracking_ = TrackMouseEvent(&tracking) == TRUE;
                }
            }
            return 0;
        case WM_MOUSELEAVE:
            mouseTracking_ = false;
            hasLastCursorPosition_ = false;
            SetCaptureCursorHidden(false);
            return 0;
        case WM_NCMOUSEMOVE:
            SetCaptureCursorHidden(false);
            return DefWindowProcW(window_, message, wParam, lParam);
        case WM_NCHITTEST: {
            const LRESULT hit = DefWindowProcW(window_, message, wParam, lParam);
            if (pictureInPicture_ && hit == HTCLIENT) return HTCAPTION;
            return hit;
        }
        case WM_NCLBUTTONDBLCLK:
            if (pictureInPicture_ && wParam == HTCAPTION) {
                ToggleFullscreen();
                return 0;
            }
            return DefWindowProcW(window_, message, wParam, lParam);
        case WM_ACTIVATEAPP:
            SetCaptureCursorHidden(false);
            lastMouseMove_ = GetTickCount64();
            hasLastCursorPosition_ = false;
            return 0;
        case WM_ENTERSIZEMOVE:
            SetCaptureCursorHidden(false);
            return 0;
        case WM_EXITSIZEMOVE:
            if (pictureInPicture_) SavePictureInPictureBounds();
            lastMouseMove_ = GetTickCount64();
            hasLastCursorPosition_ = false;
            return 0;
        case WM_KEYDOWN:
            // Hotkeys del viewer. El bit 30 evita repetir la accion si alguien
            // deja la tecla aplastada como si le debiera dinero.
            if (wParam == VK_F6 && !(lParam & (1LL << 30)) && running_.load() && !pictureInPicture_) ToggleEffects();
            else if (wParam == VK_F7 && !(lParam & (1LL << 30)) && running_.load() && !pictureInPicture_) OpenEffectsEditor();
            else if (wParam == VK_F8 && !(lParam & (1LL << 30)) && running_.load()) ToggleLatencyHud();
            else if (wParam == VK_F9 && !(lParam & (1LL << 30)) && running_.load()) TogglePacingScope();
            else if (wParam == VK_F11 && !(lParam & (1LL << 30))) ToggleFullscreen();
            else if (wParam == VK_F12 && !(lParam & (1LL << 30)) && running_.load()) TogglePictureInPicture();
            else if (wParam == VK_F10 && !(lParam & (1LL << 30)) && running_.load()) {
                ToggleOverlays();
            }
            else if (wParam == VK_ESCAPE && !(lParam & (1LL << 30))) {
                const EscapeWindowAction action = DecideEscapeWindowAction(
                    running_.load(), pictureInPicture_, fullscreen_, IsZoomed(window_) != FALSE);
                if (action == EscapeWindowAction::LeavePictureInPicture) TogglePictureInPicture();
                else if (action == EscapeWindowAction::LeaveFullscreen) ToggleFullscreen();
                else if (action == EscapeWindowAction::RestoreMaximized)
                    ShowWindow(window_, SW_RESTORE);
            } else if (wParam == 'M' && !(lParam & (1LL << 30)) && running_.load()) {
                audio_.ToggleMute();
                UpdateFpsTitle();
            }
            return 0;
        case WM_CHAR:
            if (running_.load() && !pictureInPicture_ && !menuOpen_ && discoSecret_.Input(
                    static_cast<wchar_t>(wParam), GetTickCount64())) {
                ToggleDiscoMode();
            }
            return 0;
        case WM_LBUTTONDBLCLK:
            if (running_.load()) ToggleFullscreen();
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, RGB(205, 213, 224));
            SetBkColor(dc, RGB(10, 13, 18));
            return reinterpret_cast<LRESULT>(darkBrush_);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, RGB(235, 240, 248));
            SetBkColor(dc, RGB(24, 29, 39));
            return reinterpret_cast<LRESULT>(controlBrush_);
        }
        case WM_DRAWITEM:
            DrawOwnerButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_PAINT:
            PaintSetup();
            return 0;
        case WM_CLOSE:
            effectsEditor_.Close();
            FlushEffectsSettings();
            overlayEditor_.Close();
            FlushOverlaySettings();
            SetCaptureCursorHidden(false);
            StopCapture();
            DestroyWindow(window_);
            return 0;
        case WM_QUERYENDSESSION:
            FlushEffectsSettings(false);
            FlushOverlaySettings(false);
            return TRUE;
        case WM_ENDSESSION:
            if (wParam) { FlushEffectsSettings(false); FlushOverlaySettings(false); }
            return 0;
        case WM_DESTROY:
            SetCaptureCursorHidden(false);
            StopCapture();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window_, message, wParam, lParam);
        }
    }

    void CreateControls() {
        // Todo esto es Win32 puro. Son mas lineas, pero el EXE queda compacto y
        // abre sin cargar un navegador disfrazado de ventana.
        darkBrush_ = CreateSolidBrush(RGB(10, 13, 18));
        controlBrush_ = CreateSolidBrush(RGB(24, 29, 39));
        font_ = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        videoCombo_ = CreateCombo(IDC_VIDEO, 48, 132, 804);
        resolutionCombo_ = CreateCombo(IDC_RESOLUTION_SETUP, 48, 232, 300);
        fpsCombo_ = CreateCombo(IDC_FPS_SETUP, 366, 232, 142);
        formatCombo_ = CreateCombo(IDC_FORMAT_SETUP, 526, 232, 326);
        formatNotice_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                        48, 270, 804, 36, window_,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FORMAT_NOTICE)),
                                        instance_, nullptr);
        audioCombo_ = CreateCombo(IDC_AUDIO, 48, 335, 804);
        gainCombo_ = CreateCombo(IDC_GAIN, 48, 417, 260);
        syncCombo_ = CreateCombo(IDC_SYNC, 326, 417, 526);
        languageCombo_ = CreateCombo(IDC_LANGUAGE, 48, 499, 260);
        autoTuneButton_ = CreateWindowExW(0, L"BUTTON", L"",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                          326, 499, 526, 32, window_,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_AUTOTUNE)),
                                          instance_, nullptr);
        startButton_ = CreateWindowExW(0, L"BUTTON", L"",
                                       WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                       48, 548, 804, 58, window_,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_START)), instance_, nullptr);
        statusText_ = CreateWindowExW(0, L"STATIC", L"",
                                      WS_CHILD | WS_VISIBLE, 48, 614, 804, 42,
                                      window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)), instance_, nullptr);
        kofiLink_ = CreateWindowExW(0, L"BUTTON", L"Support Dayaah on Ko-fi",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 48, 674, 260, 64, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_KOFI)), instance_, nullptr);
        githubLink_ = CreateWindowExW(0, L"BUTTON", L"GitHub",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 326, 674, 190, 64, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_GITHUB)), instance_, nullptr);
        HWND controls[] = {videoCombo_, resolutionCombo_, fpsCombo_, formatCombo_, formatNotice_,
                           audioCombo_, gainCombo_, syncCombo_, languageCombo_, statusText_,
                           autoTuneButton_, kofiLink_, githubLink_};
        for (HWND control : controls) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
        }
        SendMessageW(startButton_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        const wchar_t* gains[] = {L"0 dB", L"+6 dB", L"+12 dB", L"+15 dB", L"+18 dB"};
        for (const wchar_t* gain : gains) ComboBox_AddString(gainCombo_, gain);
        ComboBox_SetCurSel(gainCombo_, 3);
        ComboBox_AddString(languageCombo_, L"Español");
        ComboBox_AddString(languageCombo_, L"English");
        ComboBox_SetCurSel(languageCombo_, language_ == AppLanguage::English ? 1 : 0);
        ApplyLanguage();
    }

    HWND CreateCombo(int id, int x, int y, int width) {
        return CreateWindowExW(0, WC_COMBOBOXW, L"",
                               WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                               x, y, width, 320, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               instance_, nullptr);
    }

    void ApplyLanguage() {
        const int syncSelection = std::max(0, ComboBox_GetCurSel(syncCombo_));
        ComboBox_ResetContent(syncCombo_);
        ComboBox_AddString(syncCombo_, Tr(language_,
            L"Latencia mínima (No VSync - VRR)", L"Minimum latency (No VSync - VRR)"));
        ComboBox_AddString(syncCombo_, Tr(language_,
            L"VSync (cola máxima: 1 cuadro)", L"VSync (maximum queue: 1 frame)"));
        ComboBox_SetCurSel(syncCombo_, std::min(syncSelection, 1));
        ComboBox_SetCurSel(languageCombo_, language_ == AppLanguage::English ? 1 : 0);
        if (audioCombo_ && ComboBox_GetCount(audioCombo_) > 0) {
            const int selected = ComboBox_GetCurSel(audioCombo_);
            ComboBox_DeleteString(audioCombo_, 0);
            SendMessageW(audioCombo_, CB_INSERTSTRING, 0,
                         reinterpret_cast<LPARAM>(Tr(language_, L"Sin audio", L"No audio")));
            ComboBox_SetCurSel(audioCombo_, selected);
        }
        UpdateFormatNotice();
        InvalidateRect(window_, nullptr, TRUE);
        if (startButton_) InvalidateRect(startButton_, nullptr, TRUE);
        if (autoTuneButton_) InvalidateRect(autoTuneButton_, nullptr, TRUE);
    }

    void PaintSetup() {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window_, &paint);
        RECT client{};
        GetClientRect(window_, &client);
        FillRect(dc, &client, darkBrush_);
        if (!capturingUi_) {
            HFONT titleFont = CreateFontW(-34, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                          DEFAULT_PITCH, L"Segoe UI");
            HFONT old = static_cast<HFONT>(SelectObject(dc, titleFont));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(104, 231, 193));
            RECT title{48, 30, 800, 72};
            DrawTextW(dc, L"DAYAAH CAPTURE", -1, &title, DT_LEFT | DT_SINGLELINE);
            HICON headerIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(102),
                                                             IMAGE_ICON, 64, 64,
                                                             LR_DEFAULTCOLOR | LR_SHARED));
            if (headerIcon) DrawIconEx(dc, 365, 12, headerIcon, 64, 64, 0, nullptr, DI_NORMAL);
            SelectObject(dc, font_);
            SetTextColor(dc, RGB(139, 151, 169));
            RECT subtitle{50, 74, 852, 104};
            DrawTextW(dc, Tr(language_,
                      L"Ruta nativa. Siempre el frame más reciente. Cero bloat.",
                      L"Native path. Always the newest frame. No bloat."), -1,
                      &subtitle, DT_LEFT | DT_SINGLELINE);
            const wchar_t* labelsEs[] = {L"DISPOSITIVO DE VIDEO", L"MODO DE VIDEO",
                                         L"ENTRADA DE AUDIO", L"AMPLIFICACIÓN", L"IDIOMA"};
            const wchar_t* labelsEn[] = {L"VIDEO DEVICE", L"VIDEO MODE",
                                         L"AUDIO INPUT", L"AMPLIFICATION", L"LANGUAGE"};
            int ys[] = {108, 188, 311, 393, 475};
            for (int i = 0; i < 5; ++i) {
                RECT label{48, ys[i], 800, ys[i] + 22};
                DrawTextW(dc, language_ == AppLanguage::English ? labelsEn[i] : labelsEs[i],
                          -1, &label, DT_LEFT | DT_SINGLELINE);
            }
            SetTextColor(dc, RGB(105, 116, 134));
            RECT resolutionLabel{48, 210, 348, 230};
            RECT fpsLabel{366, 210, 508, 230};
            RECT formatLabel{526, 210, 852, 230};
            DrawTextW(dc, Tr(language_, L"Resolución", L"Resolution"), -1, &resolutionLabel, DT_LEFT | DT_SINGLELINE);
            DrawTextW(dc, L"FPS", -1, &fpsLabel, DT_LEFT | DT_SINGLELINE);
            DrawTextW(dc, Tr(language_, L"Formato", L"Format"), -1, &formatLabel, DT_LEFT | DT_SINGLELINE);
            SetTextColor(dc, RGB(139, 151, 169));
            RECT syncLabel{326, 393, 852, 415};
            DrawTextW(dc, Tr(language_, L"SINCRONIZACIÓN", L"SYNCHRONIZATION"), -1,
                      &syncLabel, DT_LEFT | DT_SINGLELINE);
            SelectObject(dc, old);
            DeleteObject(titleFont);
        }
        EndPaint(window_, &paint);
    }

    void DrawOwnerButton(const DRAWITEMSTRUCT* item) {
        if (!item) return;
        if (item->CtlID == IDC_KOFI || item->CtlID == IDC_GITHUB) {
            const bool pressed = (item->itemState & ODS_SELECTED) != 0;
            const bool kofi = item->CtlID == IDC_KOFI;
            const COLORREF background = kofi
                ? (pressed ? RGB(45, 51, 64) : RGB(58, 66, 82))
                : (pressed ? RGB(224, 224, 224) : RGB(248, 248, 248));
            HBRUSH backgroundBrush = CreateSolidBrush(background);
            FillRect(item->hDC, &item->rcItem, backgroundBrush);
            DeleteObject(backgroundBrush);
            HPEN border = CreatePen(PS_SOLID, 1, kofi ? RGB(77, 88, 108) : RGB(185, 185, 185));
            HGDIOBJ oldPen = SelectObject(item->hDC, border);
            HGDIOBJ oldBrush = SelectObject(item->hDC, GetStockObject(NULL_BRUSH));
            Rectangle(item->hDC, item->rcItem.left, item->rcItem.top,
                      item->rcItem.right, item->rcItem.bottom);
            SelectObject(item->hDC, oldBrush);
            SelectObject(item->hDC, oldPen);
            DeleteObject(border);

            SetBkMode(item->hDC, TRANSPARENT);
            if (kofi) {
                HBRUSH blue = CreateSolidBrush(RGB(42, 193, 235));
                POINT accent[] = {{item->rcItem.left, item->rcItem.top},
                                  {item->rcItem.left + 88, item->rcItem.top},
                                  {item->rcItem.left + 70, item->rcItem.bottom},
                                  {item->rcItem.left, item->rcItem.bottom}};
                oldBrush = SelectObject(item->hDC, blue);
                oldPen = SelectObject(item->hDC, GetStockObject(NULL_PEN));
                Polygon(item->hDC, accent, _countof(accent));
                SelectObject(item->hDC, oldPen);
                SelectObject(item->hDC, oldBrush);
                DeleteObject(blue);

                HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
                oldBrush = SelectObject(item->hDC, white);
                RoundRect(item->hDC, item->rcItem.left + 18, item->rcItem.top + 15,
                          item->rcItem.left + 61, item->rcItem.top + 48, 8, 8);
                Ellipse(item->hDC, item->rcItem.left + 52, item->rcItem.top + 21,
                        item->rcItem.left + 73, item->rcItem.top + 43);
                SelectObject(item->hDC, oldBrush);
                DeleteObject(white);

                HBRUSH red = CreateSolidBrush(RGB(255, 79, 94));
                oldBrush = SelectObject(item->hDC, red);
                oldPen = SelectObject(item->hDC, GetStockObject(NULL_PEN));
                Ellipse(item->hDC, item->rcItem.left + 28, item->rcItem.top + 22,
                        item->rcItem.left + 41, item->rcItem.top + 35);
                Ellipse(item->hDC, item->rcItem.left + 38, item->rcItem.top + 22,
                        item->rcItem.left + 51, item->rcItem.top + 35);
                POINT heart[] = {{item->rcItem.left + 28, item->rcItem.top + 29},
                                 {item->rcItem.left + 51, item->rcItem.top + 29},
                                 {item->rcItem.left + 39, item->rcItem.top + 44}};
                Polygon(item->hDC, heart, _countof(heart));
                SelectObject(item->hDC, oldPen);
                SelectObject(item->hDC, oldBrush);
                DeleteObject(red);

                HFONT brand = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
                HGDIOBJ oldFont = SelectObject(item->hDC, brand);
                SetTextColor(item->hDC, RGB(255, 255, 255));
                RECT title{item->rcItem.left + 94, item->rcItem.top + 10,
                           item->rcItem.right - 8, item->rcItem.bottom - 28};
                RECT subtitle{item->rcItem.left + 94, item->rcItem.top + 34,
                              item->rcItem.right - 8, item->rcItem.bottom - 6};
                DrawTextW(item->hDC, L"Support Dayaah", -1, &title, DT_LEFT | DT_SINGLELINE);
                DrawTextW(item->hDC, L"on Ko-fi", -1, &subtitle, DT_LEFT | DT_SINGLELINE);
                SelectObject(item->hDC, oldFont);
                DeleteObject(brand);
            } else {
                // Si: este Octocat medio chuequito es INTENCIONAL. Se queda asi
                // porque da risa y ya es parte del lore de Dayaah Capture jajaja.
                HBRUSH black = CreateSolidBrush(RGB(20, 20, 20));
                oldBrush = SelectObject(item->hDC, black);
                oldPen = SelectObject(item->hDC, GetStockObject(NULL_PEN));
                POINT leftEar[] = {{item->rcItem.left + 18, item->rcItem.top + 24},
                                   {item->rcItem.left + 23, item->rcItem.top + 10},
                                   {item->rcItem.left + 34, item->rcItem.top + 21}};
                POINT rightEar[] = {{item->rcItem.left + 44, item->rcItem.top + 21},
                                    {item->rcItem.left + 56, item->rcItem.top + 10},
                                    {item->rcItem.left + 59, item->rcItem.top + 25}};
                Polygon(item->hDC, leftEar, _countof(leftEar));
                Polygon(item->hDC, rightEar, _countof(rightEar));
                Ellipse(item->hDC, item->rcItem.left + 17, item->rcItem.top + 17,
                        item->rcItem.left + 61, item->rcItem.top + 58);
                SelectObject(item->hDC, oldPen);
                SelectObject(item->hDC, oldBrush);
                DeleteObject(black);

                HFONT brand = CreateFontW(-25, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
                HGDIOBJ oldFont = SelectObject(item->hDC, brand);
                SetTextColor(item->hDC, RGB(20, 20, 20));
                RECT title{item->rcItem.left + 68, item->rcItem.top,
                           item->rcItem.right - 8, item->rcItem.bottom};
                DrawTextW(item->hDC, L"GitHub", -1, &title,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(item->hDC, oldFont);
                DeleteObject(brand);
            }
            if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
            return;
        }
        if (item->CtlID == IDC_AUTOTUNE) {
            const bool pressed = (item->itemState & ODS_SELECTED) != 0;
            HBRUSH background = CreateSolidBrush(pressed ? RGB(32, 105, 88) : RGB(27, 74, 66));
            FillRect(item->hDC, &item->rcItem, background);
            DeleteObject(background);
            HPEN border = CreatePen(PS_SOLID, 1, RGB(83, 215, 178));
            HGDIOBJ oldPen = SelectObject(item->hDC, border);
            HGDIOBJ oldBrush = SelectObject(item->hDC, GetStockObject(NULL_BRUSH));
            RoundRect(item->hDC, item->rcItem.left, item->rcItem.top,
                      item->rcItem.right, item->rcItem.bottom, 7, 7);
            SelectObject(item->hDC, oldBrush);
            SelectObject(item->hDC, oldPen);
            DeleteObject(border);
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, RGB(216, 255, 244));
            HFONT old = static_cast<HFONT>(SelectObject(item->hDC, font_));
            RECT rect = item->rcItem;
            DrawTextW(item->hDC, Tr(language_, L"AUTO-OPTIMIZAR CAPTURA",
                                               L"AUTO-OPTIMIZE CAPTURE"),
                      -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(item->hDC, old);
            if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
            return;
        }
        if (item->CtlID != IDC_START) return;
        COLORREF color = (item->itemState & ODS_SELECTED) ? RGB(67, 188, 157) : RGB(83, 215, 178);
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(item->hDC, &item->rcItem, brush);
        DeleteObject(brush);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, RGB(5, 19, 16));
        HFONT old = static_cast<HFONT>(SelectObject(item->hDC, font_));
        RECT rect = item->rcItem;
        DrawTextW(item->hDC, Tr(language_, L"INICIAR CAPTURA", L"START CAPTURE"), -1, &rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(item->hDC, old);
    }

    void EnumerateDevices() {
        // Primero le preguntamos a Media Foundation por video y luego a WASAPI
        // por entradas de audio. Los nombres vienen del driver, no de una lista
        // inventada por nosotros.
        IMFAttributes* attributes = nullptr;
        IMFActivate** devices = nullptr;
        UINT32 count = 0;
        HRESULT hr = MFCreateAttributes(&attributes, 1);
        if (SUCCEEDED(hr)) {
            hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                     MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        }
        if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(attributes, &devices, &count);
        SafeRelease(attributes);

        if (SUCCEEDED(hr)) {
            for (UINT32 i = 0; i < count; ++i) {
                wchar_t* name = nullptr;
                UINT32 length = 0;
                devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                                               &name, &length);
                VideoDeviceInfo info;
                info.name = name ? name : L"Dispositivo de video";
                if (name) CoTaskMemFree(name);
                info.activate = devices[i];
                info.activate->AddRef();
                EnumerateVideoModes(info);
                if (!info.modes.empty()) videoDevices_.push_back(std::move(info));
                else SafeRelease(info.activate);
                devices[i]->Release();
            }
            CoTaskMemFree(devices);
        }

        IMMDeviceCollection* collection = nullptr;
        hr = audioEnumerator_->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);
        if (SUCCEEDED(hr)) {
            UINT countAudio = 0;
            collection->GetCount(&countAudio);
            for (UINT i = 0; i < countAudio; ++i) {
                IMMDevice* device = nullptr;
                IPropertyStore* store = nullptr;
                PROPVARIANT value;
                PropVariantInit(&value);
                if (SUCCEEDED(collection->Item(i, &device)) &&
                    SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) &&
                    SUCCEEDED(store->GetValue(kPkeyDeviceFriendlyName, &value))) {
                    AudioDeviceInfo info;
                    info.name = value.pwszVal ? value.pwszVal : L"Entrada de audio";
                    info.device = device;
                    info.device->AddRef();
                    audioDevices_.push_back(std::move(info));
                }
                PropVariantClear(&value);
                SafeRelease(store);
                SafeRelease(device);
            }
            collection->Release();
        }
        LogLine(L"Enumerados %zu dispositivos de video y %zu entradas de audio",
                videoDevices_.size(), audioDevices_.size());
    }

    void EnumerateVideoModes(VideoDeviceInfo& device) {
        // Solo conservamos formatos crudos que sabemos pintar. MJPEG puede salir
        // anunciado por la capturadora, pero aqui priorizamos la ruta nativa sin
        // meter decodificacion adicional en medio.
        IMFMediaSource* source = nullptr;
        IMFSourceReader* reader = nullptr;
        if (FAILED(device.activate->ActivateObject(IID_PPV_ARGS(&source)))) return;
        if (FAILED(MFCreateSourceReaderFromMediaSource(source, nullptr, &reader))) {
            device.activate->ShutdownObject();
            source->Release();
            return;
        }
        for (DWORD index = 0;; ++index) {
            IMFMediaType* type = nullptr;
            HRESULT hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &type);
            if (hr == MF_E_NO_MORE_TYPES) break;
            if (FAILED(hr)) break;
            GUID subtype{};
            UINT32 width = 0, height = 0, num = 0, den = 1;
            if (SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) && SupportedRawSubtype(subtype) &&
                SUCCEEDED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height)) &&
                SUCCEEDED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den))) {
                VideoMode mode{width, height, num, den, subtype, index};
                bool duplicate = std::any_of(device.modes.begin(), device.modes.end(),
                    [&](const VideoMode& existing) { return existing.Key() == mode.Key(); });
                if (!duplicate) device.modes.push_back(mode);
            }
            type->Release();
        }
        std::sort(device.modes.begin(), device.modes.end(), [](const VideoMode& a, const VideoMode& b) {
            if (a.width != b.width) return a.width > b.width;
            if (a.height != b.height) return a.height > b.height;
            if (std::fabs(a.Fps() - b.Fps()) > 0.01) return a.Fps() > b.Fps();
            return GuidFormatName(a.subtype) < GuidFormatName(b.subtype);
        });
        reader->Release();
        device.activate->ShutdownObject();
        source->Release();
    }

    void PopulateControls() {
        ComboBox_ResetContent(videoCombo_);
        const std::wstring savedVideo = ReadSetting(L"video_device", L"");
        int preferredVideo = 0;
        for (size_t i = 0; i < videoDevices_.size(); ++i) {
            ComboBox_AddString(videoCombo_, videoDevices_[i].name.c_str());
            if (!savedVideo.empty() && videoDevices_[i].name == savedVideo) {
                preferredVideo = static_cast<int>(i);
            }
        }
        if (!videoDevices_.empty()) ComboBox_SetCurSel(videoCombo_, preferredVideo);
        const std::wstring savedMode = ReadSetting(L"video_mode", L"");
        int preferredMode = -1;
        if (preferredVideo < static_cast<int>(videoDevices_.size())) {
            for (size_t i = 0; i < videoDevices_[preferredVideo].modes.size(); ++i) {
                if (videoDevices_[preferredVideo].modes[i].Key() == savedMode) preferredMode = static_cast<int>(i);
            }
        }
        PopulateModeSelectors(preferredVideo, preferredMode);

        ComboBox_ResetContent(audioCombo_);
        ComboBox_AddString(audioCombo_, Tr(language_, L"Sin audio", L"No audio"));
        const std::wstring savedAudio = ReadSetting(L"audio_device", L"");
        int preferredAudio = 0;
        for (size_t i = 0; i < audioDevices_.size(); ++i) {
            ComboBox_AddString(audioCombo_, audioDevices_[i].name.c_str());
            if (!savedAudio.empty() && audioDevices_[i].name == savedAudio) {
                preferredAudio = static_cast<int>(i + 1);
            }
        }
        ComboBox_SetCurSel(audioCombo_, preferredAudio);

        int gain = GetPrivateProfileIntW(L"capture", L"gain_db", 15, IniPath().c_str());
        const int gains[] = {0, 6, 12, 15, 18};
        for (int i = 0; i < 5; ++i) {
            if (gain == gains[i]) ComboBox_SetCurSel(gainCombo_, i);
        }
        ComboBox_SetCurSel(syncCombo_,
                           GetPrivateProfileIntW(L"capture", L"ultra_low_latency", 1,
                                                 IniPath().c_str()) ? 0 : 1);
        activeInputFullRange_.store(
            GetPrivateProfileIntW(L"capture", L"input_full_range", 0,
                                  IniPath().c_str()) == 1);

        if (videoDevices_.empty()) {
            SetWindowTextW(statusText_, Tr(language_,
                L"No encontré una capturadora con un formato compatible.",
                L"No capture card with a compatible format was found."));
            EnableWindow(startButton_, FALSE);
            EnableWindow(autoTuneButton_, FALSE);
        } else {
            EnableWindow(startButton_, TRUE);
            EnableWindow(autoTuneButton_, TRUE);
            wchar_t status[180]{};
            _snwprintf_s(status, _countof(status), _TRUNCATE, language_ == AppLanguage::English
                ? L"Ready: %zu raw modes. F8: latency · F9: pacing · F11: fullscreen · F12: PiP · M: mute"
                : L"Listo: %zu modos crudos. F8: latencia · F9: pacing · F11: pantalla completa · F12: PiP · M: silencio",
                videoDevices_[preferredVideo].modes.size());
            SetWindowTextW(statusText_, status);
        }
        UpdateFormatNotice();
    }

    int DefaultModeIndex(int video) const {
        // Preferencia sensata cuando no hay nada guardado: 1080p60 NV12. Si no
        // existe, el puntaje va bajando con alternativas compatibles.
        if (video < 0 || video >= static_cast<int>(videoDevices_.size())) return -1;
        int preferred = -1;
        int bestScore = -1;
        for (size_t i = 0; i < videoDevices_[video].modes.size(); ++i) {
            const VideoMode& mode = videoDevices_[video].modes[i];
            int score = 0;
            if (mode.width == 1920 && mode.height == 1080) score += 100;
            if (std::fabs(mode.Fps() - 60.0) < 0.1) score += 50;
            if (mode.subtype == MFVideoFormat_NV12) score += 30;
            else if (mode.subtype == MFVideoFormat_YUY2) score += 20;
            else if (mode.subtype == MFVideoFormat_I420) score += 10;
            if (score > bestScore) { bestScore = score; preferred = static_cast<int>(i); }
        }
        return preferred;
    }

    void PopulateModeSelectors(int video, int preferredMode) {
        // El driver puede anunciar una lista larguisima. La partimos en tres
        // pasos: resolucion -> FPS -> formato, para que Configuracion no parezca
        // menu de restaurante con 800 platillos.
        ComboBox_ResetContent(resolutionCombo_);
        setupResolutions_.clear();
        if (video < 0 || video >= static_cast<int>(videoDevices_.size())) {
            ComboBox_ResetContent(fpsCombo_);
            ComboBox_ResetContent(formatCombo_);
            return;
        }
        const auto& modes = videoDevices_[video].modes;
        if (preferredMode < 0 || preferredMode >= static_cast<int>(modes.size())) {
            preferredMode = DefaultModeIndex(video);
        }
        for (const VideoMode& mode : modes) {
            const auto value = std::make_pair(mode.width, mode.height);
            if (std::find(setupResolutions_.begin(), setupResolutions_.end(), value) == setupResolutions_.end()) {
                setupResolutions_.push_back(value);
                wchar_t label[64]{};
                _snwprintf_s(label, _countof(label), _TRUNCATE, L"%u x %u", mode.width, mode.height);
                ComboBox_AddString(resolutionCombo_, label);
            }
        }
        int selectedResolution = 0;
        if (preferredMode >= 0) {
            const auto wanted = std::make_pair(modes[preferredMode].width, modes[preferredMode].height);
            const auto found = std::find(setupResolutions_.begin(), setupResolutions_.end(), wanted);
            if (found != setupResolutions_.end()) selectedResolution = static_cast<int>(found - setupResolutions_.begin());
        }
        ComboBox_SetCurSel(resolutionCombo_, selectedResolution);
        const double wantedFps = preferredMode >= 0 ? modes[preferredMode].Fps() : 60.0;
        const GUID* wantedFormat = preferredMode >= 0 ? &modes[preferredMode].subtype : nullptr;
        PopulateFpsForResolution(video, wantedFps, wantedFormat);
    }

    void PopulateFpsForResolution(int video, double wantedFps, const GUID* wantedFormat) {
        ComboBox_ResetContent(fpsCombo_);
        setupFps_.clear();
        const int resolution = ComboBox_GetCurSel(resolutionCombo_);
        if (video < 0 || video >= static_cast<int>(videoDevices_.size()) ||
            resolution < 0 || resolution >= static_cast<int>(setupResolutions_.size())) return;
        const auto selectedResolution = setupResolutions_[resolution];
        for (const VideoMode& mode : videoDevices_[video].modes) {
            if (mode.width != selectedResolution.first || mode.height != selectedResolution.second) continue;
            if (std::none_of(setupFps_.begin(), setupFps_.end(),
                [&](double fps) { return std::fabs(fps - mode.Fps()) < 0.01; })) {
                setupFps_.push_back(mode.Fps());
                wchar_t label[48]{};
                _snwprintf_s(label, _countof(label), _TRUNCATE,
                    std::fabs(mode.Fps() - std::round(mode.Fps())) < 0.01 ? L"%.0f fps" : L"%.2f fps",
                    mode.Fps());
                ComboBox_AddString(fpsCombo_, label);
            }
        }
        int selected = 0;
        for (size_t i = 0; i < setupFps_.size(); ++i) {
            if (std::fabs(setupFps_[i] - wantedFps) < 0.01) selected = static_cast<int>(i);
        }
        if (!setupFps_.empty()) ComboBox_SetCurSel(fpsCombo_, selected);
        PopulateFormatsForFps(video, wantedFormat);
    }

    void PopulateFormatsForFps(int video, const GUID* wantedFormat) {
        ComboBox_ResetContent(formatCombo_);
        setupFormats_.clear();
        const int resolution = ComboBox_GetCurSel(resolutionCombo_);
        const int fps = ComboBox_GetCurSel(fpsCombo_);
        if (video < 0 || video >= static_cast<int>(videoDevices_.size()) ||
            resolution < 0 || resolution >= static_cast<int>(setupResolutions_.size()) ||
            fps < 0 || fps >= static_cast<int>(setupFps_.size())) return;
        const auto selectedResolution = setupResolutions_[resolution];
        for (const VideoMode& mode : videoDevices_[video].modes) {
            if (mode.width != selectedResolution.first || mode.height != selectedResolution.second ||
                std::fabs(mode.Fps() - setupFps_[fps]) >= 0.01) continue;
            if (std::none_of(setupFormats_.begin(), setupFormats_.end(),
                             [&](const GUID& value) { return value == mode.subtype; })) {
                setupFormats_.push_back(mode.subtype);
                ComboBox_AddString(formatCombo_, GuidFormatName(mode.subtype).c_str());
            }
        }
        int selected = 0;
        if (wantedFormat) for (size_t i = 0; i < setupFormats_.size(); ++i) {
            if (setupFormats_[i] == *wantedFormat) selected = static_cast<int>(i);
        }
        if (!setupFormats_.empty()) ComboBox_SetCurSel(formatCombo_, selected);
        UpdateFormatNotice();
    }

    int SelectedModeIndex() const {
        const int video = ComboBox_GetCurSel(videoCombo_);
        const int resolution = ComboBox_GetCurSel(resolutionCombo_);
        const int fps = ComboBox_GetCurSel(fpsCombo_);
        const int format = ComboBox_GetCurSel(formatCombo_);
        if (video < 0 || video >= static_cast<int>(videoDevices_.size()) ||
            resolution < 0 || resolution >= static_cast<int>(setupResolutions_.size()) ||
            fps < 0 || fps >= static_cast<int>(setupFps_.size()) ||
            format < 0 || format >= static_cast<int>(setupFormats_.size())) return -1;
        const auto selectedResolution = setupResolutions_[resolution];
        for (size_t i = 0; i < videoDevices_[video].modes.size(); ++i) {
            const VideoMode& mode = videoDevices_[video].modes[i];
            if (mode.width == selectedResolution.first && mode.height == selectedResolution.second &&
                std::fabs(mode.Fps() - setupFps_[fps]) < 0.01 && mode.subtype == setupFormats_[format]) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void UpdateFormatNotice() {
        const int format = ComboBox_GetCurSel(formatCombo_);
        SetWindowTextW(formatNotice_, format >= 0 && format < static_cast<int>(setupFormats_.size())
            ? FormatNotice(setupFormats_[format], language_) : L"");
    }

    LRESULT OnCommand(int id, int notification) {
        if ((id == IDC_KOFI || id == IDC_GITHUB) && notification == BN_CLICKED) {
            ShellExecuteW(window_, L"open", id == IDC_KOFI ? kKofiUrl : kGithubUrl,
                          nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (id == IDC_AUTOTUNE && notification == BN_CLICKED) {
            RunAutoTune();
            return 0;
        }
        if (id == IDC_VIDEO && notification == CBN_SELCHANGE) {
            PopulateModeSelectors(ComboBox_GetCurSel(videoCombo_), -1);
            return 0;
        }
        if (id == IDC_RESOLUTION_SETUP && notification == CBN_SELCHANGE) {
            const int fpsIndex = ComboBox_GetCurSel(fpsCombo_);
            const double wantedFps = fpsIndex >= 0 && fpsIndex < static_cast<int>(setupFps_.size())
                ? setupFps_[fpsIndex] : 60.0;
            const int formatIndex = ComboBox_GetCurSel(formatCombo_);
            GUID wanted{};
            const GUID* wantedFormat = nullptr;
            if (formatIndex >= 0 && formatIndex < static_cast<int>(setupFormats_.size())) {
                wanted = setupFormats_[formatIndex];
                wantedFormat = &wanted;
            }
            PopulateFpsForResolution(ComboBox_GetCurSel(videoCombo_), wantedFps, wantedFormat);
            return 0;
        }
        if (id == IDC_FPS_SETUP && notification == CBN_SELCHANGE) {
            const int formatIndex = ComboBox_GetCurSel(formatCombo_);
            GUID wanted{};
            const GUID* wantedFormat = nullptr;
            if (formatIndex >= 0 && formatIndex < static_cast<int>(setupFormats_.size())) {
                wanted = setupFormats_[formatIndex];
                wantedFormat = &wanted;
            }
            PopulateFormatsForFps(ComboBox_GetCurSel(videoCombo_), wantedFormat);
            return 0;
        }
        if (id == IDC_FORMAT_SETUP && notification == CBN_SELCHANGE) {
            UpdateFormatNotice();
            return 0;
        }
        if (id == IDC_LANGUAGE && notification == CBN_SELCHANGE) {
            language_ = ComboBox_GetCurSel(languageCombo_) == 1
                ? AppLanguage::English : AppLanguage::Spanish;
            WritePrivateProfileStringW(L"capture", L"language",
                language_ == AppLanguage::English ? L"en" : L"es", IniPath().c_str());
            effectsEditor_.Close();
            overlayEditor_.Close();
            ApplyLanguage();
            PopulateControls();
            return 0;
        }
        if (id == IDC_GAIN && notification == CBN_SELCHANGE && running_.load()) {
            audio_.SetGain(SelectedGain());
            return 0;
        }
        if (id == IDC_START && notification == BN_CLICKED) {
            HRESULT hr = StartCapture();
            if (FAILED(hr)) {
                std::wstring text = std::wstring(Tr(language_,
                    L"No pude iniciar la captura:\n\n", L"Capture could not start:\n\n")) + HrText(hr) +
                    Tr(language_,
                        L"\n\nAbre un Issue en GitHub y adjunta DayaahCapture.log:\n",
                        L"\n\nOpen a GitHub Issue and attach DayaahCapture.log:\n") +
                    L"https://github.com/dayitaah/Dayaah-Capture/issues";
                MessageBoxW(window_, text.c_str(), L"Dayaah Capture", MB_ICONERROR);
            }
            return 0;
        }
        return 0;
    }

    static int FormatCode(const GUID& subtype) {
        if (subtype == MFVideoFormat_NV12) return 1;
        if (subtype == MFVideoFormat_YUY2) return 2;
        if (subtype == MFVideoFormat_I420) return 3;
        if (subtype == MFVideoFormat_P010) return 4;
        if (subtype == MFVideoFormat_RGB32) return 5;
        return 0;
    }

    int ClosestModeIndex(const ModeCandidate& requested, ModeAxis axis) const {
        if (activeDeviceIndex_ < 0 ||
            activeDeviceIndex_ >= static_cast<int>(videoDevices_.size())) return -1;
        const auto& modes = videoDevices_[activeDeviceIndex_].modes;
        std::vector<ModeCandidate> candidates;
        candidates.reserve(modes.size());
        for (const VideoMode& mode : modes) {
            candidates.push_back({mode.width, mode.height, mode.Fps(), FormatCode(mode.subtype)});
        }
        const ModeCandidate current{activeMode_.width, activeMode_.height,
                                    activeMode_.Fps(), FormatCode(activeMode_.subtype)};
        return SelectClosestMode(candidates, current, requested, axis);
    }

    void SwitchCaptureMode(int modeIndex) {
        if (!running_.load() || activeDeviceIndex_ < 0 ||
            activeDeviceIndex_ >= static_cast<int>(videoDevices_.size())) return;
        const auto& modes = videoDevices_[activeDeviceIndex_].modes;
        if (modeIndex < 0 || modeIndex >= static_cast<int>(modes.size()) ||
            modes[modeIndex].Key() == activeMode_.Key()) return;

        const int deviceIndex = activeDeviceIndex_;
        const int previousMode = SelectedModeIndex();
        const bool previousMuted = audio_.IsMuted();
        const std::wstring previousLabel = activeMode_.Label();
        const std::wstring requestedLabel = modes[modeIndex].Label();
        LogLine(L"Cambio de modo solicitado: %s -> %s",
                previousLabel.c_str(), requestedLabel.c_str());

        PopulateModeSelectors(deviceIndex, modeIndex);
        StopCapture();
        HRESULT hr = StartCapture();
        if (SUCCEEDED(hr)) {
            if (previousMuted && !audio_.IsMuted()) audio_.ToggleMute();
            LogLine(L"Cambio de modo completado: %s", requestedLabel.c_str());
            return;
        }

        const HRESULT requestedHr = hr;
        LogLine(L"Cambio de modo fallo; intentando restaurar: %s", HrText(hr).c_str());
        PopulateModeSelectors(deviceIndex, previousMode);
        const HRESULT restoreHr = StartCapture();
        if (SUCCEEDED(restoreHr) && previousMuted && !audio_.IsMuted()) audio_.ToggleMute();
        std::wstring message = std::wstring(Tr(language_,
            L"La capturadora rechazó el modo:\n\n", L"The capture device rejected this mode:\n\n")) +
            requestedLabel + L"\n\n" + HrText(requestedHr);
        if (SUCCEEDED(restoreHr)) {
            message += Tr(language_, L"\n\nSe restauró el modo anterior.",
                          L"\n\nThe previous mode was restored.");
        } else {
            message += Tr(language_,
                L"\n\nTampoco pude restaurar el modo anterior. Volví a la configuración.",
                L"\n\nThe previous mode could not be restored either. Returning to settings.");
            if (fullscreen_) ToggleFullscreen();
            ShowSetup(true);
        }
        MessageBoxW(window_, message.c_str(), Tr(language_,
            L"Dayaah Capture - cambio de modo", L"Dayaah Capture - mode change"), MB_ICONWARNING);
    }

    void BuildModeMenu(HMENU root) {
        auto entry = [](HMENU menu, UINT id, const wchar_t* name, bool checked = false) {
            AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : 0), id, name);
        };
        menuResolutions_.clear();
        menuFps_.clear();
        menuFormats_.clear();
        if (activeDeviceIndex_ < 0 ||
            activeDeviceIndex_ >= static_cast<int>(videoDevices_.size())) return;

        const auto& modes = videoDevices_[activeDeviceIndex_].modes;
        for (const VideoMode& mode : modes) {
            const auto resolution = std::make_pair(mode.width, mode.height);
            if (std::find(menuResolutions_.begin(), menuResolutions_.end(), resolution) ==
                menuResolutions_.end()) {
                menuResolutions_.push_back(resolution);
            }
            if (std::none_of(menuFps_.begin(), menuFps_.end(),
                             [&](double fps) { return std::fabs(fps - mode.Fps()) < 0.01; })) {
                menuFps_.push_back(mode.Fps());
            }
            if (std::none_of(menuFormats_.begin(), menuFormats_.end(),
                             [&](const GUID& format) { return format == mode.subtype; })) {
                menuFormats_.push_back(mode.subtype);
            }
        }

        HMENU modeMenu = CreatePopupMenu();
        HMENU resolutionMenu = CreatePopupMenu();
        HMENU fpsMenu = CreatePopupMenu();
        HMENU formatMenu = CreatePopupMenu();
        if (!modeMenu || !resolutionMenu || !fpsMenu || !formatMenu) {
            if (modeMenu) DestroyMenu(modeMenu);
            if (resolutionMenu) DestroyMenu(resolutionMenu);
            if (fpsMenu) DestroyMenu(fpsMenu);
            if (formatMenu) DestroyMenu(formatMenu);
            return;
        }

        const std::wstring currentLabel = Tr(language_, L"Actual: ", L"Current: ") + activeMode_.Label();
        AppendMenuW(modeMenu, MF_STRING | MF_GRAYED, 0, currentLabel.c_str());
        AppendMenuW(modeMenu, MF_SEPARATOR, 0, nullptr);

        for (size_t i = 0; i < menuResolutions_.size() && i < 100; ++i) {
            wchar_t label[64]{};
            _snwprintf_s(label, _countof(label), _TRUNCATE, L"%u x %u",
                         menuResolutions_[i].first, menuResolutions_[i].second);
            entry(resolutionMenu, IDM_RESOLUTION + static_cast<UINT>(i), label,
                  activeMode_.width == menuResolutions_[i].first &&
                  activeMode_.height == menuResolutions_[i].second);
        }
        for (size_t i = 0; i < menuFps_.size() && i < 100; ++i) {
            wchar_t label[64]{};
            if (std::fabs(menuFps_[i] - std::round(menuFps_[i])) < 0.01) {
                _snwprintf_s(label, _countof(label), _TRUNCATE, L"%.0f fps", menuFps_[i]);
            } else {
                _snwprintf_s(label, _countof(label), _TRUNCATE, L"%.2f fps", menuFps_[i]);
            }
            entry(fpsMenu, IDM_FPS + static_cast<UINT>(i), label,
                  std::fabs(activeMode_.Fps() - menuFps_[i]) < 0.01);
        }
        for (size_t i = 0; i < menuFormats_.size() && i < 100; ++i) {
            std::wstring label = GuidFormatName(menuFormats_[i]);
            if (menuFormats_[i] == MFVideoFormat_NV12) label += Tr(language_, L" (recomendado)", L" (recommended)");
            else if (menuFormats_[i] == MFVideoFormat_I420) label += L" (CPU)";
            else if (menuFormats_[i] == MFVideoFormat_P010) label += L" (10-bit)";
            else if (menuFormats_[i] == MFVideoFormat_RGB32) label += Tr(language_, L" (ancho de banda alto)", L" (high bandwidth)");
            entry(formatMenu, IDM_FORMAT + static_cast<UINT>(i), label.c_str(),
                  activeMode_.subtype == menuFormats_[i]);
        }

        AppendMenuW(modeMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(resolutionMenu),
                    Tr(language_, L"Resolución", L"Resolution"));
        AppendMenuW(modeMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(fpsMenu), L"FPS");
        AppendMenuW(modeMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(formatMenu),
                    Tr(language_, L"Formato crudo", L"Raw format"));
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(modeMenu),
                    Tr(language_, L"Modo de video (reinicia captura)", L"Video mode (restarts capture)"));
    }

    void ShowContextMenu(LPARAM position) {
        menuOpen_ = true;
        SetCaptureCursorHidden(false);

        POINT point{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
        if (point.x == -1 && point.y == -1) {
            RECT rect{};
            GetClientRect(window_, &rect);
            point = {(rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2};
            ClientToScreen(window_, &point);
        }

        HMENU menu = CreatePopupMenu();
        if (!menu) {
            menuOpen_ = false;
            lastMouseMove_ = GetTickCount64();
            return;
        }
        auto entry = [](HMENU target, UINT id, const wchar_t* name,
                        bool checked = false, bool enabled = true) {
            AppendMenuW(target, MF_STRING | (checked ? MF_CHECKED : 0) |
                        (enabled ? 0 : MF_GRAYED), id, name);
        };

        HMENU gain = CreatePopupMenu();
        if (gain) {
            const int gains[] = {0, 6, 12, 15, 18};
            for (int i = 0; i < 5; ++i) {
                const std::wstring label = (gains[i] ? L"+" : L"") +
                                           std::to_wstring(gains[i]) + L" dB";
                entry(gain, IDM_GAIN + i, label.c_str(), SelectedGain() == gains[i]);
            }
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(gain),
                        Tr(language_, L"Amplificación de audio", L"Audio amplification"));
        }
        entry(menu, IDM_MUTE, Tr(language_, L"Silencio\tM", L"Mute\tM"), audio_.IsMuted());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        entry(menu, IDM_VSYNC, Tr(language_, L"VSync (máximo 1 cuadro)", L"VSync (maximum 1 frame)"), !activeLowLatency_.load());
        entry(menu, IDM_AUTOTUNE, Tr(language_, L"AutoTune: optimizar captura", L"AutoTune: optimize capture"));
        HMENU colorRange = CreatePopupMenu();
        if (colorRange) {
            entry(colorRange, IDM_COLOR_LIMITED, Tr(language_, L"Limitado (16-235)", L"Limited (16-235)"),
                  !activeInputFullRange_.load());
            entry(colorRange, IDM_COLOR_FULL, Tr(language_, L"Completo (0-255)", L"Full (0-255)"),
                  activeInputFullRange_.load());
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(colorRange),
                        Tr(language_, L"Rango de color de entrada", L"Input color range"));
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        entry(menu, IDM_EFFECTS_TOGGLE, Tr(language_,
              pictureInPicture_ ? L"Efectos pausados durante PiP" : L"Activar efectos de video\tF6",
              pictureInPicture_ ? L"Effects paused during PiP" : L"Enable video effects\tF6"),
              effectsSettings_.enabled, !pictureInPicture_);
        entry(menu, IDM_EFFECTS_EDITOR,
              Tr(language_, L"Ajustar nitidez, color y CRT...\tF7",
                            L"Adjust sharpening, color and CRT...\tF7"),
              false, !pictureInPicture_);
        entry(menu, IDM_LATENCY_HUD, Tr(language_, L"Latencia aproximada\tF8", L"Estimated latency\tF8"),
              latencyHudEnabled_.load());
        entry(menu, IDM_PACING_SCOPE, Tr(language_, L"Osciloscopio de frame pacing\tF9",
                                                     L"Frame pacing scope\tF9"),
              pacingScopeEnabled_.load());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        const auto overlay = std::atomic_load(&overlayConfig_);
        entry(menu, IDM_OVERLAY_TOGGLE, Tr(language_, L"Mostrar overlays\tF10", L"Show overlays\tF10"),
              overlay && overlay->enabled);
        entry(menu, IDM_OVERLAY_EDITOR, Tr(language_, L"Editor de overlays...", L"Overlay editor..."));
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        BuildModeMenu(menu);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        entry(menu, IDM_FULLSCREEN, Tr(language_, L"Pantalla completa\tF11", L"Fullscreen\tF11"), fullscreen_);
        entry(menu, IDM_PICTURE_IN_PICTURE,
              Tr(language_, L"Imagen en imagen\tF12", L"Picture in Picture\tF12"),
              pictureInPicture_);
        entry(menu, IDM_SETTINGS, Tr(language_, L"Configuración...", L"Settings..."));

        const UINT selected = TrackPopupMenu(menu,
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_NOANIMATION,
            point.x, point.y, 0, window_, nullptr);
        DestroyMenu(menu);
        menuOpen_ = false;
        lastMouseMove_ = GetTickCount64();
        hasLastCursorPosition_ = false;
        SetCaptureCursorHidden(false);
        if (selected == IDM_SETTINGS) ReturnToSettings();
        else if (selected && running_.load()) OnMenuCommand(selected);
    }

    void OnMenuCommand(UINT id) {
        if (id == IDM_SETTINGS) {
            ReturnToSettings();
            return;
        }
        if (!running_.load()) return;
        if (id >= IDM_RESOLUTION && id < IDM_RESOLUTION + 100) {
            const size_t index = id - IDM_RESOLUTION;
            if (index < menuResolutions_.size()) {
                ModeCandidate requested{};
                requested.width = menuResolutions_[index].first;
                requested.height = menuResolutions_[index].second;
                SwitchCaptureMode(ClosestModeIndex(requested, ModeAxis::Resolution));
            }
            return;
        }
        if (id >= IDM_FPS && id < IDM_FPS + 100) {
            const size_t index = id - IDM_FPS;
            if (index < menuFps_.size()) {
                ModeCandidate requested{};
                requested.fps = menuFps_[index];
                SwitchCaptureMode(ClosestModeIndex(requested, ModeAxis::Fps));
            }
            return;
        }
        if (id >= IDM_FORMAT && id < IDM_FORMAT + 100) {
            const size_t index = id - IDM_FORMAT;
            if (index < menuFormats_.size()) {
                ModeCandidate requested{};
                requested.format = FormatCode(menuFormats_[index]);
                SwitchCaptureMode(ClosestModeIndex(requested, ModeAxis::Format));
            }
            return;
        }

        if (id == IDM_FULLSCREEN) {
            ToggleFullscreen();
        } else if (id == IDM_PICTURE_IN_PICTURE) {
            TogglePictureInPicture();
        } else if (id == IDM_AUTOTUNE) {
            RunAutoTune();
        } else if (id == IDM_MUTE) {
            audio_.ToggleMute();
            UpdateFpsTitle();
        } else if (id == IDM_VSYNC) {
            const bool newLowLatency = !activeLowLatency_.load();
            activeLowLatency_.store(newLowLatency);
            if (renderer_) renderer_->SetLowLatency(newLowLatency);
            ComboBox_SetCurSel(syncCombo_, newLowLatency ? 0 : 1);
            WritePrivateProfileStringW(L"capture", L"ultra_low_latency",
                                       newLowLatency ? L"1" : L"0", IniPath().c_str());
            cadenceWarningShown_ = false;
            MaybeShowCadenceWarning();
            LogLine(L"Sincronización cambiada en vivo: %s",
                    newLowLatency ? L"VSync OFF / intervalo 0" : L"VSync ON / intervalo 1");
        } else if (id == IDM_EFFECTS_TOGGLE) {
            if (!pictureInPicture_) ToggleEffects();
        } else if (id == IDM_EFFECTS_EDITOR) {
            if (!pictureInPicture_) OpenEffectsEditor();
        } else if (id == IDM_LATENCY_HUD) {
            ToggleLatencyHud();
        } else if (id == IDM_PACING_SCOPE) {
            TogglePacingScope();
        } else if (id == IDM_OVERLAY_TOGGLE) {
            ToggleOverlays();
        } else if (id == IDM_OVERLAY_EDITOR) {
            OpenOverlayEditor();
        } else if (id == IDM_COLOR_LIMITED || id == IDM_COLOR_FULL) {
            const bool fullRange = id == IDM_COLOR_FULL;
            activeInputFullRange_.store(fullRange);
            if (renderer_) renderer_->SetInputFullRange(fullRange);
            WritePrivateProfileStringW(L"capture", L"input_full_range",
                                       fullRange ? L"1" : L"0", IniPath().c_str());
            LogLine(L"Rango de entrada cambiado en vivo: %s",
                    fullRange ? L"Completo 0-255" : L"Limitado 16-235");
        } else if (id >= IDM_GAIN && id < IDM_GAIN + 5) {
            ComboBox_SetCurSel(gainCombo_, id - IDM_GAIN);
            audio_.SetGain(SelectedGain());
            const std::wstring value = std::to_wstring(SelectedGain());
            WritePrivateProfileStringW(L"capture", L"gain_db", value.c_str(), IniPath().c_str());
        }
    }

    void ToggleLatencyHud() {
        const bool enabled = !latencyHudEnabled_.load();
        latencyHudEnabled_.store(enabled);
        WritePrivateProfileStringW(L"capture", L"latency_hud",
                                   enabled ? L"1" : L"0", IniPath().c_str());
        LogLine(L"HUD de latencia aproximada: %s", enabled ? L"activado" : L"desactivado");
    }

    void TogglePacingScope() {
        const bool enabled = !pacingScopeEnabled_.load();
        pacingScopeEnabled_.store(enabled);
        WritePrivateProfileStringW(L"capture", L"pacing_scope",
                                   enabled ? L"1" : L"0", IniPath().c_str());
        ShowViewerNotice(enabled
            ? Tr(language_, L"Osciloscopio de frame pacing activado (F9)",
                            L"Frame pacing scope enabled (F9)")
            : Tr(language_, L"Osciloscopio de frame pacing desactivado",
                            L"Frame pacing scope disabled"), 2600);
        LogLine(L"Frame pacing scope: %s", enabled ? L"activado" : L"desactivado");
    }

    void ShowViewerNotice(const std::wstring& text, ULONGLONG durationMs = 4500) {
        auto notice = std::make_shared<ViewerNotice>();
        notice->text = text;
        notice->startedAt = GetTickCount64();
        notice->durationMs = std::max<ULONGLONG>(durationMs, 600);
        std::atomic_store(&viewerNotice_, std::shared_ptr<const ViewerNotice>(notice));
    }

    AutoTuneTarget CurrentDisplayTarget() const {
        AutoTuneTarget target{};
        MONITORINFOEXW monitor{};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor)) {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
                target.width = std::max<DWORD>(640, std::min<DWORD>(mode.dmPelsWidth, 1920));
                target.height = std::max<DWORD>(480, std::min<DWORD>(mode.dmPelsHeight, 1080));
                if (mode.dmDisplayFrequency > 1) {
                    target.refreshHz = NormalizeWindowsRefreshRate(mode.dmDisplayFrequency);
                }
            }
        }
        return target;
    }

    void RunAutoTune() {
        // AutoTune no adivina: puntua TODOS los modos reales del dispositivo,
        // prefiere formatos ligeros y luego prueba el ganador durante unos
        // segundos para ver si en la vida real no se anda cayendo a pedazos.
        const int deviceIndex = running_.load() ? activeDeviceIndex_
                                                : ComboBox_GetCurSel(videoCombo_);
        if (deviceIndex < 0 || deviceIndex >= static_cast<int>(videoDevices_.size())) return;
        const auto& modes = videoDevices_[deviceIndex].modes;
        std::vector<ModeCandidate> candidates;
        candidates.reserve(modes.size());
        for (const VideoMode& mode : modes) {
            candidates.push_back({mode.width, mode.height, mode.Fps(), FormatCode(mode.subtype)});
        }
        const AutoTuneDecision decision = SelectAutoTuneMode(candidates, CurrentDisplayTarget());
        if (decision.index < 0 || decision.index >= static_cast<int>(modes.size())) {
            MessageBoxW(window_, Tr(language_,
                L"AutoTune no encontró un modo crudo compatible.",
                L"AutoTune could not find a compatible raw mode."), kAppTitle, MB_ICONWARNING);
            return;
        }

        const bool wasRunning = running_.load();
        const bool wasMuted = audio_.IsMuted();
        const bool previousLowLatency = activeLowLatency_.load();
        int previousMode = -1;
        if (wasRunning) {
            for (std::size_t i = 0; i < modes.size(); ++i) {
                if (modes[i].Key() == activeMode_.Key()) previousMode = static_cast<int>(i);
            }
            StopCapture();
        }

        ComboBox_SetCurSel(videoCombo_, deviceIndex);
        PopulateModeSelectors(deviceIndex, decision.index);
        ComboBox_SetCurSel(syncCombo_, 0);
        HRESULT hr = StartCapture();
        bool restoredPrevious = false;
        if (FAILED(hr) && wasRunning && previousMode >= 0 && previousMode != decision.index) {
            LogLine(L"AutoTune: el modo recomendado fallo; restaurando el anterior");
            PopulateModeSelectors(deviceIndex, previousMode);
            ComboBox_SetCurSel(syncCombo_, previousLowLatency ? 0 : 1);
            hr = StartCapture();
            restoredPrevious = SUCCEEDED(hr);
        }
        if (FAILED(hr)) {
            ShowSetup(true);
            std::wstring text = std::wstring(Tr(language_,
                L"AutoTune no pudo iniciar el modo recomendado:\n\n",
                L"AutoTune could not start the recommended mode:\n\n")) + HrText(hr);
            MessageBoxW(window_, text.c_str(), kAppTitle, MB_ICONWARNING);
            return;
        }
        if (wasMuted && !audio_.IsMuted()) audio_.ToggleMute();
        if (restoredPrevious) {
            ShowViewerNotice(Tr(language_,
                L"La capturadora rechazó el modo de AutoTune. Se restauró el modo anterior.",
                L"The capture device rejected the AutoTune mode. The previous mode was restored."),
                5200);
            return;
        }

        autoTuneActive_ = true;
        autoTuneStartedAt_ = GetTickCount64();
        autoTuneStartReceived_ = receivedFrames_.load();
        autoTuneStartDiscarded_ = discardedFrames_.load();
        autoTuneCandidateCount_ = candidates.size();
        autoTuneEstimatedMegabytesPerSecond_ = decision.estimatedMegabytesPerSecond;
        ShowViewerNotice(Tr(language_,
            L"AutoTune eligió el mejor candidato. Validando estabilidad durante 5 segundos...",
            L"AutoTune selected the best candidate. Validating stability for 5 seconds..."), 5200);
        LogLine(L"AutoTune: %zu modos evaluados; candidato %s; USB estimado %.1f MiB/s",
                candidates.size(), activeMode_.Label().c_str(), decision.estimatedMegabytesPerSecond);
    }

    void UpdateAutoTune() {
        // Esta segunda mitad corre despues de la prueba. Mide jitter, frames
        // recibidos/descartados y tiempo de GPU para dar una calificacion util.
        if (!autoTuneActive_) return;
        if (!running_.load()) {
            autoTuneActive_ = false;
            return;
        }
        const ULONGLONG elapsed = GetTickCount64() - autoTuneStartedAt_;
        const ULONGLONG received = receivedFrames_.load() - autoTuneStartReceived_;
        if (elapsed < 5000 || (received < 30 && elapsed < 10000)) return;

        FramePacingSnapshot pacing{};
        EnterCriticalSection(&frameLock_);
        pacing = framePacing_.Snapshot();
        LeaveCriticalSection(&frameLock_);
        const ULONGLONG discarded = discardedFrames_.load() - autoTuneStartDiscarded_;
        const double expectedMs = activeMode_.Fps() > 1.0 ? 1000.0 / activeMode_.Fps() : 16.6667;
        const int score = AutoTuneQualityScore(expectedMs, pacing, received, discarded,
                                               lastProcessingDurationMs_.load(),
                                               lastPresentDurationMs_.load());
        autoTuneActive_ = false;

        wchar_t text[420]{};
        _snwprintf_s(text, _countof(text), _TRUNCATE,
            language_ == AppLanguage::English
                ? L"AutoTune complete: %d/100 · %s\n%zu modes evaluated · jitter %.2f ms · %llu governor drops"
                : L"AutoTune completado: %d/100 · %s\n%zu modos evaluados · jitter %.2f ms · %llu descartes del governor",
            score, activeMode_.Label().c_str(), autoTuneCandidateCount_, pacing.jitterMs,
            governorDiscardedFrames_.load());
        ShowViewerNotice(text, 6500);

        const std::wstring path = IniPath();
        const std::wstring scoreText = std::to_wstring(score);
        wchar_t bandwidth[48]{};
        _snwprintf_s(bandwidth, _countof(bandwidth), _TRUNCATE, L"%.1f",
                     autoTuneEstimatedMegabytesPerSecond_);
        WritePrivateProfileStringW(L"autotune", L"last_score", scoreText.c_str(), path.c_str());
        WritePrivateProfileStringW(L"autotune", L"last_mode", activeMode_.Key().c_str(), path.c_str());
        WritePrivateProfileStringW(L"autotune", L"estimated_mib_s", bandwidth, path.c_str());
        LogLine(L"AutoTune completado: score=%d, recibidos=%llu, descartados=%llu, jitter=%.3f ms",
                score, received, discarded, pacing.jitterMs);
    }

    void MaybeShowCadenceWarning() {
        if (!running_.load() || activeLowLatency_.load()) {
            cadenceWarningShown_ = false;
            return;
        }
        const double displayHz = CurrentDisplayTarget().refreshHz;
        const CadenceMismatch mismatch = DetectCadenceMismatch(activeMode_.Fps(), displayHz, true);
        if (!mismatch.visible || cadenceWarningShown_) return;
        cadenceWarningShown_ = true;
        wchar_t text[360]{};
        _snwprintf_s(text, _countof(text), _TRUNCATE,
            language_ == AppLanguage::English
                ? L"Signal %.2f Hz → display %.2f Hz\nVSync may repeat a frame about every %.1f seconds."
                : L"Señal %.2f Hz → pantalla %.2f Hz\nVSync puede repetir un frame aproximadamente cada %.1f segundos.",
            activeMode_.Fps(), displayHz, mismatch.repeatEverySeconds);
        ShowViewerNotice(text, 6800);
    }

    void ScheduleRecovery(HRESULT reason) {
        // Phoenix no reinicia todo al primer estornudo. Programa el intento para
        // que el mensaje llegue a la UI y evita dos recuperaciones simultaneas.
        if (!running_.load() || recoveryPending_ || recoveryInProgress_) return;
        if (!PhoenixMayAttemptRecovery(recoveryAttempts_)) {
            AbandonRecovery(reason);
            return;
        }
        autoTuneActive_ = false;
        recoveryReason_ = reason;
        recoveryPending_ = true;
        recoveryDueTick_ = GetTickCount64() + 350;
        ShowViewerNotice(Tr(language_,
            L"La señal se interrumpió. Phoenix está reiniciando la captura...",
            L"The signal was interrupted. Phoenix is restarting capture..."), 3600);
        LogLine(L"Phoenix programado: %s", HrText(reason).c_str());
    }

    void AbandonRecovery(HRESULT reason) {
        recoveryPending_ = false;
        recoveryInProgress_ = false;
        recoveryReason_ = reason;
        autoTuneActive_ = false;
        LogLine(L"Phoenix agotó 3 intentos; reinicio seguro a Configuración: %s",
                HrText(reason).c_str());
        // Usamos el mismo reinicio seguro del menu Configuracion. Al matar el
        // proceso viejo nos aseguramos de que WASAPI, Media Foundation, D3D y
        // el ultimo frame congelado NO se cuelen a la nueva ventana.
        ReturnToSettings(reason);
    }

    void AttemptRecovery() {
        // Conservamos dispositivo, modo y mute; desmontamos la tuberia completa
        // y la armamos otra vez. Maximo tres intentos para no quedar en bucle.
        if (!recoveryPending_ || recoveryInProgress_) return;
        recoveryPending_ = false;
        recoveryInProgress_ = true;
        ++recoveryAttempts_;
        const int deviceIndex = activeDeviceIndex_;
        int modeIndex = -1;
        if (deviceIndex >= 0 && deviceIndex < static_cast<int>(videoDevices_.size())) {
            const auto& modes = videoDevices_[deviceIndex].modes;
            for (std::size_t i = 0; i < modes.size(); ++i) {
                if (modes[i].Key() == activeMode_.Key()) modeIndex = static_cast<int>(i);
            }
        }
        const bool wasMuted = audio_.IsMuted();
        StopCapture();
        if (deviceIndex >= 0 && modeIndex >= 0) {
            ComboBox_SetCurSel(videoCombo_, deviceIndex);
            PopulateModeSelectors(deviceIndex, modeIndex);
        }
        const HRESULT hr = StartCapture(true);
        if (SUCCEEDED(hr)) {
            if (wasMuted && !audio_.IsMuted()) audio_.ToggleMute();
            recoveryInProgress_ = false;
            recoveryReason_ = S_OK;
            recoveryStableSince_ = GetTickCount64();
            ShowViewerNotice(Tr(language_,
                L"Phoenix restauró video y audio correctamente.",
                L"Phoenix restored video and audio successfully."), 3800);
            LogLine(L"Phoenix completo en intento %d", recoveryAttempts_);
            return;
        }

        recoveryInProgress_ = false;
        recoveryReason_ = hr;
        if (PhoenixMayAttemptRecovery(recoveryAttempts_)) {
            recoveryPending_ = true;
            recoveryDueTick_ = GetTickCount64() +
                               static_cast<ULONGLONG>(400 * (1 << recoveryAttempts_));
            wchar_t retry[180]{};
            _snwprintf_s(retry, _countof(retry), _TRUNCATE,
                language_ == AppLanguage::English
                    ? L"Phoenix retry %d of 3..." : L"Reintento Phoenix %d de 3...",
                recoveryAttempts_ + 1);
            ShowViewerNotice(retry, 3000);
            LogLine(L"Phoenix fallo; reintento pendiente: %s", HrText(hr).c_str());
            return;
        }

        AbandonRecovery(hr);
    }

    void UpdateRecoveryAndHealth() {
        const ULONGLONG now = GetTickCount64();
        if (running_.load() && !recoveryPending_ && !recoveryInProgress_) {
            const ULONGLONG lastFrame = lastFrameArrivalTick_.load();
            if (lastFrame && now > lastFrame && now - lastFrame > 1800) {
                ScheduleRecovery(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
            } else if (recoveryAttempts_ > 0 && recoveryStableSince_ &&
                       now - recoveryStableSince_ > 10000) {
                recoveryAttempts_ = 0;
            }
        }
        if (recoveryPending_ && now >= recoveryDueTick_) AttemptRecovery();
    }

    void ReturnToSettings(HRESULT phoenixFailure = S_OK) {
        // Configuracion se abre en un proceso nuevo y espera a que este termine.
        // Es mas drastico que esconder el viewer, pero tambien mucho mas limpio:
        // no deja audio sonando ni el ultimo frame pegado como calcomania.
        FlushEffectsSettings(false);
        FlushOverlaySettings(false);
        wchar_t executable[32768]{};
        if (!GetModuleFileNameW(nullptr, executable, _countof(executable))) return;
        std::wstring command = L"\"" + std::wstring(executable) +
            L"\" --settings --wait-for=" + std::to_wstring(GetCurrentProcessId());
        if (FAILED(phoenixFailure)) {
            wchar_t failure[48]{};
            _snwprintf_s(failure, _countof(failure), _TRUNCATE,
                         L" --phoenix-failed=0x%08lX",
                         static_cast<unsigned long>(phoenixFailure));
            command += failure;
        }
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(executable, mutableCommand.data(), nullptr, nullptr, FALSE, 0,
                           nullptr, ModuleDirectory().c_str(), &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            ExitProcess(0);
        }
        const DWORD error = GetLastError();
        MessageBoxW(window_, (std::wstring(Tr(language_,
            L"No se pudo reiniciar la aplicación en Configuración.\n\n",
            L"The application could not restart in Settings.\n\n")) +
            HrText(HRESULT_FROM_WIN32(error))).c_str(), kAppTitle, MB_ICONERROR);
    }

    void PublishEffects(VideoEffects settings) {
        // La UI conserva lo que eligio la usuaria, pero en PiP publicamos una
        // copia con efectos apagados. Al salir de PiP reaparece la configuracion
        // original sin que nadie tenga que acordarse de como estaba.
        settings.Sanitize();
        effectsSettings_ = settings;
        VideoEffects runtimeSettings = settings;
        runtimeSettings.enabled = ShouldApplyVisualEffects(settings.enabled, pictureInPicture_);
        effectsPacked_.store(runtimeSettings.Pack(), std::memory_order_relaxed);
        effectsEditor_.Sync(settings);
        effectsDirty_ = true;
        if (!SetTimer(window_, 4, 350, nullptr)) FlushEffectsSettings();
    }
    void ToggleEffects() {
        VideoEffects settings = effectsSettings_;
        settings.enabled = !settings.enabled;
        PublishEffects(settings);
    }
    void OpenEffectsEditor() {
        SetCaptureCursorHidden(false);
        effectsEditor_.Open(instance_, window_, effectsSettings_, language_, [this](const VideoEffects& s) {
            PublishEffects(s);
        });
    }
    void FlushEffectsSettings(bool showError = true) {
        if (window_) KillTimer(window_, 4);
        if (!effectsDirty_) return;
        if (video_effects_settings::Save(IniPath(), effectsSettings_)) {
            effectsDirty_ = false;
            effectsSaveErrorShown_ = false;
        } else {
            LogLine(L"No se pudieron guardar los efectos de video");
            if (showError && !effectsSaveErrorShown_ && IsWindow(window_)) {
                effectsSaveErrorShown_ = true;
                MessageBoxW(window_, Tr(language_,
                    L"Los efectos funcionan, pero no se pudo guardar DayaahCapture.ini.\nExtrae el programa en una carpeta donde tengas permiso de escritura.",
                    L"Effects remain active, but DayaahCapture.ini could not be saved.\nExtract the program to a folder where you have write access."),
                    Tr(language_, L"Efectos de video", L"Video effects"), MB_OK | MB_ICONWARNING);
            }
        }
    }

    void ToggleDiscoMode() {
        if (!renderer_ || !renderer_->SupportsDiscoMode()) {
            LogLine(L"Modo disco RGB no compatible con el procesador de video actual");
            MessageBoxW(window_, Tr(language_,
                        L"Esta GPU no admite el filtro de tono necesario para el modo disco RGB.",
                        L"This GPU does not support the hue filter required for RGB disco mode."),
                        kAppTitle, MB_OK | MB_ICONINFORMATION);
            return;
        }
        const bool enabled = !activeDiscoMode_.load();
        activeDiscoMode_.store(enabled);
        renderer_->SetDiscoMode(enabled);
        const std::wstring title = std::wstring(kAppTitle) + (enabled
            ? Tr(language_, L" - DISCO RGB ACTIVADO", L" - DISCO RGB ENABLED")
            : Tr(language_, L" - DISCO RGB DESACTIVADO", L" - DISCO RGB DISABLED"));
        SetWindowTextW(window_, title.c_str());
        LogLine(L"Modo disco RGB: %s", enabled ? L"activado" : L"desactivado");
    }

    void PublishOverlayConfig(const OverlayConfig& value) {
        // Publicamos una foto completa de la configuracion. El render puede
        // leerla sin agarrar un candado mientras el editor prepara la siguiente.
        auto next = std::make_shared<OverlayConfig>(value);
        std::atomic_store(&overlayConfig_, std::shared_ptr<const OverlayConfig>(next));
        overlaySettingsDirty_ = true;
        if (!SetTimer(window_, 3, 350, nullptr)) FlushOverlaySettings();
    }

    std::wstring OverlaySettingsPath() const {
        return JoinPath(ModuleDirectory(), L"DayaahCapture.overlays.json");
    }

    void RestoreOverlaySettings() {
        const std::wstring path = OverlaySettingsPath();
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES &&
            (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)) return;
        OverlayConfig restored{};
        std::wstring error;
        if (!overlay_profile::Load(path, restored, error)) {
            overlayProfileNeedsBackup_ = true;
            LogLine(L"No se pudo restaurar el overlay: %s", error.c_str());
            MessageBoxW(window_, (std::wstring(Tr(language_,
                        L"No pude restaurar los overlays guardados.\n\n",
                        L"Saved overlays could not be restored.\n\n")) + error +
                        Tr(language_, L"\n\nEl archivo original no se ha modificado.",
                                      L"\n\nThe original file was not modified.")).c_str(),
                        L"Dayaah Capture - overlays", MB_ICONWARNING);
            return;
        }
        const std::wstring missing = OverlayEditor::PrepareProfileImages(restored, path);
        std::atomic_store(&overlayConfig_,
            std::shared_ptr<const OverlayConfig>(std::make_shared<OverlayConfig>(std::move(restored))));
        overlaySettingsDirty_ = false;
        overlayProfileNeedsBackup_ = false;
        if (!missing.empty()) {
            LogLine(L"Overlays restaurados con imágenes ausentes: %s", missing.c_str());
            MessageBoxW(window_, (std::wstring(Tr(language_,
                        L"Se restauraron los overlays, pero faltan estas imágenes:",
                        L"Overlays were restored, but these images are missing:")) + missing +
                        Tr(language_, L"\n\nConserva los PNG en su ubicación original.",
                                      L"\n\nKeep PNG files at their original location.")).c_str(),
                        L"Dayaah Capture - overlays", MB_ICONWARNING);
        }
    }

    void FlushOverlaySettings(bool showError = true) {
        if (IsWindow(window_)) KillTimer(window_, 3);
        if (!overlaySettingsDirty_) return;
        const auto config = std::atomic_load(&overlayConfig_);
        if (!config) return;
        const std::wstring path = OverlaySettingsPath();
        std::wstring error;
        if (overlayProfileNeedsBackup_) {
            const std::wstring backup = path + L".bad";
            if (!CopyFileW(path.c_str(), backup.c_str(), FALSE)) {
                error = L"No se pudo respaldar el perfil inválido como .bad (error " +
                        std::to_wstring(GetLastError()) + L").";
            } else {
                overlayProfileNeedsBackup_ = false;
                LogLine(L"Perfil de overlays invalido respaldado en %s", backup.c_str());
            }
        }
        if (error.empty() && overlay_profile::Save(path, *config, error)) {
            overlaySettingsDirty_ = false;
            overlaySaveErrorShown_ = false;
        } else {
            LogLine(L"No se pudo guardar el overlay: %s", error.c_str());
            if (showError && !overlaySaveErrorShown_ && IsWindow(window_)) {
                overlaySaveErrorShown_ = true;
                MessageBoxW(window_, (std::wstring(Tr(language_,
                            L"Los cambios siguen activos, pero no pude guardarlos para la próxima sesión.\n\n",
                            L"Changes remain active, but they could not be saved for the next session.\n\n")) +
                            path + L"\n\n" + error + Tr(language_,
                            L"\n\nExtrae la aplicación en una carpeta donde puedas escribir.",
                            L"\n\nExtract the application to a folder where you have write access.")).c_str(),
                            L"Dayaah Capture - overlays", MB_ICONWARNING);
            }
        }
    }

    void ToggleOverlays() {
        const auto current = std::atomic_load(&overlayConfig_);
        OverlayConfig next = current ? *current : OverlayConfig{};
        next.enabled = !next.enabled;
        ++next.revision;
        PublishOverlayConfig(next);
        overlayEditor_.SetEnabled(next.enabled);
        LogLine(L"Overlays: %s", next.enabled ? L"activados" : L"desactivados");
    }

    void OpenOverlayEditor() {
        SetCaptureCursorHidden(false);
        overlayEditor_.Open(instance_, window_, std::atomic_load(&overlayConfig_), language_,
                            [this](const OverlayConfig& value) {
                                PublishOverlayConfig(value);
                            }, [this]() {
                                FlushOverlaySettings();
                                lastMouseMove_ = GetTickCount64();
                                hasLastCursorPosition_ = false;
                                SetCaptureCursorHidden(false);
                            });
    }

    int SelectedGain() const {
        const int gains[] = {0, 6, 12, 15, 18};
        int selected = ComboBox_GetCurSel(gainCombo_);
        return selected >= 0 && selected < 5 ? gains[selected] : 15;
    }

    bool LowLatencySelected() const {
        return ComboBox_GetCurSel(syncCombo_) != 1;
    }

    static bool IsCaptureDeviceBusy(HRESULT hr) {
        // Algunos drivers dicen "acceso denegado" mientras terminan de soltar
        // el pin de video anterior. Compartir la capturadora con otra app sigue
        // fallando igual; nomas le damos una oportunidad breve al cierre nuestro.
        return hr == E_ACCESSDENIED ||
               hr == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) ||
               hr == HRESULT_FROM_WIN32(ERROR_BUSY);
    }

    HRESULT ActivateVideoSource(int deviceIndex) {
        // Hay capturadoras que liberan el USB unos milisegundos DESPUES de que
        // Media Foundation ya dijo que termino. Pasa mucho al cambiar de modo:
        // sin esta pausita AutoTune se tropieza con su propia puerta cerrada.
        if (lastCaptureStopTick_ != 0) {
            constexpr ULONGLONG kRestartGraceMs = 180;
            const ULONGLONG elapsed = GetTickCount64() - lastCaptureStopTick_;
            if (elapsed < kRestartGraceMs) {
                Sleep(static_cast<DWORD>(kRestartGraceMs - elapsed));
            }
        }

        constexpr DWORD kBusyRetryDelaysMs[] = {80, 160, 320, 500};
        HRESULT hr = E_FAIL;
        for (std::size_t attempt = 0; ; ++attempt) {
            IMFMediaSource* candidate = nullptr;
            hr = videoDevices_[deviceIndex].activate->ActivateObject(IID_PPV_ARGS(&candidate));
            if (SUCCEEDED(hr)) {
                mediaSource_ = candidate;
                if (attempt > 0) {
                    LogLine(L"Capturadora liberada tras %zu reintento(s)", attempt);
                }
                return S_OK;
            }

            // Aunque un driver no deberia devolver objeto cuando falla, mas
            // vale barrerlo: una referencia colgada aqui nos bloquearia solitos.
            if (candidate) {
                candidate->Shutdown();
                candidate->Release();
            }
            if (!IsCaptureDeviceBusy(hr) || attempt >= _countof(kBusyRetryDelaysMs)) {
                return hr;
            }
            const DWORD delay = kBusyRetryDelaysMs[attempt];
            LogLine(L"Capturadora ocupada al reabrir (%s); reintento %zu en %lu ms",
                    HrText(hr).c_str(), attempt + 1, delay);
            Sleep(delay);
        }
    }

    HRESULT StartCapture(bool recoveryAttempt = false) {
        // Esta funcion arma la tuberia completa en orden: renderer -> fuente de
        // video -> Source Reader -> hilo de render -> audio. Si falla cualquier
        // pieza, brincamos a `failed` y StopCapture barre todo lo ya creado.
        if (running_.load()) return S_OK;
        if (!recoveryAttempt) {
            recoveryPending_ = false;
            recoveryInProgress_ = false;
            recoveryAttempts_ = 0;
        }
        discoSecret_.Reset();
        IMFAttributes* attributes = nullptr;
        IMFMediaType* selectedType = nullptr;
        IMFSourceReader* newReader = nullptr;
        std::uint64_t sessionId = 0;
        const wchar_t* startStage = L"validar seleccion";
        int deviceIndex = ComboBox_GetCurSel(videoCombo_);
        int modeIndex = SelectedModeIndex();
        if (deviceIndex < 0 || deviceIndex >= static_cast<int>(videoDevices_.size())) return E_INVALIDARG;
        if (modeIndex < 0 || modeIndex >= static_cast<int>(videoDevices_[deviceIndex].modes.size())) return E_INVALIDARG;
        activeMode_ = videoDevices_[deviceIndex].modes[modeIndex];
        activeDeviceIndex_ = deviceIndex;
        const bool lowLatency = LowLatencySelected();
        activeLowLatency_.store(lowLatency);
        SaveSettings(deviceIndex, modeIndex);

        ShowSetup(false);
        if (GetForegroundWindow() == window_) SetFocus(window_);
        startStage = L"inicializar renderer";
        renderer_ = new D3DVideoRenderer();
        HRESULT hr = renderer_->Initialize(window_, activeMode_.width, activeMode_.height,
                                           activeMode_.subtype, lowLatency,
                                           activeInputFullRange_.load());
        if (FAILED(hr)) {
            delete renderer_;
            renderer_ = nullptr;
            if (!recoveryAttempt) ShowSetup(true);
            return hr;
        }
        if (activeDiscoMode_.load() && !pictureInPicture_) {
            if (renderer_->SupportsDiscoMode()) renderer_->SetDiscoMode(true);
            else activeDiscoMode_.store(false);
        }

        sessionId = nextSessionId_.fetch_add(1) + 1;
        activeSession_.store(sessionId);

        startStage = L"abrir capturadora";
        hr = ActivateVideoSource(deviceIndex);
        if (FAILED(hr)) goto failed;

        startStage = L"crear atributos del Source Reader";
        hr = MFCreateAttributes(&attributes, 4);
        // Le pedimos baja latencia y prohibimos convertidores automaticos. Asi
        // sabemos exactamente que formato viaja y no aparece una conversion CPU
        // misteriosa entre la capturadora y nosotros.
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(kMfLowLatency, TRUE);
        callback_ = new SourceReaderCallback(this, sessionId);
        if (SUCCEEDED(hr)) hr = attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback_);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
        if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(mediaSource_, attributes, &newReader);
        SafeRelease(attributes);
        if (FAILED(hr)) goto failed;

        startStage = L"aplicar modo de video";
        hr = newReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                           activeMode_.nativeIndex, &selectedType);
        if (SUCCEEDED(hr)) {
            hr = newReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                nullptr, selectedType);
        }
        SafeRelease(selectedType);
        if (FAILED(hr)) goto failed;

        startStage = L"crear eventos de captura";
        flushEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        frameReadyEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        resizeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        renderStopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!flushEvent_ || !frameReadyEvent_ || !resizeEvent_ || !renderStopEvent_) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            goto failed;
        }
        {
            ExclusiveSrwGuard guard(readerLock_);
            reader_ = newReader;
            newReader = nullptr;
        }
        receivedFrames_ = 0;
        renderedFrames_ = 0;
        discardedFrames_ = 0;
        governorDiscardedFrames_ = 0;
        receivedFps_ = 0;
        presentedFps_ = 0;
        lastReceivedFpsFrames_ = 0;
        lastPresentedFpsFrames_ = 0;
        lastFrameArrivalTick_.store(GetTickCount64());
        lastProcessingDurationMs_.store(0.0);
        lastPresentDurationMs_.store(0.0);
        EnterCriticalSection(&frameLock_);
        framePacing_.Reset();
        framePending_ = false;
        latestFrameArrivalTick_ = lastFrameArrivalTick_.load();
        renderFrameArrivalTick_ = latestFrameArrivalTick_;
        latestFrameStride_ = 0;
        renderFrameStride_ = 0;
        LeaveCriticalSection(&frameLock_);
        running_.store(true);
        cadenceWarningShown_ = false;
        recoveryStableSince_ = GetTickCount64();
        startStage = L"crear hilo de render";
        renderThread_ = CreateThread(nullptr, 0, RenderThreadEntry, this, 0, nullptr);
        if (!renderThread_) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            goto failed;
        }
        lastMouseMove_ = GetTickCount64();
        hasLastCursorPosition_ = false;
        SetCaptureCursorHidden(false);
        {
            int audioIndex = ComboBox_GetCurSel(audioCombo_);
            if (audioIndex > 0 && audioIndex <= static_cast<int>(audioDevices_.size())) {
                HRESULT audioHr = audio_.Start(audioDevices_[audioIndex - 1].device, SelectedGain());
                if (FAILED(audioHr)) LogLine(L"Audio no pudo iniciar: %s", HrText(audioHr).c_str());
            }
        }

        LogLine(L"Captura iniciada: %s, %s",
                videoDevices_[deviceIndex].name.c_str(), activeMode_.Label().c_str());
        startStage = L"pedir primer frame";
        hr = ContinueReading(sessionId);
        if (FAILED(hr)) goto failed;
        MarkFirstRunComplete();
        return S_OK;

    failed:
        SafeRelease(newReader);
        LogLine(L"Fallo al iniciar en '%s': %s", startStage, HrText(hr).c_str());
        StopCapture();
        if (!recoveryAttempt) ShowSetup(true);
        return hr;
    }

    void StopCapture() {
        // El orden importa: paramos audio, pedimos flush al Source Reader,
        // detenemos el hilo y AL FINAL soltamos D3D/dispositivos. Hacerlo al
        // reves es una receta muy sabrosa para accesos a memoria ya liberada.
        discoSecret_.Reset();
        const bool wasRunning = running_.exchange(false);
        IMFSourceReader* readerToStop = nullptr;
        {
            // Quitamos el reader visible primero. Cualquier callback que llegue
            // tarde ya no puede pedir otro sample ni brincar al reader nuevo.
            ExclusiveSrwGuard guard(readerLock_);
            readerToStop = reader_;
            reader_ = nullptr;
        }
        if (!wasRunning && !readerToStop && !renderer_ && !renderThread_ &&
            !callback_ && !mediaSource_ && !flushEvent_) {
            activeSession_.store(0);
            return;
        }
        audio_.Stop();
        if (readerToStop) {
            if (flushEvent_) ResetEvent(flushEvent_);
            const HRESULT flushHr = readerToStop->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
            if (SUCCEEDED(flushHr) && flushEvent_) {
                const DWORD wait = WaitForSingleObject(flushEvent_, 500);
                if (wait != WAIT_OBJECT_0) LogLine(L"Source Reader: timeout esperando OnFlush");
            } else if (FAILED(flushHr)) {
                LogLine(L"Source Reader: Flush fallo: %s", HrText(flushHr).c_str());
            }
        }
        // Va DESPUES de OnFlush: si lo hicieramos antes, el callback ya no
        // podria prender flushEvent_ y siempre nos comeriamos el timeout.
        if (callback_) callback_->Detach();
        SafeRelease(readerToStop);
        if (renderStopEvent_) SetEvent(renderStopEvent_);
        if (renderThread_) {
            WaitForSingleObject(renderThread_, INFINITE);
            CloseHandle(renderThread_);
            renderThread_ = nullptr;
        }
        if (frameReadyEvent_) {
            CloseHandle(frameReadyEvent_);
            frameReadyEvent_ = nullptr;
        }
        if (resizeEvent_) {
            CloseHandle(resizeEvent_);
            resizeEvent_ = nullptr;
        }
        if (renderStopEvent_) {
            CloseHandle(renderStopEvent_);
            renderStopEvent_ = nullptr;
        }
        // Release del Source Reader suele apagar la fuente, pero lo hacemos
        // explicito tambien. Luego ShutdownObject borra las referencias internas
        // del IMFActivate para que el siguiente ActivateObject cree una limpia.
        if (mediaSource_) {
            const HRESULT sourceShutdownHr = mediaSource_->Shutdown();
            if (FAILED(sourceShutdownHr) && sourceShutdownHr != MF_E_SHUTDOWN) {
                LogLine(L"Media Source: Shutdown fallo: %s", HrText(sourceShutdownHr).c_str());
            }
        }
        if (activeDeviceIndex_ >= 0 && activeDeviceIndex_ < static_cast<int>(videoDevices_.size())) {
            const HRESULT activateShutdownHr =
                videoDevices_[activeDeviceIndex_].activate->ShutdownObject();
            if (FAILED(activateShutdownHr) && activateShutdownHr != MF_E_SHUTDOWN) {
                LogLine(L"IMFActivate: ShutdownObject fallo: %s",
                        HrText(activateShutdownHr).c_str());
            }
        }
        SafeRelease(mediaSource_);
        activeDeviceIndex_ = -1;
        activeSession_.store(0);
        if (callback_) {
            callback_->Release();
            callback_ = nullptr;
        }
        if (flushEvent_) {
            CloseHandle(flushEvent_);
            flushEvent_ = nullptr;
        }
        delete renderer_;
        renderer_ = nullptr;
        SetCaptureCursorHidden(false);
        EnterCriticalSection(&frameLock_);
        latestFrame_.clear();
        renderFrame_.clear();
        framePending_ = false;
        LeaveCriticalSection(&frameLock_);
        {
            ExclusiveSrwGuard guard(sampleLock_);
            captureFrame_.clear();
            captureI420Native_.clear();
        }
        SetWindowTextW(window_, kAppTitle);
        lastCaptureStopTick_ = GetTickCount64();
        LogLine(L"Captura detenida");
    }

    static DWORD WINAPI RenderThreadEntry(LPVOID context) {
        return static_cast<DayaahApp*>(context)->RenderLoop();
    }

    DWORD RenderLoop() {
        // Este hilo solo despierta por tres cosas: cerrar, resize o frame nuevo.
        // El timeout de 16 ms existe para animar avisos aunque la señal se quede
        // quieta; si no hay animacion, vuelve a dormir.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        HANDLE events[] = {renderStopEvent_, resizeEvent_, frameReadyEvent_};
        for (;;) {
            const DWORD wait = WaitForMultipleObjects(_countof(events), events, FALSE, 16);
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_OBJECT_0 + 1) {
                const UINT width = pendingWidth_.load();
                const UINT height = pendingHeight_.load();
                if (renderer_ && width >= 16 && height >= 16) {
                    const HRESULT hr = renderer_->Resize(width, height);
                    if (FAILED(hr)) LogLine(L"Resize D3D11 fallo: %s", HrText(hr).c_str());
                }
                continue;
            }
            bool animationRedraw = false;
            if (wait == WAIT_TIMEOUT) {
                const auto notice = std::atomic_load(&viewerNotice_);
                const ULONGLONG now = GetTickCount64();
                animationRedraw = notice && now >= notice->startedAt &&
                                  now - notice->startedAt < notice->durationMs &&
                                  !renderFrame_.empty();
                if (!animationRedraw) continue;
            } else if (wait != WAIT_OBJECT_0 + 2) {
                LogLine(L"Hilo de render detenido por WaitForMultipleObjects: %lu", wait);
                break;
            }

            bool haveFrame = animationRedraw;
            bool newFrame = false;
            FramePacingSnapshot pacing{};
            std::uint32_t queueDepth = 0;
            EnterCriticalSection(&frameLock_);
            if (!animationRedraw && framePending_) {
                latestFrame_.swap(renderFrame_);
                renderFrameArrivalTick_ = latestFrameArrivalTick_;
                renderFrameStride_ = latestFrameStride_;
                framePending_ = false;
                haveFrame = true;
                newFrame = true;
            }
            LeaveCriticalSection(&frameLock_);

            EnterCriticalSection(&frameLock_);
            // Latency Governor: si llego otro frame antes de empezar la GPU,
            // mandamos el anterior alv con cariño y usamos el nuevo. Mostrar un
            // frame ya viejo solo subiria la latencia por ser "completistas".
            if (newFrame && framePending_) {
                latestFrame_.swap(renderFrame_);
                renderFrameArrivalTick_ = latestFrameArrivalTick_;
                renderFrameStride_ = latestFrameStride_;
                framePending_ = false;
                discardedFrames_++;
                governorDiscardedFrames_++;
            }
            pacing = framePacing_.Snapshot();
            queueDepth = framePending_ ? 1u : 0u;
            LeaveCriticalSection(&frameLock_);
            if (!haveFrame || !renderer_ || renderFrame_.empty() || IsIconic(window_)) continue;

            OverlayStats stats{};
            stats.receivedFrames = receivedFrames_.load();
            stats.presentedFrames = renderedFrames_.load();
            stats.discardedFrames = discardedFrames_.load();
            stats.receivedFps = receivedFps_.load();
            stats.presentedFps = presentedFps_.load();
            stats.lastFrameAgeMs = GetTickCount64() - renderFrameArrivalTick_;
            stats.width = activeMode_.width;
            stats.height = activeMode_.height;
            stats.sourceFps = activeMode_.Fps();
            stats.format = GuidFormatName(activeMode_.subtype);
            stats.lowLatency = activeLowLatency_.load();
            stats.processingMs = lastProcessingDurationMs_.load();
            stats.presentMs = lastPresentDurationMs_.load();
            stats.frameIntervalMs = pacing.averageMs;
            stats.frameJitterMs = pacing.jitterMs;
            stats.frameMinimumMs = pacing.minimumMs;
            stats.frameMaximumMs = pacing.maximumMs;
            stats.frameIntervalsMs = pacing.intervalsMs;
            stats.frameIntervalCount = pacing.count;
            stats.queueDepth = queueDepth;
            stats.governorDiscardedFrames = governorDiscardedFrames_.load();
            stats.pacingScope = pacingScopeEnabled_.load();
            stats.approximateLatencyMs = EstimateViewerLatencyMs(
                stats.sourceFps, stats.lastFrameAgeMs,
                lastPresentDurationMs_.load(), stats.lowLatency);
            stats.english = language_ == AppLanguage::English;
            const auto notice = std::atomic_load(&viewerNotice_);
            if (notice) {
                const ULONGLONG now = GetTickCount64();
                if (now >= notice->startedAt && now - notice->startedAt < notice->durationMs) {
                    stats.noticeText = notice->text;
                    stats.noticeAgeMs = now - notice->startedAt;
                    stats.noticeDurationMs = notice->durationMs;
                }
            }
            RenderTiming timing{};
            const HRESULT hr = renderer_->Render(renderFrame_.data(), renderFrame_.size(), renderFrameStride_,
                                                 std::atomic_load(&overlayConfig_), stats,
                                                 VideoEffects::Unpack(effectsPacked_.load(std::memory_order_relaxed)),
                                                 latencyHudEnabled_.load(), &timing);
            if (SUCCEEDED(hr)) {
                lastProcessingDurationMs_.store(timing.processingMs);
                lastPresentDurationMs_.store(timing.presentMs);
                if (newFrame) renderedFrames_++;
            }
            else if (hr != DXGI_STATUS_OCCLUDED) {
                LogLine(L"Present fallo: %s", HrText(hr).c_str());
            }
        }
        return 0;
    }

    void ShowSetup(bool show) {
        if (show && pictureInPicture_) ExitPictureInPicture(false);
        capturingUi_ = !show;
        HWND controls[] = {videoCombo_, resolutionCombo_, fpsCombo_, formatCombo_, formatNotice_,
                           audioCombo_, gainCombo_, syncCombo_, languageCombo_, autoTuneButton_, startButton_,
                           statusText_, kofiLink_, githubLink_};
        for (HWND control : controls) ShowWindow(control, show ? SW_SHOW : SW_HIDE);
        if (show) {
            SetWindowTextW(window_, kAppTitle);
            InvalidateRect(window_, nullptr, TRUE);
        } else {
            const std::wstring title = std::wstring(kAppTitle) +
                Tr(language_, L" - iniciando...", L" - starting...");
            SetWindowTextW(window_, title.c_str());
        }
    }

    void UpdateFpsTitle() {
        if (!running_.load()) return;
        const ULONGLONG receivedNow = receivedFrames_.load();
        const ULONGLONG presentedNow = renderedFrames_.load();
        const ULONGLONG received = receivedNow - lastReceivedFpsFrames_;
        const ULONGLONG presented = presentedNow - lastPresentedFpsFrames_;
        lastReceivedFpsFrames_ = receivedNow;
        lastPresentedFpsFrames_ = presentedNow;
        receivedFps_.store(received);
        presentedFps_.store(presented);
        wchar_t title[240]{};
        _snwprintf_s(title, _countof(title), _TRUNCATE,
                     language_ == AppLanguage::English
                        ? L"Dayaah Capture 2.2.0  -  %llu fps  -  %ux%u %.2f Hz %s  -  Audio %s%s"
                        : L"Dayaah Capture 2.2.0  -  %llu fps  -  %ux%u %.2f Hz %s  -  Audio %s%s",
                     presented, activeMode_.width, activeMode_.height, activeMode_.Fps(),
                     GuidFormatName(activeMode_.subtype).c_str(),
                     audio_.IsMuted() ? L"MUTE" : L"ON",
                     activeDiscoMode_.load() ? L"  -  DISCO RGB" : L"");
        SetWindowTextW(window_, title);
    }

    void UpdateCursorVisibility() {
        if (menuOpen_) {
            SetCaptureCursorHidden(false);
            return;
        }
        RecordRealCursorMovement();

        POINT cursor{};
        RECT client{};
        bool pointerInside = false;
        if (GetCursorPos(&cursor)) {
            ScreenToClient(window_, &cursor);
            GetClientRect(window_, &client);
            pointerInside = PtInRect(&client, cursor) == TRUE;
        }
        const bool shouldHide = ShouldHideCaptureCursor(
            running_.load(), GetForegroundWindow() == window_, IsIconic(window_) != FALSE,
            pointerInside, GetTickCount64() - lastMouseMove_);
        SetCaptureCursorHidden(shouldHide);
    }

    void RecordRealCursorMovement() {
        POINT cursor{};
        if (!GetCursorPos(&cursor)) return;
        if (!PointerPositionChanged(hasLastCursorPosition_,
                                    lastCursorPosition_.x, lastCursorPosition_.y,
                                    cursor.x, cursor.y)) {
            return;
        }
        lastCursorPosition_ = cursor;
        hasLastCursorPosition_ = true;
        lastMouseMove_ = GetTickCount64();
        SetCaptureCursorHidden(false);
    }

    void SetCaptureCursorHidden(bool hidden) {
        cursorHidden_ = hidden && !menuOpen_;

        if (!window_) return;
        POINT cursor{};
        RECT client{};
        if (!GetCursorPos(&cursor) || !ScreenToClient(window_, &cursor) ||
            !GetClientRect(window_, &client) || !PtInRect(&client, cursor)) {
            return;
        }
        const bool hideNow = cursorHidden_ && running_.load() &&
                             GetForegroundWindow() == window_;
        SetCursor(hideNow ? hiddenCursor_ : visibleCursor_);
    }

    void ToggleFullscreen() {
        // Pantalla completa real sin bordes. Guardamos estilo y posicion para
        // devolver la ventana exactamente a donde estaba.
        if (!running_.load() && !fullscreen_) return;
        if (pictureInPicture_) ExitPictureInPicture(false);
        if (!fullscreen_) {
            SetCaptureCursorHidden(false);
            windowStyle_ = GetWindowLongPtrW(window_, GWL_STYLE);
            windowPlacement_.length = sizeof(windowPlacement_);
            GetWindowPlacement(window_, &windowPlacement_);
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
            SetWindowLongPtrW(window_, GWL_STYLE, windowStyle_ & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(window_, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                         monitor.rcMonitor.right - monitor.rcMonitor.left,
                         monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
            fullscreen_ = true;
        } else {
            SetWindowLongPtrW(window_, GWL_STYLE, windowStyle_);
            SetWindowPlacement(window_, &windowPlacement_);
            SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                         SWP_NOZORDER | SWP_NOOWNERZORDER);
            fullscreen_ = false;
            lastMouseMove_ = GetTickCount64();
        }
    }

    void SavePictureInPictureBounds() {
        if (!pictureInPicture_ || !window_ || IsIconic(window_)) return;
        RECT rect{};
        if (!GetWindowRect(window_, &rect)) return;
        const std::wstring path = IniPath();
        const auto writeNumber = [&](const wchar_t* key, long value) {
            const std::wstring text = std::to_wstring(value);
            WritePrivateProfileStringW(L"picture_in_picture", key, text.c_str(), path.c_str());
        };
        writeNumber(L"left", rect.left);
        writeNumber(L"top", rect.top);
        writeNumber(L"width", rect.right - rect.left);
        writeNumber(L"height", rect.bottom - rect.top);
    }

    void EnterPictureInPicture() {
        // PiP reutiliza la MISMA captura; solo cambia la ropa de la ventana.
        // Pausamos shaders creativos y modo disco porque en una ventanita nomas
        // gastarian GPU, pero overlays, audio y telemetria siguen vivos.
        if (pictureInPicture_ || !running_.load()) return;
        if (fullscreen_) ToggleFullscreen();

        effectsEditor_.Close();
        FlushEffectsSettings(false);
        SetCaptureCursorHidden(false);
        pictureInPictureStyle_ = GetWindowLongPtrW(window_, GWL_STYLE);
        pictureInPictureExStyle_ = GetWindowLongPtrW(window_, GWL_EXSTYLE);
        pictureInPicturePlacement_.length = sizeof(pictureInPicturePlacement_);
        GetWindowPlacement(window_, &pictureInPicturePlacement_);

        constexpr LONG_PTR pipStyle = static_cast<LONG_PTR>(WS_POPUP | WS_THICKFRAME | WS_VISIBLE);
        const LONG_PTR pipExStyle =
            (pictureInPictureExStyle_ | static_cast<LONG_PTR>(WS_EX_TOPMOST | WS_EX_TOOLWINDOW)) &
            ~static_cast<LONG_PTR>(WS_EX_APPWINDOW);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);

        RECT defaultRect{0, 0, 480, 270};
        AdjustWindowRectEx(&defaultRect, static_cast<DWORD>(pipStyle), FALSE,
                           static_cast<DWORD>(pipExStyle));
        const long defaultWidth = defaultRect.right - defaultRect.left;
        const long defaultHeight = defaultRect.bottom - defaultRect.top;
        const long fallbackLeft = monitor.rcWork.right - defaultWidth - 24;
        const long fallbackTop = monitor.rcWork.top + 24;
        const auto readPipNumber = [&](const wchar_t* key, long fallback) {
            wchar_t fallbackText[32]{};
            wchar_t value[32]{};
            _snwprintf_s(fallbackText, _countof(fallbackText), _TRUNCATE, L"%ld", fallback);
            GetPrivateProfileStringW(L"picture_in_picture", key, fallbackText,
                                     value, _countof(value), IniPath().c_str());
            wchar_t* end = nullptr;
            const long parsed = wcstol(value, &end, 10);
            return end != value ? parsed : fallback;
        };
        PictureInPictureBounds requested{
            readPipNumber(L"left", fallbackLeft),
            readPipNumber(L"top", fallbackTop), 0, 0};
        requested.right = requested.left + readPipNumber(L"width", defaultWidth);
        requested.bottom = requested.top + readPipNumber(L"height", defaultHeight);
        const PictureInPictureBounds work{
            monitor.rcWork.left, monitor.rcWork.top, monitor.rcWork.right, monitor.rcWork.bottom};
        const PictureInPictureBounds bounds = ClampPictureInPictureBounds(
            requested, work, 320, 180);

        pictureInPicture_ = true;
        VideoEffects runtimeEffects = effectsSettings_;
        runtimeEffects.enabled = ShouldApplyVisualEffects(effectsSettings_.enabled, pictureInPicture_);
        effectsPacked_.store(runtimeEffects.Pack(), std::memory_order_relaxed);
        if (renderer_ && activeDiscoMode_.load()) renderer_->SetDiscoMode(false);

        SetWindowLongPtrW(window_, GWL_STYLE, pipStyle);
        SetWindowLongPtrW(window_, GWL_EXSTYLE, pipExStyle);
        ShowWindow(window_, SW_RESTORE);
        SetWindowPos(window_, HWND_TOPMOST, bounds.left, bounds.top,
                     bounds.right - bounds.left, bounds.bottom - bounds.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
        ShowViewerNotice(Tr(language_,
            L"PiP activo · efectos pausados · F12 para salir",
            L"PiP active · effects paused · F12 to exit"), 3800);
        LogLine(L"PiP activado; efectos visuales pausados");
    }

    void ExitPictureInPicture(bool showNotice = true) {
        // Restauramos estilo, posicion, z-order y efectos que estaban activos.
        // Nada se pierde: PiP es una pausa visual, no otra configuracion.
        if (!pictureInPicture_) return;
        SavePictureInPictureBounds();
        SetCaptureCursorHidden(false);
        pictureInPicture_ = false;

        SetWindowLongPtrW(window_, GWL_STYLE, pictureInPictureStyle_);
        SetWindowLongPtrW(window_, GWL_EXSTYLE, pictureInPictureExStyle_);
        SetWindowPlacement(window_, &pictureInPicturePlacement_);
        const HWND zOrder = (pictureInPictureExStyle_ & WS_EX_TOPMOST)
            ? HWND_TOPMOST : HWND_NOTOPMOST;
        SetWindowPos(window_, zOrder, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOOWNERZORDER | SWP_SHOWWINDOW);

        effectsPacked_.store(effectsSettings_.Pack(), std::memory_order_relaxed);
        if (renderer_ && activeDiscoMode_.load() && renderer_->SupportsDiscoMode()) {
            renderer_->SetDiscoMode(true);
        }
        UpdateFpsTitle();
        lastMouseMove_ = GetTickCount64();
        hasLastCursorPosition_ = false;
        if (showNotice) {
            ShowViewerNotice(Tr(language_,
                effectsSettings_.enabled
                    ? L"PiP cerrado · efectos restaurados"
                    : L"PiP cerrado",
                effectsSettings_.enabled
                    ? L"PiP closed · effects restored"
                    : L"PiP closed"), 3000);
        }
        LogLine(L"PiP desactivado; estado de efectos restaurado");
    }

    void TogglePictureInPicture() {
        if (pictureInPicture_) ExitPictureInPicture();
        else EnterPictureInPicture();
    }

    void ReleaseDevices() {
        for (auto& device : videoDevices_) SafeRelease(device.activate);
        for (auto& device : audioDevices_) SafeRelease(device.device);
        videoDevices_.clear();
        audioDevices_.clear();
    }

    std::wstring IniPath() const {
        return JoinPath(ModuleDirectory(), L"DayaahCapture.ini");
    }

    std::wstring ReadSetting(const wchar_t* key, const wchar_t* fallback) const {
        wchar_t value[512]{};
        GetPrivateProfileStringW(L"capture", key, fallback, value, _countof(value), IniPath().c_str());
        return value;
    }

    std::wstring ReadFirstRunSetting(const wchar_t* key, const wchar_t* fallback) const {
        wchar_t value[512]{};
        GetPrivateProfileStringW(L"first_run", key, fallback, value, _countof(value), IniPath().c_str());
        return value;
    }

    void MarkFirstRunComplete() {
        // Solo marcamos el primer arranque como completado DESPUES de iniciar
        // una captura buena. Si algo fallo, Configuracion vuelve a salir en el
        // siguiente intento en vez de aventar al usuario al vacio.
        if (!firstRun_) return;
        const std::wstring path = IniPath();
        const bool deviceSaved = WritePrivateProfileStringW(
            L"first_run", L"device", deviceFingerprint_.c_str(), path.c_str()) != FALSE;
        const bool completeSaved = WritePrivateProfileStringW(
            L"first_run", L"complete", L"1", path.c_str()) != FALSE;
        const bool markerSaved = WritePrivateProfileStringW(
            L"first_run", L"Firstrun", L"true", path.c_str()) != FALSE;
        if (deviceSaved && completeSaved && markerSaved) firstRun_ = false;
    }

    void SaveSettings(int videoIndex, int modeIndex) {
        const std::wstring path = IniPath();
        WritePrivateProfileStringW(L"capture", L"video_device",
                                   videoDevices_[videoIndex].name.c_str(), path.c_str());
        WritePrivateProfileStringW(L"capture", L"video_mode",
                                   videoDevices_[videoIndex].modes[modeIndex].Key().c_str(), path.c_str());
        int audioIndex = ComboBox_GetCurSel(audioCombo_);
        const wchar_t* audioName = L"";
        if (audioIndex > 0 && audioIndex <= static_cast<int>(audioDevices_.size())) {
            audioName = audioDevices_[audioIndex - 1].name.c_str();
        }
        WritePrivateProfileStringW(L"capture", L"audio_device", audioName, path.c_str());
        wchar_t number[16]{};
        _snwprintf_s(number, _countof(number), _TRUNCATE, L"%d", SelectedGain());
        WritePrivateProfileStringW(L"capture", L"gain_db", number, path.c_str());
        WritePrivateProfileStringW(L"capture", L"ultra_low_latency",
                                   LowLatencySelected() ? L"1" : L"0",
                                   path.c_str());
        WritePrivateProfileStringW(L"capture", L"input_full_range",
                                   activeInputFullRange_.load() ? L"1" : L"0",
                                   path.c_str());
    }

    // ----- Ventana y controles de Configuracion -----
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND videoCombo_ = nullptr;
    HWND resolutionCombo_ = nullptr;
    HWND fpsCombo_ = nullptr;
    HWND formatCombo_ = nullptr;
    HWND formatNotice_ = nullptr;
    HWND audioCombo_ = nullptr;
    HWND gainCombo_ = nullptr;
    HWND syncCombo_ = nullptr;
    HWND languageCombo_ = nullptr;
    HWND autoTuneButton_ = nullptr;
    HWND startButton_ = nullptr;
    HWND statusText_ = nullptr;
    HWND kofiLink_ = nullptr;
    HWND githubLink_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH darkBrush_ = nullptr;
    HBRUSH controlBrush_ = nullptr;
    HCURSOR visibleCursor_ = nullptr;
    HCURSOR hiddenCursor_ = nullptr;
    // ----- Dispositivos elegidos y objetos de captura -----
    IMMDeviceEnumerator* audioEnumerator_ = nullptr;
    std::vector<VideoDeviceInfo> videoDevices_;
    std::vector<AudioDeviceInfo> audioDevices_;
    std::vector<std::pair<UINT32, UINT32>> menuResolutions_;
    std::vector<double> menuFps_;
    std::vector<GUID> menuFormats_;
    std::vector<std::pair<UINT32, UINT32>> setupResolutions_;
    std::vector<double> setupFps_;
    std::vector<GUID> setupFormats_;
    VideoMode activeMode_{};
    int activeDeviceIndex_ = -1;
    D3DVideoRenderer* renderer_ = nullptr;
    AudioBridge audio_;
    IMFMediaSource* mediaSource_ = nullptr;
    SRWLOCK readerLock_ = SRWLOCK_INIT;
    IMFSourceReader* reader_ = nullptr;
    SourceReaderCallback* callback_ = nullptr;
    HANDLE flushEvent_ = nullptr;
    std::atomic<std::uint64_t> nextSessionId_{0};
    std::atomic<std::uint64_t> activeSession_{0};
    ULONGLONG lastCaptureStopTick_ = 0;
    // ----- Buzon de frames entre Media Foundation y el hilo de render -----
    // Solo caben el frame mas reciente y el que se esta pintando. Ese limite
    // pequeño es completamente a proposito.
    std::atomic<bool> running_{false};
    std::atomic<HRESULT> captureError_{S_OK};
    SRWLOCK sampleLock_ = SRWLOCK_INIT;
    CRITICAL_SECTION frameLock_{};
    std::vector<BYTE> captureFrame_;
    std::vector<BYTE> captureI420Native_;
    std::vector<BYTE> latestFrame_;
    std::vector<BYTE> renderFrame_;
    bool framePending_ = false;
    HANDLE frameReadyEvent_ = nullptr;
    HANDLE resizeEvent_ = nullptr;
    HANDLE renderStopEvent_ = nullptr;
    HANDLE renderThread_ = nullptr;
    std::atomic<UINT> pendingWidth_{0};
    std::atomic<UINT> pendingHeight_{0};
    // ----- Telemetria y Latency Governor -----
    std::atomic<ULONGLONG> receivedFrames_{0};
    std::atomic<ULONGLONG> renderedFrames_{0};
    std::atomic<ULONGLONG> discardedFrames_{0};
    std::atomic<ULONGLONG> receivedFps_{0};
    std::atomic<ULONGLONG> presentedFps_{0};
    std::atomic<ULONGLONG> lastFrameArrivalTick_{0};
    ULONGLONG latestFrameArrivalTick_ = 0;
    ULONGLONG renderFrameArrivalTick_ = 0;
    LONG latestFrameStride_ = 0;
    LONG renderFrameStride_ = 0;
    FramePacingTracker framePacing_;
    std::atomic<ULONGLONG> governorDiscardedFrames_{0};
    std::atomic<double> lastProcessingDurationMs_{0.0};
    std::atomic<double> lastPresentDurationMs_{0.0};
    std::atomic<bool> activeLowLatency_{true};
    std::atomic<bool> activeInputFullRange_{false};
    std::atomic<bool> activeDiscoMode_{false};
    std::atomic<bool> latencyHudEnabled_{false};
    std::atomic<bool> pacingScopeEnabled_{false};
    std::shared_ptr<const ViewerNotice> viewerNotice_;
    bool autoTuneActive_ = false;
    ULONGLONG autoTuneStartedAt_ = 0;
    ULONGLONG autoTuneStartReceived_ = 0;
    ULONGLONG autoTuneStartDiscarded_ = 0;
    std::size_t autoTuneCandidateCount_ = 0;
    double autoTuneEstimatedMegabytesPerSecond_ = 0.0;
    bool cadenceWarningShown_ = false;
    // ----- Phoenix Recovery -----
    bool recoveryPending_ = false;
    bool recoveryInProgress_ = false;
    int recoveryAttempts_ = 0;
    ULONGLONG recoveryDueTick_ = 0;
    ULONGLONG recoveryStableSince_ = 0;
    HRESULT recoveryReason_ = S_OK;
    DiscoSecret discoSecret_;
    ULONGLONG lastReceivedFpsFrames_ = 0;
    ULONGLONG lastPresentedFpsFrames_ = 0;
    // ----- Overlays y efectos publicados sin frenar al render -----
    std::shared_ptr<const OverlayConfig> overlayConfig_;
    OverlayEditor overlayEditor_;
    VideoEffects effectsSettings_;
    std::atomic<std::uint64_t> effectsPacked_{VideoEffects{}.Pack()};
    VideoEffectsEditor effectsEditor_;
    bool effectsDirty_ = false;
    bool effectsSaveErrorShown_ = false;
    bool overlaySettingsDirty_ = false;
    bool overlaySaveErrorShown_ = false;
    bool overlayProfileNeedsBackup_ = false;
    // ----- Comportamiento de ventana, cursor, fullscreen y PiP -----
    ULONGLONG lastMouseMove_ = 0;
    POINT lastCursorPosition_{};
    bool hasLastCursorPosition_ = false;
    bool cursorHidden_ = false;
    bool mouseTracking_ = false;
    bool menuOpen_ = false;
    bool capturingUi_ = false;
    bool fullscreen_ = false;
    bool pictureInPicture_ = false;
    LONG_PTR windowStyle_ = 0;
    WINDOWPLACEMENT windowPlacement_{};
    LONG_PTR pictureInPictureStyle_ = 0;
    LONG_PTR pictureInPictureExStyle_ = 0;
    WINDOWPLACEMENT pictureInPicturePlacement_{};
    AppLanguage language_ = AppLanguage::Spanish;
    std::wstring deviceFingerprint_;
    bool firstRun_ = true;
};

STDMETHODIMP SourceReaderCallback::QueryInterface(REFIID iid, void** object) {
    if (!object) return E_POINTER;
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFSourceReaderCallback)) {
        *object = static_cast<IMFSourceReaderCallback*>(this);
        AddRef();
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP SourceReaderCallback::OnReadSample(HRESULT status, DWORD, DWORD streamFlags,
                                                 LONGLONG timestamp, IMFSample* sample) {
    SharedSrwGuard guard(appLock_);
    DayaahApp* app = app_;
    if (!app || !app->IsActiveSession(sessionId_)) return S_OK;
    // Un cambio de formato a media captura puede cambiar tamaño/stride sin
    // avisarle al renderer. Lo tratamos como perdida de señal para que Phoenix
    // reconstruya todo de forma segura.
    if (FAILED(status)) {
        app->ReaderError(sessionId_, status);
        return S_OK;
    }
    if (streamFlags & MF_SOURCE_READERF_ERROR) {
        app->ReaderError(sessionId_, E_FAIL);
        return S_OK;
    }
    if (streamFlags & (MF_SOURCE_READERF_ENDOFSTREAM |
                       MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED |
                       MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)) {
        app->ReaderError(sessionId_, MF_E_TRANSFORM_STREAM_CHANGE);
        return S_OK;
    }
    if (sample) app->OnVideoSample(sessionId_, sample, timestamp);
    const HRESULT readHr = app->ContinueReading(sessionId_);
    if (FAILED(readHr) && readHr != MF_E_NOTACCEPTING && readHr != MF_E_SHUTDOWN) {
        app->ReaderError(sessionId_, readHr);
    }
    return S_OK;
}

STDMETHODIMP SourceReaderCallback::OnFlush(DWORD) {
    SharedSrwGuard guard(appLock_);
    if (app_) app_->ReaderFlushed(sessionId_);
    return S_OK;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // Los argumentos especiales los usa el reinicio seguro: el proceso nuevo
    // espera al anterior antes de abrir Configuracion. Asi nunca quedan dos
    // procesos peleandose por la misma capturadora.
    bool forceSettings = false;
    DWORD waitForProcess = 0;
    HRESULT phoenixFailure = S_OK;
    bool phoenixFailureProvided = false;
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments) {
        for (int i = 1; i < argumentCount; ++i) {
            const std::wstring argument = arguments[i];
            if (argument == L"--settings") forceSettings = true;
            else if (argument.rfind(L"--wait-for=", 0) == 0) {
                waitForProcess = static_cast<DWORD>(wcstoul(argument.c_str() + 11, nullptr, 10));
            } else if (argument.rfind(L"--phoenix-failed=", 0) == 0) {
                phoenixFailure = static_cast<HRESULT>(wcstoul(argument.c_str() + 17, nullptr, 0));
                phoenixFailureProvided = true;
            }
        }
        LocalFree(arguments);
    }
    if (waitForProcess) {
        HANDLE previous = OpenProcess(SYNCHRONIZE, FALSE, waitForProcess);
        if (previous) {
            WaitForSingleObject(previous, 5000);
            CloseHandle(previous);
        }
    }
    std::wstring logPath = JoinPath(ModuleDirectory(), L"DayaahCapture.log");
    if (!waitForProcess) {
        // Arranque normal: el log anterior sigue disponible como .log.1. Si la
        // rotacion falla, preferimos conservarlo y añadir al final antes que
        // borrar justo la evidencia que alguien iba a mandar en un Issue.
        const std::wstring previousLog = JoinPath(ModuleDirectory(), L"DayaahCapture.log.1");
        if (GetFileAttributesW(logPath.c_str()) != INVALID_FILE_ATTRIBUTES &&
            !MoveFileExW(logPath.c_str(), previousLog.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            LogLine(L"No se pudo rotar el log anterior (error %lu)", GetLastError());
        }
    } else {
        LogLine(L"---------------- reinicio interno ----------------");
        LogLine(L"Esperando al proceso anterior: PID %lu", waitForProcess);
    }
    LogLine(L"Dayaah Capture 2.2.0 iniciando");
    if (phoenixFailureProvided) {
        LogLine(L"Motivo del reinicio Phoenix: --phoenix-failed=0x%08lX",
                static_cast<unsigned long>(phoenixFailure));
    }

    DayaahApp app;
    HRESULT hr = app.Initialize(instance, forceSettings, phoenixFailure);
    if (FAILED(hr)) {
        std::wstring text = L"Dayaah Capture no pudo iniciar:\n\n" + HrText(hr) +
            L"\n\nAbre un Issue en GitHub y adjunta DayaahCapture.log:\n"
            L"https://github.com/dayitaah/Dayaah-Capture/issues";
        LogLine(L"Inicializacion fallida: %s", HrText(hr).c_str());
        MessageBoxW(nullptr, text.c_str(), L"Dayaah Capture", MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
