#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// OJO: es una aproximacion honesta, no una medicion electrica del HDMI.
// Sumamos el tiempo probable de captura/transporte, la edad real del frame,
// el Present medido y medio barrido de pantalla. Sirve para comparar cambios
// como VSync ON/OFF sin vender humo de "0 ms".
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

