#!/usr/bin/env bash
# Builds turnhub-core with sanitizers and replays shared/core/scenarios/.
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
out="$root/shared/core/build"
mkdir -p "$out"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-sanitize-recover=all \
  -I"$root/shared/core/include" \
  "$root/shared/core/src/turnhub_json.cpp" "$root/shared/core/src/turnhub_core.cpp" "$root/shared/core/tests/scenario_runner.cpp" \
  -o "$out/scenario_runner"
"$out/scenario_runner" "$root/shared/core/scenarios" "$root/protocol/examples"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-sanitize-recover=all \
  -I"$root/shared/core/include" \
  "$root/shared/core/src/turnhub_json.cpp" "$root/shared/core/src/turnhub_core.cpp" "$root/shared/core/src/turnhub_offline.cpp" \
  "$root/shared/core/tests/offline_test.cpp" -o "$out/offline_test"
"$out/offline_test"
