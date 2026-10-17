#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

inline std::uint32_t EstimateViewerLatencyMs(double sourceFps, std::uint64_t frameAgeMs,
                                              double lastPresentMs, bool lowLatency) {
    const double frameMs = sourceFps > 1.0 ? 1000.0 / sourceFps : 16.6667;
    const double sourceAndTransport = frameMs * 1.5;
    const double displayScanout = frameMs * 0.5;
    const double presentation = lowLatency ? std::max(0.0, lastPresentMs)
                                           : std::max(frameMs, lastPresentMs);
    const double total = sourceAndTransport + displayScanout +
                         static_cast<double>(frameAgeMs) + presentation;
    return static_cast<std::uint32_t>(std::clamp(std::lround(total), 0L, 999L));
}

