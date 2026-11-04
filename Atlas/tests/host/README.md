# Atlas native regression scenarios

These tests compile the **actual** Atlas application modules (`main.cpp` plus the
handler and adapter modules declared in `atlas_app.h`), the hosted-game relay
(`hosted_game.cpp`, `hosted_sigils.cpp`) and `IntentDispatcher`. Clock, radio, GPIO, web server, presentation, and
NVS boundaries are replaced by deterministic stubs. No firmware is flashed.

On Linux, run `bash Atlas/tests/host/run-linux.sh` from the repository root.
It builds the application, storage, profile-store, OTA and battery-adapter suites with C++17 (like the firmware; the application suite links the shared core's JSON reader), AddressSanitizer and UBSan, then runs them.
GitHub Actions uses this runner; see [CI checks](../../../Documentation/engineering/CONTINUOUS_INTEGRATION.md).

On Windows, run `Atlas\tests\host\run.cmd` from an x64 Native Tools Command
Prompt for Visual Studio (C++ workload required). Assertions must remain enabled.

Alternatively, run `Atlas\tests\host\run-gcc.ps1` in PowerShell with the
PlatformIO `platformio/toolchain-gccmingw32` package installed, or pass
`-Compiler` with another Windows GCC path. This runner uses C++17
and `-mno-ms-bitfields` to preserve the packed radio layout.
Both Windows runners execute application and storage scenarios.
They also compile the real `profile_store.cpp` against an in-memory NVS boundary
and run `profile_store_scenarios`: unused seat lookups, 100 reconnect cycles,
guest reads/writes, temporary bindings, legacy remember-key cleanup and placeholder filtering before
the account limit. No saved user records are deleted by this filtering.

`src/atlas_board_none.cpp` (the S3 Atlas's parts without drivers yet: status
LED, BOOT sleep, no speaker), `src/atlas_battery_s3.cpp` (its ADC), `src/sd_card.cpp` (Arduino SD_MMC),
`src/secure_link_backend.cpp` (mbedTLS self-test), `src/home_wifi_link.cpp`
(home Wi-Fi joins and the release-feed fetch) and `src/home_tls_server.cpp`
(HTTPS on the home network) are firmware-only;
`test_globals.cpp` stubs them (no speaker, a battery reading the scenarios
set, no card, self-test passes). The gauge logic and checkpoint
codec/throttling are header-only `battery_gauge.h` and `battery_checkpoint.h`,
tested directly for load noise, fast discharge, source cycles,
restore/replacement, safety, elapsed-time rollover and write failures. The
speaker scenario plugs a fake `ToneOutput` into the real `AudioController`.

With GCC/Clang on another host, from this directory:

```sh
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Istubs -I../../include -I../../../shared/include -I../../../shared/core/include scenarios.cpp test_globals.cpp profile_fixture.cpp \
    ../../src/app_context.cpp ../../src/device_intents.cpp ../../src/sigil_input.cpp ../../src/sigil_menu.cpp \
    ../../src/web_adapters.cpp ../../src/front_panel.cpp ../../src/touch_controls.cpp \
    ../../src/audio_controller.cpp ../../src/led_renderer.cpp \
    ../../src/web_api.cpp ../../src/web_session.cpp ../../src/web_profile_api.cpp ../../src/web_atlas_api.cpp \
    ../../src/web_standalone_api.cpp ../../src/standalone_import.cpp ../../src/sigil_update_service.cpp \
    ../../src/sigil_update_jobs.cpp ../../src/web_admin_api.cpp ../../src/profile_statistics.cpp \
    ../../src/ota_manager.cpp ../../src/intent_dispatcher.cpp ../../src/nvs_blob_store.cpp ../../src/serial_log.cpp \
    ../../src/match_archive.cpp ../../src/hosted_game.cpp ../../src/hosted_sigils.cpp ../../src/web_hosted_api.cpp \
    ../../src/card_cache.cpp ../../../shared/core/src/turnhub_json.cpp ../../../shared/core/src/turnhub_core.cpp \
    -o build/scenarios
./build/scenarios
c++ -std=c++17 -Wall -Wextra -Istorage_stubs -Istubs -I../../include -I../../../shared/include \
    storage_scenarios.cpp test_globals.cpp ../../src/nvs_blob_store.cpp ../../src/sd_blob_store.cpp \
    ../../src/profile_stats_storage.cpp ../../src/profile_policy.cpp -o build/storage_scenarios
./build/storage_scenarios
c++ -std=c++17 -Wall -Wextra -Istorage_stubs -Istubs -I../../include -I../../../shared/include \
    profile_store_scenarios.cpp test_globals.cpp ../../src/profile_store.cpp \
    ../../src/nvs_blob_store.cpp ../../src/profile_stats_storage.cpp \
    ../../src/profile_policy.cpp -o build/profile_store_scenarios
./build/profile_store_scenarios
c++ -std=c++17 -Wall -Wextra -Istubs -I../../include -I../../../shared/include \
    ota_scenarios.cpp ../../src/sigil_update_jobs.cpp -o build/ota_scenarios
./build/ota_scenarios
```

`ota_scenarios` covers Sigil OTA on Atlas (`sigil_update_jobs.cpp`):
staging a Sigil package with every check, reloading it after a restart, and
one update job from offer to done or each failure.

The application executable covers the hosted game (lease, revisions, inputs,
resume offer, Sigil join and keys, the Atlas screen), Atlas state v1, the
dispatcher contract, profile registration and login, profile policy, account
setup and permissions, game import and match history, room backup, pairing
(code check, window, forget), the table code, first-run setup, factory reset,
power, sleep, theme and speaker settings, battery, home Wi-Fi, the update
notice, Sigil OTA service, artwork uploads, personalization, the card cache,
Mana Tap and the serial log. The rules themselves are tested in `shared/core/` and the
app, not here; Atlas has run no game since 2026-11-04.

`python audit_adapters.py` checks that the transport/input adapters do not
bypass the dispatcher.

This is application regression coverage, not proof of radio delivery, GPIO
debounce, flash persistence, buzzer/LED timing or hardware behavior.

The storage executable compiles the production NVS blob adapter and statistics
repository against injectable NVS calls. It checks identity contracts, deployed
v1 bytes, missing versus unreadable records, unsupported schemas, write protection
and read/set/commit failures, plus the policy, `pairwin` and `spkvol` codecs. It
does not simulate physical flash power loss. `profile_fixture` is an in-memory
repository substitute; the SHA stub is a deterministic test double, not
cryptography.

Sigil receive coverage copies both the 7-byte control packet and the 156-byte
display packet through the production receive buffer, rejecting incorrect sizes.

The application executable emits `build/client-*.json` from the live info and
state handlers. After running the host suite, run `python check_client_contract.py`
to check these actual responses and the shared `protocol/examples` fixtures
against the contract schemas (standard library only). The checker supports only
the schema keywords used here and rejects unknown ones.
