# Dayaah Capture Changelog

**English** | [Español](CHANGELOG.ES.md)

## 3.4.1 BETA — 2026-11-16

The recommended stable release remains **2.2.0**.

- Fixed live capture-mode changes such as 1080p30 → 1080p60 failing with
  `E_ACCESSDENIED` after NVIDIA VFG had been active.
- Renderer shutdown now releases every backbuffer reference, clears and flushes
  the D3D11 context, and waits up to 500 ms for queued GPU work before releasing
  the old flip-model swap chain.
- Added bounded DXGI retries for the short interval in which a driver may still
  own the previous swap chain for the same window, with exact diagnostic logs.

## 3.4.0 BETA — 2026-11-16

The recommended stable release remains **2.2.0**.

- Added AMD AMF Frame Rate Conversion as a third independent 2× backend beside
  NVIDIA OFA and NVIDIA VFG.
- Added Performance, Quality and Maximum profiles with stable INI values 7–9,
  mapped to AMF Low/Performance, High/Native and Super/Native.
- Added direct D3D11 BGRA integration: the Video Processor feeds AMF textures
  and the component returns interpolated-only output while preserving the
  existing generated → real order and pacing.
- Dynamically loads `amfrt64.dll` from the installed AMD driver; no proprietary
  AMD DLL is bundled. Missing or unsupported AMF/FRC falls back safely to normal
  capture.
- Disabled `FutureFrame` to avoid a third frame of latency and enabled blended
  fallback only for AMF low-confidence regions.
- Hybrid systems now explicitly select a Radeon adapter for AMF while retaining
  explicit NVIDIA adapter selection for OFA/VFG.
- Bundled the official AMF 1.5.3 interface headers under their MIT license and
  extended mode-persistence/mapping tests for the new backend.
- Fixed NVIDIA VFG by keeping its D3D11 textures RGBA8 end to
  end, avoiding `NvCVImage_Transfer -9` when copying CUDA RGBA8 output into an
  unsupported BGRA8 destination.

## 3.3.0 BETA — 2026-11-16

The recommended stable release remains **2.2.0**.

- Added NVIDIA Video Frame Generation as a separate optional backend with Low,
  Medium and High neural models. It generates one midpoint (`t=0.5`) for 2×
  presentation while keeping the existing OFA modes unchanged.
- Added a D3D11↔CUDA bridge using NVIDIA NvCVImage and synchronous VFG execution
  so source surfaces are not reused while the model is reading them.
- Kept the regular package independent from proprietary VFG DLLs. A separately
  labeled NVIDIA-VFG package includes the official unmodified VFX 0.2.0.0
  runtime and its NVIDIA licenses; missing/unsupported VFG falls back safely.
- Preserved all existing INI values: OFA remains 1–3 and VFG uses 4–6.

## 3.2.4 BETA — 2026-11-16

The recommended stable release remains **2.2.0**.

- Removed the hard per-pixel selection introduced in 3.2.3: hardware testing
  showed that it exposed the 2×2 confidence grid as large blocks in every
  generated frame. Real frames remained complete and the log stayed clean.
- Forward and backward now resolve to one symmetric path used to project both
  source frames. When they disagree the lower-cost field wins; ties consistently
  keep backward.
- Confidence no longer selects pieces from different images. It continuously
  attenuates the resolved vector and resamples both sources along the same path,
  avoiding rectangular boundaries and four-contour color crossfades.
- Diagnostics 1–8 remain for reproducibility. The recommended stable release
  continues to be 2.2.0.

## 3.2.3 BETA — 2026-11-16

The recommended stable release remains **2.2.0**.

- Removed the temporal crossfade between fallback and warp that softened
  generated frames and made hair and fine detail flicker when
  alternating with real frames during 30→60 conversion.
- Forward/backward projections are averaged only when their colors agree. When
  they disagree, the projection with clearly lower OFA cost wins; ties prefer
  the current frame to prevent candidate changes between pixels and frames.
