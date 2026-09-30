#pragma once

#include <cstdint>

inline int ComputeDiscoHueLevel(int minimum, int maximum,
                                std::uint64_t elapsedMs,
                                std::uint64_t cycleMs = 2400) {
    if (maximum <= minimum || cycleMs == 0) return minimum;
    const std::uint64_t phase = elapsedMs % cycleMs;
    const std::uint64_t span = static_cast<std::uint64_t>(maximum - minimum);
    return minimum + static_cast<int>((span * phase + cycleMs / 2) / cycleMs);
}

inline int ComputeDiscoSaturationLevel(int defaultLevel, int maximum) {
    if (maximum <= defaultLevel) return defaultLevel;
    return defaultLevel + (maximum - defaultLevel) / 3;
}
