#pragma once
#include <algorithm>
#include <cstdint>

struct VideoEffects {
    bool enabled = false;
    bool sharpenEnabled = true;
    bool colorEnabled = true;
    bool crtEnabled = false;
    int sharpness = 0;
    int contrast = 100;
    int saturation = 100;
    int gamma = 100;
    int temperature = 0;
    int scanlines = 35;
    int mask = 15;
    int curvature = 0;

    void Sanitize() {
        sharpness = std::clamp(sharpness, 0, 100);
        contrast = std::clamp(contrast, 50, 150);
        saturation = std::clamp(saturation, 0, 200);
        gamma = std::clamp(gamma, 50, 200);
        temperature = std::clamp(temperature, -100, 100);
        scanlines = std::clamp(scanlines, 0, 100);
        mask = std::clamp(mask, 0, 100);
        curvature = std::clamp(curvature, 0, 100);
    }
    bool HasWork() const {
        return enabled && ((sharpenEnabled && sharpness != 0) ||
            (colorEnabled && (contrast != 100 || saturation != 100 || gamma != 100 || temperature != 0)) ||
            (crtEnabled && (scanlines != 0 || mask != 0 || curvature != 0)));
    }
    // One atomic publication: no partial configurations, allocations or locks.
    std::uint64_t Pack() const {
        VideoEffects s = *this;
        s.Sanitize();
        return std::uint64_t(s.enabled) | (std::uint64_t(s.sharpenEnabled) << 1) |
            (std::uint64_t(s.colorEnabled) << 2) | (std::uint64_t(s.crtEnabled) << 3) |
            (std::uint64_t(s.sharpness) << 4) | (std::uint64_t(s.contrast - 50) << 11) |
            (std::uint64_t(s.saturation) << 18) | (std::uint64_t(s.gamma - 50) << 26) |
            (std::uint64_t(s.temperature + 100) << 34) | (std::uint64_t(s.scanlines) << 42) |
            (std::uint64_t(s.mask) << 49) | (std::uint64_t(s.curvature) << 56);
    }
    static VideoEffects Unpack(std::uint64_t v) {
        VideoEffects s;
        s.enabled = (v & 1) != 0;
        s.sharpenEnabled = (v & 2) != 0;
        s.colorEnabled = (v & 4) != 0;
        s.crtEnabled = (v & 8) != 0;
        s.sharpness = int((v >> 4) & 127);
        s.contrast = int((v >> 11) & 127) + 50;
        s.saturation = int((v >> 18) & 255);
        s.gamma = int((v >> 26) & 255) + 50;
        s.temperature = int((v >> 34) & 255) - 100;
        s.scanlines = int((v >> 42) & 127);
        s.mask = int((v >> 49) & 127);
        s.curvature = int((v >> 56) & 127);
        s.Sanitize();
        return s;
    }
};
