#include "../src/disco_effect.h"

#include <cassert>
#include <iostream>

int main() {
    assert(ComputeDiscoHueLevel(-720, 720, 0) == -720);
    assert(ComputeDiscoHueLevel(-720, 720, 1200) == 0);
    assert(ComputeDiscoHueLevel(-720, 720, 2399) >= 719);
    assert(ComputeDiscoHueLevel(-720, 720, 2400) == -720);
    assert(ComputeDiscoHueLevel(10, 10, 500) == 10);
    assert(ComputeDiscoHueLevel(-10, 10, 500, 0) == -10);

    assert(ComputeDiscoSaturationLevel(100, 200) == 133);
    assert(ComputeDiscoSaturationLevel(100, 100) == 100);

    std::cout << "Disco hue cycle and saturation policy: PASS\n";
}
