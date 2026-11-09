# Dayaah Capture Changelog

**English** | [Español](CHANGELOG.ES.md)

## 2.2.0 

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

## 2.0.1 

- Fixed **Settings** by restarting into setup, fully terminating capture,
  audio, rendering and the residual final frame.

## 2.0.0 

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

## 1.4.0-beta1 

Adaptive sharpening, SDR color and lightweight CRT shaders; F6 bypass, F7 panel, persistent INI settings and direct-path fallback. Effects precede overlays.

## Version 1.3.0 — HDMI capture card viewer

- THPS4-inspired RGB disco mode.
- RGB disco mode is a hidden keyboard easter egg during capture. Its active
  state appears in the window title; unsupported hardware displays a clear notice.
- The context menu now focuses on day-to-day capture controls.
- The overlay editor labels its canvas as a viewer.
- Right-click context menu while capture is running.
- Live gain, mute, VSync, and input-range controls.
- Resolution, FPS, and raw-format switching with a controlled restart and
  automatic rollback when the device rejects the change.
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
