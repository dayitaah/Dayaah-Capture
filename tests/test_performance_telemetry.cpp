#include "../src/performance_telemetry.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    FramePacingTracker tracker;
    for (int i = 0; i < 90; ++i) {
        tracker.Add(100000 + static_cast<std::int64_t>(i) * 166667, i * 166667, 10000000);
    }
    const FramePacingSnapshot pacing = tracker.Snapshot();
    assert(pacing.count == 89);
    assert(std::fabs(pacing.averageMs - 16.6667) < 0.01);
    assert(pacing.jitterMs < 0.01);

    const std::vector<ModeCandidate> modes = {
        {3840, 2160, 60.0, 5},
        {1920, 1080, 60.0, 2},
        {1920, 1080, 60.0, 1},
        {1920, 1080, 30.0, 1},
        {1280, 720, 60.0, 1},
    };
    const AutoTuneDecision decision = SelectAutoTuneMode(modes, {1920, 1080, 60.0});
    assert(decision.index == 2);
    assert(decision.estimatedMegabytesPerSecond > 170.0);
    assert(decision.estimatedMegabytesPerSecond < 190.0);

    const CadenceMismatch mismatch = DetectCadenceMismatch(60000.0 / 1001.0, 60.0, true);
    assert(mismatch.visible);
    assert(mismatch.repeatEverySeconds > 16.0 && mismatch.repeatEverySeconds < 17.5);
    assert(!DetectCadenceMismatch(60.0, 60.0, true).visible);
    assert(!DetectCadenceMismatch(60000.0 / 1001.0, 60.0, false).visible);
    assert(std::fabs(NormalizeWindowsRefreshRate(59.0) - 60000.0 / 1001.0) < 0.001);
    assert(std::fabs(NormalizeWindowsRefreshRate(60.0) - 60.0) < 0.001);

    assert(AutoTuneQualityScore(16.6667, pacing, 300, 0, 0.4, 0.2) >= 95);
    assert(AutoTuneQualityScore(16.6667, pacing, 300, 30, 0.4, 0.2) < 60);
    assert(PhoenixMayAttemptRecovery(0));
    assert(PhoenixMayAttemptRecovery(2));
    assert(!PhoenixMayAttemptRecovery(3));

    const NoticeAnimation hidden = ComputeNoticeAnimation(5000, 5000);
    const NoticeAnimation entering = ComputeNoticeAnimation(130, 5000);
    const NoticeAnimation steady = ComputeNoticeAnimation(1000, 5000);
    const NoticeAnimation leaving = ComputeNoticeAnimation(4900, 5000);
    assert(hidden.alpha == 0.0f);
    assert(entering.alpha > 0.0f && entering.alpha < 1.0f && entering.slide > 0.0f);
    assert(steady.alpha == 1.0f && steady.slide == 0.0f);
    assert(leaving.alpha > 0.0f && leaving.alpha < 1.0f && leaving.slide > 0.0f);

    std::cout << "AutoTune, cadence, pacing telemetry and notice animation: PASS\n";
}
