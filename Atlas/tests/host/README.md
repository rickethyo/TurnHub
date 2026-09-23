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
They also compile the real `profile_store.cpp` against an in-memory NVS boundary
and run `profile_store_scenarios`: unused seat lookups, 100 reconnect cycles,
guest reads/writes, temporary bindings, legacy remember-key cleanup and placeholder filtering before
the account limit. No saved user records are deleted by this filtering.

With GCC/Clang on another host, from this directory:

```sh
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Istubs -I../../include scenarios.cpp test_globals.cpp profile_fixture.cpp \
    ../../src/audio_controller.cpp ../../src/led_renderer.cpp ../../src/controller_profiles.cpp ../../src/web_api.cpp \
    ../../src/profile_statistics.cpp ../../src/stats_page.cpp \
    ../../src/profile_login_page.cpp \
    ../../src/game_engine.cpp ../../src/lobby.cpp ../../src/intent_dispatcher.cpp ../../src/client_state.cpp \
    -o build/scenarios
./build/scenarios
c++ -std=c++17 -Wall -Wextra -Istorage_stubs -Istubs -I../../include \
    storage_scenarios.cpp test_globals.cpp ../../src/nvs_blob_store.cpp \
    ../../src/profile_stats_storage.cpp ../../src/profile_policy.cpp -o build/storage_scenarios
./build/storage_scenarios
c++ -std=c++17 -Wall -Wextra -Istorage_stubs -Istubs -I../../include \
    profile_store_scenarios.cpp test_globals.cpp ../../src/profile_store.cpp \
    ../../src/nvs_blob_store.cpp ../../src/profile_stats_storage.cpp \
    ../../src/profile_policy.cpp -o build/profile_store_scenarios
./build/profile_store_scenarios
```

Six scenario groups cover dispatcher ownership; lobby and lifecycle restrictions;
shared-seat win-response order and denial restoration; elimination versus
concession; PASS grace/cancellation/rollover and actor validation; optional NVS
absence versus injected type/handle/erase/commit errors.

`python audit_adapters.py` checks that current transport/input adapters do not
call canonical mutators or authoritative transition helpers directly.

This is application regression coverage, not proof of radio delivery, GPIO
debounce, flash persistence, buzzer/LED timing, browser authentication, or hardware
behavior. See `Documentation/engineering/ATLAS_INTENT_VERIFICATION.md` for the hardware gate.

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

Life approval and Commander coverage: the native gameplay executable checks
recipient-only acceptance/rejection, duplicate and stale IDs, Atlas's 15-second
deadline including rollover, concurrent edits, lifecycle cancellation and atomic
Commander/life bounds. `node counter_smoke.cjs` uses the same Playwright setup as
the portal smoke check and opens two independent browser contexts to check the
production UI, cross-tab prompts, keyboard focus, corrections and reconnects.
These counter features still require a hardware table check after flashing.

Manual pairing Intent scenarios cover origin authorization, radio unavailability,
30-second timeout, clock rollover and rejection during gameplay. Radio transport
and persistence still require the manual pairing bench checklist.

Named-profile attachment scenarios cover adopting a joined guest, preserving its
host and secondary seat, merging an already joined browser player, companion login
without duplication, and rejecting replacement of another named account. Profile
attachment and name edits push display names directly instead of requesting the
Sigil reconnect handshake that clears transient bindings. On hardware, attach a
named profile more than five seconds after boot and verify its name and player count
remain stable, then sign into the same profile from a second browser.

Sigil receive coverage copies both the 7-byte control packet and the 110-byte
game display through the production receive buffer, rejecting incorrect sizes.
The physical companion scenario checks game-end seat clearing, saved statistics,
rematch restoration and guest behavior after reset.

Native-client scenarios exercise the live state/info handlers, session-resolved
PASS, boot/revision conflicts, no-op revisions, deferred commits, life approval
expiry including clock rollover, Commander serialization, full 16-player matrices
and reconnect. They emit `build/client-*.json`. After running the host suite, run
`python check_client_contract.py` to check these actual responses and the shared
`protocol/examples` fixtures against the contract schemas (standard library only).
The checker supports only the schema keywords used here and rejects unknown ones.
Display-name tests cover repeated deliveries, truncation, shortening and clearing;
profile-store tests assert identical saves do not add NVS writes.
