#!/usr/bin/env bash
# Run from any directory. Keep sources aligned with run-gcc.ps1.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

compiler="${CXX:-g++}"
flags=(-std=c++14 -Wall -Wextra -Werror -mno-ms-bitfields -g -O1
       -fsanitize=address,undefined -fno-omit-frame-pointer)
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
includes=(-Istubs -I../../include -I../../../shared/include)
build_dir=../../.pio/host-tests
mkdir -p "$build_dir"

"$compiler" "${flags[@]}" "${includes[@]}" -DTURNHUB_DISPLAY_OLED=1 \
  oled_scenarios.cpp ../../src/oled_display.cpp ../../src/sigil_display.cpp \
  ../../src/sigil_menu.cpp -o "$build_dir/oled_scenarios"
"$build_dir/oled_scenarios"

"$compiler" "${flags[@]}" "${includes[@]}" \
  led_scenarios.cpp ../../src/sigil_led.cpp -o "$build_dir/led_scenarios"
"$build_dir/led_scenarios"

"$compiler" "${flags[@]}" "${includes[@]}" \
  menu_scenarios.cpp ../../src/sigil_menu.cpp -o "$build_dir/menu_scenarios"
"$build_dir/menu_scenarios"

"$compiler" "${flags[@]}" "${includes[@]}" \
  secure_link_scenarios.cpp -o "$build_dir/secure_link_scenarios"
"$build_dir/secure_link_scenarios"

"$compiler" "${flags[@]}" "${includes[@]}" \n  pairing_scenarios.cpp -o "$build_dir/pairing_scenarios"
"$build_dir/pairing_scenarios"
