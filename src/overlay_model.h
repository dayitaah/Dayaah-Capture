#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class OverlayItemType { Text, Clock, Image, Stats };
enum class OverlayAnchor { TopLeft, TopRight, BottomLeft, BottomRight, Center };
enum class OverlayAlignment { Left, Center, Right };

struct OverlayImagePixels {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    std::vector<std::uint8_t> bgraPremultiplied;
};

struct OverlayItem {
    std::uint64_t id = 0;
    OverlayItemType type = OverlayItemType::Text;
    bool visible = true;
    std::wstring name = L"Texto";
    std::wstring text = L"Dayaah Capture";
    std::wstring imagePath;
    std::shared_ptr<const OverlayImagePixels> imagePixels;
    std::wstring fontFamily = L"Segoe UI";
    float fontSize = 36.0f;
    float opacity = 1.0f;
    std::uint32_t argb = 0xFFFFFFFFu;
    OverlayAnchor anchor = OverlayAnchor::TopLeft;
    OverlayAlignment alignment = OverlayAlignment::Left;
    float x = 0.03f;
    float y = 0.03f;
    float width = 0.35f;
    float height = 0.10f;
    bool clock24h = true;
};

struct OverlayConfig {
    bool enabled = true;
    std::uint64_t revision = 1;
    std::vector<OverlayItem> items;
};

struct OverlayStats {
    std::uint64_t receivedFrames = 0;
    std::uint64_t presentedFrames = 0;
    std::uint64_t discardedFrames = 0;
    std::uint64_t receivedFps = 0;
    std::uint64_t presentedFps = 0;
    std::uint64_t lastFrameAgeMs = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    double sourceFps = 0.0;
    std::wstring format;
    bool lowLatency = true;
};

struct OverlayRectF {
    float left = 0;
    float top = 0;
    float right = 0;
    float bottom = 0;
};

inline OverlayRectF ResolveOverlayRect(const OverlayItem& item) {
    OverlayRectF result{};
    if (item.anchor == OverlayAnchor::TopLeft) {
        result.left = item.x;
        result.top = item.y;
    } else if (item.anchor == OverlayAnchor::TopRight) {
        result.left = 1.0f - item.x - item.width;
        result.top = item.y;
    } else if (item.anchor == OverlayAnchor::BottomLeft) {
        result.left = item.x;
        result.top = 1.0f - item.y - item.height;
    } else if (item.anchor == OverlayAnchor::BottomRight) {
        result.left = 1.0f - item.x - item.width;
        result.top = 1.0f - item.y - item.height;
    } else {
        result.left = 0.5f + item.x - item.width * 0.5f;
        result.top = 0.5f + item.y - item.height * 0.5f;
    }
    result.right = result.left + item.width;
    result.bottom = result.top + item.height;
    return result;
}

inline void SetOverlayTopLeft(OverlayItem& item, float left, float top) {
    if (item.anchor == OverlayAnchor::TopLeft) {
        item.x = left;
        item.y = top;
    } else if (item.anchor == OverlayAnchor::TopRight) {
        item.x = 1.0f - left - item.width;
        item.y = top;
    } else if (item.anchor == OverlayAnchor::BottomLeft) {
        item.x = left;
        item.y = 1.0f - top - item.height;
    } else if (item.anchor == OverlayAnchor::BottomRight) {
        item.x = 1.0f - left - item.width;
        item.y = 1.0f - top - item.height;
    } else {
        item.x = left + item.width * 0.5f - 0.5f;
        item.y = top + item.height * 0.5f - 0.5f;
    }
}

inline void ClampOverlayItem(OverlayItem& item) {
    item.width = std::clamp(item.width, 0.03f, 1.0f);
    item.height = std::clamp(item.height, 0.03f, 1.0f);
    item.opacity = std::clamp(item.opacity, 0.0f, 1.0f);
    item.fontSize = std::clamp(item.fontSize, 8.0f, 240.0f);

    OverlayRectF rect = ResolveOverlayRect(item);
    float left = std::clamp(rect.left, 0.0f, 1.0f - item.width);
    float top = std::clamp(rect.top, 0.0f, 1.0f - item.height);
    SetOverlayTopLeft(item, left, top);
}

inline OverlayItem MakeDefaultOverlayItem(OverlayItemType type, std::uint64_t id) {
    OverlayItem item{};
    item.id = id;
    item.type = type;
    if (type == OverlayItemType::Text) {
        item.name = L"Texto";
        item.text = L"Dayaah Capture";
    } else if (type == OverlayItemType::Clock) {
        item.name = L"Reloj";
        item.width = 0.12f;
        item.height = 0.08f;
        item.fontSize = 42.0f;
    } else if (type == OverlayItemType::Image) {
        item.name = L"Imagen PNG";
        item.width = 0.20f;
        item.height = 0.20f;
    } else {
        item.name = L"Estadísticas";
        item.width = 0.34f;
        item.height = 0.25f;
        item.fontSize = 25.0f;
    }
    return item;
}
