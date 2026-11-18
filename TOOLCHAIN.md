# Reproducible portable toolchain

Dayaah Capture 3.4.1 was cross-compiled with the official LLVM-MinGW release:

- Release: `llvm-mingw 20261104` with LLVM 23.1.5
- Runtime: UCRT
- Host: Ubuntu 22.04 x86-64
- Target: x86_64 Windows GNU/MinGW
- Archive: `llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64.tar.xz`
- Upstream: <https://github.com/mstorsjo/llvm-mingw/releases/tag/20261104>

After extracting the archive, build from the source package root with:

```bash
bash src/build-llvm-mingw.sh /absolute/path/to/llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64
```

The script treats compiler warnings as errors and produces
`build/DayaahCapture.exe`. The compiler runtime and C++ standard library are
linked statically; Windows system components such as Media Foundation, D3D11,
WASAPI, Direct2D and UCRT remain normal operating-system dependencies.
