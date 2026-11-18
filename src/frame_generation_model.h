#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

// Las opciones visibles de Frame Generation viven fuera del renderer para que
// podamos probarlas en Linux sin fingir que tenemos una RTX conectada aqui.
enum class FrameGenerationMode : std::uint32_t {
    Off = 0,
    Performance = 1,
    Balanced = 2,
    Quality = 3,
    VfgLow = 4,
    VfgMedium = 5,
    VfgHigh = 6,
    AmdFrcPerformance = 7,
    AmdFrcQuality = 8,
    AmdFrcMaximum = 9,
};

inline FrameGenerationMode FrameGenerationModeFromInt(int value) {
    if (value == static_cast<int>(FrameGenerationMode::Performance)) {
        return FrameGenerationMode::Performance;
    }
    if (value == static_cast<int>(FrameGenerationMode::Balanced)) {
        return FrameGenerationMode::Balanced;
    }
    if (value == static_cast<int>(FrameGenerationMode::Quality)) {
        return FrameGenerationMode::Quality;
    }
    if (value == static_cast<int>(FrameGenerationMode::VfgLow)) {
        return FrameGenerationMode::VfgLow;
    }
    if (value == static_cast<int>(FrameGenerationMode::VfgMedium)) {
        return FrameGenerationMode::VfgMedium;
    }
    if (value == static_cast<int>(FrameGenerationMode::VfgHigh)) {
        return FrameGenerationMode::VfgHigh;
    }
    if (value == static_cast<int>(FrameGenerationMode::AmdFrcPerformance)) {
        return FrameGenerationMode::AmdFrcPerformance;
    }
    if (value == static_cast<int>(FrameGenerationMode::AmdFrcQuality)) {
        return FrameGenerationMode::AmdFrcQuality;
    }
    if (value == static_cast<int>(FrameGenerationMode::AmdFrcMaximum)) {
        return FrameGenerationMode::AmdFrcMaximum;
    }
    return FrameGenerationMode::Off;
}

inline bool FrameGenerationIsVfg(FrameGenerationMode mode) {
    return mode == FrameGenerationMode::VfgLow ||
           mode == FrameGenerationMode::VfgMedium ||
           mode == FrameGenerationMode::VfgHigh;
}

inline bool FrameGenerationIsOfa(FrameGenerationMode mode) {
    return mode == FrameGenerationMode::Performance ||
           mode == FrameGenerationMode::Balanced ||
           mode == FrameGenerationMode::Quality;
}

inline bool FrameGenerationIsAmdFrc(FrameGenerationMode mode) {
    return mode == FrameGenerationMode::AmdFrcPerformance ||
           mode == FrameGenerationMode::AmdFrcQuality ||
           mode == FrameGenerationMode::AmdFrcMaximum;
}

// AMF FRC expone Low=0, High=1 y Super=2. Los nombres visibles de Dayaah son
// mas claros para una usuaria que solo quiere saber cuánto trabajo pedirle a
// la Radeon, pero guardamos los valores oficiales aqui y no regados por la UI.
inline int FrameGenerationAmdFrcProfile(FrameGenerationMode mode) {
    if (mode == FrameGenerationMode::AmdFrcMaximum) return 2;
    if (mode == FrameGenerationMode::AmdFrcQuality) return 1;
    return 0;
}

inline bool FrameGenerationAmdFrcPerformanceSearch(FrameGenerationMode mode) {
    return mode == FrameGenerationMode::AmdFrcPerformance;
}

// NVIDIA VFG llama a sus modelos Low=0, Medium=1 y High=2. Dejamos esa
// traduccion aqui para que la UI y el runtime no anden inventando numeros por
// separado (porque asi empiezan los bugs mensos jajaja).
inline unsigned FrameGenerationVfgModel(FrameGenerationMode mode) {
    if (mode == FrameGenerationMode::VfgHigh) return 2;
    if (mode == FrameGenerationMode::VfgMedium) return 1;
    return 0;
}

inline int FrameGenerationRequestedGrid(FrameGenerationMode mode) {
    if (mode == FrameGenerationMode::Quality) return 1;
    if (mode == FrameGenerationMode::Balanced) return 2;
    if (mode == FrameGenerationMode::Performance) return 4;
    return 0;
}

// Los numeros coinciden con NV_OF_PERF_LEVEL sin obligar a los tests a incluir
// encabezados de Windows: 5=SLOW, 10=MEDIUM y 20=FAST.
inline int FrameGenerationPerformanceCode(FrameGenerationMode mode) {
    if (mode == FrameGenerationMode::Quality) return 5;
    if (mode == FrameGenerationMode::Balanced) return 10;
    if (mode == FrameGenerationMode::Performance) return 20;
    return 0;
}

inline bool FrameGenerationUsesCostMap(FrameGenerationMode mode) {
    return mode == FrameGenerationMode::Balanced || mode == FrameGenerationMode::Quality;
}

// Dayaah Line Cleaner se reserva para los perfiles que ya priorizan calidad.
// Rendimiento conserva el shader corto para no cobrar muestras extra a quien
// solo quiere sacar todos los FPS posibles.
inline int FrameGenerationLineCleanerLevel(FrameGenerationMode mode) {
    if (mode == FrameGenerationMode::Quality) return 2;
    if (mode == FrameGenerationMode::Balanced) return 1;
    return 0;
}

inline int SelectFrameGenerationGrid(int requested, std::vector<int> supported) {
    if (requested <= 0 || supported.empty()) return 0;
    supported.erase(std::remove_if(supported.begin(), supported.end(),
                                   [](int value) { return value != 1 && value != 2 && value != 4; }),
                    supported.end());
    if (supported.empty()) return 0;
    std::sort(supported.begin(), supported.end());
    supported.erase(std::unique(supported.begin(), supported.end()), supported.end());
    const auto exact = std::find(supported.begin(), supported.end(), requested);
    if (exact != supported.end()) return *exact;
    // Si la GPU no tiene la rejilla pedida escogemos la mas cercana. En empate
    // gana la mas fina porque conserva mas detalle de movimiento.
    return *std::min_element(supported.begin(), supported.end(), [requested](int a, int b) {
        const int distanceA = std::abs(a - requested);
        const int distanceB = std::abs(b - requested);
        return distanceA == distanceB ? a < b : distanceA < distanceB;
    });
}

