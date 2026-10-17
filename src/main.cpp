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
#include "window_behavior.h"
#include "video_effects_editor.h"
#include "video_effects_renderer.h"
#include "video_effects_settings.h"

constexpr UINT WM_DAYAAH_EFFECTS_ERROR = WM_APP + 19;
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

inline constexpr const wchar_t* kAppTitle = L"Dayaah Capture 2.0.1";
inline constexpr const wchar_t* kGithubUrl = L"https://github.com/dayitaah/Dayaah-Capture";
inline constexpr const wchar_t* kKofiUrl = L"https://ko-fi.com/dayaah";

static const PROPERTYKEY kPkeyDeviceFriendlyName = {
    {0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14
};

static const GUID kWaveSubtypeFloat = {
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}
};

template <typename T> static void SafeRelease(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

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
    std::vector<char> utf8(static_cast<size_t>(bytesNeeded));
    WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8.data(), bytesNeeded, nullptr, nullptr);

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
                   bool latencyHud, double* presentDurationMs) {
        if (!data || !inputTexture_ || !processor_ || !outputView_) return E_POINTER;
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
            // Retry the same frame on the direct path; keep audio/capture alive.
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
        LARGE_INTEGER frequency{};
        LARGE_INTEGER started{};
        LARGE_INTEGER finished{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&started);
        const HRESULT presentHr = swapChain_->Present(lowLatency ? 0 : 1,
            lowLatency && allowTearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
        QueryPerformanceCounter(&finished);
        if (presentDurationMs && frequency.QuadPart > 0) {
            *presentDurationMs = static_cast<double>(finished.QuadPart - started.QuadPart) *
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

class AudioBridge {
public:
    AudioBridge() { InitializeCriticalSection(&stateLock_); }
    ~AudioBridge() {
        Stop();
        DeleteCriticalSection(&stateLock_);
    }

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

    CRITICAL_SECTION stateLock_{};
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

class SourceReaderCallback final : public IMFSourceReaderCallback {
public:
    explicit SourceReaderCallback(DayaahApp* app) : app_(app) {}
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

private:
    ~SourceReaderCallback() = default;
    LONG refs_ = 1;
    DayaahApp* app_ = nullptr;
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
    IDM_SETTINGS,
    IDM_GAIN = 2500,
    IDM_RESOLUTION = 2600,
    IDM_FPS = 2700,
    IDM_FORMAT = 2800
};

constexpr UINT WM_DAYAAH_FRAME = WM_APP + 17;
constexpr UINT WM_DAYAAH_CAPTURE_ERROR = WM_APP + 18;

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

    HRESULT Initialize(HINSTANCE instance, bool forceSettings) {
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

        EnumerateDevices();
        PopulateControls();
        RestoreOverlaySettings();
        effectsSettings_ = video_effects_settings::Load(IniPath());
        effectsPacked_.store(effectsSettings_.Pack());
        latencyHudEnabled_.store(GetPrivateProfileIntW(
            L"capture", L"latency_hud", 0, IniPath().c_str()) == 1);
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

    void OnVideoSample(IMFSample* sample) {
        if (!running_.load() || !sample) return;
        IMFMediaBuffer* buffer = nullptr;
        IMF2DBuffer* buffer2d = nullptr;
        BYTE* flat = nullptr;
        DWORD maximum = 0;
        DWORD current = 0;

        HRESULT hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) return;
        hr = buffer->QueryInterface(IID_PPV_ARGS(&buffer2d));

        const size_t tightSize = TightFrameSize(activeMode_.subtype,
                                                activeMode_.width, activeMode_.height);
        std::vector<BYTE> native;
        if (SUCCEEDED(hr)) {
            DWORD contiguous = 0;
            hr = buffer2d->GetContiguousLength(&contiguous);
            if (SUCCEEDED(hr) && contiguous >= tightSize) {
                native.resize(contiguous);
                hr = buffer2d->ContiguousCopyTo(native.data(), contiguous);
            } else if (SUCCEEDED(hr)) {
                hr = MF_E_BUFFERTOOSMALL;
            }
        }
        if (FAILED(hr)) {
            native.clear();
            hr = buffer->Lock(&flat, &maximum, &current);
            if (SUCCEEDED(hr)) {
                if (current >= tightSize) {
                    native.assign(flat, flat + tightSize);
                } else {
                    hr = MF_E_BUFFERTOOSMALL;
                }
                buffer->Unlock();
            }
        }
        SafeRelease(buffer2d);
        SafeRelease(buffer);
        if (FAILED(hr)) return;

        std::vector<BYTE> incoming;
        LONG incomingStride = 0;
        if (activeMode_.subtype == MFVideoFormat_I420) {
            const size_t ySize = static_cast<size_t>(activeMode_.width) * activeMode_.height;
            const size_t chromaSize = ySize / 4;
            if (native.size() < ySize + chromaSize * 2) return;
            incoming.resize(ySize + chromaSize * 2);
            memcpy(incoming.data(), native.data(), ySize);
            const BYTE* u = native.data() + ySize;
            const BYTE* v = u + chromaSize;
            BYTE* uv = incoming.data() + ySize;
            for (size_t i = 0; i < chromaSize; ++i) {
                uv[i * 2] = u[i];
                uv[i * 2 + 1] = v[i];
            }
            incomingStride = static_cast<LONG>(activeMode_.width);
        } else {
            incoming.assign(native.begin(), native.begin() + tightSize);
            incomingStride = static_cast<LONG>(TightRowPitch(activeMode_.subtype,
                                                              activeMode_.width));
        }

        const ULONGLONG arrival = GetTickCount64();
        EnterCriticalSection(&frameLock_);
        if (framePending_) discardedFrames_++;
        latestFrame_.swap(incoming);
        latestFrameArrivalTick_ = arrival;
        latestFrameStride_ = incomingStride;
        framePending_ = true;
        LeaveCriticalSection(&frameLock_);
        receivedFrames_++;
        lastFrameArrivalTick_.store(arrival);
        if (frameReadyEvent_) SetEvent(frameReadyEvent_);
    }

    void ContinueReading() {
        if (running_.load() && reader_) {
            reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                nullptr, nullptr, nullptr, nullptr);
        }
    }

    void ReaderFlushed() {
        if (flushEvent_) SetEvent(flushEvent_);
    }

    void ReaderError(HRESULT hr) {
        captureError_.store(hr);
        PostMessageW(window_, WM_DAYAAH_CAPTURE_ERROR, 0, 0);
    }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
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
            if (effectsSettings_.enabled) {
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
            if (fullscreen_) ToggleFullscreen();
            StopCapture();
            ShowSetup(true);
            std::wstring text = std::wstring(Tr(language_,
                L"La captura se detuvo:\n\n", L"Capture stopped:\n\n")) + HrText(hr) +
                Tr(language_,
                    L"\n\nAbre un Issue en GitHub y adjunta DayaahCapture.log:\n",
                    L"\n\nOpen a GitHub Issue and attach DayaahCapture.log:\n") +
                L"https://github.com/dayitaah/Dayaah-Capture/issues";
            MessageBoxW(window_, text.c_str(), L"Dayaah Capture", MB_ICONERROR);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMinTrackSize.x = capturingUi_ ? 320 : 900;
            limits->ptMinTrackSize.y = capturingUi_ ? 240 : 790;
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
            if (wParam == 1) UpdateFpsTitle();
            else if (wParam == 2) UpdateCursorVisibility();
            else if (wParam == 3) FlushOverlaySettings();
            else if (wParam == 4) FlushEffectsSettings();
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
        case WM_ACTIVATEAPP:
            SetCaptureCursorHidden(false);
            lastMouseMove_ = GetTickCount64();
            hasLastCursorPosition_ = false;
            return 0;
        case WM_ENTERSIZEMOVE:
            SetCaptureCursorHidden(false);
            return 0;
        case WM_EXITSIZEMOVE:
            lastMouseMove_ = GetTickCount64();
            hasLastCursorPosition_ = false;
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_F6 && !(lParam & (1LL << 30)) && running_.load()) ToggleEffects();
            else if (wParam == VK_F7 && !(lParam & (1LL << 30)) && running_.load()) OpenEffectsEditor();
            else if (wParam == VK_F8 && !(lParam & (1LL << 30)) && running_.load()) ToggleLatencyHud();
            else if (wParam == VK_F11 && !(lParam & (1LL << 30))) ToggleFullscreen();
            else if (wParam == VK_F10 && !(lParam & (1LL << 30)) && running_.load()) {
                ToggleOverlays();
            }
            else if (wParam == VK_ESCAPE && !(lParam & (1LL << 30))) {
                const EscapeWindowAction action = DecideEscapeWindowAction(
                    running_.load(), fullscreen_, IsZoomed(window_) != FALSE);
                if (action == EscapeWindowAction::LeaveFullscreen) ToggleFullscreen();
                else if (action == EscapeWindowAction::RestoreMaximized)
                    ShowWindow(window_, SW_RESTORE);
            } else if (wParam == 'M' && running_.load()) {
                audio_.ToggleMute();
                UpdateFpsTitle();
            }
            return 0;
        case WM_CHAR:
            if (running_.load() && !menuOpen_ && discoSecret_.Input(
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
                           kofiLink_, githubLink_};
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
            L"Latencia mínima (puede haber tearing)", L"Minimum latency (tearing possible)"));
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
                      L"Captura nativa. Un cuadro de cola. Cero adornos.",
                      L"Native capture. One-frame queue. No bloat."), -1,
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
            if ((!savedVideo.empty() && videoDevices_[i].name == savedVideo) ||
                (savedVideo.empty() && videoDevices_[i].name.find(L"UGREEN 25173") != std::wstring::npos)) {
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
            if ((!savedAudio.empty() && audioDevices_[i].name == savedAudio) ||
                (savedAudio.empty() && (audioDevices_[i].name.find(L"UGREEN 25173") != std::wstring::npos ||
                 audioDevices_[i].name.find(L"HDMI") != std::wstring::npos))) {
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
        } else {
            wchar_t status[180]{};
            _snwprintf_s(status, _countof(status), _TRUNCATE, language_ == AppLanguage::English
                ? L"Ready: %zu raw modes. Right-click: options · F8: latency · F11: fullscreen · M: mute"
                : L"Listo: %zu modos crudos. Clic derecho: opciones · F8: latencia · F11: pantalla completa · M: silencio",
                videoDevices_[preferredVideo].modes.size());
            SetWindowTextW(statusText_, status);
        }
        UpdateFormatNotice();
    }

    int DefaultModeIndex(int video) const {
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
        auto entry = [](HMENU target, UINT id, const wchar_t* name, bool checked = false) {
            AppendMenuW(target, MF_STRING | (checked ? MF_CHECKED : 0), id, name);
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
        entry(menu, IDM_EFFECTS_TOGGLE, Tr(language_, L"Activar efectos de video\tF6", L"Enable video effects\tF6"), effectsSettings_.enabled);
        entry(menu, IDM_EFFECTS_EDITOR, Tr(language_, L"Ajustar nitidez, color y CRT...\tF7", L"Adjust sharpening, color and CRT...\tF7"));
        entry(menu, IDM_LATENCY_HUD, Tr(language_, L"Latencia aproximada\tF8", L"Estimated latency\tF8"),
              latencyHudEnabled_.load());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        const auto overlay = std::atomic_load(&overlayConfig_);
        entry(menu, IDM_OVERLAY_TOGGLE, Tr(language_, L"Mostrar overlays\tF10", L"Show overlays\tF10"),
              overlay && overlay->enabled);
        entry(menu, IDM_OVERLAY_EDITOR, Tr(language_, L"Editor de overlays...", L"Overlay editor..."));
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        BuildModeMenu(menu);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        entry(menu, IDM_FULLSCREEN, Tr(language_, L"Pantalla completa\tF11", L"Fullscreen\tF11"), fullscreen_);
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
            LogLine(L"Sincronización cambiada en vivo: %s",
                    newLowLatency ? L"VSync OFF / intervalo 0" : L"VSync ON / intervalo 1");
        } else if (id == IDM_EFFECTS_TOGGLE) {
            ToggleEffects();
        } else if (id == IDM_EFFECTS_EDITOR) {
            OpenEffectsEditor();
        } else if (id == IDM_LATENCY_HUD) {
            ToggleLatencyHud();
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

    void ReturnToSettings() {
        FlushEffectsSettings(false);
        FlushOverlaySettings(false);
        wchar_t executable[32768]{};
        if (!GetModuleFileNameW(nullptr, executable, _countof(executable))) return;
        std::wstring command = L"\"" + std::wstring(executable) +
            L"\" --settings --wait-for=" + std::to_wstring(GetCurrentProcessId());
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
        settings.Sanitize();
        effectsSettings_ = settings;
        effectsPacked_.store(settings.Pack(), std::memory_order_relaxed);
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
        if (overlay_profile::Save(path, *config, error)) {
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

    HRESULT StartCapture() {
        if (running_.load()) return S_OK;
        discoSecret_.Reset();
        IMFAttributes* attributes = nullptr;
        IMFMediaType* selectedType = nullptr;
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
        renderer_ = new D3DVideoRenderer();
        HRESULT hr = renderer_->Initialize(window_, activeMode_.width, activeMode_.height,
                                           activeMode_.subtype, lowLatency,
                                           activeInputFullRange_.load());
        if (FAILED(hr)) {
            delete renderer_;
            renderer_ = nullptr;
            ShowSetup(true);
            return hr;
        }
        if (activeDiscoMode_.load()) {
            if (renderer_->SupportsDiscoMode()) renderer_->SetDiscoMode(true);
            else activeDiscoMode_.store(false);
        }

        hr = videoDevices_[deviceIndex].activate->ActivateObject(IID_PPV_ARGS(&mediaSource_));
        if (FAILED(hr)) goto failed;

        hr = MFCreateAttributes(&attributes, 4);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
        callback_ = new SourceReaderCallback(this);
        if (SUCCEEDED(hr)) hr = attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback_);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
        if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(mediaSource_, attributes, &reader_);
        SafeRelease(attributes);
        if (FAILED(hr)) goto failed;

        hr = reader_->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                         activeMode_.nativeIndex, &selectedType);
        if (SUCCEEDED(hr)) {
            hr = reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                              nullptr, selectedType);
        }
        SafeRelease(selectedType);
        if (FAILED(hr)) goto failed;

        flushEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        frameReadyEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        resizeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        renderStopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!flushEvent_ || !frameReadyEvent_ || !resizeEvent_ || !renderStopEvent_) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            goto failed;
        }
        receivedFrames_ = 0;
        renderedFrames_ = 0;
        discardedFrames_ = 0;
        receivedFps_ = 0;
        presentedFps_ = 0;
        lastReceivedFpsFrames_ = 0;
        lastPresentedFpsFrames_ = 0;
        lastFrameArrivalTick_.store(GetTickCount64());
        lastPresentDurationMs_.store(0.0);
        EnterCriticalSection(&frameLock_);
        framePending_ = false;
        latestFrameArrivalTick_ = lastFrameArrivalTick_.load();
        renderFrameArrivalTick_ = latestFrameArrivalTick_;
        latestFrameStride_ = 0;
        renderFrameStride_ = 0;
        LeaveCriticalSection(&frameLock_);
        running_.store(true);
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
        hr = reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                 nullptr, nullptr, nullptr, nullptr);
        if (FAILED(hr)) goto failed;
        MarkFirstRunComplete();
        return S_OK;

    failed:
        LogLine(L"Fallo al iniciar: %s", HrText(hr).c_str());
        StopCapture();
        ShowSetup(true);
        return hr;
    }

    void StopCapture() {
        discoSecret_.Reset();
        if (!running_.exchange(false) && !reader_ && !renderer_ && !renderThread_) return;
        audio_.Stop();
        if (reader_) {
            if (flushEvent_) ResetEvent(flushEvent_);
            reader_->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
            if (flushEvent_) WaitForSingleObject(flushEvent_, 500);
        }
        SafeRelease(reader_);
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
        if (activeDeviceIndex_ >= 0 && activeDeviceIndex_ < static_cast<int>(videoDevices_.size())) {
            videoDevices_[activeDeviceIndex_].activate->ShutdownObject();
        } else if (mediaSource_) {
            mediaSource_->Shutdown();
        }
        SafeRelease(mediaSource_);
        activeDeviceIndex_ = -1;
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
        SetWindowTextW(window_, kAppTitle);
        LogLine(L"Captura detenida");
    }

    static DWORD WINAPI RenderThreadEntry(LPVOID context) {
        return static_cast<DayaahApp*>(context)->RenderLoop();
    }

    DWORD RenderLoop() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        HANDLE events[] = {renderStopEvent_, resizeEvent_, frameReadyEvent_};
        for (;;) {
            const DWORD wait = WaitForMultipleObjects(_countof(events), events, FALSE, INFINITE);
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
            if (wait != WAIT_OBJECT_0 + 2) {
                LogLine(L"Hilo de render detenido por WaitForMultipleObjects: %lu", wait);
                break;
            }

            bool haveFrame = false;
            EnterCriticalSection(&frameLock_);
            if (framePending_) {
                latestFrame_.swap(renderFrame_);
                renderFrameArrivalTick_ = latestFrameArrivalTick_;
                renderFrameStride_ = latestFrameStride_;
                framePending_ = false;
                haveFrame = true;
            }
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
            stats.approximateLatencyMs = EstimateViewerLatencyMs(
                stats.sourceFps, stats.lastFrameAgeMs,
                lastPresentDurationMs_.load(), stats.lowLatency);
            stats.english = language_ == AppLanguage::English;
            double presentDurationMs = 0.0;
            const HRESULT hr = renderer_->Render(renderFrame_.data(), renderFrame_.size(), renderFrameStride_,
                                                 std::atomic_load(&overlayConfig_), stats,
                                                 VideoEffects::Unpack(effectsPacked_.load(std::memory_order_relaxed)),
                                                 latencyHudEnabled_.load(), &presentDurationMs);
            if (SUCCEEDED(hr)) {
                lastPresentDurationMs_.store(presentDurationMs);
                renderedFrames_++;
            }
            else if (hr != DXGI_STATUS_OCCLUDED) {
                LogLine(L"Present fallo: %s", HrText(hr).c_str());
            }
        }
        return 0;
    }

    void ShowSetup(bool show) {
        capturingUi_ = !show;
        HWND controls[] = {videoCombo_, resolutionCombo_, fpsCombo_, formatCombo_, formatNotice_,
                           audioCombo_, gainCombo_, syncCombo_, languageCombo_, startButton_,
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
                        ? L"Dayaah Capture 2.0.1  -  %llu fps  -  %ux%u %.2f Hz %s  -  Audio %s%s"
                        : L"Dayaah Capture 2.0.1  -  %llu fps  -  %ux%u %.2f Hz %s  -  Audio %s%s",
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
        if (!running_.load() && !fullscreen_) return;
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
    HWND startButton_ = nullptr;
    HWND statusText_ = nullptr;
    HWND kofiLink_ = nullptr;
    HWND githubLink_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH darkBrush_ = nullptr;
    HBRUSH controlBrush_ = nullptr;
    HCURSOR visibleCursor_ = nullptr;
    HCURSOR hiddenCursor_ = nullptr;
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
    IMFSourceReader* reader_ = nullptr;
    SourceReaderCallback* callback_ = nullptr;
    HANDLE flushEvent_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<HRESULT> captureError_{S_OK};
    CRITICAL_SECTION frameLock_{};
    std::vector<BYTE> latestFrame_;
    std::vector<BYTE> renderFrame_;
    bool framePending_ = false;
    HANDLE frameReadyEvent_ = nullptr;
    HANDLE resizeEvent_ = nullptr;
    HANDLE renderStopEvent_ = nullptr;
    HANDLE renderThread_ = nullptr;
    std::atomic<UINT> pendingWidth_{0};
    std::atomic<UINT> pendingHeight_{0};
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
    std::atomic<double> lastPresentDurationMs_{0.0};
    std::atomic<bool> activeLowLatency_{true};
    std::atomic<bool> activeInputFullRange_{false};
    std::atomic<bool> activeDiscoMode_{false};
    std::atomic<bool> latencyHudEnabled_{false};
    DiscoSecret discoSecret_;
    ULONGLONG lastReceivedFpsFrames_ = 0;
    ULONGLONG lastPresentedFpsFrames_ = 0;
    std::shared_ptr<const OverlayConfig> overlayConfig_;
    OverlayEditor overlayEditor_;
    VideoEffects effectsSettings_;
    std::atomic<std::uint64_t> effectsPacked_{VideoEffects{}.Pack()};
    VideoEffectsEditor effectsEditor_;
    bool effectsDirty_ = false;
    bool effectsSaveErrorShown_ = false;
    bool overlaySettingsDirty_ = false;
    bool overlaySaveErrorShown_ = false;
    ULONGLONG lastMouseMove_ = 0;
    POINT lastCursorPosition_{};
    bool hasLastCursorPosition_ = false;
    bool cursorHidden_ = false;
    bool mouseTracking_ = false;
    bool menuOpen_ = false;
    bool capturingUi_ = false;
    bool fullscreen_ = false;
    LONG_PTR windowStyle_ = 0;
    WINDOWPLACEMENT windowPlacement_{};
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
                                                 LONGLONG, IMFSample* sample) {
    if (FAILED(status)) {
        app_->ReaderError(status);
        return S_OK;
    }
    if (streamFlags & MF_SOURCE_READERF_ERROR) {
        app_->ReaderError(E_FAIL);
        return S_OK;
    }
    if (sample) app_->OnVideoSample(sample);
    app_->ContinueReading();
    return S_OK;
}

STDMETHODIMP SourceReaderCallback::OnFlush(DWORD) {
    app_->ReaderFlushed();
    return S_OK;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    bool forceSettings = false;
    DWORD waitForProcess = 0;
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments) {
        for (int i = 1; i < argumentCount; ++i) {
            const std::wstring argument = arguments[i];
            if (argument == L"--settings") forceSettings = true;
            else if (argument.rfind(L"--wait-for=", 0) == 0) {
                waitForProcess = static_cast<DWORD>(wcstoul(argument.c_str() + 11, nullptr, 10));
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
    DeleteFileW(logPath.c_str());
    LogLine(L"Dayaah Capture 2.0.1 iniciando");

    DayaahApp app;
    HRESULT hr = app.Initialize(instance, forceSettings);
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
