# Atlas native regression scenarios

These tests compile the **actual** Atlas application modules (`main.cpp` plus the
handler and adapter modules declared in `atlas_app.h`), `GameEngine`, `Lobby`,
and `IntentDispatcher`. Clock, radio, GPIO, web server, presentation, and
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

`src/atlas_speaker.cpp` (ESP32 DAC) and `src/sd_card.cpp` (Arduino SD) are
firmware-only too; `test_globals.cpp` stubs them (no speaker, no card), and the
speaker scenario plugs a fake `ToneOutput` into the real `AudioController`.
`src/atlas_display.cpp` is firmware-only (it needs LovyanGFX), so it is not in
any runner's source list. `test_globals.cpp` provides a no-op `beginAtlasDisplay()`.

With GCC/Clang on another host, from this directory:

```sh
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Istubs -I../../include -I../../../shared/include scenarios.cpp test_globals.cpp profile_fixture.cpp \
    ../../src/app_context.cpp ../../src/gameplay_intents.cpp ../../src/table_intents.cpp \
    ../../src/moderation_intent.cpp ../../src/sigil_input.cpp ../../src/sigil_menu.cpp ../../src/profile_picker.cpp ../../src/web_adapters.cpp ../../src/front_panel.cpp ../../src/touch_controls.cpp ../../src/harness_link.cpp ../../src/sigil_accessibility.cpp \
    ../../src/audio_controller.cpp ../../src/led_renderer.cpp ../../src/controller_profiles.cpp ../../src/web_api.cpp ../../src/web_session.cpp ../../src/web_profile_api.cpp ../../src/web_game_api.cpp ../../src/web_admin_api.cpp \
    ../../src/profile_statistics.cpp ../../src/profile_stats_bridge.cpp ../../src/stats_page.cpp \
    ../../src/profile_login_page.cpp \
    ../../src/game_engine.cpp ../../src/lobby.cpp ../../src/intent_dispatcher.cpp ../../src/client_state.cpp \
    ../../src/game_checkpoint.cpp ../../src/game_recovery.cpp ../../src/game_recovery_store.cpp ../../src/nvs_blob_store.cpp ../../src/serial_log.cpp \
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
(or `PLAYWRIGHT_MODULE` set to its module path) and Edge installed (or set
`PLAYWRIGHT_CHANNEL=` to use Playwright's bundled Chromium, e.g. on Linux/CI). It uses local
HTTP fixtures and checks the rendered portal/login flow at phone and desktop sizes.
It also checks policy saving/reloading and that polling preserves unsaved choices.
The gameplay executable also covers ending a match as a draw through the real
touchscreen adapter (hold threshold, overrides, statistics once, recovery
validation), table presence codes (request, confirm, wrong codes, expiry), no table host, the OLED one-player limit, the
Atlas speaker's cue routing and volume setting, and admin device management (forget one/all Sigils,
seated/in-game refusal, storage failure, the 15/30/60-second pairing window). The
storage executable checks the `pairwin` and `spkvol` codecs and the v1 `Draw` result byte.
The portal smoke also covers the Paired Sigils card and the Draw label.

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
15-second timeout, clock rollover and rejection during gameplay. Radio transport
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

Interrupted-match recovery: the gameplay executable now also compiles
`game_checkpoint.cpp`, `game_recovery.cpp`, `game_recovery_store.cpp` and
`nvs_blob_store.cpp`, and runs a `gameRecoveryLifecycle` scenario against the
real `beginGameRecovery()`/`checkpointGame()` entry points main.cpp calls
(`stubs/nvs.h` gained a real in-memory blob backing for this, opt-in via
`useRealNvsBlobs` so the pre-existing `OptionalPreferences` error-injection
scenarios keep their original pure-error-code contract; this scenario runs
last for the same reason -- opening the recovery store is a one-way,
process-wide singleton latch). It checks: `NotFound` on a fresh store; a
checkpoint is written after a dispatched intent with no direct call to
`checkpointGame()` from the test, i.e. through the same observer hook
main.cpp uses; a simulated reboot (fresh `GameEngine`/`Lobby`, same
in-memory "flash") restores a paused, unfinished match with the elapsed game
clock unaffected by the simulated downtime and without replaying the
game-completed statistics callback; and a corrupted record fails safe to
`Corrupt` with no players restored rather than loading ambiguous state. This
does not simulate real flash power loss mid-write; see
`Documentation/engineering/STAGED_CHANGES.md` for the remaining hardware
acceptance item and the Resume/Discard UI gap noted there.
