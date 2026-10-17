#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <cstring>
#include <string>
#include "video_effects_model.h"
#include "video_effects_shader.h"

class VideoEffectsRenderer {
public:
    ~VideoEffectsRenderer() { Shutdown(); }
    bool Ready() const { return inputView_ && target_; }
    ID3D11VideoProcessorOutputView* InputView() const { return inputView_; }
    const std::string& Error() const { return error_; }

    HRESULT Initialize(ID3D11Device* device) {
        Shutdown();
        // Resolve the compiler from Windows, never the current folder.
        wchar_t path[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(path, MAX_PATH);
        if (!length || length + 20 >= MAX_PATH) return E_FAIL;
        std::wstring compilerPath(path);
        compilerPath += L"\\d3dcompiler_47.dll";
        HMODULE compiler = LoadLibraryW(compilerPath.c_str());
        if (!compiler) return HRESULT_FROM_WIN32(GetLastError());
        using CompileFn = HRESULT (WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
            ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
        const FARPROC address = GetProcAddress(compiler, "D3DCompile");
        CompileFn compile = nullptr;
        static_assert(sizeof(compile) == sizeof(address));
        std::memcpy(&compile, &address, sizeof(compile));
        if (!compile) { FreeLibrary(compiler); return E_NOINTERFACE; }
        auto build = [&](const char* entry, const char* profile, ID3DBlob** blob) {
            ID3DBlob* errors = nullptr;
            HRESULT hr = compile(kVideoEffectsShader, sizeof(kVideoEffectsShader) - 1,
                "DayaahOriginalEffects", nullptr, nullptr, entry, profile,
                D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob, &errors);
            if (errors) {
                if (FAILED(hr)) error_.assign(static_cast<const char*>(errors->GetBufferPointer()),
                                               errors->GetBufferSize());
                Release(errors);
            }
            return hr;
        };
        ID3DBlob* vs = nullptr;
        ID3DBlob* ps = nullptr;
        HRESULT hr = build("VSMain", "vs_4_0", &vs);
        if (SUCCEEDED(hr)) hr = build("PSMain", "ps_4_0", &ps);
        if (SUCCEEDED(hr)) hr = device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vs_);
        if (SUCCEEDED(hr)) hr = device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &ps_);
        Release(vs);
        Release(ps);
        FreeLibrary(compiler);
        if (FAILED(hr)) return hr;
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(Parameters);
        buffer.Usage = D3D11_USAGE_DYNAMIC;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = device->CreateBuffer(&buffer, nullptr, &constants_);
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (SUCCEEDED(hr)) hr = device->CreateSamplerState(&sampler, &sampler_);
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        if (SUCCEEDED(hr)) hr = device->CreateRasterizerState(&raster, &raster_);
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = FALSE;
        depth.StencilEnable = FALSE;
        if (SUCCEEDED(hr)) hr = device->CreateDepthStencilState(&depth, &depth_);
        return hr;
    }

    HRESULT CreateTarget(ID3D11Device* device, ID3D11VideoDevice* videoDevice,
                         ID3D11VideoProcessorEnumerator* processor, IDXGISwapChain1* swapChain) {
        BeforeResize();
        if (!vs_ || !ps_ || !constants_ || !sampler_ || !raster_ || !depth_) return E_FAIL;
        ID3D11Texture2D* back = nullptr;
        HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
        if (FAILED(hr)) return hr;
        D3D11_TEXTURE2D_DESC desc{};
        back->GetDesc(&desc);
        width_ = desc.Width;
        height_ = desc.Height;
        hr = device->CreateRenderTargetView(back, nullptr, &target_);
        Release(back);
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        if (SUCCEEDED(hr)) hr = device->CreateTexture2D(&desc, nullptr, &image_);
        if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(image_, nullptr, &source_);
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC out{};
        out.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        if (SUCCEEDED(hr)) hr = videoDevice->CreateVideoProcessorOutputView(image_, processor, &out, &inputView_);
        if (FAILED(hr)) BeforeResize();
        return hr;
    }

    HRESULT Draw(ID3D11DeviceContext* context, const VideoEffects& s, const RECT& r) {
        if (!Ready()) return E_UNEXPECTED;
        Parameters p{};
        p.geometry[0] = 1.0f / width_;
        p.geometry[1] = 1.0f / height_;
        p.geometry[2] = s.sharpenEnabled ? s.sharpness / 100.0f : 0;
        p.geometry[3] = s.crtEnabled ? s.curvature * 0.0015f : 0;
        p.color[0] = s.colorEnabled ? s.contrast / 100.0f : 1;
        p.color[1] = s.colorEnabled ? s.saturation / 100.0f : 1;
        p.color[2] = s.colorEnabled ? 100.0f / s.gamma : 1;
        p.color[3] = s.colorEnabled ? s.temperature / 100.0f : 0;
        p.crt[0] = s.crtEnabled ? s.scanlines / 100.0f : 0;
        p.crt[1] = s.crtEnabled ? s.mask / 100.0f : 0;
        p.crt[2] = std::min(240.0f, (r.bottom - r.top) * 0.5f);
        p.rect[0] = float(r.left) / width_;
        p.rect[1] = float(r.top) / height_;
        p.rect[2] = float(r.right) / width_;
        p.rect[3] = float(r.bottom) / height_;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context->Map(constants_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) return hr;
        std::memcpy(mapped.pData, &p, sizeof(p));
        context->Unmap(constants_, 0);
        // D2D may have changed graphics state during the previous frame.
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs_, nullptr, 0);
        context->PSSetShader(ps_, nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        context->OMSetDepthStencilState(depth_, 0);
        context->RSSetState(raster_);
        D3D11_VIEWPORT viewport{0, 0, float(width_), float(height_), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->OMSetRenderTargets(1, &target_, nullptr);
        context->PSSetConstantBuffers(0, 1, &constants_);
        context->PSSetSamplers(0, 1, &sampler_);
        context->PSSetShaderResources(0, 1, &source_);
        context->Draw(3, 0);
        // Unbind before D2D, the next VideoProcessorBlt or ResizeBuffers.
        ID3D11ShaderResourceView* empty = nullptr;
        context->PSSetShaderResources(0, 1, &empty);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        return S_OK;
    }
    void BeforeResize() {
        Release(inputView_);
        Release(source_);
        Release(image_);
        Release(target_);
    }
    void Shutdown() {
        BeforeResize();
        Release(depth_);
        Release(raster_);
        Release(sampler_);
        Release(constants_);
        Release(ps_);
        Release(vs_);
        error_.clear();
    }
private:
    template<class T> static void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }
    struct Parameters { float geometry[4], color[4], crt[4], rect[4]; };
    static_assert(sizeof(Parameters) == 64);
    UINT width_ = 0, height_ = 0;
    std::string error_;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11Buffer* constants_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11RasterizerState* raster_ = nullptr;
    ID3D11DepthStencilState* depth_ = nullptr;
    ID3D11Texture2D* image_ = nullptr;
    ID3D11ShaderResourceView* source_ = nullptr;
    ID3D11RenderTargetView* target_ = nullptr;
    ID3D11VideoProcessorOutputView* inputView_ = nullptr;
};
