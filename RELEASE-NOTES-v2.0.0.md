# Dayaah Capture 2.0.0

Major interface, capture-format and overlay update.

## Highlights

- Clear resolution/FPS/format setup instead of one long mode list.
- NV12, YUY2, XRGB, I420 and P010 input support with format-specific notices.
- English/Spanish UI selector, GitHub and Ko-fi links.
- Machine-local first-run setup with automatic viewer startup on later launches.
- Stronger adaptive sharpening.
- `F8` approximate-latency HUD that responds to FPS, frame age and VSync.
- Centered crosshair overlay.
- Settings command in the capture context menu.

The latency display is an engineering estimate, not a sensor measurement. I420
is converted to NV12 on the CPU. P010 carries 10-bit samples but does not enable
HDR by itself. XRGB can substantially increase USB and memory bandwidth.

Built for Windows 10/11 x64 under the MIT License.