- Regions that fail consistency, photometric or cost validation locally repeat
  the current source instead of blending two uncompensated contours. Only the
  ambiguous area loses interpolation in exchange for stable sharp detail.
- Telemetry and heat maps retain the same cause classification; diagnostics
  1–8 remain available and disabled by default.

## 3.2.2 BETA — 2026-11-16

The recommended stable release remains **2.2.0**.

- Removed the second forward/backward lookup from already displaced positions.
  With 20/40/80-pixel motion it could jump from the object to the background and
  replace a correct vector with one belonging to another surface.
- Normal reconstruction now uses the first OFA pair directly; modes 7/8
  validated its S10.5 scale, sign, magnitude and cost.
- Photometry, consistency, cost and Dayaah Line Cleaner remain as downstream
  safeguards. Pacing and presentation are unchanged.
- Diagnostics 7/8 remain available but are disabled by default.

## 3.2.1 BETA — 2026-11-16

The recommended stable release remains **2.2.0**. The 3.2.0 sequence
5/6/1/2/4/3 confirmed that both source frames are intact and fully serializing
OFA does not remove the corruption. The remaining scope is the flow fields and
warp reconstruction.

- Added `DAYAAH_FG_BYPASS=7`, a persistent 2×2 atlas of forward/backward flow
  and both cost maps. Direction is encoded as color and magnitude as brightness;
  static regions should remain black.
- Added `DAYAAH_FG_BYPASS=8`, comparing forward-only and backward-only
  reconstruction in one image without the second lookup, stabilizer or Line Cleaner.
- Both modes consume stable copies and remain visible during the real Present,
  avoiding rapid alternation between video and diagnostics.
- Added one-second `FGFLOW` logging with X/Y ranges, mean/maximum magnitude,
  percentages above 1 px and 128 px, and mean/maximum OFA cost.
- Modes 1–6 remain compiled for reproducibility, but the new hardware pass only
  requires values 7 and 8.

## 3.2.0 BETA — 2026-11-15

The recommended stable release remains **2.2.0**. This diagnostic beta isolates
the rectangular corruption observed only on NVIDIA OFA-generated frames.

- Added `DAYAAH_FG_BYPASS=1..6` with an explicit startup log: hard early
  crossfade, OFA-free crossfade, three-source rotation, fully serialized OFA,
  previous-only and current-only generated-frame paths.
- The early bypasses return before reading flow, cost maps or Line Cleaner, so
  their output cannot be contaminated by the normal warp shader.
- Bypass 2 preserves the exact source-index rotation while omitting
  `nvOFExecute`, separating source conversion from OFA interaction.
- Bypass 3 registers and rotates three real OFA source textures, including all
  Video Processor output views and resource slots, instead of merely changing
  an array size.
- Bypass 4 serializes Video Processor→OFA, OFA→shader and draw→resource-reuse
  boundaries. It consumes and copies the complete forward/backward flow and
  both cost outputs, waits on D3D11 event queries and renders from stable copies.
- Bypass 4 automatically logs average/maximum wait times once per second. Its
  deliberate GPU copies and CPU waits are diagnostic and not a production path.
- Normal launches with no bypass preserve the 3.1.2 rendering path.

## 3.1.2 BETA — 2026-11-15

The recommended stable release remains **2.2.0**. This beta refines Dayaah Line
Cleaner after the first 3.1.0 hardware-facing implementation.

- Expresses neighbor and rescue searches in physical pixels instead of raw OFA
  cells. Balanced and Quality now inspect comparable distances: about four
  pixels nearby and up to 32 pixels for a bounded disocclusion rescue.
- Evaluates candidates from both forward and backward flow. This covers leading
  object edges whose useful motion exists only in the return field.
- Stops averaging incompatible object/background vectors. When forward and
  backward disagree beyond tolerance, the lower-cost field wins; without a cost
  map, both directed alternatives are evaluated photometrically.
