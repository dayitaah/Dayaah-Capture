# Contributing to Dayaah Capture

Thank you for helping improve Dayaah Capture. Bug reports, ideas, documentation fixes, and code contributions are welcome in English or Spanish.

[Leer en español](CONTRIBUTING.es.md)

## Before opening an issue

- Check that the problem still happens with the latest release.
- Include your Windows version and capture-card model.
- Include the selected resolution, frame rate, and pixel format.
- State whether **Minimum latency** or **VSync** was selected.
- Attach `DayaahCapture.log` only after reviewing it for information you do not want to share publicly.

## Building

Use Windows 10/11 x64 and Visual Studio 2022 Build Tools with **Desktop development with C++** and a Windows SDK installed. Run `build.bat` from an `x64 Native Tools Command Prompt for VS 2022`.

## Pull requests

- Keep the native low-latency design intact.
- Avoid adding mandatory frameworks, package managers, telemetry, or network access.
- Do not commit `build/`, `DayaahCapture.ini`, or `DayaahCapture.log`.
- Test both presentation modes when a change touches rendering.
- Describe any observed impact on latency, frame pacing, or device compatibility.

By contributing, you agree that your contribution may be distributed under the project's MIT License.
