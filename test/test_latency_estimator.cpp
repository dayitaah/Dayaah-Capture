#include "../src/latency_estimator.h"

#include <cassert>
#include <iostream>

int main() {
    const auto low60 = EstimateViewerLatencyMs(60.0, 0, 0.0, true);
    const auto vsync60 = EstimateViewerLatencyMs(60.0, 0, 0.0, false);
    assert(low60 == 33);
    assert(vsync60 == 50);
    assert(vsync60 > low60);
    assert(EstimateViewerLatencyMs(60.0, 7, 0.0, true) == low60 + 7);
    assert(EstimateViewerLatencyMs(30.0, 0, 0.0, true) == 67);
    assert(EstimateViewerLatencyMs(0.0, 0, 0.0, true) == 33);
    assert(EstimateViewerLatencyMs(60.0, 5000, 0.0, true) == 999);
    std::cout << "Estimated latency: frame age, refresh rate, VSync and bounds: PASS\n";
}
