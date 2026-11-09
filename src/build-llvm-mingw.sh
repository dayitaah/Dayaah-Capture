#!/usr/bin/env bash
set -euo pipefail

# Compilacion cruzada: este script corre en Linux pero produce el EXE de
# Windows. Solo pasale la carpeta donde descomprimiste el LLVM-MinGW portable.
dayaah_toolchain_root="${1:?Usage: bash build-llvm-mingw.sh /absolute/path/to/llvm-mingw}"
dayaah_source_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
dayaah_project_root="$(cd -- "$dayaah_source_dir/.." && pwd)"
dayaah_bin="$dayaah_toolchain_root/bin"
dayaah_cxx="$dayaah_bin/x86_64-w64-mingw32-clang++"
dayaah_windres="$dayaah_bin/x86_64-w64-mingw32-windres"

if [[ ! -x "$dayaah_cxx" || ! -x "$dayaah_windres" ]]; then
    echo "The selected folder is not a compatible x86_64 LLVM-MinGW toolchain." >&2
    exit 1
fi

mkdir -p "$dayaah_project_root/build"
# El recurso mete icono, manifest y version. Usamos un temporal para no dejar
# mugrero de compilacion dentro de src/.
dayaah_resource_object="$(mktemp "${TMPDIR:-/tmp}/dayaah-resource.XXXXXX.o")"
trap 'rm -f "$dayaah_resource_object"' EXIT

"$dayaah_windres" -O coff "$dayaah_source_dir/DayaahCapture.rc" -o "$dayaah_resource_object"
# -Werror es la policia: si aparece un warning nuevo, el build se detiene.
"$dayaah_cxx" \
    -std=c++17 -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
    -municode -mwindows -static \
    "$dayaah_source_dir/main.cpp" "$dayaah_resource_object" \
    -o "$dayaah_project_root/build/DayaahCapture.exe" \
    -lole32 -loleaut32 -luuid -ld3d11 -ldxgi -ldxguid \
    -lmf -lmfplat -lmfreadwrite -lmfuuid -lavrt -lpropsys -lshlwapi \
    -lshell32 -ladvapi32 -ldwmapi -luxtheme -lcomctl32 -lcomdlg32 \
    -ld2d1 -ldwrite -lwindowscodecs -lgdi32 -luser32

echo "Dayaah Capture built at $dayaah_project_root/build/DayaahCapture.exe"
sha256sum "$dayaah_project_root/build/DayaahCapture.exe"