- Prevents the original consistency mismatch from vetoing a cleaner-selected
  candidate that already passes the photometric check. OFA cost and photometry
  can still reject it.
- Raises neighbor preference margins above ordinary capture noise to reduce
  vector-choice flicker. The Line Cleaner gate is now tunable through the ninth
  and tenth `DAYAAH_FG_TUNE` values.

## 3.1.0 BETA — 2026-11-15

The recommended stable release remains **2.2.0**. This beta focuses on the
quality and pacing of the optional NVIDIA OFA interpolation introduced in 3.0.0.

- Reworked motion confidence so valid optical flow is no longer rejected by a
  fixed pixel threshold during fast pans. Forward/backward tolerance now scales
  with motion magnitude.
- Added a photometric check, an OFA cost knee and a steeper final confidence
  curve. Unreliable regions still fall back locally without turning most valid
  generated frames into a ghosted crossfade.
- Added **Dayaah Line Cleaner** to Balanced and Quality. It detects motion
  boundaries, compares nearby forward/backward vectors against both source
  frames and uses one symmetric consensus vector to keep object edges together.
- Line Cleaner also performs a bounded second search for newly uncovered
  background before falling back. Performance mode bypasses it to preserve the
  lighter shader path.
- Replaced the No VSync relative `Sleep` pacing with an absolute QPC target and
  a high-resolution waitable timer followed by a short final spin.
- Draws the real frame before waiting and delays only its `Present`, preventing
  shader and overlay work from shifting the requested midpoint cadence.
- Added opt-in `DAYAAH_FG_TELEMETRY=1` logging for submitted/visible presents,
  vblanks, pacing gaps, OFA/warp GPU timing and warp-versus-fallback coverage.
- Added `DAYAAH_FG_DEBUG=1/2` heat maps and `DAYAAH_FG_TUNE` runtime thresholds
  for hardware testing without recompiling. Debug mode `3` highlights Line
  Cleaner pixels in cyan; `DAYAAH_FG_LINE_CLEANER=0` provides an A/B comparison.
- Logs a failed `SetMaximumFrameLatency(1)` call instead of silently assuming a
  one-frame DXGI queue.

## 3.0.0 BETA — 2026-11-14

The recommended stable release remains **2.2.0**. This beta needs acceptance
testing on real NVIDIA GPUs, drivers, capture modes and high-refresh displays.

- Added optional NVIDIA OFA Frame Generation that inserts one motion-compensated
  midpoint for up to 2× presentation FPS. It is a video interpolator, not DLSS.
- Added Performance, Balanced and Quality selectors. They request 4×4/Fast,
  2×2/Medium and 1×1/Slow optical-flow grids respectively; Balanced and Quality
  use bidirectional confidence-cost maps when the driver supports them.
- Converts the selected capture path to RGBA surfaces for OFA, computes forward
  and backward flow, then reconstructs the midpoint with a bounded D3D11 shader.
- Detects the driver-installed `nvofapi64.dll`, supported formats and grid sizes
  at runtime. Unsupported hardware or a runtime OFA error falls back to the
  normal renderer without stopping capture or audio.
- Negotiates either B8G8R8A8 or R8G8B8A8 directly from the D3D11 OFA driver
  instead of assuming one RGB byte order, and logs every advertised input,
  output and cost surface when negotiation still fails.
- Keeps NVIDIA's runtime DLL out of the package. Only the MIT-licensed Optical
  Flow interface headers are included in source, with a third-party notice.
- Draws overlays, crosshair and telemetry after both generated and real frames.
  The F8 HUD includes the interpolation interval and the F9 scope reports OFA time.
- Stabilizes generated frames with bilinear flow-field sampling, a subpixel-motion
  dead zone, bounded displacement and forward/backward consistency rejection.
  Unreliable regions fall back locally to a plain blend instead of shaking or
  stretching the complete image.
- Pauses video effects and Disco RGB while Frame Generation is active, preserving
  the saved values for the normal renderer.
