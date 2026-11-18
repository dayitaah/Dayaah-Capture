#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

// Diagnostico de Frame Generation. Todo esto esta APAGADO por defecto y se
// enciende con variables de entorno, para no cobrarle nada a quien no lo usa:
//
//   DAYAAH_FG_TELEMETRY=1   una vez por segundo escribe dos lineas "FGTEL" en
//                           DayaahCapture.log (pacing real, vblanks, OFA, warp%)
//   DAYAAH_FG_DEBUG=1       pinta el frame generado con un mapa de calor:
//                           verde = warp, rojo = fallback, azul = quieto
//   DAYAAH_FG_DEBUG=2       igual, pero por causa: rojo = consistencia ida/vuelta,
//                           amarillo = fotometria, magenta = costo OFA
//   DAYAAH_FG_DEBUG=3       muestra donde trabajo Dayaah Line Cleaner (cian)
//   DAYAAH_FG_LINE_CLEANER=0  lo apaga para comparar A/B sin recompilar
//   DAYAAH_FG_TUNE=a,b,c,d,e,f,g,h,i,j  sobreescribe los umbrales del
//                           estabilizador y el gate de Line Cleaner sin recompilar
//   DAYAAH_FG_BYPASS=7     atlas: forward/backward/costos en cuatro cuadrantes
//   DAYAAH_FG_BYPASS=8     compara warp forward (izq.) y backward (der.)
// Los modos 1..6 de la 3.2.0 se conservan para reproducir aquella prueba, pero
// ya no forman parte del recorrido corto que le pedimos a la usuaria.

inline bool FrameGenEnvFlag(const wchar_t* name) {
    wchar_t value[16]{};
    const DWORD length = GetEnvironmentVariableW(name, value, 16);
    return length > 0 && length < 16 && value[0] != L'0';
}

inline bool FrameGenEnvDefaultOn(const wchar_t* name) {
    wchar_t value[16]{};
    const DWORD length = GetEnvironmentVariableW(name, value, 16);
    return length == 0 || length >= 16 || value[0] != L'0';
}

inline int FrameGenDebugMode() {
    wchar_t value[16]{};
    const DWORD length = GetEnvironmentVariableW(L"DAYAAH_FG_DEBUG", value, 16);
    if (length == 0 || length >= 16) return 0;
    if (value[0] == L'0') return 0;
    if (value[0] == L'3' || value[0] == L'l' || value[0] == L'L') return 3;
    if (value[0] == L'2' || value[0] == L'w' || value[0] == L'W') return 2;
    return 1;
}

inline int FrameGenBypassMode() {
    wchar_t value[16]{};
    const DWORD length = GetEnvironmentVariableW(L"DAYAAH_FG_BYPASS", value, 16);
    if (length == 0 || length >= 16) return 0;
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(value, &end, 10);
    return end != value && parsed >= 1 && parsed <= 8 ? static_cast<int>(parsed) : 0;
}

inline bool FrameGenBypassUsesStableFlow(int mode) {
    return mode == 4 || mode == 7 || mode == 8;
}

inline const wchar_t* FrameGenBypassName(int mode) {
    switch (mode) {
    case 1: return L"crossfade temprano, OFA activo";
    case 2: return L"crossfade temprano, OFA omitido";
    case 3: return L"crossfade temprano, OFA activo, 3 fuentes";
    case 4: return L"shader normal, OFA serializado y salidas estables";
    case 5: return L"solo PreviousFrame";
    case 6: return L"solo CurrentFrame";
    case 7: return L"atlas forward/backward/costos, salidas OFA estables";
    case 8: return L"warp forward vs backward, salidas OFA estables";
    default: return L"apagado";
    }
}

// Umbrales del estabilizador (todos en pixeles del frame fuente salvo que se diga).
//  v[0], v[1]  inicio de la tolerancia de |ida+vuelta|: base + relativa * |flujo|
//  v[2], v[3]  fin de la tolerancia (ahi la confianza de consistencia llega a 0)
//  v[4], v[5]  exceso fotometrico (0..1): cuanto peor que "no mover nada" puede
//              ser el warp antes de caer a crossfade
//  v[6]        rodilla del costo OFA (0..255); por debajo no se castiga
//  v[7]        umbral de decision de la confianza final (0.5 = neutro)
//  v[8], v[9]  gate Line Cleaner: diferencia base + relativa * |flujo|
struct FrameGenTuning {
    float v[10] = {0.75f, 0.10f, 3.50f, 0.25f, 0.04f, 0.14f, 128.0f, 0.5f,
                   1.25f, 0.04f};
};

