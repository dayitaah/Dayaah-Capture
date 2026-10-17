#pragma once

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>

#include "overlay_model.h"

class OverlayRenderer {
public:
    ~OverlayRenderer() { Shutdown(); }

    HRESULT Initialize() {
        Shutdown();
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, &d2dFactory_);
        if (SUCCEEDED(hr)) {
            hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(&writeFactory_));
        }
        return hr;
    }

    HRESULT CreateTarget(IDXGISwapChain1* swapChain) {
        ReleaseTarget();
        if (!swapChain || !d2dFactory_) return E_POINTER;
        IDXGISurface* surface = nullptr;
        HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&surface));
        if (SUCCEEDED(hr)) {
            D2D1_RENDER_TARGET_PROPERTIES properties{};
            properties.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
            properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
            properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
            properties.dpiX = 96.0f;
            properties.dpiY = 96.0f;
            properties.usage = D2D1_RENDER_TARGET_USAGE_NONE;
            properties.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
            hr = d2dFactory_->CreateDxgiSurfaceRenderTarget(surface, &properties, &target_);
        }
        Release(surface);
        if (SUCCEEDED(hr)) {
            hr = target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &brush_);
        }
        return hr;
    }

    void BeforeResize() { ReleaseTarget(); }

    HRESULT Draw(const std::shared_ptr<const OverlayConfig>& config,
                 const OverlayStats& stats, const RECT& viewport, bool latencyHud) {
        const bool regularOverlays = config && config->enabled && !config->items.empty();
        if (!target_ || (!regularOverlays && !latencyHud)) return S_OK;
        const float viewportWidth = static_cast<float>(viewport.right - viewport.left);
        const float viewportHeight = static_cast<float>(viewport.bottom - viewport.top);
        if (viewportWidth < 1.0f || viewportHeight < 1.0f) return S_OK;

        target_->BeginDraw();
        target_->PushAxisAlignedClip(
            D2D1::RectF(static_cast<float>(viewport.left), static_cast<float>(viewport.top),
                        static_cast<float>(viewport.right), static_cast<float>(viewport.bottom)),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        if (regularOverlays) for (const OverlayItem& item : config->items) {
            if (!item.visible) continue;
            const OverlayRectF normalized = ResolveOverlayRect(item);
            const D2D1_RECT_F destination = D2D1::RectF(
                viewport.left + normalized.left * viewportWidth,
                viewport.top + normalized.top * viewportHeight,
                viewport.left + normalized.right * viewportWidth,
                viewport.top + normalized.bottom * viewportHeight);
            if (item.type == OverlayItemType::Image) {
                ID2D1Bitmap* bitmap = GetBitmap(item);
                if (bitmap) {
                    target_->DrawBitmap(bitmap, destination, item.opacity,
                                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
                }
                continue;
            }
            if (item.type == OverlayItemType::Crosshair) {
                DrawCrosshair(item, viewport, viewportHeight);
                continue;
            }

            std::wstring text;
            if (item.type == OverlayItemType::Text) text = item.text;
            else if (item.type == OverlayItemType::Clock) text = ClockText(item.clock24h);
            else text = StatsText(stats);
            if (text.empty()) continue;

            const float scale = viewportHeight / 1080.0f;
            IDWriteTextFormat* format = GetTextFormat(item, std::max(7.0f, item.fontSize * scale));
            if (!format) continue;
            const float alpha = ((item.argb >> 24) & 0xff) / 255.0f * item.opacity;
            brush_->SetColor(D2D1::ColorF(
                ((item.argb >> 16) & 0xff) / 255.0f,
                ((item.argb >> 8) & 0xff) / 255.0f,
                (item.argb & 0xff) / 255.0f, alpha));
            target_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format,
                               destination, brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP,
                               DWRITE_MEASURING_MODE_NATURAL);
        }

        if (latencyHud) DrawLatencyHud(stats, viewport, viewportHeight);

        target_->PopAxisAlignedClip();
        const HRESULT hr = target_->EndDraw();
        const HRESULT recreateTarget = static_cast<HRESULT>(D2DERR_RECREATE_TARGET);
        if (hr == recreateTarget) ReleaseTarget();
        return hr == recreateTarget ? S_OK : hr;
    }

    void Shutdown() {
        ReleaseTarget();
        for (auto& value : textFormats_) Release(value.second);
        textFormats_.clear();
        Release(writeFactory_);
        Release(d2dFactory_);
    }

private:
    template <typename T> static void Release(T*& value) {
        if (value) {
            value->Release();
            value = nullptr;
        }
    }

    void ReleaseTarget() {
        for (auto& value : bitmaps_) Release(value.second);
        bitmaps_.clear();
        Release(brush_);
        Release(target_);
    }

    IDWriteTextFormat* GetTextFormat(const OverlayItem& item, float pixels) {
        const unsigned rounded = static_cast<unsigned>(std::lround(pixels * 10.0f));
        const std::wstring key = item.fontFamily + L"|" + std::to_wstring(rounded) + L"|" +
                                 std::to_wstring(static_cast<int>(item.alignment));
        auto found = textFormats_.find(key);
        if (found != textFormats_.end()) return found->second;
        IDWriteTextFormat* result = nullptr;
        if (FAILED(writeFactory_->CreateTextFormat(
                item.fontFamily.empty() ? L"Segoe UI" : item.fontFamily.c_str(), nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, pixels, L"es-MX", &result))) {
            return nullptr;
        }
        DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING;
        if (item.alignment == OverlayAlignment::Center) alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
        if (item.alignment == OverlayAlignment::Right) alignment = DWRITE_TEXT_ALIGNMENT_TRAILING;
        result->SetTextAlignment(alignment);
        result->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        result->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        textFormats_[key] = result;
        return result;
    }

    ID2D1Bitmap* GetBitmap(const OverlayItem& item) {
        if (item.imagePath.empty() || !item.imagePixels || !target_) return nullptr;
        const OverlayImagePixels* imageKey = item.imagePixels.get();
        auto found = bitmaps_.find(imageKey);
        if (found != bitmaps_.end()) return found->second;
        const OverlayImagePixels& pixels = *item.imagePixels;
        if (!pixels.width || !pixels.height || !pixels.stride || pixels.bgraPremultiplied.empty()) {
            return nullptr;
        }
        ID2D1Bitmap* bitmap = nullptr;
        D2D1_BITMAP_PROPERTIES properties{};
        properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        properties.dpiX = 96.0f;
        properties.dpiY = 96.0f;
        target_->CreateBitmap(D2D1::SizeU(pixels.width, pixels.height),
                              pixels.bgraPremultiplied.data(), pixels.stride,
                              &properties, &bitmap);
        bitmaps_[imageKey] = bitmap;
        return bitmap;
    }

    static std::wstring ClockText(bool clock24h) {
        SYSTEMTIME value{};
        GetLocalTime(&value);
        wchar_t output[32]{};
        if (clock24h) {
            _snwprintf_s(output, _countof(output), _TRUNCATE, L"%02u:%02u:%02u",
                         value.wHour, value.wMinute, value.wSecond);
        } else {
            const unsigned hour = value.wHour % 12 ? value.wHour % 12 : 12;
            _snwprintf_s(output, _countof(output), _TRUNCATE, L"%u:%02u:%02u %s",
                         hour, value.wMinute, value.wSecond, value.wHour < 12 ? L"AM" : L"PM");
        }
        return output;
    }

    static std::wstring StatsText(const OverlayStats& stats) {
        wchar_t output[512]{};
        if (stats.english) {
            _snwprintf_s(output, _countof(output), _TRUNCATE,
                         L"Input: %llu fps  |  Presented: %llu fps\n"
                         L"Frames: %llu received  |  %llu dropped\n"
                         L"%ux%u  %.2f Hz  %s\n"
                         L"%s  |  Latest frame: %llu ms",
                         stats.receivedFps, stats.presentedFps,
                         stats.receivedFrames, stats.discardedFrames,
                         stats.width, stats.height, stats.sourceFps, stats.format.c_str(),
                         stats.lowLatency ? L"Minimum latency / VSync OFF" : L"VSync ON",
                         stats.lastFrameAgeMs);
        } else {
            _snwprintf_s(output, _countof(output), _TRUNCATE,
                         L"Entrada: %llu fps  |  Presentado: %llu fps\n"
                         L"Frames: %llu recibidos  |  %llu descartados\n"
                         L"%ux%u  %.2f Hz  %s\n"
                         L"%s  |  Último frame: %llu ms",
                         stats.receivedFps, stats.presentedFps,
                         stats.receivedFrames, stats.discardedFrames,
                         stats.width, stats.height, stats.sourceFps, stats.format.c_str(),
                         stats.lowLatency ? L"Latencia mínima / VSync OFF" : L"VSync ON",
                         stats.lastFrameAgeMs);
        }
        return output;
    }

    void DrawCrosshair(const OverlayItem& item, const RECT& viewport, float viewportHeight) {
        const float scale = viewportHeight / 1080.0f;
        const float size = std::max(8.0f, item.fontSize * scale);
        const float gap = size * 0.20f;
        const float arm = size * 0.55f;
        const float thickness = std::max(1.0f, size * 0.07f);
        const float centerX = (viewport.left + viewport.right) * 0.5f;
        const float centerY = (viewport.top + viewport.bottom) * 0.5f;
        const float alpha = ((item.argb >> 24) & 0xff) / 255.0f * item.opacity;
        brush_->SetColor(D2D1::ColorF(
            ((item.argb >> 16) & 0xff) / 255.0f,
            ((item.argb >> 8) & 0xff) / 255.0f,
            (item.argb & 0xff) / 255.0f, alpha));
        target_->DrawLine(D2D1::Point2F(centerX - gap - arm, centerY),
                          D2D1::Point2F(centerX - gap, centerY), brush_, thickness);
        target_->DrawLine(D2D1::Point2F(centerX + gap, centerY),
                          D2D1::Point2F(centerX + gap + arm, centerY), brush_, thickness);
        target_->DrawLine(D2D1::Point2F(centerX, centerY - gap - arm),
                          D2D1::Point2F(centerX, centerY - gap), brush_, thickness);
        target_->DrawLine(D2D1::Point2F(centerX, centerY + gap),
                          D2D1::Point2F(centerX, centerY + gap + arm), brush_, thickness);
    }

    void DrawLatencyHud(const OverlayStats& stats, const RECT& viewport, float viewportHeight) {
        const float scale = std::max(0.65f, viewportHeight / 1080.0f);
        const float left = viewport.left + 14.0f * scale;
        const float top = viewport.top + 14.0f * scale;
        const D2D1_RECT_F box = D2D1::RectF(left, top, left + 350.0f * scale,
                                            top + 48.0f * scale);
        brush_->SetColor(D2D1::ColorF(0.02f, 0.025f, 0.035f, 0.72f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(box, 7.0f * scale, 7.0f * scale), brush_);
        OverlayItem style{};
        style.fontFamily = L"Segoe UI";
        style.fontSize = 22.0f;
        style.alignment = OverlayAlignment::Left;
        IDWriteTextFormat* format = GetTextFormat(style, 22.0f * scale);
        if (!format) return;
        wchar_t text[96]{};
        _snwprintf_s(text, _countof(text), _TRUNCATE,
                     stats.english ? L"Estimated latency: %u ms" : L"Latencia aproximada: %u ms",
                     stats.approximateLatencyMs);
        brush_->SetColor(D2D1::ColorF(0.93f, 0.97f, 1.0f, 1.0f));
        const D2D1_RECT_F textRect = D2D1::RectF(box.left + 12.0f * scale,
                                                box.top + 7.0f * scale,
                                                box.right - 8.0f * scale,
                                                box.bottom - 5.0f * scale);
        target_->DrawTextW(text, static_cast<UINT32>(wcslen(text)), format, textRect, brush_,
                           D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
    }

    ID2D1Factory* d2dFactory_ = nullptr;
    IDWriteFactory* writeFactory_ = nullptr;
    ID2D1RenderTarget* target_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    std::map<std::wstring, IDWriteTextFormat*> textFormats_;
    std::map<const OverlayImagePixels*, ID2D1Bitmap*> bitmaps_;
};