- Documents that interpolation adds approximately one source-frame interval and
  that a display running near twice the source rate is recommended.

## 2.5.0 BETA — 2026-11-11

The recommended stable release remains **2.2.0**. This beta broadens device
compatibility and requires real-world testing across capture cards and drivers.

- Enumerates MJPEG and H.264 alongside raw formats without hiding devices that
  advertise compressed video only.
- Adds an opt-in compatibility checkbox, disabled by default, with explicit
  latency and CPU/GPU warnings for non-raw formats.
- Fixes the compatibility label under the dark theme and compacts the rescan
  button and format notices so translated text cannot clip or overlap controls.
- Adds a device/audio rescan button that does not require restarting the app.
- Decodes MJPEG/H.264 through Media Foundation to NV12 with YUY2 fallback.
- Attempts DXVA for H.264 and automatically falls back to software decoding
  when accelerated buffers cannot be consumed with the current driver.
- Safely handles stride, coded size, display aperture, NV12/YUY2 cropping,
  buffer bounds and decoder output-type changes.
- Publishes decoded format metadata with each frame so pixels and descriptors
  cannot come from different output formats.
- Adds a five-second compressed startup window, consecutive-invalid-sample
  handling and deterministic-failure separation from Phoenix Recovery.
- AutoTune always prefers raw video, uses compressed modes only as fallback and
  no longer estimates unknown formats as eight bytes per pixel.
- Marks compressed latency HUD readings as incomplete because device encoding
  and internal decoder delay cannot be isolated by the Source Reader.

## 2.2.0 — 2026-11-07

This is the single cumulative update after 2.0.1; there are no separate 2.1.x
entries to follow.

- Added Picture in Picture on `F12`: a borderless, always-on-top, movable and
  resizable viewer with persistent geometry. It reuses the existing capture,
  pauses video effects to save GPU work and restores them exactly on exit.
- Overlays, crosshair, audio and telemetry HUDs remain available in PiP. `Esc`
  leaves PiP first, and double-clicking PiP switches to fullscreen.
- Renamed the fast path to **Minimum latency (No VSync - VRR)**. It negotiates
  tearing support and uses `DXGI_PRESENT_ALLOW_TEARING` when Windows allows it.
- Added Dayaah AutoTune with advertised-mode scoring and five-second live
  validation using pacing, drops, GPU work and presentation measurements.
- Made AutoTune, live mode changes and Phoenix tolerate capture drivers that
  release their video pin a little late, with a short restart grace period,
  bounded busy-device retries and exact startup-stage logging.
- Added the latest-frame Latency Governor, pre-GPU stale-frame replacement and
  discard telemetry.
- Added the `F9` Frame Pacing Scope with intervals, jitter, queue depth, GPU and
  presentation timing, plus a 120-frame graph.
- Added smooth slide/fade notices and a 59.94/60 Hz VSync cadence warning.
- Added Phoenix Recovery for stalls, reader errors and live format changes,
  bounded to three attempts. A final failure returns to Settings through the
  safe replacement-process path without residual audio or a frozen last frame.
- Isolated every Source Reader callback with a capture-session ID. A callback
  from an old session can no longer submit a frame, report an error, signal the
  new flush event or start a second `ReadSample` chain on the new reader.
- Closed the Source Reader lifetime race with a small lock, a local `AddRef`
  around `ReadSample`, and ordered `Flush`, callback detach and COM release.
- Replaced per-frame temporary vectors and duplicate pixel copies with reusable
  producer buffers, row-aware `Lock2D`, a contiguous fallback and the existing
  flat-lock compatibility path. I420 keeps only its required CPU interleave.
- Normal launches rotate `DayaahCapture.log` to `.log.1`. Internal restarts
  preserve the active log, append a separator and record the Phoenix reason.
- Prevented the `M` mute hotkey from repeating while held.
- Removed model-specific UGREEN and generic HDMI first-run defaults. Unsaved
  setups now select the first video device and leave audio disabled.
