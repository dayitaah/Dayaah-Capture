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
#include <string>
#include <vector>

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
    if (subtype == MFVideoFormat_MJPG) return L"MJPEG";
    if (subtype == MFVideoFormat_RGB32) return L"RGB32";
    return L"Otro";
}

static bool SupportedRawSubtype(const GUID& subtype) {
    return subtype == MFVideoFormat_NV12 || subtype == MFVideoFormat_YUY2;
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
                       const GUID& subtype, bool lowLatency) {
        Shutdown();
        window_ = window;
        inputWidth_ = inputWidth;
        inputHeight_ = inputHeight;
        inputFormat_ = subtype == MFVideoFormat_NV12 ? DXGI_FORMAT_NV12 : DXGI_FORMAT_YUY2;
        lowLatency_ = lowLatency;

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
        if (lowLatency_ && SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory5)))) {
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

        hr = RebuildVideoProcessor(width, height);
        if (SUCCEEDED(hr)) {
            LogLine(L"D3D11 listo: entrada %ux%u %s, tearing=%s",
                    inputWidth_, inputHeight_, GuidFormatName(subtype).c_str(),
                    allowTearing_ ? L"si" : L"no");
        }
        return hr;
    }

    HRESULT Resize(UINT width, UINT height) {
        if (!swapChain_ || width < 16 || height < 16) return S_OK;
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

    HRESULT Render(const BYTE* data, size_t size, LONG sourceStride) {
        if (!data || !inputTexture_ || !processor_ || !outputView_) return E_POINTER;
        const UINT rowPitch = inputFormat_ == DXGI_FORMAT_NV12 ? inputWidth_ : inputWidth_ * 2;
        const size_t expected = inputFormat_ == DXGI_FORMAT_NV12
                                    ? static_cast<size_t>(inputWidth_) * inputHeight_ * 3 / 2
                                    : static_cast<size_t>(inputWidth_) * inputHeight_ * 2;
        if (size < expected || sourceStride != static_cast<LONG>(rowPitch)) return MF_E_BUFFERTOOSMALL;

        context_->UpdateSubresource(inputTexture_, 0, nullptr, data, rowPitch,
                                    static_cast<UINT>(expected));

        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = inputView_;
        HRESULT hr = videoContext_->VideoProcessorBlt(processor_, outputView_, 0, 1, &stream);
        if (FAILED(hr)) return hr;
        return swapChain_->Present(lowLatency_ ? 0 : 1,
                                   allowTearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
    }

    void Shutdown() {
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
        window_ = nullptr;
    }

private:
    HRESULT RebuildVideoProcessor(UINT outputWidth, UINT outputHeight) {
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

        D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputColor{};
        inputColor.YCbCr_Matrix = 1; // BT.709
        inputColor.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputColor{};
        outputColor.RGB_Range = 0;
        outputColor.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
        videoContext_->VideoProcessorSetStreamColorSpace(processor_, 0, &inputColor);
        videoContext_->VideoProcessorSetOutputColorSpace(processor_, &outputColor);

        D3D11_VIDEO_COLOR clear{};
        clear.RGBA.A = 1.0f;
        videoContext_->VideoProcessorSetOutputBackgroundColor(processor_, FALSE, &clear);
        return S_OK;
    }

    HWND window_ = nullptr;
    UINT32 inputWidth_ = 0;
    UINT32 inputHeight_ = 0;
    DXGI_FORMAT inputFormat_ = DXGI_FORMAT_UNKNOWN;
    bool lowLatency_ = true;
    bool allowTearing_ = false;
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
        const size_t maximumFrames = renderFormat_->nSamplesPerSec / 25; // 40 ms ceiling.
        const size_t targetFrames = renderFormat_->nSamplesPerSec / 100; // Return to 10 ms.
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
    IDC_MODE = 1002,
    IDC_AUDIO = 1003,
    IDC_GAIN = 1004,
    IDC_SYNC = 1005,
    IDC_START = 1006,
    IDC_STATUS = 1007
};

constexpr UINT WM_DAYAAH_FRAME = WM_APP + 17;
constexpr UINT WM_DAYAAH_CAPTURE_ERROR = WM_APP + 18;

class DayaahApp {
public:
    DayaahApp() { InitializeCriticalSection(&frameLock_); }
    ~DayaahApp() {
        StopCapture();
        ReleaseDevices();
        DeleteCriticalSection(&frameLock_);
        SafeRelease(audioEnumerator_);
        MFShutdown();
        CoUninitialize();
    }

    HRESULT Initialize(HINSTANCE instance) {
        instance_ = instance;
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) return hr;
        hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(hr)) return hr;
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&audioEnumerator_));
        if (FAILED(hr)) return hr;

        SetProcessDPIAware();
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
        windowClass.hbrBackground = CreateSolidBrush(RGB(10, 13, 18));
        windowClass.lpszClassName = L"DayaahCaptureWindow";
        windowClass.hIconSm = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(101),
                                                            IMAGE_ICON,
                                                            GetSystemMetrics(SM_CXSMICON),
                                                            GetSystemMetrics(SM_CYSMICON),
                                                            LR_DEFAULTCOLOR));
        if (!RegisterClassExW(&windowClass)) return HRESULT_FROM_WIN32(GetLastError());

        window_ = CreateWindowExW(0, windowClass.lpszClassName, L"Dayaah Capture",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  860, 600, nullptr, nullptr, instance_, this);
        if (!window_) return HRESULT_FROM_WIN32(GetLastError());

        BOOL dark = TRUE;
        DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
        CreateControls();
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
        SetTimer(window_, 1, 1000, nullptr);
        SetTimer(window_, 2, 100, nullptr);

        EnumerateDevices();
        PopulateControls();
        return S_OK;
    }

    int Run() {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

    void OnVideoSample(IMFSample* sample) {
        if (!running_.load() || !sample) return;
        IMFMediaBuffer* buffer = nullptr;
        IMF2DBuffer* buffer2d = nullptr;
        BYTE* scanline = nullptr;
        BYTE* flat = nullptr;
        LONG pitch = 0;
        DWORD maximum = 0;
        DWORD current = 0;

        HRESULT hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) return;
        hr = buffer->QueryInterface(IID_PPV_ARGS(&buffer2d));

        const UINT rowBytes = activeMode_.subtype == MFVideoFormat_NV12
                                  ? activeMode_.width
                                  : activeMode_.width * 2;
        const UINT rows = activeMode_.subtype == MFVideoFormat_NV12
                              ? activeMode_.height + activeMode_.height / 2
                              : activeMode_.height;
        const size_t tightSize = static_cast<size_t>(rowBytes) * rows;

        std::vector<BYTE> incoming(tightSize);
        if (SUCCEEDED(hr) && SUCCEEDED(buffer2d->Lock2D(&scanline, &pitch))) {
            if (pitch > 0 && static_cast<UINT>(pitch) >= rowBytes) {
                for (UINT row = 0; row < rows; ++row) {
                    memcpy(incoming.data() + static_cast<size_t>(row) * rowBytes,
                           scanline + static_cast<size_t>(row) * pitch, rowBytes);
                }
                sourceStride_ = static_cast<LONG>(rowBytes);
            } else {
                hr = E_UNEXPECTED;
            }
            buffer2d->Unlock2D();
        } else {
            hr = buffer->Lock(&flat, &maximum, &current);
            if (SUCCEEDED(hr)) {
                if (current >= tightSize) {
                    memcpy(incoming.data(), flat, tightSize);
                    sourceStride_ = static_cast<LONG>(rowBytes);
                } else {
                    hr = MF_E_BUFFERTOOSMALL;
                }
                buffer->Unlock();
            }
        }
        SafeRelease(buffer2d);
        SafeRelease(buffer);
        if (FAILED(hr)) return;

        EnterCriticalSection(&frameLock_);
        latestFrame_.swap(incoming);
        latestSerial_++;
        LeaveCriticalSection(&frameLock_);
        receivedFrames_++;
        if (InterlockedCompareExchange(&frameMessagePending_, 1, 0) == 0) {
            PostMessageW(window_, WM_DAYAAH_FRAME, 0, 0);
        }
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
        case WM_DAYAAH_FRAME:
            RenderLatestFrame();
            return 0;
        case WM_DAYAAH_CAPTURE_ERROR: {
            HRESULT hr = captureError_.load();
            StopCapture();
            ShowSetup(true);
            std::wstring text = L"La captura se detuvo:\n\n" + HrText(hr) +
                                L"\n\nMándame DayaahCapture.log si vuelve a pasar.";
            MessageBoxW(window_, text.c_str(), L"Dayaah Capture", MB_ICONERROR);
            return 0;
        }
        case WM_SIZE:
            if (capturingUi_ && renderer_) {
                renderer_->Resize(LOWORD(lParam), HIWORD(lParam));
            }
            return 0;
        case WM_TIMER:
            if (wParam == 1) UpdateFpsTitle();
            else if (wParam == 2) UpdateCursorVisibility();
            return 0;
        case WM_MOUSEMOVE:
            if (running_.load()) {
                lastMouseMove_ = GetTickCount64();
                if (cursorHidden_) {
                    cursorHidden_ = false;
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                }
            }
            return 0;
        case WM_SETCURSOR:
            if (running_.load() && LOWORD(lParam) == HTCLIENT && cursorHidden_) {
                SetCursor(nullptr);
                return TRUE;
            }
            return DefWindowProcW(window_, message, wParam, lParam);
        case WM_KEYDOWN:
            if (wParam == VK_F11) ToggleFullscreen();
            else if (wParam == VK_ESCAPE) {
                if (fullscreen_) ToggleFullscreen();
                else if (running_.load()) {
                    StopCapture();
                    ShowSetup(true);
                }
            } else if (wParam == 'M' && running_.load()) {
                audio_.ToggleMute();
                UpdateFpsTitle();
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
            DestroyWindow(window_);
            return 0;
        case WM_DESTROY:
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

        videoCombo_ = CreateCombo(IDC_VIDEO, 48, 132, 750);
        modeCombo_ = CreateCombo(IDC_MODE, 48, 212, 750);
        audioCombo_ = CreateCombo(IDC_AUDIO, 48, 292, 750);
        gainCombo_ = CreateCombo(IDC_GAIN, 48, 372, 240);

        syncCombo_ = CreateCombo(IDC_SYNC, 326, 372, 472);
        startButton_ = CreateWindowExW(0, L"BUTTON", L"INICIAR CAPTURA",
                                       WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                       48, 432, 750, 58, window_,
                                       reinterpret_cast<HMENU>(IDC_START), instance_, nullptr);
        statusText_ = CreateWindowExW(0, L"STATIC", L"Buscando dispositivos...",
                                      WS_CHILD | WS_VISIBLE, 48, 498, 750, 24,
                                      window_, reinterpret_cast<HMENU>(IDC_STATUS), instance_, nullptr);
        HWND controls[] = {videoCombo_, modeCombo_, audioCombo_, gainCombo_, syncCombo_, statusText_};
        for (HWND control : controls) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
        }
        SendMessageW(startButton_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        const wchar_t* gains[] = {L"0 dB", L"+6 dB", L"+12 dB", L"+15 dB", L"+18 dB"};
        for (const wchar_t* gain : gains) ComboBox_AddString(gainCombo_, gain);
        ComboBox_SetCurSel(gainCombo_, 3);
        ComboBox_AddString(syncCombo_, L"Latencia mínima (puede haber tearing)");
        ComboBox_AddString(syncCombo_, L"VSync (cola máxima: 1 cuadro)");
        ComboBox_SetCurSel(syncCombo_, 0);
    }

    HWND CreateCombo(int id, int x, int y, int width) {
        return CreateWindowExW(0, WC_COMBOBOXW, L"",
                               WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                               x, y, width, 320, window_, reinterpret_cast<HMENU>(id),
                               instance_, nullptr);
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
            RECT subtitle{50, 74, 800, 104};
            DrawTextW(dc, L"Captura nativa. Un cuadro de cola. Cero adornos.", -1,
                      &subtitle, DT_LEFT | DT_SINGLELINE);
            const wchar_t* labels[] = {L"DISPOSITIVO DE VIDEO", L"MODO DE VIDEO",
                                       L"ENTRADA DE AUDIO", L"AMPLIFICACIÓN"};
            int ys[] = {108, 188, 268, 348};
            for (int i = 0; i < 4; ++i) {
                RECT label{48, ys[i], 800, ys[i] + 22};
                DrawTextW(dc, labels[i], -1, &label, DT_LEFT | DT_SINGLELINE);
            }
            RECT syncLabel{326, 348, 800, 370};
            DrawTextW(dc, L"SINCRONIZACIÓN", -1, &syncLabel, DT_LEFT | DT_SINGLELINE);
            SelectObject(dc, old);
            DeleteObject(titleFont);
        }
        EndPaint(window_, &paint);
    }

    void DrawOwnerButton(const DRAWITEMSTRUCT* item) {
        if (!item || item->CtlID != IDC_START) return;
        COLORREF color = (item->itemState & ODS_SELECTED) ? RGB(67, 188, 157) : RGB(83, 215, 178);
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(item->hDC, &item->rcItem, brush);
        DeleteObject(brush);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, RGB(5, 19, 16));
        HFONT old = static_cast<HFONT>(SelectObject(item->hDC, font_));
        RECT rect = item->rcItem;
        DrawTextW(item->hDC, L"INICIAR CAPTURA", -1, &rect,
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
        PopulateModes();

        const std::wstring savedMode = ReadSetting(L"video_mode", L"");
        if (!savedMode.empty() && preferredVideo < static_cast<int>(videoDevices_.size())) {
            for (size_t i = 0; i < videoDevices_[preferredVideo].modes.size(); ++i) {
                if (videoDevices_[preferredVideo].modes[i].Key() == savedMode) {
                    ComboBox_SetCurSel(modeCombo_, static_cast<int>(i));
                    break;
                }
            }
        }

        ComboBox_ResetContent(audioCombo_);
        ComboBox_AddString(audioCombo_, L"Sin audio");
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

        if (videoDevices_.empty()) {
            SetWindowTextW(statusText_, L"No encontré una capturadora con salida NV12 o YUY2.");
            EnableWindow(startButton_, FALSE);
        } else {
            wchar_t status[180]{};
            _snwprintf_s(status, _countof(status), _TRUNCATE,
                         L"Listo: %zu modos crudos disponibles. F11: pantalla completa · Esc: volver · M: silencio",
                         videoDevices_[preferredVideo].modes.size());
            SetWindowTextW(statusText_, status);
        }
    }

    void PopulateModes() {
        ComboBox_ResetContent(modeCombo_);
        int video = ComboBox_GetCurSel(videoCombo_);
        if (video < 0 || video >= static_cast<int>(videoDevices_.size())) return;
        int preferred = 0;
        int bestScore = -1;
        for (size_t i = 0; i < videoDevices_[video].modes.size(); ++i) {
            const VideoMode& mode = videoDevices_[video].modes[i];
            ComboBox_AddString(modeCombo_, mode.Label().c_str());
            int score = 0;
            if (mode.width == 1920 && mode.height == 1080) score += 100;
            if (std::fabs(mode.Fps() - 60.0) < 0.1) score += 50;
            if (mode.subtype == MFVideoFormat_NV12) score += 25;
            if (score > bestScore) {
                bestScore = score;
                preferred = static_cast<int>(i);
            }
        }
        if (!videoDevices_[video].modes.empty()) ComboBox_SetCurSel(modeCombo_, preferred);
    }

    LRESULT OnCommand(int id, int notification) {
        if (id == IDC_VIDEO && notification == CBN_SELCHANGE) {
            PopulateModes();
            return 0;
        }
        if (id == IDC_GAIN && notification == CBN_SELCHANGE && running_.load()) {
            audio_.SetGain(SelectedGain());
            return 0;
        }
        if (id == IDC_START && notification == BN_CLICKED) {
            HRESULT hr = StartCapture();
            if (FAILED(hr)) {
                std::wstring text = L"No pude iniciar la captura:\n\n" + HrText(hr) +
                                    L"\n\nRevisa DayaahCapture.log.";
                MessageBoxW(window_, text.c_str(), L"Dayaah Capture", MB_ICONERROR);
            }
            return 0;
        }
        return 0;
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
        IMFAttributes* attributes = nullptr;
        IMFMediaType* selectedType = nullptr;
        int deviceIndex = ComboBox_GetCurSel(videoCombo_);
        int modeIndex = ComboBox_GetCurSel(modeCombo_);
        if (deviceIndex < 0 || deviceIndex >= static_cast<int>(videoDevices_.size())) return E_INVALIDARG;
        if (modeIndex < 0 || modeIndex >= static_cast<int>(videoDevices_[deviceIndex].modes.size())) return E_INVALIDARG;
        activeMode_ = videoDevices_[deviceIndex].modes[modeIndex];
        activeDeviceIndex_ = deviceIndex;
        const bool lowLatency = LowLatencySelected();
        SaveSettings(deviceIndex, modeIndex);

        ShowSetup(false);
        renderer_ = new D3DVideoRenderer();
        HRESULT hr = renderer_->Initialize(window_, activeMode_.width, activeMode_.height,
                                           activeMode_.subtype, lowLatency);
        if (FAILED(hr)) {
            delete renderer_;
            renderer_ = nullptr;
            ShowSetup(true);
            return hr;
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
        running_.store(true);
        lastMouseMove_ = GetTickCount64();
        cursorHidden_ = false;
        receivedFrames_ = 0;
        renderedFrames_ = 0;
        lastFpsFrames_ = 0;
        frameMessagePending_ = 0;

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
        return S_OK;

    failed:
        LogLine(L"Fallo al iniciar: %s", HrText(hr).c_str());
        StopCapture();
        ShowSetup(true);
        return hr;
    }

    void StopCapture() {
        if (!running_.exchange(false) && !reader_ && !renderer_) return;
        audio_.Stop();
        if (reader_) {
            if (flushEvent_) ResetEvent(flushEvent_);
            reader_->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
            if (flushEvent_) WaitForSingleObject(flushEvent_, 500);
        }
        SafeRelease(reader_);
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
        cursorHidden_ = false;
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        EnterCriticalSection(&frameLock_);
        latestFrame_.clear();
        renderFrame_.clear();
        LeaveCriticalSection(&frameLock_);
        SetWindowTextW(window_, L"Dayaah Capture");
        LogLine(L"Captura detenida");
    }

    void RenderLatestFrame() {
        ULONGLONG serial = 0;
        EnterCriticalSection(&frameLock_);
        latestFrame_.swap(renderFrame_);
        serial = latestSerial_;
        LeaveCriticalSection(&frameLock_);
        if (renderer_ && !renderFrame_.empty()) {
            HRESULT hr = renderer_->Render(renderFrame_.data(), renderFrame_.size(), sourceStride_);
            if (SUCCEEDED(hr)) renderedFrames_++;
            else if (hr != DXGI_STATUS_OCCLUDED) LogLine(L"Present fallo: %s", HrText(hr).c_str());
        }
        renderedSerial_ = serial;
        InterlockedExchange(&frameMessagePending_, 0);

        EnterCriticalSection(&frameLock_);
        bool newer = latestSerial_ != renderedSerial_;
        LeaveCriticalSection(&frameLock_);
        if (newer && InterlockedCompareExchange(&frameMessagePending_, 1, 0) == 0) {
            PostMessageW(window_, WM_DAYAAH_FRAME, 0, 0);
        }
    }

    void ShowSetup(bool show) {
        capturingUi_ = !show;
        HWND controls[] = {videoCombo_, modeCombo_, audioCombo_, gainCombo_, syncCombo_,
                           startButton_, statusText_};
        for (HWND control : controls) ShowWindow(control, show ? SW_SHOW : SW_HIDE);
        if (show) {
            SetWindowTextW(window_, L"Dayaah Capture");
            InvalidateRect(window_, nullptr, TRUE);
        } else {
            SetWindowTextW(window_, L"Dayaah Capture - iniciando...");
        }
    }

    void UpdateFpsTitle() {
        if (!running_.load()) return;
        ULONGLONG now = renderedFrames_.load();
        ULONGLONG fps = now - lastFpsFrames_;
        lastFpsFrames_ = now;
        wchar_t title[240]{};
        _snwprintf_s(title, _countof(title), _TRUNCATE,
                     L"Dayaah Capture  -  %llu fps  -  %ux%u %.2f Hz %s  -  Audio %s",
                     fps, activeMode_.width, activeMode_.height, activeMode_.Fps(),
                     GuidFormatName(activeMode_.subtype).c_str(),
                     audio_.IsMuted() ? L"MUTE" : L"ON");
        SetWindowTextW(window_, title);
    }

    void UpdateCursorVisibility() {
        if (!running_.load()) {
            cursorHidden_ = false;
            return;
        }
        if (!cursorHidden_ && GetTickCount64() - lastMouseMove_ >= 1000) {
            POINT cursor{};
            RECT client{};
            if (GetCursorPos(&cursor)) {
                ScreenToClient(window_, &cursor);
                GetClientRect(window_, &client);
                if (PtInRect(&client, cursor)) {
                    cursorHidden_ = true;
                    SetCursor(nullptr);
                }
            }
        }
    }

    void ToggleFullscreen() {
        if (!running_.load()) return;
        if (!fullscreen_) {
            windowStyle_ = GetWindowLongW(window_, GWL_STYLE);
            GetWindowRect(window_, &windowRect_);
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
            SetWindowLongW(window_, GWL_STYLE, windowStyle_ & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(window_, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                         monitor.rcMonitor.right - monitor.rcMonitor.left,
                         monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
            fullscreen_ = true;
        } else {
            SetWindowLongW(window_, GWL_STYLE, windowStyle_);
            SetWindowPos(window_, nullptr, windowRect_.left, windowRect_.top,
                         windowRect_.right - windowRect_.left,
                         windowRect_.bottom - windowRect_.top,
                         SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER);
            fullscreen_ = false;
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
    }

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND videoCombo_ = nullptr;
    HWND modeCombo_ = nullptr;
    HWND audioCombo_ = nullptr;
    HWND gainCombo_ = nullptr;
    HWND syncCombo_ = nullptr;
    HWND startButton_ = nullptr;
    HWND statusText_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH darkBrush_ = nullptr;
    HBRUSH controlBrush_ = nullptr;
    IMMDeviceEnumerator* audioEnumerator_ = nullptr;
    std::vector<VideoDeviceInfo> videoDevices_;
    std::vector<AudioDeviceInfo> audioDevices_;
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
    ULONGLONG latestSerial_ = 0;
    ULONGLONG renderedSerial_ = 0;
    LONG sourceStride_ = 0;
    LONG frameMessagePending_ = 0;
    std::atomic<ULONGLONG> receivedFrames_{0};
    std::atomic<ULONGLONG> renderedFrames_{0};
    ULONGLONG lastFpsFrames_ = 0;
    ULONGLONG lastMouseMove_ = 0;
    bool cursorHidden_ = false;
    bool capturingUi_ = false;
    bool fullscreen_ = false;
    DWORD windowStyle_ = 0;
    RECT windowRect_{};
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
    std::wstring logPath = JoinPath(ModuleDirectory(), L"DayaahCapture.log");
    DeleteFileW(logPath.c_str());
    LogLine(L"Dayaah Capture 1.0 iniciando");

    DayaahApp app;
    HRESULT hr = app.Initialize(instance);
    if (FAILED(hr)) {
        std::wstring text = L"Dayaah Capture no pudo iniciar:\n\n" + HrText(hr);
        LogLine(L"Inicializacion fallida: %s", HrText(hr).c_str());
        MessageBoxW(nullptr, text.c_str(), L"Dayaah Capture", MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
