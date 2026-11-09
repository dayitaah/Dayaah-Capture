#pragma once
#include <cstdint>

enum class EscapeWindowAction {
    None,
    LeavePictureInPicture,
    LeaveFullscreen,
    RestoreMaximized,
};

// ESC va desarmando modos por capas: primero PiP, luego fullscreen y luego una
// ventana maximizada. No cierra la app de golpe y nos evita sustos.
constexpr EscapeWindowAction DecideEscapeWindowAction(bool running,
                                                       bool pictureInPicture,
                                                       bool fullscreen,
                                                       bool maximized) {
    if (!running) return EscapeWindowAction::None;
    if (pictureInPicture) return EscapeWindowAction::LeavePictureInPicture;
    if (fullscreen) return EscapeWindowAction::LeaveFullscreen;
    if (maximized) return EscapeWindowAction::RestoreMaximized;
    return EscapeWindowAction::None;
}

struct PictureInPictureBounds {
    long left = 0;
    long top = 0;
    long right = 0;
    long bottom = 0;
};

constexpr long BoundLong(long value, long minimum, long maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

constexpr PictureInPictureBounds ClampPictureInPictureBounds(
    PictureInPictureBounds requested,
    PictureInPictureBounds workArea,
    long minimumWidth,
    long minimumHeight) {
    // Si guardaste PiP en un monitor que ya desconectaste, lo regresamos al
    // area visible del monitor actual para que no quede perdido en el limbo.
    const long workWidth = workArea.right - workArea.left;
    const long workHeight = workArea.bottom - workArea.top;
    const long requestedWidth = requested.right - requested.left;
    const long requestedHeight = requested.bottom - requested.top;
    const long effectiveMinimumWidth = minimumWidth < workWidth ? minimumWidth : workWidth;
    const long effectiveMinimumHeight = minimumHeight < workHeight ? minimumHeight : workHeight;
    const long width = BoundLong(requestedWidth, effectiveMinimumWidth, workWidth);
    const long height = BoundLong(requestedHeight, effectiveMinimumHeight, workHeight);
    const long left = BoundLong(requested.left, workArea.left, workArea.right - width);
    const long top = BoundLong(requested.top, workArea.top, workArea.bottom - height);
    return {left, top, left + width, top + height};
}

constexpr bool ShouldApplyVisualEffects(bool configuredEnabled,
                                        bool pictureInPicture) {
    // PiP pausa efectos, pero NO borra la preferencia elegida por la usuaria.
    return configuredEnabled && !pictureInPicture;
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