inline FrameGenTuning LoadFrameGenTuning() {
    FrameGenTuning tuning;
    wchar_t text[256]{};
    const DWORD length = GetEnvironmentVariableW(L"DAYAAH_FG_TUNE", text, 256);
    if (length == 0 || length >= 256) return tuning;
    const wchar_t* cursor = text;
    for (int i = 0; i < 10 && *cursor; ++i) {
        wchar_t* end = nullptr;
        const double parsed = std::wcstod(cursor, &end);
        if (end == cursor) break;
        tuning.v[i] = static_cast<float>(parsed);
        cursor = end;
        while (*cursor == L',' || *cursor == L';' || *cursor == L' ') ++cursor;
    }
    return tuning;
}

// Fracciones del AREA DEL VIDEO (0..1) clasificadas por la GPU en el ultimo
// frame generado medido. warp + fallback* + quiet = 1.
struct FrameGenWarpStats {
    bool valid = false;
    float warp = 0.0f;
    float fallbackConsistency = 0.0f;
    float fallbackPhoto = 0.0f;
    float fallbackCost = 0.0f;
    float quiet = 0.0f;
};

// Una muestra por cada frame fuente nuevo que paso por la ruta de FG.
struct FrameGenSample {
    bool generated = false;
    double sincePreviousPresentMs = -1.0; // Present real anterior -> Present generado
    double pairGapMs = -1.0;              // Present generado -> Present real
    double paceTargetMs = 0.0;            // lo que se pidio (0 = VSync, bloquea Present)
    double paceActualMs = 0.0;            // lo que realmente bloqueo la espera
    double opticalSubmitMs = 0.0;         // CPU: solo el costo de ENVIAR nvOFExecute
    double opticalGpuMs = -1.0;           // GPU: inicio -> despues de nvOFExecute
    double warpGpuMs = -1.0;              // GPU: despues de nvOFExecute -> fin del draw
    double syncVideoToOfaMs = 0.0;         // BYPASS 4: VP termino antes de que OFA lea
    double syncOfaToShaderMs = 0.0;        // BYPASS 4: salidas OFA copiadas y disponibles
    double syncBeforeReuseMs = 0.0;        // BYPASS 4: draw anterior termino antes de reusar
    FrameGenWarpStats warp;
    bool statsValid = false;              // GetFrameStatistics dio datos utiles
    std::uint32_t presentDelta = 0;       // presents que la pantalla ya "vio" desde la muestra anterior
    std::uint32_t refreshDelta = 0;       // vblanks transcurridos en ese mismo intervalo
    double refreshHz = 0.0;               // refresco real medido por DXGI
    HRESULT statsHr = S_OK;
};

class FrameGenTelemetry {
public:
    void Reset(ULONGLONG nowMs, std::uint64_t discardedNow) {
        windowStartMs_ = nowMs;
        discardedAtStart_ = discardedNow;
        cycles_ = 0;
        sinceGaps_.clear();
        pairGaps_.clear();
        paceTargetSum_ = paceActualSum_ = paceActualMax_ = 0.0;
        opticalSubmitSum_ = opticalSubmitMax_ = 0.0;
        syncVideoSum_ = syncVideoMax_ = 0.0;
        syncOfaSum_ = syncOfaMax_ = 0.0;
        syncReuseSum_ = syncReuseMax_ = 0.0;
        gpuSamples_ = 0;
        opticalGpuSum_ = opticalGpuMax_ = warpGpuSum_ = 0.0;
        warpSamples_ = 0;
        warpSum_ = consistencySum_ = photoSum_ = costSum_ = quietSum_ = 0.0;
        statsSamples_ = 0;
        presentDeltaSum_ = refreshDeltaSum_ = 0;
        hzSum_ = 0.0;
        hzSamples_ = 0;
    }

    void Add(const FrameGenSample& sample) {
        if (!sample.generated) return;
        ++cycles_;
        if (sample.sincePreviousPresentMs >= 0.0) sinceGaps_.push_back(sample.sincePreviousPresentMs);
        if (sample.pairGapMs >= 0.0) pairGaps_.push_back(sample.pairGapMs);
        paceTargetSum_ += sample.paceTargetMs;
        paceActualSum_ += sample.paceActualMs;
        paceActualMax_ = std::max(paceActualMax_, sample.paceActualMs);
        opticalSubmitSum_ += sample.opticalSubmitMs;
        opticalSubmitMax_ = std::max(opticalSubmitMax_, sample.opticalSubmitMs);
        syncVideoSum_ += sample.syncVideoToOfaMs;
        syncVideoMax_ = std::max(syncVideoMax_, sample.syncVideoToOfaMs);
        syncOfaSum_ += sample.syncOfaToShaderMs;
        syncOfaMax_ = std::max(syncOfaMax_, sample.syncOfaToShaderMs);
        syncReuseSum_ += sample.syncBeforeReuseMs;
        syncReuseMax_ = std::max(syncReuseMax_, sample.syncBeforeReuseMs);
        if (sample.opticalGpuMs >= 0.0) {
            ++gpuSamples_;
            opticalGpuSum_ += sample.opticalGpuMs;
            opticalGpuMax_ = std::max(opticalGpuMax_, sample.opticalGpuMs);
            warpGpuSum_ += std::max(0.0, sample.warpGpuMs);
        }
        if (sample.warp.valid) {
            ++warpSamples_;
            warpSum_ += sample.warp.warp;
            consistencySum_ += sample.warp.fallbackConsistency;
            photoSum_ += sample.warp.fallbackPhoto;
            costSum_ += sample.warp.fallbackCost;
            quietSum_ += sample.warp.quiet;
        }
        if (sample.statsValid) {
            ++statsSamples_;
            presentDeltaSum_ += sample.presentDelta;
            refreshDeltaSum_ += sample.refreshDelta;
            if (sample.refreshHz > 1.0) {
                hzSum_ += sample.refreshHz;
                ++hzSamples_;
            }
        } else {
            statsHr_ = sample.statsHr;
        }
    }