- Overlay profiles now validate schema 1, keep defaults for missing optional
  fields and preserve an invalid original as `.bad` before overwriting it.
- Bounded PNG decoding to 8192 pixels per side and 32 million total pixels,
  handled allocation failure, and cleared resize-dependent text formats.
- Hardened UTF-8 logging, removed an unused audio lock and linked the MSVC
  build to the static runtime with `/MT`.

## 2.0.1 — 2026-10-04

- Fixed **Settings** by restarting into setup, fully terminating capture,
  audio, rendering and the residual final frame.

## 2.0.0 — 2026-10-04

- Reworked video-mode setup into resolution, FPS and format selectors.
- Added XRGB, I420 and P010 beside NV12/YUY2, including cost/compatibility notices.
- Added selectable English and Spanish UI.
- Added GitHub and Ko-fi links to setup.
- Added machine-local first-run tracking and direct viewer startup after a
  successful capture.
- Strengthened adaptive sharpening for a visible result.
- Added an `F8` approximate-latency HUD responsive to FPS, frame age and VSync.
- Added a centered crosshair to the overlay editor.
- Added a Settings command to the capture context menu.

## 1.4.0-beta1 — 2026-10-02

Adaptive sharpening, SDR color and lightweight CRT shaders; F6 bypass, F7 panel, persistent INI settings and direct-path fallback. Effects precede overlays.

## Version 1.3.0 — HDMI capture card viewer

- THPS4-inspired RGB disco mode.
- RGB disco mode is a hidden keyboard easter egg during capture. Its active
  state appears in the window title; unsupported hardware displays a clear notice.
- The context menu now focuses on day-to-day capture controls.
- The overlay editor labels its canvas as a viewer.
- Fixed Windows CI linking for the overlay editor and corrected x64 control-ID
  casts that caused MSVC warning C4312.
- Right-click context menu while capture is running.
- Live gain, mute, VSync, and input-range controls.
- Resolution, FPS, and raw-format switching with a controlled restart and
  automatic rollback when the device rejects the change.
- Separate overlay editor with text, 12/24-hour clock, PNGs, statistics,
  drag/resize, styling, anchors, and JSON profiles.
- Global overlay toggle in the context menu or with `F10`.
- Automatic overlay save/restore and corrected loading of profiles containing text.
- Live opacity/size input and editor close that returns focus to capture.

## Version 1.2.0 beta — Context menu

- Right-click context menu while capture is running.
- Live gain, mute, VSync, and input-range controls.
- Resolution, FPS, and raw-format switching with a controlled restart and
  automatic rollback when the device rejects the change.
- THPS4-inspired RGB disco mode.
- Separate overlay editor with text, 12/24-hour clock, PNGs, statistics,
  drag/resize, styling, anchors, and JSON profiles.
- Global overlay toggle in the context menu or with `F10`.
- Automatic overlay save/restore and corrected loading of profiles containing text.
- Live opacity/size input and editor close that returns focus to capture.

## Version 1.1.3

Stability and usability update based on the proven v1.1 threaded capture path.

### Fixed

- Synthetic or duplicate mouse-move messages no longer keep the cursor visible
  when its physical screen coordinates did not change.
- Replaced the global `ShowCursor` counter with a private transparent cursor.
- Windows can no longer restore the arrow over the video while the cursor is
  supposed to remain hidden.
- The cursor remains visible over window borders and when leaving or switching
  away from Dayaah Capture.
- Fullscreen now correctly restores a previously maximized window.
- Repeated `Esc` presses no longer stop capture or leave the final frame frozen.
- The setup window now leaves enough room below the status line.

### Notes

- Capture, audio, frame-queue, and latency settings were intentionally left
  unchanged.
- Existing `DayaahCapture.ini` files remain compatible.

If you find a regression, open a
[GitHub Issue](https://github.com/dayitaah/Dayaah-Capture/issues) and attach
`DayaahCapture.log`.
