#pragma once
#include <cstdint>

enum class EscapeWindowAction {
    None,
    LeaveFullscreen,
    RestoreMaximized,
};

constexpr EscapeWindowAction DecideEscapeWindowAction(bool running,
                                                       bool fullscreen,
                                                       bool maximized) {
    if (!running) return EscapeWindowAction::None;
    if (fullscreen) return EscapeWindowAction::LeaveFullscreen;
    if (maximized) return EscapeWindowAction::RestoreMaximized;
    return EscapeWindowAction::None;
}

constexpr bool ShouldHideCaptureCursor(bool running,
                                       bool foreground,
                                       bool minimized,
                                       bool pointerInside,
                                       std::uint64_t idleMilliseconds) {
    return running && foreground && !minimized && pointerInside &&
           idleMilliseconds >= 1000;
}

constexpr bool PointerPositionChanged(bool hasPreviousPosition,
                                      long previousX,
                                      long previousY,
                                      long currentX,
                                      long currentY) {
    return !hasPreviousPosition || previousX != currentX || previousY != currentY;
}
