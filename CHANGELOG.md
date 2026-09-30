# Dayaah Capture Changelog

**English** | [Español](CHANGELOG.ES.md)

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
