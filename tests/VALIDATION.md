# Validation — 2.0.1

## Completed

- Windows x64 GUI cross-compiled with C++17, optimization, `-Wall`, `-Wextra`
  and `-Werror`; no compiler warnings.
- Portable tests pass for disco mode, mode selection, latency estimation,
  overlay geometry/crosshair, JSON profiles, cursor/window behavior and effects.
- The latency tests cover 60/30 FPS, frame age, VSync, minimum-latency mode and
  the displayed upper bound.
- Overlay profile tests round-trip all five item types, including crosshair and
  Unicode content.
- Effects tests cover neutral bypass, clamps, 50,000 randomized serialization
  round trips and concurrent atomic publication.
- The result is a PE32+ x64 Windows GUI executable.

## Requires Windows hardware acceptance

- Real capture input and audio from each supported device.
- D3D11 video-processor acceptance of XRGB and P010 on the target GPU.
- I420 plane order and sustained CPU use at every advertised resolution.
- Visual sharpening quality, P010 tone/range, long-running stability and USB load.
- First-run behavior on two different physical PCs.
- Photon-to-photon latency with an external high-speed camera or sensor.

The `F8` HUD is intentionally labeled approximate. It models source/capture
transport, display scanout, current frame age and measured `Present` wait. It
does not claim to measure the PS5 output or display panel without external
hardware.

## Suggested hardware checklist

1. Start 1920×1080 60 FPS NV12 in minimum-latency mode.
2. Verify the next launch enters the viewer, then use **Settings** to return.
3. Copy the folder including INI to a second PC and verify setup opens.
4. Exercise YUY2, XRGB, I420 and P010 if the device advertises each mode.
5. Toggle VSync and confirm the `F8` estimate increases accordingly.
6. Compare sharpening at 0%, 35%, 70% and 100% on text and fine textures.
7. Add a crosshair and verify it remains centered while resizing/fullscreen.
8. Switch English/Spanish and inspect setup, context menu and both editors.
