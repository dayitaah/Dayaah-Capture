<p align="center">
  <img src="assets/dayaah-capture-icon.png" width="180" alt="Dayaah Capture">
</p>

<h1 align="center">Dayaah Capture</h1>

<p align="center">
  Native ultra-low-latency HDMI capture card viewer for Windows 10/11 x64.
</p>

<p align="center">
  <img alt="Version 2.0.1" src="https://img.shields.io/badge/version-2.0.1-53D7B2">
  <img alt="Windows 10 y 11" src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4">
  <img alt="C++17" src="https://img.shields.io/badge/C%2B%2B-17-00599C">
  <img alt="Licencia MIT" src="https://img.shields.io/badge/license-MIT-53D7B2">
</p>


# Dayaah Capture

**English** · [Español](README.es.md)

Native ultra-low-latency HDMI capture card viewer for Windows 10/11 x64.

Dayaah Capture reads raw video directly from a compatible capture card, keeps
only the newest frame, and presents it through D3D11. Capture-card audio is
played through WASAPI in the same process. No MPV, FFmpeg, installer, account,
or background service is required.

The current stable release is **v2.0.1**.

## Features

- Native capture through Windows Media Foundation.
- Raw NV12,YUY2,XRGB,P010 video with no recompression.
- I420 video uses CPU into NV12
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
exposed by Windows Media Foundation when they provide raw NV12, YUY2, XRGB, I420 and P010
output.

Devices that expose only MJPEG, H.264, or another compressed format will not be
listed. Staying on the raw path avoids hidden decode and conversion stages and
keeps latency predictable.

The capture card's audio must appear as a separate recording device in Windows.
Audio is sent to the current default playback device.

Only one application can use some capture devices at a time.

## Controls

| Control | Action |
| --- | --- |
| Right-click | Open the context menu |
| `F6` | Toggle video effects |
| `F7` | Open video-effect settings |
| `F8` | Toggle approximate latency |
| `F10` | Toggle overlays |
| `F11` or double-click | Toggle fullscreen |
| `Esc` | Leave fullscreen or restore the window |
| `M` | Toggle audio mute |
| Move the mouse | Show the cursor; it hides after one second of real inactivity |


The hidden RGB effect displays its active state in the window title and reports
when the GPU cannot support the required hue filter.
Type DISCO to activate it while the viewer is running

Open the editor from the context menu. Its canvas is a viewer for arranging
layers. Internal statistics cover received and presented FPS, discarded frames,
resolution/format, sync mode, and last-frame age.

The layout automatically saves to `DayaahCapture.overlays.json` next to the EXE
and reloads at startup. Keep that file when updating and retain PNGs at their
original paths. Opacity and size update while typing; closing the editor returns
focus to capture without changing its window state.

## Video formats

| Format | Path | Note |
| --- | --- | --- |
| NV12 | Direct GPU | Recommended for low bandwidth |
| YUY2 | Direct GPU | Uses more bandwidth than NV12 |
| XRGB | Direct GPU | 4 bytes per pixel; potentially high penalty |
| I420 | CPU conversion to NV12 | May increase CPU load |
| P010 | 10-bit GPU | Limited compatibility; does not enable HDR by itself |

Only modes advertised by the capture device are shown. If a live change is
rejected, the application attempts to restore the previous mode.

## Presentation modes

| Mode | Behavior |
| --- | --- |
| Minimum latency | Presents immediately; a tearing line may appear |
| VSync | Removes tearing; may add up to one display refresh interval |

## What is included in new versions

## 2.0.1 fix

- **Settings** now performs a clean process restart into setup, removing the
  final video frame and completely closing the previous audio path.

## New in 2.0.0

- Setup video modes are split into resolution, FPS and format selectors.
- NV12, YUY2, XRGB, I420 and P010 raw formats with performance/compatibility notices.
- Full English and Spanish interface selector.
- Machine-local first run: after one successful capture, later launches go
  directly to the viewer. Copying the ZIP and INI to another PC shows setup again.
- Ko-fi and GitHub links at the bottom of setup.
- Stronger adaptive sharpening with a visibly useful range and local halo limiting.
- Approximate latency HUD on `F8`.
- Centered crosshair in the overlay editor.
- A **Settings** context-menu command to return to setup.

The device fingerprint is stored only as a local hash in `DayaahCapture.ini`.
It is not transmitted, tied to an account, or used as DRM.

See [CHANGELOG.md](CHANGELOG.md) for the complete release notes.

## Technical design

```text
UVC capture card
  -> Media Foundation Source Reader (asynchronous, low latency)
  -> newest-frame buffer
  -> NV12/YUY2/XRGB/I420/P010 D3D11 texture
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
- +15 and +18 dB may clip an already loud input signal.
- Not tested HDR Compatibility or VRR

## Privacy

Dayaah Capture works locally. It contains no telemetry, advertising, accounts,
analytics, or network connections.

## Contributing

Bug reports and improvements are welcome in English or Spanish. Read
[CONTRIBUTING.md](CONTRIBUTING.md) before submitting a change.

## Support Dayaah Capture

If Dayaah Capture saved you from capture-latency hell, you can support its
development on Ko-fi:

<p>
  <a href="https://ko-fi.com/dayaah"><img alt="Support Dayaah on Ko-fi" src="https://img.shields.io/badge/Support%20Dayaah-Ko--fi-FF5E5B?logo=kofi&logoColor=white"></a>
</p>

## License

Released under the [MIT License](LICENSE). Copyright © 2026 Dayaah.
