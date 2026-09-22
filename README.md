<p align="center">
  <img src="assets/dayaah-capture-icon.png" width="180" alt="Dayaah Capture">
</p>

<h1 align="center">Dayaah Capture</h1>

<p align="center">
  Vista previa nativa y de latencia ultrabaja para capturadoras HDMI en Windows.
</p>

<p align="center">
  <img alt="Version 1.1.3" src="https://img.shields.io/badge/version-1.1.3-53D7B2">
  <img alt="Windows 10 y 11" src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4">
  <img alt="C++17" src="https://img.shields.io/badge/C%2B%2B-17-00599C">
  <img alt="Licencia MIT" src="https://img.shields.io/badge/license-MIT-53D7B2">
</p>


# Dayaah Capture

**English** · [Español](README.es.md)

Native ultra-low-latency HDMI capture preview for Windows 10/11 x64.

Dayaah Capture reads raw video directly from a compatible capture card, keeps
only the newest frame, and presents it through D3D11. Capture-card audio is
played through WASAPI in the same process. No MPV, FFmpeg, installer, account,
or background service is required.

The current stable release is **v1.1.3**.

## Features

- Native capture through Windows Media Foundation.
- Raw NV12 and YUY2 video with no recompression.
- D3D11 Video Processor color conversion and scaling.
- Dedicated render thread separated from the window interface.
- Latest-frame-only queue to prevent latency from accumulating.
- Minimum-latency presentation mode with tearing allowed.
- Optional VSync with a maximum one-frame queue.
- WASAPI audio capture and playback in the same process.
- Selectable audio gain: 0, +6, +12, +15, or +18 dB.
- Automatic enumeration of devices, resolutions, frame rates, and formats.
- Fullscreen mode, quick mute, and reliable automatic cursor hiding.
- Local INI configuration that remembers the last selected options.

## Download and use

1. Open the [latest release](https://github.com/dayitaah/Dayaah-Capture/releases/latest).
2. Download the Windows x64 ZIP.
3. Extract the complete folder.
4. Run `DayaahCapture.exe`.
5. Select your capture card, video mode, audio input, gain, and presentation
   mode.
6. Click **INICIAR CAPTURA**.

Recommended starting point for the UGREEN 25173:

```text
1920 x 1080 - 60 fps - NV12
HDMI (UGREEN 25173)
+15 dB
Minimum latency (tearing may occur)
```

## Compatibility

Dayaah Capture is not locked to one capture-card brand. It lists video devices
exposed by Windows Media Foundation when they provide raw `NV12` or `YUY2`
output.

Devices that expose only MJPEG, H.264, or another compressed format will not be
listed. Staying on the raw path avoids hidden decode and conversion stages and
keeps latency predictable.

The capture card's audio must appear as a separate recording device in Windows.
Audio is sent to the current default playback device.

Only one application can use some capture devices at a time. Close OBS, camera
applications, browser capture pages, or other viewers if Dayaah Capture cannot
open the card.

## Controls

| Control | Action |
| --- | --- |
| `F11` or double-click | Enter or leave fullscreen |
| `Esc` | Leave fullscreen or restore a maximized window |
| `M` | Mute or restore audio |
| Move the mouse | Show the cursor; it hides after one second of real inactivity |

## Presentation modes

| Mode | Behavior |
| --- | --- |
| Minimum latency | Presents immediately; a tearing line may appear |
| VSync | Removes tearing; may add up to one display refresh interval |

## What's new in v1.1.3

- Duplicate or synthetic mouse-move messages no longer prevent the cursor from
  hiding.
- Fullscreen reliably restores the previous normal or maximized window state.
- `Esc` no longer stops video or audio after repeated presses.
- The initial window has enough vertical space for the complete status line.

See [CHANGELOG.md](CHANGELOG.md) for the complete release notes.

## Technical design

```text
UVC capture card
  -> Media Foundation Source Reader (asynchronous, low latency)
  -> newest-frame buffer
  -> NV12/YUY2 D3D11 texture
  -> D3D11 Video Processor
  -> flip-model swap chain

Capture-card audio
  -> WASAPI capture
  -> digital gain
  -> WASAPI render
```

The video queue never grows: a newly received frame replaces any older frame
that has not been presented yet. This is the central rule that prevents the
preview from slowly falling behind the live signal.

## Build on Windows

You need Windows 10/11, Visual Studio 2022 Build Tools, **Desktop development
with C++**, and a Windows SDK.

1. Open **x64 Native Tools Command Prompt for VS 2022**.
2. Enter the project directory.
3. Run `src\build-msvc.bat`.

The resulting executable is written to the project root as
`DayaahCapture.exe`. A MinGW cross-build helper is also included for
maintainers.

## Reporting bugs

Open a [GitHub Issue](https://github.com/dayitaah/Dayaah-Capture/issues) and
attach `DayaahCapture.log`. Include:

- Dayaah Capture version.
- Windows version and GPU.
- Capture-card model and connection type.
- Selected resolution, frame rate, and pixel format.
- Exact steps needed to reproduce the problem.

Copy the log before launching Dayaah Capture again because a new log is created
on every run.

## Current constraints

- Windows x64 only.
- Raw NV12/YUY2 input only.
- The current SDR conversion uses limited-range BT.709 input and full-range RGB
  output.
- Audio uses the Windows default playback device.
- +15 and +18 dB may clip an already loud input signal.

## Privacy

Dayaah Capture works locally. It contains no telemetry, advertising, accounts,
analytics, or network connections.

## Contributing

Bug reports and improvements are welcome in English or Spanish. Read
[CONTRIBUTING.md](CONTRIBUTING.md) before submitting a change.

## Support Dayaah Capture

If Dayaah Capture saved you from capture-latency hell, you can support its
development on [Ko-fi](https://ko-fi.com/dayaah).

## License

Released under the [MIT License](LICENSE). Copyright © 2026 Dayaah.
