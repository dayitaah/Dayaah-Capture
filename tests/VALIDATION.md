# Validation — 2.2.0

## Completed

- Windows x64 GUI cross-compiled with LLVM-MinGW 20261104, C++17, UCRT,
  optimization, `-Wall`, `-Wextra` and `-Werror`; no compiler warnings.
- The result is a PE32+ x86-64 Windows GUI executable with its C++ runtime
  linked statically and only Windows system-DLL imports.
- Portable tests pass for disco mode, mode selection, latency estimation,
  overlay geometry/crosshair, JSON profiles, cursor/window behavior and effects.
- PiP policy tests cover `Esc` precedence, off-screen recovery, minimum size and
  clamping to both normal and undersized monitor work areas.
- AutoTune tests cover raw-format preference, display matching and bandwidth;
  performance tests cover 120-frame pacing telemetry, cadence mismatch,
  bounded quality scoring and notice animation curves.
- The latency tests cover 60/30 FPS, frame age, VSync, minimum-latency mode and
  the displayed upper bound.
- Overlay profile tests round-trip all five item types, including crosshair and
  Unicode content.
- Effects tests cover neutral bypass, clamps, 50,000 randomized serialization
  round trips and concurrent atomic publication.

## Requires Windows hardware acceptance

- Real capture input and audio from each supported device.
- D3D11 video-processor acceptance of XRGB and P010 on the target GPU.
- I420 plane order and sustained CPU use at every advertised resolution.
- Visual sharpening quality, P010 tone/range, long-running stability and USB load.
- First-run behavior on two different physical PCs.
- Photon-to-photon latency with an external high-speed camera or sensor.
- AutoTune acceptance with every mode exposed by the UGREEN 25173.
- Phoenix recovery after unplug/replug, HDMI resolution changes and source loss.
- Confirm that the third failed Phoenix recovery replaces the process and that
  setup contains neither the previous audio stream nor the final video frame.
- Enter PiP from normal, maximized and fullscreen states; resize/move it on each
  monitor and verify the exact previous window state returns.
- Verify PiP pauses every video/disco effect, preserves overlays and restores
  the saved effect state after exit and after Phoenix recovery.
- Verify No VSync/VRR on a compatible display both inside and outside its VRR
  range, then repeat with VRR disabled to distinguish ordinary tearing.

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
6. Enable `F9`; confirm the interval graph and 59.94/60 warning when applicable.
7. Run AutoTune and confirm its five-second score and selected mode.
8. Unplug/replug HDMI and USB separately; verify Phoenix stops after three failures.
9. Compare sharpening at 0%, 35%, 70% and 100% on text and fine textures.
10. Add a crosshair and verify it remains centered while resizing/fullscreen.
11. Switch English/Spanish and inspect setup, context menu and both editors.
12. Press `F12`, move/resize PiP, restart it and confirm its geometry persists.
13. Enable sharpening/CRT, enter PiP, confirm the effects pause, then leave PiP
    and confirm the same settings return.
