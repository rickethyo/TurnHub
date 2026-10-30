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

"$compiler" "${flags[@]}" "${includes[@]}" \
  menu_scenarios.cpp ../../src/sigil_menu.cpp -o "$build_dir/menu_scenarios"
"$build_dir/menu_scenarios"

"$compiler" "${flags[@]}" "${includes[@]}" \
  secure_link_scenarios.cpp -o "$build_dir/secure_link_scenarios"
"$build_dir/secure_link_scenarios"

"$compiler" "${flags[@]}" "${includes[@]}" \
  pairing_scenarios.cpp -o "$build_dir/pairing_scenarios"
"$build_dir/pairing_scenarios"

"$compiler" "${flags[@]}" "${includes[@]}" \
  session_scenarios.cpp -o "$build_dir/session_scenarios"
"$build_dir/session_scenarios"

"$compiler" "${flags[@]}" "${includes[@]}"   firmware_package_scenarios.cpp -o "$build_dir/firmware_package_scenarios"
"$build_dir/firmware_package_scenarios"

"$compiler" "${flags[@]}" "${includes[@]}" touch_scenarios.cpp -o "$build_dir/touch_scenarios"
"$build_dir/touch_scenarios"

"$compiler" "${flags[@]}" "${includes[@]}" ring_scenarios.cpp -o "$build_dir/ring_scenarios"
"$build_dir/ring_scenarios"

"$compiler" "${flags[@]}" "${includes[@]}" mana_tap_scenarios.cpp -o "$build_dir/mana_tap_scenarios"
"$build_dir/mana_tap_scenarios"
