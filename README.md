<p align="right"><a href="LEEME.md">Español</a> · <strong>English</strong></p>
<p align="center">
  <img src="src/DayaahCapture-icon.png" width="180" alt="Dayaah Capture">
</p>

<h1 align="center">Dayaah Capture</h1>

<p align="center">
  Native ultra-low-latency HDMI capture card viewer for Windows 10/11 x64.
</p>

<p align="center">
  <img alt="Version 3.4.1 Beta" src="https://img.shields.io/badge/version-3.4.1%20BETA-D89C36">
  <img alt="Windows 10 and 11" src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4">
  <img alt="C++17" src="https://img.shields.io/badge/C%2B%2B-17-00599C">
  <img alt="MIT license" src="https://img.shields.io/badge/license-MIT-53D7B2">
</p>


# Dayaah Capture

**English** · [Español](LEEME.md)

Native ultra-low-latency HDMI capture card viewer for Windows 10/11 x64.

Dayaah Capture reads raw video directly from a compatible capture card, keeps
only the newest frame, and presents it through D3D11. Capture-card audio is
played through WASAPI in the same process. No MPV, FFmpeg, installer, account,
or background service is required.

This source package is **3.4.1 Beta**. The recommended stable release is
**2.2.0**. For beta releases and feedback, contact `@dayaah` on Discord.

## Features

- Native capture through Windows Media Foundation.
- Raw NV12, YUY2, XRGB, I420 and P010, with optional MJPEG/H.264 compatibility.
- D3D11 Video Processor for color conversion and scaling.
- Dedicated render thread and a latest-frame-only capture buffer.
- Minimum latency with tearing/VRR support, plus optional VSync.
- WASAPI audio capture and playback, with 0, +6, +12, +15 or +18 dB gain.
- Dayaah AutoTune, Latency Governor and Phoenix Recovery.
- Frame Pacing Scope with a 120-frame graph and an approximate latency HUD.
- Picture in Picture, fullscreen, quick mute and automatic cursor hiding.
- Local settings, overlays, crosshair and optional video effects.
- Experimental 2× video interpolation with NVIDIA OFA, NVIDIA VFG or AMD AMF FRC.

## Download and use

