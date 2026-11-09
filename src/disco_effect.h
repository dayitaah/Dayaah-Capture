#pragma once

#include <cstdint>

// Hace que el tono recorra el rango que soporte la GPU. No hay una tabla de
// colores ni magia negra: es una vuelta completa cada `cycleMs` milisegundos.
inline int ComputeDiscoHueLevel(int minimum, int maximum,
                                std::uint64_t elapsedMs,
                                std::uint64_t cycleMs = 2400) {
    if (maximum <= minimum || cycleMs == 0) return minimum;
    const std::uint64_t phase = elapsedMs % cycleMs;
    const std::uint64_t span = static_cast<std::uint64_t>(maximum - minimum);
    return minimum + static_cast<int>((span * phase + cycleMs / 2) / cycleMs);
}

inline int ComputeDiscoSaturationLevel(int defaultLevel, int maximum) {
    // Le damos un empujoncito al color sin poner la saturacion hasta Narnia.
    if (maximum <= defaultLevel) return defaultLevel;
    return defaultLevel + (maximum - defaultLevel) / 3;
}