    bool Due(ULONGLONG nowMs) const { return nowMs - windowStartMs_ >= 1000ULL; }

    // Llena dos lineas listas para LogLine y reinicia la ventana.
    void Report(ULONGLONG nowMs, std::uint64_t discardedNow, double sourceIntervalMs,
                std::wstring& pacingLine, std::wstring& flowLine) {
        const double windowSeconds = std::max(0.001, static_cast<double>(nowMs - windowStartMs_) / 1000.0);
        const double ideal = std::max(0.5, sourceIntervalMs * 0.5);
        const double cycleCount = static_cast<double>(std::max<std::uint32_t>(cycles_, 1));

        std::vector<double> allGaps = sinceGaps_;
        allGaps.insert(allGaps.end(), pairGaps_.begin(), pairGaps_.end());
        std::size_t bunched = 0;
        std::size_t distant = 0;
        for (double gap : allGaps) {
            if (gap < ideal * 0.5) ++bunched;
            if (gap > ideal * 1.5) ++distant;
        }
        const double gapCount = static_cast<double>(std::max<std::size_t>(allGaps.size(), 1));

        // Presents que DXGI ya mostro frente a vblanks transcurridos. Si hubo mas
        // vblanks que presents, la pantalla repitio un cuadro. Si hubo mas presents
        // que vblanks, un Present fue pisado por otro y nunca se vio.
        const std::uint64_t seenPresents = presentDeltaSum_;
        const std::uint64_t vblanks = refreshDeltaSum_;
        const std::uint64_t repeated = vblanks > seenPresents ? vblanks - seenPresents : 0;
        const std::uint64_t merged = seenPresents > vblanks ? seenPresents - vblanks : 0;
        const double hz = hzSamples_ ? hzSum_ / static_cast<double>(hzSamples_) : 0.0;

        wchar_t line[1024]{};
        _snwprintf_s(line, _countof(line), _TRUNCATE,
            L"FGTEL pacing: ciclos/s=%.1f descartados=%llu | real->gen med/p95/max=%.1f/%.1f/%.1f ms | "
            L"gen->real med/p95/max=%.1f/%.1f/%.1f ms | ideal=%.1f ms | pegados(<50%%)=%.0f%% lejanos(>150%%)=%.0f%% | "
            L"espera objetivo/real/max=%.2f/%.2f/%.2f ms",
            static_cast<double>(cycles_) / windowSeconds,
            static_cast<unsigned long long>(discardedNow - discardedAtStart_),
            Percentile(sinceGaps_, 0.5), Percentile(sinceGaps_, 0.95), Percentile(sinceGaps_, 1.0),
            Percentile(pairGaps_, 0.5), Percentile(pairGaps_, 0.95), Percentile(pairGaps_, 1.0),
            ideal, 100.0 * static_cast<double>(bunched) / gapCount,
            100.0 * static_cast<double>(distant) / gapCount,
            paceTargetSum_ / cycleCount, paceActualSum_ / cycleCount, paceActualMax_);
        pacingLine = line;
        if (statsSamples_) {
            wchar_t vblank[256]{};
            _snwprintf_s(vblank, _countof(vblank), _TRUNCATE,
                L" | vblank: refresco=%.2f Hz presents_vistos=%llu vblanks=%llu repetidos=%llu pisados=%llu",
                hz, static_cast<unsigned long long>(seenPresents),
                static_cast<unsigned long long>(vblanks),
                static_cast<unsigned long long>(repeated),
                static_cast<unsigned long long>(merged));
            pacingLine += vblank;
        } else {
            wchar_t vblank[128]{};
            _snwprintf_s(vblank, _countof(vblank), _TRUNCATE,
                L" | vblank: n/d (GetFrameStatistics hr=0x%08X)", static_cast<unsigned>(statsHr_));
            pacingLine += vblank;
        }

        wchar_t flow[768]{};
        const double gpuCount = static_cast<double>(std::max<std::uint32_t>(gpuSamples_, 1));
        const double warpCount = static_cast<double>(std::max<std::uint32_t>(warpSamples_, 1));
        if (warpSamples_) {
            const double warp = 100.0 * warpSum_ / warpCount;
            const double consistency = 100.0 * consistencySum_ / warpCount;
            const double photo = 100.0 * photoSum_ / warpCount;
            const double cost = 100.0 * costSum_ / warpCount;
            const double quiet = 100.0 * quietSum_ / warpCount;
            const double moving = std::max(0.001, warp + consistency + photo + cost);
            _snwprintf_s(flow, _countof(flow), _TRUNCATE,
                L"FGTEL flujo: OFA submit CPU prom/max=%.2f/%.2f ms | GPU hasta-OFA=%.2f (max %.2f) warp=%.2f ms | "
                L"del frame: warp=%.0f%% fallback=%.0f%% [consistencia=%.0f%% foto=%.0f%% costo=%.0f%%] quieto=%.0f%% | "
                L"en movimiento: warp=%.0f%%",
                opticalSubmitSum_ / cycleCount, opticalSubmitMax_,
                gpuSamples_ ? opticalGpuSum_ / gpuCount : -1.0, opticalGpuMax_,
                gpuSamples_ ? warpGpuSum_ / gpuCount : -1.0,
                warp, consistency + photo + cost, consistency, photo, cost, quiet,
                100.0 * warp / moving);
        } else {
            _snwprintf_s(flow, _countof(flow), _TRUNCATE,
                L"FGTEL flujo: OFA submit CPU prom/max=%.2f/%.2f ms | GPU hasta-OFA=%.2f (max %.2f) warp=%.2f ms | warp%%: n/d",
                opticalSubmitSum_ / cycleCount, opticalSubmitMax_,
                gpuSamples_ ? opticalGpuSum_ / gpuCount : -1.0, opticalGpuMax_,
                gpuSamples_ ? warpGpuSum_ / gpuCount : -1.0);
        }
        flowLine = flow;
        if (syncVideoSum_ > 0.0 || syncOfaSum_ > 0.0 || syncReuseSum_ > 0.0) {
            wchar_t sync[320]{};
            _snwprintf_s(sync, _countof(sync), _TRUNCATE,
                L" | BYPASS4 esperas prom/max: VP->OFA=%.2f/%.2f OFA->shader=%.2f/%.2f reuso=%.2f/%.2f ms",
                syncVideoSum_ / cycleCount, syncVideoMax_,
                syncOfaSum_ / cycleCount, syncOfaMax_,
                syncReuseSum_ / cycleCount, syncReuseMax_);
            flowLine += sync;
        }
        Reset(nowMs, discardedNow);
    }

private:
    static double Percentile(std::vector<double> values, double fraction) {
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        const double position = fraction * static_cast<double>(values.size() - 1);
        const std::size_t low = static_cast<std::size_t>(position);
        const std::size_t high = std::min(low + 1, values.size() - 1);
        const double weight = position - static_cast<double>(low);
        return values[low] + (values[high] - values[low]) * weight;
    }

