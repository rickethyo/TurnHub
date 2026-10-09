#!/usr/bin/env bash
# Renders the Atlas touchscreen art (src/atlas_art.cpp) to PNGs on the host
# and checks incremental redraws against full redraws. Needs LovyanGFX from
# PlatformIO: run `pio run -e atlas` (or `pio pkg install -e atlas`) in Atlas/
# once. Output: build/screens/*.png. Linux only (LovyanGFX's framebuffer
# back end is selected; no display is opened).
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

lgfx=../../.pio/libdeps/atlas/LovyanGFX/src
if [ ! -d "$lgfx" ]; then
  echo "LovyanGFX not found at $lgfx: run 'pio pkg install -e atlas' in Atlas/ first" >&2
  exit 2
fi

compiler="${CXX:-g++}"
mkdir -p build/lgfx build/screens
defines=(-DLGFX_LINUX_FB)
# LovyanGFX itself, built once.
if [ ! -f build/lgfx/liblgfx.a ]; then
  "$compiler" -std=c++17 -O1 -w "${defines[@]}" -I"$lgfx" -c "$lgfx/lgfx/v1/lgfx_v1.cpp" -o build/lgfx/lgfx_v1.o
  while IFS= read -r -d '' source; do
    gcc -O1 -w "${defines[@]}" -I"$lgfx" -c "$source" -o "build/lgfx/$(basename "$source" .c).o"
  done < <(find "$lgfx/lgfx" -name '*.c' -print0)
  ar rcs build/lgfx/liblgfx.a build/lgfx/*.o
fi

"$compiler" -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined "${defines[@]}" \
  -Istubs -I../../include -I../../../shared/include -isystem "$lgfx" \
  render_atlas_screens.cpp ../../src/atlas_art.cpp build/lgfx/liblgfx.a -o build/render_atlas_screens
for theme in 0 1 2 3; do
  ./build/render_atlas_screens build/screens "$theme"
done
