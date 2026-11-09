#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "mode_selection.h"

constexpr std::size_t kFramePacingHistory = 120;

// Guardamos los ultimos 120 intervalos: a 60 fps son unos dos segundos. Alcanza
// para ver tirones sin acumular datos eternamente ni convertir esto en Excel.
struct FramePacingSnapshot {
    std::array<float, kFramePacingHistory> intervalsMs{};
    std::size_t count = 0;
    double averageMs = 0.0;
    double jitterMs = 0.0;
    double minimumMs = 0.0;
    double maximumMs = 0.0;
};

class FramePacingTracker {
public:
    void Reset() {
        values_.fill(0.0f);
        count_ = 0;
        next_ = 0;
        lastSourceTimestamp_ = 0;
        lastArrivalCounter_ = 0;
    }

    void Add(std::int64_t sourceTimestamp100ns, std::int64_t arrivalCounter,
             std::int64_t counterFrequency) {
        double intervalMs = 0.0;
        if (sourceTimestamp100ns > 0 && lastSourceTimestamp_ > 0 &&
            sourceTimestamp100ns > lastSourceTimestamp_) {
            intervalMs = static_cast<double>(sourceTimestamp100ns - lastSourceTimestamp_) / 10000.0;
        } else if (arrivalCounter > 0 && lastArrivalCounter_ > 0 &&
                   arrivalCounter > lastArrivalCounter_ && counterFrequency > 0) {
            intervalMs = static_cast<double>(arrivalCounter - lastArrivalCounter_) * 1000.0 /
                         static_cast<double>(counterFrequency);
        }
        if (sourceTimestamp100ns > 0) lastSourceTimestamp_ = sourceTimestamp100ns;
        if (arrivalCounter > 0) lastArrivalCounter_ = arrivalCounter;

        // Ignoramos saltos absurdos causados por reinicio del dispositivo o
        // cambio de señal. Eso no es jitter normal y ensuciaria la grafica.
        if (intervalMs < 1.0 || intervalMs > 250.0) return;
        values_[next_] = static_cast<float>(intervalMs);
        next_ = (next_ + 1) % values_.size();
        count_ = std::min(count_ + 1, values_.size());
    }

    FramePacingSnapshot Snapshot() const {
        FramePacingSnapshot result{};
        result.count = count_;
        if (!count_) return result;

        const std::size_t first = count_ == values_.size() ? next_ : 0;
        double sum = 0.0;
        result.minimumMs = std::numeric_limits<double>::infinity();
        result.maximumMs = 0.0;
        for (std::size_t i = 0; i < count_; ++i) {
            const float value = values_[(first + i) % values_.size()];
            result.intervalsMs[i] = value;
            sum += value;
            result.minimumMs = std::min(result.minimumMs, static_cast<double>(value));
            result.maximumMs = std::max(result.maximumMs, static_cast<double>(value));
        }
        result.averageMs = sum / static_cast<double>(count_);
        double variance = 0.0;
        for (std::size_t i = 0; i < count_; ++i) {
            const double delta = result.intervalsMs[i] - result.averageMs;
            variance += delta * delta;
        }
        result.jitterMs = std::sqrt(variance / static_cast<double>(count_));
        return result;
    }

private:
    std::array<float, kFramePacingHistory> values_{};
    std::size_t count_ = 0;
    std::size_t next_ = 0;
    std::int64_t lastSourceTimestamp_ = 0;
    std::int64_t lastArrivalCounter_ = 0;
};

struct AutoTuneTarget {
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    double refreshHz = 60.0;
};

struct AutoTuneDecision {
    int index = -1;
    double score = -std::numeric_limits<double>::infinity();
    double estimatedMegabytesPerSecond = 0.0;
};

inline double FormatBytesPerPixel(int format) {
    if (format == 1 || format == 3) return 1.5; // NV12 / I420
    if (format == 2) return 2.0;                // YUY2
    if (format == 4) return 3.0;                // P010
    if (format == 5) return 4.0;                // XRGB
    return 8.0;
}

inline double AutoTuneFormatScore(int format) {
    if (format == 1) return 520.0; // NV12: ruta GPU nativa y poquito ancho de banda.
    if (format == 2) return 390.0; // YUY2: ruta nativa, pero pesa mas.
    if (format == 5) return 250.0; // XRGB: sin croma, pero se come el USB sabroso.
    if (format == 4) return 210.0; // P010: 10 bits utiles, caros para un viewer SDR.
    if (format == 3) return 170.0; // I420: requiere reacomodar U/V por CPU.
    return -1000.0;
}