1. For the stable build, open the [latest release](https://github.com/dayitaah/Dayaah-Capture/releases/latest).
   If you already have the beta, use its regular Windows x64 ZIP, or the
   separately labeled `NVIDIA-VFG` ZIP to try NVIDIA Video Frame Generation.
2. Extract the complete Windows ZIP into its own folder.
3. Run `DayaahCapture.exe`.
4. Select your capture card, video mode, audio input, gain and presentation mode.
5. Click **Start capture**.

Recommended starting point for the UGREEN 25173:

```text
1920 x 1080 - 60 fps - NV12
HDMI (UGREEN 25173)
+15 dB
Minimum latency (No VSync - VRR)
```

## Compatibility

Dayaah Capture lists devices exposed by Windows Media Foundation. Raw video is
the default. Enable **MJPEG/H.264 (compressed compatibility)** to see those
modes, including on cards that expose only compressed output. **Rescan**
refreshes capture devices, audio inputs and modes without restarting.

The capture card's audio must appear as a Windows recording device. Audio is
sent to the default playback device. Some cards can be used by only one
application at a time.

## Controls

| Control | Action |
| --- | --- |
| Right-click | Open the context menu |
| `F6` | Toggle video effects |
| `F7` | Open video-effect settings |
| `F8` | Toggle approximate latency |
| `F9` | Toggle the Frame Pacing Scope |
| `F10` | Toggle overlays |
| `F11` or double-click | Toggle fullscreen |
| `F12` | Enter or leave Picture in Picture |
| `Esc` | Leave PiP, leave fullscreen or restore the window |
| `M` | Toggle audio mute |
| Move the mouse | Show the cursor; it hides after one second of real inactivity |


The hidden RGB effect displays its active state in the window title and reports
when the GPU cannot support the required hue filter.
Type `DISCO` to activate it while the viewer is running.

Open the editor from the context menu. Its canvas is a viewer for arranging
layers. Internal statistics cover received and presented FPS, discarded frames,
resolution/format, sync mode, and last-frame age.

The layout automatically saves to `DayaahCapture.overlays.json` next to the EXE
and reloads at startup. Keep that file when updating and retain PNGs at their
original paths. Opacity and size update while typing; closing the editor returns
focus to capture without changing its window state.

`F8` estimates latency from capture timing, frame age and presentation.
It cannot include all encoder and decoder delay for MJPEG/H.264.

## Video formats

| Format | Path | Note |
| --- | --- | --- |
| NV12 | Direct GPU | Recommended for low bandwidth |
| YUY2 | Direct GPU | Uses more bandwidth than NV12 |
| XRGB | Direct GPU | 4 bytes per pixel; potentially high penalty |
| I420 | CPU conversion to NV12 | May increase CPU load |
| P010 | 10-bit GPU | Limited compatibility; does not enable HDR by itself |
| MJPEG | Media Foundation decoder | Compressed; may add CPU work and latency |
| H.264 | Media Foundation with DXVA or software decoding | Compressed; latency varies by device |

Only modes advertised by the capture device are shown. AutoTune prefers raw
video and uses compressed modes when no raw option is usable. If a live mode
change fails, Dayaah Capture tries to restore the previous mode.

## Presentation modes

| Mode | Behavior |
| --- | --- |
| Minimum latency (No VSync - VRR) | Presents without VSync and allows tearing when supported. VRR requires a compatible display and GPU with VRR enabled. |
| VSync | Synchronizes presentation to the display refresh; can add presentation delay. |

## What is included in new versions

### 3.4.1 Beta

- Fixed live capture-mode changes such as 1080p30 → 1080p60 that could fail with
  `E_ACCESSDENIED` after using NVIDIA VFG. The renderer releases the previous
  DXGI swap chain with a bounded GPU wait and short retries when needed.
- Includes optional MJPEG/H.264 support and the three experimental
  interpolation backends introduced in previous betas.

| Frame Generation backend | Options | Requirements |
| --- | --- | --- |
| NVIDIA OFA | Performance, Balanced, Quality | Compatible NVIDIA Turing or newer GPU and a driver with `nvofapi64.dll` |
| NVIDIA VFG | Low, Medium, High | Compatible NVIDIA Ada or Blackwell GPU and driver. The separate `NVIDIA-VFG` ZIP includes NVIDIA VFX 0.2.0.0 under `runtime/nvidia-vfg` with its licenses. |
| AMD AMF FRC | Performance, Quality, Maximum | Compatible Radeon GPU and driver with `amfrt64.dll` and FRC support |

Frame Generation creates one intermediate frame per pair for up to 2×
presentation FPS. It adds about one source-frame interval. Use **Off** for
the lowest latency, and a 120 Hz or faster display for 60 → 120 FPS. If the
selected backend is unavailable, capture continues normally. Interpolation
pauses video effects and Disco RGB while keeping audio, overlays and the
crosshair available.

The regular ZIP and Source ZIP omit the proprietary VFG runtime. For a
first 30 → 60 FPS test with the NVIDIA-VFG package, start with **Medium**.

### 2.2.0 (stable)

- Added F12 Picture in Picture: borderless, always on top, movable and resizable, with persistent size and position. Effects pause automatically while overlays, crosshair, audio and telemetry remain available.
- Renamed the fast path to Minimum latency (No VSync - VRR), using tearing support when available.
- Added Dayaah AutoTune with mode scoring, live stability testing and safer device reopening for slow capture drivers.
- Added the latest-frame Latency Governor, which replaces stale frames before GPU processing to prevent latency buildup.
- Added the F9 Frame Pacing Scope with jitter, queue, GPU and presentation timing, plus a 120-frame graph.
- Added smooth notifications and a warning for 59.94/60 Hz VSync cadence mismatches.
- Added Phoenix Recovery for stalls, reader errors and format changes, with three recovery attempts and a safe return to Settings.
- Hardened Source Reader callbacks, session isolation, shutdown order and thread safety.
- Reduced per-frame memory copies and allocations using reusable buffers and improved Lock2D handling.
- Improved log preservation, mute hotkey behavior, first-run device defaults, overlay profile recovery and PNG safety limits.

See [CHANGELOG.md](CHANGELOG.md) for the complete release notes.

## Technical design

```text
UVC capture card
  -> Media Foundation Source Reader (asynchronous, low latency)
  -> Media Foundation decode (if MJPEG/H.264 is selected)
  -> newest-frame buffer
  -> D3D11 upload and Video Processor
  -> optional Frame Generation
  -> flip-model swap chain

Capture-card audio
  -> WASAPI capture
  -> digital gain
  -> WASAPI render
```

In the normal capture path, each new frame replaces the previous pending frame.
Optional Frame Generation keeps the neighboring real frames it needs to create an
intermediate frame.

## Build on Windows

You need Windows 10/11, Visual Studio 2022 Build Tools, **Desktop development
with C++**, and a Windows SDK.

1. Open **x64 Native Tools Command Prompt for VS 2022**.
2. Enter the project directory.
3. Run `build.bat`. The executable is written to `build\DayaahCapture.exe`.

For the reproducible Linux cross-build with LLVM-MinGW, see
[TOOLCHAIN.md](TOOLCHAIN.md).

## Reporting bugs

Open a [GitHub Issue](https://github.com/dayitaah/Dayaah-Capture/issues) and
attach `DayaahCapture.log`. Include:

- Dayaah Capture version.
- Windows version, GPU and driver version.
- Capture-card model and connection type.
- Selected resolution, frame rate, pixel format, presentation mode and Frame Generation preset.
- Exact steps needed to reproduce the problem.

Copy the log before relaunching. Normal launches rotate the previous log to
`DayaahCapture.log.1`.

## Current constraints

- Windows x64 only.
- +15 and +18 dB may clip an already loud input signal.
- P010 support does not establish HDR compatibility.
- Compressed capture and Frame Generation can add delay. For minimum latency,
  start with raw NV12 and Frame Generation off.

## Privacy

Capture settings, the first-run device hash and diagnostic logs stay local.
The hash is stored in `DayaahCapture.ini`. The viewer does not require an
account, send analytics or upload diagnostic data.

## Contributing

Bug reports and improvements are welcome in English or Spanish. Read
[CONTRIBUTING.md](https://github.com/dayitaah/Dayaah-Capture/blob/main/CONTRIBUTING.md) before submitting a change.

## Support Dayaah Capture

If Dayaah Capture saved you from capture-latency hell, you can support its
development on Ko-fi:

<p>
  <a href="https://ko-fi.com/dayaah"><img alt="Support Dayaah on Ko-fi" src="https://img.shields.io/badge/Support%20Dayaah-Ko--fi-FF5E5B?logo=kofi&logoColor=white"></a>
</p>

## License

Released under the [MIT License](LICENSE.txt). Copyright © 2026 Dayaah.
See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for the AMD and NVIDIA terms.