    ULONGLONG windowStartMs_ = 0;
    std::uint64_t discardedAtStart_ = 0;
    std::uint32_t cycles_ = 0;
    std::vector<double> sinceGaps_;
    std::vector<double> pairGaps_;
    double paceTargetSum_ = 0.0;
    double paceActualSum_ = 0.0;
    double paceActualMax_ = 0.0;
    double opticalSubmitSum_ = 0.0;
    double opticalSubmitMax_ = 0.0;
    double syncVideoSum_ = 0.0;
    double syncVideoMax_ = 0.0;
    double syncOfaSum_ = 0.0;
    double syncOfaMax_ = 0.0;
    double syncReuseSum_ = 0.0;
    double syncReuseMax_ = 0.0;
    std::uint32_t gpuSamples_ = 0;
    double opticalGpuSum_ = 0.0;
    double opticalGpuMax_ = 0.0;
    double warpGpuSum_ = 0.0;
    std::uint32_t warpSamples_ = 0;
    double warpSum_ = 0.0;
    double consistencySum_ = 0.0;
    double photoSum_ = 0.0;
    double costSum_ = 0.0;
    double quietSum_ = 0.0;
    std::uint32_t statsSamples_ = 0;
    std::uint64_t presentDeltaSum_ = 0;
    std::uint64_t refreshDeltaSum_ = 0;
    double hzSum_ = 0.0;
    std::uint32_t hzSamples_ = 0;
    HRESULT statsHr_ = S_OK;
};
