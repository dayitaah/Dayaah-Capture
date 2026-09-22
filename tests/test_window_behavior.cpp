#include "../src/window_behavior.h"
#include <cassert>
#include <iostream>

int main() {
    assert(!ShouldHideCaptureCursor(false, true, false, true, 5000));
    assert(!ShouldHideCaptureCursor(true, false, false, true, 5000));
    assert(!ShouldHideCaptureCursor(true, true, true, true, 5000));
    assert(!ShouldHideCaptureCursor(true, true, false, false, 5000));
    assert(!ShouldHideCaptureCursor(true, true, false, true, 999));
    assert(ShouldHideCaptureCursor(true, true, false, true, 1000));

    assert(PointerPositionChanged(false, 100, 100, 100, 100));
    assert(!PointerPositionChanged(true, 100, 100, 100, 100));
    assert(PointerPositionChanged(true, 100, 100, 101, 100));
    assert(PointerPositionChanged(true, 100, 100, 100, 99));

    assert(DecideEscapeWindowAction(true, true, true) ==
           EscapeWindowAction::LeaveFullscreen);
    assert(DecideEscapeWindowAction(true, false, true) ==
           EscapeWindowAction::RestoreMaximized);
    assert(DecideEscapeWindowAction(true, false, false) ==
           EscapeWindowAction::None);
    assert(DecideEscapeWindowAction(false, true, true) ==
           EscapeWindowAction::None);

    std::cout << "Window, cursor and pointer policy: PASS\n";
}
