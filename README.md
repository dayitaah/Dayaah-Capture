<p align="right"><strong>English</strong> · <a href="README.es.md">Español</a></p>

<p align="center">
  <img src="assets/dayaah-capture-icon.png" width="180" alt="Dayaah Capture">
</p>

<h1 align="center">Dayaah Capture -- Ultra-Low-Latency Capture Preview for Windows</h1>

<p align="center">
  Native ultra-low-latency HDMI capture preview for Windows.
</p>

<p align="center">
  <img alt="Version 1.0" src="https://img.shields.io/badge/version-1.0-53D7B2">
  <img alt="Windows 10 and 11" src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4">
  <img alt="C++17" src="https://img.shields.io/badge/C%2B%2B-17-00599C">
  <img alt="MIT license" src="https://img.shields.io/badge/license-MIT-53D7B2">
</p>

Dayaah Capture displays raw video directly from a compatible capture card, keeps only the newest frame, and presents it through D3D11. It does not require MPV, FFmpeg, an installer, or background services.

## Features

- Native capture through Windows Media Foundation.
- Raw NV12 and YUY2 modes with no recompression.
- D3D11 Video Processor color conversion and scaling.
- Single-frame, *latest frame wins* video queue.
- Minimum-latency mode with tearing allowed.
- Optional VSync with a maximum one-frame queue.
- WASAPI audio in the same process.
- Selectable amplification: 0, +6, +12, +15, or +18 dB.
- Automatic device, resolution, frame-rate, and format enumeration.
- Fullscreen mode, quick mute, and automatic cursor hiding.
- Persistent local INI configuration.

## Download and use

1. Open **Releases** on the repository page.
2. Download `Dayaah-Capture-1.0-Windows-x64.zip`.
3. Extract the complete folder.
4. Run `DayaahCapture.exe`.
5. Select your capture card, video mode, and audio input.

Recommended starting point for the UGREEN 25173:

```text
1920 x 1080 - 60 fps - NV12
HDMI (UGREEN 25173)
+15 dB
Minimum latency (tearing may occur)
```

## Compatibility

Dayaah Capture is not locked to a particular brand. It enumerates capture devices exposed by Windows through Media Foundation when they provide raw `NV12` or `YUY2` video.

Devices that expose only MJPEG, H.264, or another compressed format will not appear. This is intentional: it avoids hidden conversion stages and keeps latency predictable.

The capture card's audio must appear as a separate recording device in Windows. Audio is sent to the default playback device.

## Controls

| Control | Action |
| --- | --- |
| `F11` or double-click | Enter or leave fullscreen |
| `Esc` | Leave fullscreen or return to setup |
| `M` | Mute or restore audio |
| Move the mouse | Show the cursor; it hides after one second |

## Presentation modes

| Mode | Behavior |
| --- | --- |
| Minimum latency | Presents immediately; a tearing line may appear |
| VSync | Removes tearing; may add up to one refresh interval |

## Build on Windows

You need Windows 10/11 and **Visual Studio 2022 Build Tools** with Desktop development with C++ and a Windows SDK.

1. Open `x64 Native Tools Command Prompt for VS 2022`.
2. Enter the project directory.
3. Run:

```bat
build.bat
```

The executable will be written to `build\DayaahCapture.exe`.

Every push and pull request also starts a clean GitHub Actions build. The resulting executable can be downloaded from the **Windows build** workflow run.

## Technical design

```text
UVC capture card
  → Media Foundation Source Reader (asynchronous, low latency)
  → newest-frame buffer
  → NV12/YUY2 texture
  → D3D11 Video Processor
  → flip-discard swap chain

Capture-card audio
  → WASAPI capture
  → digital gain
  → WASAPI render
```

## Known limitations

- Windows x64 only.
- Raw NV12/YUY2 video only; no MJPEG or H.264.
- HDR is not implemented yet.
- Current conversion assumes limited-range BT.709 to full-range RGB.
- +15 and +18 dB may clip an already loud input signal.
- No recording, shaders, overlays, or frame generation.

## Privacy

Dayaah Capture works locally. It contains no telemetry, advertising, accounts, analytics, or network connections.

## Contributing

Bug reports and improvements are welcome in English or Spanish. Read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting a change.

## Support Dayaah Capture

If the app saved you from capture latency hell, you can support its development on Ko-fi:

<p>
  <a href="https://ko-fi.com/dayaah"><img alt="Support Dayaah on Ko-fi" src="https://img.shields.io/badge/Support%20Dayaah-Ko--fi-FF5E5B?logo=kofi&logoColor=white"></a>
</p>

## License

Released under the [MIT License](LICENSE). Copyright © 2026 Dayaah.
