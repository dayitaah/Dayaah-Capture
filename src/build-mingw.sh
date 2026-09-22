#!/usr/bin/env bash
set -euo pipefail
# Argument: extracted MinGW root (contains usr/bin and usr/share/mingw-w64).
dayaah_mingw_root="${1:?Usage: bash build-mingw.sh /absolute/path/to/toolchain-mingw}"
dayaah_src_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
dayaah_bin="$dayaah_mingw_root/usr/bin"
export PATH="$dayaah_bin:$PATH"
cd "$dayaah_src_dir"
"$dayaah_bin/x86_64-w64-mingw32-windres" -O coff DayaahCapture.rc -o DayaahCapture.res
"$dayaah_bin/x86_64-w64-mingw32-g++-posix" \
    -B"$dayaah_bin/x86_64-w64-mingw32-" \
    -idirafter "$dayaah_mingw_root/usr/share/mingw-w64/include" \
    -std=c++17 -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
    -municode -mwindows -static -static-libgcc -static-libstdc++ \
    main.cpp DayaahCapture.res -o ../DayaahCapture.exe \
    -lole32 -loleaut32 -luuid -ld3d11 -ldxgi -ldxguid \
    -lmf -lmfplat -lmfreadwrite -lmfuuid -lavrt -lpropsys -lshlwapi \
    -ldwmapi -luxtheme -lcomctl32 -lgdi32 -luser32
