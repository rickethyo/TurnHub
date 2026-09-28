#!/usr/bin/env bash
# Run from any directory. Keep sources aligned with run-gcc.ps1 and run.cmd.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

compiler="${CXX:-g++}"
flags=(-std=c++14 -Wall -Wextra -mno-ms-bitfields -g -O1
       -fsanitize=address,undefined -fno-omit-frame-pointer)
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
includes=(-Istubs -I../../include -I../../../shared/include)
application_sources=(
  scenarios.cpp test_globals.cpp profile_fixture.cpp
  ../../src/app_context.cpp ../../src/gameplay_intents.cpp ../../src/table_intents.cpp
  ../../src/moderation_intent.cpp ../../src/sigil_input.cpp ../../src/sigil_menu.cpp
  ../../src/profile_picker.cpp ../../src/web_adapters.cpp ../../src/front_panel.cpp
  ../../src/touch_controls.cpp ../../src/harness_link.cpp ../../src/sigil_accessibility.cpp
  ../../src/audio_controller.cpp ../../src/led_renderer.cpp ../../src/controller_profiles.cpp
  ../../src/web_api.cpp ../../src/web_session.cpp ../../src/web_profile_api.cpp
  ../../src/web_game_api.cpp ../../src/web_admin_api.cpp ../../src/profile_statistics.cpp
  ../../src/profile_stats_bridge.cpp ../../src/stats_page.cpp ../../src/profile_login_page.cpp
  ../../src/game_engine.cpp ../../src/lobby.cpp ../../src/intent_dispatcher.cpp
  ../../src/client_state.cpp ../../src/game_checkpoint.cpp ../../src/game_recovery.cpp
  ../../src/game_recovery_store.cpp ../../src/nvs_blob_store.cpp ../../src/serial_log.cpp
)

mkdir -p build
# Contract checks must use responses from this run, not an earlier executable.
rm -f build/client-*.json
"$compiler" "${flags[@]}" "${includes[@]}" "${application_sources[@]}" -o build/scenarios
./build/scenarios

"$compiler" "${flags[@]}" -Istorage_stubs "${includes[@]}" \
  storage_scenarios.cpp test_globals.cpp ../../src/nvs_blob_store.cpp ../../src/sd_blob_store.cpp \
  ../../src/profile_stats_storage.cpp ../../src/profile_policy.cpp -o build/storage_scenarios
./build/storage_scenarios

"$compiler" "${flags[@]}" -Istorage_stubs "${includes[@]}" \
  profile_store_scenarios.cpp test_globals.cpp ../../src/profile_store.cpp \
  ../../src/nvs_blob_store.cpp ../../src/profile_stats_storage.cpp ../../src/profile_policy.cpp \
  -o build/profile_store_scenarios
./build/profile_store_scenarios
