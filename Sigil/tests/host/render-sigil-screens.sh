#!/usr/bin/env bash
# Renders the Sigil screens (e-ink and OLED) to PNGs on the host with the
# firmware's own display classes over Adafruit GFX's canvas. Needs Adafruit
# GFX from PlatformIO: run `pio pkg install -e sigil` in Sigil/ once.
# Output: ../../.pio/host-tests/screens/*.png.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

gfx="../../.pio/libdeps/sigil/Adafruit GFX Library"
if [ ! -d "$gfx" ]; then
  echo "Adafruit GFX not found: run 'pio pkg install -e sigil' in Sigil/ first" >&2
  exit 2
fi
compiler="${CXX:-g++}"
out=../../.pio/host-tests/screens
mkdir -p "$out"
flags=(-std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -DARDUINO=100 -DTURNHUB_SCREEN_PREVIEW)
includes=(-Ipreview_stubs -I../../include -I../../../shared/include -isystem "$gfx")

"$compiler" "${flags[@]}" "${includes[@]}" render_sigil_screens.cpp ../../src/epaper_display.cpp \
  ../../src/sigil_menu.cpp "$gfx/Adafruit_GFX.cpp" -o ../../.pio/host-tests/render_eink_screens
for theme in 0 1 2 3; do
  ../../.pio/host-tests/render_eink_screens "$out" "$theme"
  ../../.pio/host-tests/render_eink_screens "$out" "$theme" 1
done

"$compiler" "${flags[@]}" "${includes[@]}" -DTURNHUB_DISPLAY_OLED=1 render_sigil_screens.cpp \
  ../../src/oled_display.cpp ../../src/sigil_menu.cpp "$gfx/Adafruit_GFX.cpp" -o ../../.pio/host-tests/render_oled_screens
for theme in 0 1 2 3; do
  ../../.pio/host-tests/render_oled_screens "$out" "$theme"
  ../../.pio/host-tests/render_oled_screens "$out" "$theme" 1
done
