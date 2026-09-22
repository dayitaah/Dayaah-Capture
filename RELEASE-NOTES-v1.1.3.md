# Dayaah Capture v1.1.3

Small stability and usability update based on the proven v1.1 threaded capture
path (not downloadable).

## Fixed

- Synthetic or duplicate mouse-move messages no longer keep the cursor visible
  when its physical screen coordinates did not change.

- Replaced the global `ShowCursor` counter with a private transparent cursor.
- The arrow no longer reappears over idle video when Windows sends a new
  `WM_SETCURSOR` message.
- The cursor remains visible over borders and after leaving or switching away
  from Dayaah Capture.
- Fullscreen did not reliably restore a previously maximized window.
- Repeated `Esc` presses could stop capture and leave the final frame visible.
- The setup window could leave too little space below the status line.

## Notes

- No capture, audio, frame-queue, or latency settings were intentionally changed.
- Existing `DayaahCapture.ini` files remain compatible.

Report regressions at <https://github.com/dayitaah/Dayaah-Capture/issues> and
attach `DayaahCapture.log`.
