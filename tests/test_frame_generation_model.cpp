#include <cassert>
#include <iostream>
#include <vector>

#include "../src/frame_generation_model.h"

int main() {
    assert(FrameGenerationModeFromInt(-1) == FrameGenerationMode::Off);
    assert(FrameGenerationModeFromInt(0) == FrameGenerationMode::Off);
    assert(FrameGenerationModeFromInt(1) == FrameGenerationMode::Performance);
    assert(FrameGenerationModeFromInt(2) == FrameGenerationMode::Balanced);
    assert(FrameGenerationModeFromInt(3) == FrameGenerationMode::Quality);
    assert(FrameGenerationModeFromInt(4) == FrameGenerationMode::VfgLow);
    assert(FrameGenerationModeFromInt(5) == FrameGenerationMode::VfgMedium);
    assert(FrameGenerationModeFromInt(6) == FrameGenerationMode::VfgHigh);
    assert(FrameGenerationModeFromInt(7) == FrameGenerationMode::AmdFrcPerformance);
    assert(FrameGenerationModeFromInt(8) == FrameGenerationMode::AmdFrcQuality);
    assert(FrameGenerationModeFromInt(9) == FrameGenerationMode::AmdFrcMaximum);
    assert(FrameGenerationModeFromInt(10) == FrameGenerationMode::Off);
    assert(FrameGenerationIsOfa(FrameGenerationMode::Balanced));
    assert(!FrameGenerationIsOfa(FrameGenerationMode::VfgMedium));
    assert(FrameGenerationIsVfg(FrameGenerationMode::VfgLow));
    assert(FrameGenerationIsVfg(FrameGenerationMode::VfgMedium));
    assert(FrameGenerationIsVfg(FrameGenerationMode::VfgHigh));
    assert(!FrameGenerationIsVfg(FrameGenerationMode::Quality));
    assert(FrameGenerationIsAmdFrc(FrameGenerationMode::AmdFrcPerformance));
    assert(FrameGenerationIsAmdFrc(FrameGenerationMode::AmdFrcQuality));
    assert(FrameGenerationIsAmdFrc(FrameGenerationMode::AmdFrcMaximum));
    assert(!FrameGenerationIsAmdFrc(FrameGenerationMode::VfgHigh));
    assert(FrameGenerationVfgModel(FrameGenerationMode::VfgLow) == 0);
    assert(FrameGenerationVfgModel(FrameGenerationMode::VfgMedium) == 1);
    assert(FrameGenerationVfgModel(FrameGenerationMode::VfgHigh) == 2);
    assert(FrameGenerationAmdFrcProfile(FrameGenerationMode::AmdFrcPerformance) == 0);
    assert(FrameGenerationAmdFrcProfile(FrameGenerationMode::AmdFrcQuality) == 1);
    assert(FrameGenerationAmdFrcProfile(FrameGenerationMode::AmdFrcMaximum) == 2);
    assert(FrameGenerationAmdFrcPerformanceSearch(FrameGenerationMode::AmdFrcPerformance));
    assert(!FrameGenerationAmdFrcPerformanceSearch(FrameGenerationMode::AmdFrcQuality));

    assert(FrameGenerationRequestedGrid(FrameGenerationMode::Performance) == 4);
    assert(FrameGenerationRequestedGrid(FrameGenerationMode::Balanced) == 2);
    assert(FrameGenerationRequestedGrid(FrameGenerationMode::Quality) == 1);
    assert(FrameGenerationPerformanceCode(FrameGenerationMode::Performance) == 20);
    assert(FrameGenerationPerformanceCode(FrameGenerationMode::Balanced) == 10);
    assert(FrameGenerationPerformanceCode(FrameGenerationMode::Quality) == 5);
    assert(!FrameGenerationUsesCostMap(FrameGenerationMode::Performance));
    assert(FrameGenerationUsesCostMap(FrameGenerationMode::Balanced));
    assert(FrameGenerationUsesCostMap(FrameGenerationMode::Quality));
    assert(FrameGenerationLineCleanerLevel(FrameGenerationMode::Off) == 0);
    assert(FrameGenerationLineCleanerLevel(FrameGenerationMode::Performance) == 0);
    assert(FrameGenerationLineCleanerLevel(FrameGenerationMode::Balanced) == 1);
    assert(FrameGenerationLineCleanerLevel(FrameGenerationMode::Quality) == 2);

    assert(SelectFrameGenerationGrid(4, {1, 2, 4}) == 4);
    assert(SelectFrameGenerationGrid(2, {4, 1}) == 1);
    assert(SelectFrameGenerationGrid(1, {4, 2}) == 2);
    assert(SelectFrameGenerationGrid(4, {0, 8, 2}) == 2);
    assert(SelectFrameGenerationGrid(1, {}) == 0);

    std::cout << "Frame Generation modes, quality mapping and grid fallback: PASS\n";
    return 0;
}
