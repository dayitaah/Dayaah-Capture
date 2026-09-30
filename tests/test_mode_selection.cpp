#include "../src/mode_selection.h"
#include <cassert>
#include <iostream>

int main() {
    const std::vector<ModeCandidate> modes = {
        {3840, 2160, 30.0, 1},
        {1920, 1080, 60.0, 1},
        {1920, 1080, 30.0, 1},
        {1920, 1080, 60.0, 2},
        {1280,  720, 60.0, 1},
        {1280,  720, 30.0, 2},
    };
    ModeCandidate current{1920, 1080, 60.0, 1};

    ModeCandidate resolution{};
    resolution.width = 3840; resolution.height = 2160;
    assert(SelectClosestMode(modes, current, resolution, ModeAxis::Resolution) == 0);
    resolution.width = 1280; resolution.height = 720;
    assert(SelectClosestMode(modes, current, resolution, ModeAxis::Resolution) == 4);

    ModeCandidate fps{};
    fps.fps = 30.0;
    assert(SelectClosestMode(modes, current, fps, ModeAxis::Fps) == 2);

    ModeCandidate format{};
    format.format = 2;
    assert(SelectClosestMode(modes, current, format, ModeAxis::Format) == 3);

    resolution.width = 7680; resolution.height = 4320;
    assert(SelectClosestMode(modes, current, resolution, ModeAxis::Resolution) == -1);
    std::cout << "Resolution, FPS and format selection preservation/fallback: PASS\n";
}