inline AutoTuneDecision SelectAutoTuneMode(const std::vector<ModeCandidate>& modes,
                                           const AutoTuneTarget& requested) {
    AutoTuneDecision best{};
    const double targetPixels = std::max(1.0, static_cast<double>(requested.width) * requested.height);
    const double targetHz = requested.refreshHz > 1.0 ? requested.refreshHz : 60.0;
    for (std::size_t i = 0; i < modes.size(); ++i) {
        const ModeCandidate& mode = modes[i];
        if (!mode.width || !mode.height || mode.fps < 1.0) continue;
        const double pixels = static_cast<double>(mode.width) * mode.height;
        const double resolutionError = std::fabs(std::log2(pixels / targetPixels));
        const double fpsError = std::fabs(mode.fps - targetHz);
        const double bandwidth = pixels * mode.fps * FormatBytesPerPixel(mode.format) /
                                 (1024.0 * 1024.0);

        double score = AutoTuneFormatScore(mode.format);
        score += 900.0 - resolutionError * 430.0;
        score += 700.0 - fpsError * 22.0;
        if (mode.width == requested.width && mode.height == requested.height) score += 350.0;
        if (fpsError < 0.12) score += 260.0;
        // Cerca del limite practico de USB 5 Gbit/s la captura cruda se vuelve
        // delicada, asi que castigamos esos modos antes de que fallen en vivo.
        if (bandwidth > 430.0) score -= (bandwidth - 430.0) * 3.5;
        if (mode.width > requested.width || mode.height > requested.height) score -= 80.0;

        if (best.index < 0 || score > best.score) {
            best.index = static_cast<int>(i);
            best.score = score;
            best.estimatedMegabytesPerSecond = bandwidth;
        }
    }
    return best;
}

struct CadenceMismatch {
    bool visible = false;
    double repeatEverySeconds = 0.0;
};

inline double NormalizeWindowsRefreshRate(double reportedHz) {
    // Windows suele redondear los modos NTSC fraccionarios a 23/29/59 Hz.
    if (std::fabs(reportedHz - 23.0) < 0.01) return 24000.0 / 1001.0;
    if (std::fabs(reportedHz - 29.0) < 0.01) return 30000.0 / 1001.0;
    if (std::fabs(reportedHz - 59.0) < 0.01) return 60000.0 / 1001.0;
    return reportedHz;
}

inline CadenceMismatch DetectCadenceMismatch(double sourceHz, double displayHz, bool vsync) {
    CadenceMismatch result{};
    if (!vsync || sourceHz < 20.0 || displayHz < 20.0) return result;
    const double difference = std::fabs(displayHz - sourceHz);
    if (difference < 0.02 || difference > 1.0) return result;
    result.visible = true;
    result.repeatEverySeconds = 1.0 / difference;
    return result;
}

inline int AutoTuneQualityScore(double expectedFrameMs, const FramePacingSnapshot& pacing,
                                std::uint64_t received, std::uint64_t discarded,
                                double processingMs, double presentMs) {
    if (received < 2 || pacing.count < 2 || expectedFrameMs <= 0.0) return 0;
    double score = 100.0;
    const double dropRatio = static_cast<double>(discarded) / static_cast<double>(received);
    score -= std::min(45.0, dropRatio * 900.0);
    score -= std::min(25.0, pacing.jitterMs * 5.0);
    score -= std::min(15.0, std::fabs(pacing.averageMs - expectedFrameMs) * 2.0);
    score -= std::min(10.0, processingMs * 1.5);
    score -= std::min(10.0, std::max(0.0, presentMs - expectedFrameMs) * 0.75);
    return static_cast<int>(std::clamp(std::lround(score), 0L, 100L));
}

inline bool PhoenixMayAttemptRecovery(int completedAttempts) {
    // Tres intentos y se acabo. Despues volvemos a Configuracion por la ruta
    // segura en lugar de hacer un bucle zombie.
    return completedAttempts >= 0 && completedAttempts < 3;
}

struct NoticeAnimation {
    float alpha = 0.0f;
    float slide = 1.0f;
};

inline float SmoothStep01(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

inline NoticeAnimation ComputeNoticeAnimation(std::uint64_t ageMs, std::uint64_t durationMs,
                                              std::uint64_t transitionMs = 260) {
    // Entrada y salida con smoothstep: empieza suave, acelera y vuelve a frenar.
    // Mucho mas bonito que aparecer/desaparecer de madrazo.
    NoticeAnimation result{};
    if (!durationMs || ageMs >= durationMs) return result;
    if (ageMs < transitionMs) {
        const float progress = SmoothStep01(static_cast<float>(ageMs) /
                                            static_cast<float>(transitionMs));
        result.alpha = progress;
        result.slide = 1.0f - progress;
        return result;
    }
    if (durationMs > transitionMs && ageMs > durationMs - transitionMs) {
        const float progress = SmoothStep01(static_cast<float>(durationMs - ageMs) /
                                            static_cast<float>(transitionMs));
        result.alpha = progress;
        result.slide = 1.0f - progress;
        return result;
    }
    result.alpha = 1.0f;
    result.slide = 0.0f;
    return result;
}
