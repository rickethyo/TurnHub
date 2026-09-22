# Atlas native regression scenarios

These tests compile the **actual** `main.cpp` handlers/adapters, `GameEngine`,
`Lobby`, and `IntentDispatcher`. Clock, radio, GPIO, web server, presentation, and
NVS boundaries are replaced by deterministic stubs. No firmware is flashed.

On Windows, run `Atlas\tests\host\run.cmd` from an x64 Native Tools Command
Prompt for Visual Studio (C++ workload required). Assertions must remain enabled.

Alternatively, run `Atlas\tests\host\run-gcc.ps1` in PowerShell with the
PlatformIO `platformio/toolchain-gccmingw32` package installed, or pass
`-Compiler` with another Windows GCC path. This runner uses C++14-compatible
test globals and `-mno-ms-bitfields` to preserve the packed radio layout.
Both Windows runners execute gameplay and storage scenarios.

With GCC/Clang on another host, from this directory:

```sh
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Istubs -I../../include scenarios.cpp test_globals.cpp profile_fixture.cpp \
    ../../src/audio_controller.cpp ../../src/led_renderer.cpp ../../src/controller_profiles.cpp ../../src/web_api.cpp \
    ../../src/profile_statistics.cpp ../../src/stats_page.cpp \
    ../../src/profile_login_page.cpp \
    ../../src/game_engine.cpp ../../src/lobby.cpp ../../src/intent_dispatcher.cpp \
    -o build/scenarios
./build/scenarios
c++ -std=c++17 -Wall -Wextra -Istorage_stubs -Istubs -I../../include \
    storage_scenarios.cpp test_globals.cpp ../../src/nvs_blob_store.cpp \
    ../../src/profile_stats_storage.cpp ../../src/profile_policy.cpp -o build/storage_scenarios
./build/storage_scenarios
```

Six scenario groups cover dispatcher ownership; lobby and lifecycle restrictions;
shared-seat win-response order and denial restoration; elimination versus
concession; PASS grace/cancellation/rollover and actor validation; optional NVS
absence versus injected type/handle/erase/commit errors.

`python audit_adapters.py` checks that current transport/input adapters do not
call canonical mutators or authoritative transition helpers directly.

This is application regression coverage, not proof of radio delivery, GPIO
debounce, flash persistence, buzzer/LED timing, browser authentication, or hardware
behavior. See `docs/engineering/ATLAS_INTENT_VERIFICATION.md` for the hardware gate.

The storage executable compiles the production NVS blob adapter and statistics
repository against injectable NVS calls. It checks identity contracts, deployed
v1 bytes, missing versus unreadable records, unsupported schemas, write protection
and read/set/commit failures. It does not simulate physical flash power loss.

The gameplay executable also compiles the real profile HTTP handlers and controller
registry. Three additional groups cover phone-only authentication/game lifecycle,
mixed physical/phone companion control, and 16-player capacity. `profile_fixture`
is an in-memory repository substitute; the SHA stub is a deterministic test double,
not cryptography. Existing storage scenarios test the real statistics blob layer.

Profile-policy scenarios cover all four owner choices, companion-session expiry,
PIN-protected physical claim rejection, hidden-stat accumulation, and unavailable
policy storage. The storage executable checks the real three-byte policy codec
and NVS failure handling; gameplay scenarios use the profile repository fixture.

Optional browser smoke check: run `node portal_smoke.cjs` with Playwright resolvable
(or `PLAYWRIGHT_MODULE` set to its module path) and Edge installed. It uses local
HTTP fixtures and checks the rendered portal/login flow at phone and desktop sizes.
It also checks policy saving/reloading and that polling preserves unsaved choices.
Game/life checks cover persisted setup, host-only edits, captured match settings,
own-life authorization, bounds, companion sessions, negative life and rematches.
The harness now links real LED/audio renderers and replaces the radio boundary.
