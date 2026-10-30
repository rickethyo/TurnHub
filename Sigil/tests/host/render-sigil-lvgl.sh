#!/usr/bin/env bash
# Renders the LVGL Sigil screen (src/sigil_display_lvgl.cpp) from fixtures to
# build/preview/*.png for review without the board. Needs the LVGL 9.3
# sources PlatformIO fetches for env:sigil (run
# `pio run -e sigil` once). LVGL is compiled once into build/preview/lvgl.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
lvgl=../../.pio/libdeps/sigil/lvgl
[ -d "$lvgl/src" ] || { echo "LVGL sources missing: run pio run -e sigil in Sigil/ first" >&2; exit 1; }
out=build/preview
mkdir -p "$out/lvgl"
defs=(-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DTURNHUB_SCREEN_PREVIEW)
cincs=(-I../../include -I"$lvgl" -I"$lvgl/src")
objs=()
while IFS= read -r src; do
  obj="$out/lvgl/$(echo "${src#$lvgl/}" | tr '/' '_').o"
  [ "$obj" -nt "$src" ] || gcc -O1 -c "${defs[@]}" "${cincs[@]}" "$src" -o "$obj"
  objs+=("$obj")
done < <(find "$lvgl/src" -name '*.c' | sort)
for font in eyebrow_13 semi_16 semi_20 bold_28 num_40 num_64 icons; do
  src=../../../shared/display/th_$font.c
  obj="$out/lvgl/th_$font.o"
  [ "$obj" -nt "$src" ] || gcc -O1 -c "${defs[@]}" "${cincs[@]}" "$src" -o "$obj"
  objs+=("$obj")
done
g++ -std=c++17 -O1 -g "${defs[@]}" -Istubs -I../../include -I../../../shared/include -I"$lvgl" -I"$lvgl/src" \
  preview/sigil_preview.cpp ../../src/sigil_display_lvgl.cpp ../../src/sigil_menu.cpp "${objs[@]}" -lm \
  -o "$out/sigil_preview"
"$out/sigil_preview" > "$out/files.txt"
if python3 -c "import PIL" 2>/dev/null; then
  python3 - "$out" <<'PY'
import sys, glob, os
from PIL import Image
for p in glob.glob(os.path.join(sys.argv[1], '*.ppm')):
    Image.open(p).save(p[:-4] + '.png'); os.remove(p)
PY
fi
ls "$out"/*.p?m 2>/dev/null || true; ls "$out"/*.png 2>/dev/null | head -40
