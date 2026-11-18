# Validation — 3.4.1 BETA

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
- Frame Generation policy tests cover INI selector parsing, quality-to-OFA
  preset/grid mapping, confidence-cost policy, Line Cleaner quality policy and
  supported-grid fallback.
- NVIDIA Optical Flow is loaded dynamically from the installed driver, so the
  executable keeps no hard dependency on a redistributed NVIDIA runtime DLL.
- NVIDIA VFG is a separate dynamic backend with stable INI values 4–6 for its
  Low, Medium and High models. The regular executable remains loadable without
  VFG DLLs and falls back to normal capture with an explicit error.
- AMD AMF FRC is a third dynamic backend with stable INI values 7–9. It loads
  `amfrt64.dll` only from the installed driver, wraps D3D11 BGRA textures and
  requests interpolated-only output with FutureFrame disabled.
- The NVIDIA-VFG package uses the unmodified official 0.2.0.0 runtime; its wheel
  SHA-256 is `5aaf6a42bc6b6dbbf52fcb714194c994a6893cbbf7ada38bc2165a1f83e4a6fc`.
- All eight `DAYAAH_FG_BYPASS` modes compile into the same binary. Modes 7/8
  reuse stable flow/cost copies and D3D11 event queries; mode 7 visualizes both
  directions and costs, while mode 8 isolates one-way reconstruction.
- Normal interpolation uses the first forward/backward pair directly. The
  displaced second lookup that changed flow cells has been removed.

## Requires Windows hardware acceptance

- On a Radeon system, test AMD AMF FRC Performance, Quality and Maximum with a
  real 30 FPS source for 30→60. Confirm generated → real order, audio continuity,
  resizing, PiP, VSync/No VSync and safe fallback when AMF FRC is unavailable.
- Compare High at 1080p/1440p and Super at 1440p/4K; attach the `AMFTEL` timing
  lines and exact Radeon model/driver. Confirm the selected D3D11 adapter is AMD
  on hybrid systems.

- On the RTX 5070, select a 30 FPS capture mode and test NVIDIA VFG Medium for
  30→60. Compare pacing, latency and artifacts with NVIDIA OFA Balanced.
- Repeat with VFG Low and High. Confirm the title/HUD identifies VFG, resizing
  and PiP preserve aspect ratio, and normal current frames remain complete.
- Remove `runtime/nvidia-vfg`, select VFG and confirm Dayaah reports the missing
  runtime in the log, continues normal video/audio, and does not crash.
- Force a VFG run or D3D/CUDA interop error and confirm hot fallback returns to
  the normal renderer in the same process.
- Re-test VFG High on the RTX 5070 and confirm the log reports
  `D3D11 RGBA8 <-> CUDA RGBA8` without `NvCVImage_Transfer -9`.

- Run Balanced mode normally, with `DAYAAH_FG_BYPASS` removed. Reproduce the
  TestUFO 20/40/80-pixel rows and confirm rectangular cuts no longer appear.
- Test real gameplay with fast pans and character silhouettes. Confirm the
  first-pair reconstruction removes slicing without restoring whole-image wobble.

- Reproduce the TestUFO scene in Balanced mode with bypass 7. Confirm the upper
  quadrants show forward/backward direction and magnitude, while the lower
  quadrants show both costs. Static UI should remain black in the flow views.
- Repeat the same scene with bypass 8. Compare the forward-only left half to the
  backward-only right half and attach the `FGFLOW` lines from both runs.
- Treat the copies, readbacks and waits in these modes as diagnostics, not
  production pacing or performance measurements.

- Run with Frame Generation disabled on NVIDIA, AMD and Intel systems and verify
  the 2.5.0 capture/render path is unchanged.
- On Turing, Ampere, Ada and newer NVIDIA hardware, exercise Performance,
  Balanced and Quality at 1080p60, 1440p60 and 4K60 where the capture card allows.
- Confirm the log reports the actual driver-selected 1×1/2×2/4×4 grid and
  whether confidence-cost maps are active; test a GPU that lacks the requested grid.
- Present 60→120 on a 120/144 Hz display with both No VSync/VRR and VSync; verify
  generated/current order, pacing, title FPS and the absence of repeated pairs.
- Compare Balanced and Quality with `DAYAAH_FG_LINE_CLEANER=0` on fast character
  limbs, fences, HUD edges and camera pans. Check that the default reduces edge
  tearing/ghost trails without making the whole image wobble.
- Run with `DAYAAH_FG_DEBUG=3` and confirm only motion boundaries and recovered
  disocclusions turn cyan; smooth static areas and uniform pans should stay dark.
- In Quality, compare slow one-pixel motion and fast 16–64 pixel motion. Confirm
  the four-pixel neighbor step no longer makes the 1×1 grid search shorter than
  Balanced, while the bounded rescue does not pull unrelated distant objects.
- Test both leading and trailing edges of a fast foreground object. The backward
  candidates should clean the leading edge instead of helping only the trailing
  edge represented by forward flow.
- Measure F9/OFA GPU time before and after Line Cleaner at 1080p and 4K. Confirm
  Performance remains on the original light path and does not enable Cleaner.
- Resize, maximize, fullscreen and enter/leave PiP while OFA is active; confirm
  both generated and real frames preserve aspect ratio and overlays stay aligned.
- Enable sharpening/CRT and Disco RGB before starting OFA. Verify they pause,
  their saved values remain intact and they return after switching back to Off.
- Force an Optical Flow runtime failure if possible and confirm the same process
  continues with normal video/audio and displays a non-modal fallback notice.
- Compare F8 against an external sensor: it adds one source-frame interval for
  interpolation but remains an estimate, not a photon-to-photon measurement.
- Use F9 to record OFA time and GPU work for every quality level; check sustained
  temperatures and dropped frames during at least a 30-minute 4K session.
- Combine MJPEG and H.264 compatibility modes with OFA and verify decoder output
  conversion remains correctly colored before interpolation.
- Real capture input and audio from each supported device.
- MJPEG decoding at every advertised resolution/FPS, especially 4K60 or higher;
  record whether Media Foundation outputs NV12 or falls back to YUY2 and measure CPU.
- H.264 startup with both DXVA and the automatic software fallback, including a
  source whose first IDR arrives after two seconds but before the five-second grace.
- A decoder that reports a coded 1088-line frame with a 1080-line display aperture;
  verify correct color and cropping without a Phoenix loop.
- A compressed mode with a nonzero, even aperture offset; verify that cropped NV12
  and YUY2 frames stay inside the published buffer and show correct colors.
- Corrupt the first MJPEG frame, when possible, and verify capture continues; then
  force 30 consecutive failures and verify the mode is disabled until rescan.
- Change the compressed stream type after the first published frame and confirm a
  transient mismatch uses Phoenix while a deterministic first-format mismatch does not.
- Verify the compressed-path `F8` label explicitly excludes the capturer/decoder's
  complete internal delay and never presents the estimate as photon-to-photon.
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
14. Confirm compressed formats are hidden by default, enable compatibility and
    verify `MJPEG (compatibility)` / `H.264 (compatibility)` appear when advertised.
15. Connect a capture card after opening setup, press **Rescan**, and verify it is
    listed without restarting Dayaah Capture.
16. Run AutoTune with raw and compressed modes together; confirm raw wins. Repeat
    with a compressed-only device and confirm the compatibility warning is shown.
