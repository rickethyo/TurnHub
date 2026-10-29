#!/usr/bin/env bash
# Builds turnhub-core with sanitizers and replays core/scenarios/.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/core/build"
mkdir -p "$out"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-sanitize-recover=all \
  -I"$root/core/include" \
  "$root/core/src/turnhub_json.cpp" "$root/core/src/turnhub_core.cpp" "$root/core/tests/scenario_runner.cpp" \
  -o "$out/scenario_runner"
"$out/scenario_runner" "$root/core/scenarios"
