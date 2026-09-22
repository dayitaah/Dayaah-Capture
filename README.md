# Dayaah Capture 1.1.3

**English** | [Español](README-ES.md)

Native, ultra-low-latency HDMI capture-card preview for Windows 10/11 x64.
No MPV, FFmpeg or installation required.

## What changed in 1.1.3

- Repeated synthetic `WM_MOUSEMOVE` messages no longer reset the cursor idle
  timer unless the physical screen coordinates actually changed.

- Replaced the unreliable global `ShowCursor` counter with a private transparent
  cursor owned by Dayaah Capture.
- Windows can no longer restore the arrow over the video while the cursor is
  supposed to be hidden.
- The cursor is restored when moving it, leaving the video, switching apps,
  resizing, or closing Dayaah Capture.
- Fullscreen now restores the previous maximized or normal window placement.
- `Esc` is non-destructive: it only leaves fullscreen or restores a maximized
  window. It never stops video or audio.
- Added vertical room to the initial setup window.


## Features

- Media Foundation raw NV12/YUY2 capture.
- D3D11 video conversion, scaling, and presentation.
- Dedicated render thread and latest-frame-only queue.
- Optional one-frame VSync or minimum-latency tearing mode.
- WASAPI audio in the same process.
- 0, +6, +12, +15, or +18 dB audio gain.

## Controls

| Control | Action |
| --- | --- |
| `F11` or double-click | Toggle fullscreen |
| `Esc` | Leave fullscreen / restore maximized window |
| `M` | Toggle mute |

The cursor hides after one second without movement over the video and returns
immediately when moved.

## Download and usage

Download the latest ZIP from [Releases](https://github.com/dayitaah/Dayaah-Capture/releases/latest),
extract it into its own folder, and run `DayaahCapture.exe`.

For a UGREEN 25173, a good starting point is `1920x1080 60 fps NV12`, its HDMI
audio input, and minimum-latency synchronization.

## Reporting bugs

Open a [GitHub Issue](https://github.com/dayitaah/Dayaah-Capture/issues) and
attach `DayaahCapture.log`. Include your capture-card model, selected video
mode, Windows version, and exact reproduction steps. Copy the log before
launching Dayaah Capture again because a new log is created on every run.

## Building

On Windows, run `src/build-msvc.bat` from an x64 Native Tools Command Prompt for
Visual Studio 2022. A MinGW cross-build helper is also included for maintainers.

Licensed under the MIT License. If Dayaah Capture helps you, you can support the
project on [Ko-fi](https://ko-fi.com/dayaah).
