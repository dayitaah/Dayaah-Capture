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

    assert(DecideEscapeWindowAction(true, true, true, true) ==
           EscapeWindowAction::LeavePictureInPicture);
    assert(DecideEscapeWindowAction(true, false, true, true) ==
           EscapeWindowAction::LeaveFullscreen);
    assert(DecideEscapeWindowAction(true, false, false, true) ==
           EscapeWindowAction::RestoreMaximized);
    assert(DecideEscapeWindowAction(true, false, false, false) ==
           EscapeWindowAction::None);
    assert(DecideEscapeWindowAction(false, true, true, true) ==
           EscapeWindowAction::None);

    constexpr PictureInPictureBounds work{0, 0, 1920, 1040};
    constexpr auto visible = ClampPictureInPictureBounds(
        {1800, 1000, 2280, 1270}, work, 320, 180);
    static_assert(visible.left == 1440 && visible.top == 770);
    static_assert(visible.right == 1920 && visible.bottom == 1040);

    constexpr auto minimum = ClampPictureInPictureBounds(
        {100, 100, 110, 110}, work, 320, 180);
    static_assert(minimum.left == 100 && minimum.top == 100);
    static_assert(minimum.right == 420 && minimum.bottom == 280);

    constexpr PictureInPictureBounds smallWork{0, 0, 200, 100};
    constexpr auto fillsSmallWork = ClampPictureInPictureBounds(
        {-50, -50, 450, 300}, smallWork, 320, 180);
    static_assert(fillsSmallWork.left == 0 && fillsSmallWork.top == 0);
    static_assert(fillsSmallWork.right == 200 && fillsSmallWork.bottom == 100);

    static_assert(ShouldApplyVisualEffects(true, false));
    static_assert(!ShouldApplyVisualEffects(true, true));
    static_assert(!ShouldApplyVisualEffects(false, false));

    std::cout << "Window, cursor, pointer and PiP policy: PASS\n";
}
