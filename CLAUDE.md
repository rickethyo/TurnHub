# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

TurnHub is a local-first tabletop game-management system (turn timer, lobby, life totals, profiles and statistics). It is a multi-target monorepo:

| Dir | What | Toolchain |
|---|---|---|
| `Atlas/` | ESP32 table controller: **the authoritative game engine**, Wi-Fi AP, web portal and HTTP API, ESP-NOW radio to Sigils, NVS persistence | PlatformIO, Arduino ESP32, C++ |
| `Sigil/` | ESP32 player controller: buttons, LEDs, buzzer, e-ink (GxEPD2), ESP-NOW | PlatformIO, Arduino ESP32, C++ |
| `Android/` | Native client over HTTP (`HttpAtlasRepository`, polling `/api/v1/state`): live table, player controls, first-run setup, firmware updates | Gradle, Kotlin |
| `shared/include/` | Firmware headers shared by Atlas and Sigil (currently `protocol.h`, the ESP-NOW radio contract) | C++ |
| `protocol/` | Transport-neutral client contract: JSON schemas, `http-v1.md`, example responses | — |
| `TestHarness/` | Retired hardware test harness: historical source only; unsupported, no builds or compatibility maintenance (see its README) | PlatformIO, Arduino ESP32, C++ |
| `KiCad/` | Sigil PCB/schematic, plus Python scripts in `PCB/Sigilv1/tools/` that build and verify the schematic | KiCad, Python |
| `design/` | V1 design system shared by the portal and Android: `tokens.json` (themes, type, motion), the icon set, Inter and Cinzel fonts, web components and the style guide. `python3 design/build_tokens.py` regenerates `design/dist/` and Android's `DesignTokens.kt` and `ic_th_*` drawables (CI runs `--check`); see `design/README.md` | Python (stdlib) |
| `Documentation/engineering/` | The durable engineering record: design decisions, invariants, staged work, verification backlog | — |

Earlier Python/Raspberry Pi generations are deliberately **not in this repo**; they survive only as history in `Documentation/engineering/GENERATION_HISTORY.md`. Don't reintroduce references to a `Controller/` directory, or its tooling (root `requirements.txt`, the Visual Studio Python project, the pySerial dependency).

`Documentation/User Manual/*.docx` is the user-facing manual. When a user-visible timing or behavior changes (e.g. the 60 s pairing window, the 15 s life-change approval `LIFE_APPROVAL_MS`), keep the code, its UI/API messages, the engineering docs and the manual in agreement.

The owner also uses GitHub Desktop, which can switch branches and auto-stash uncommitted work as `!!GitHub_Desktop<branch>`. If files suddenly revert mid-session, check `git branch --show-current` and `git stash list` before redoing anything.

## Commands

### Atlas firmware (run from `Atlas/`)
```
pio run -e atlas                      # build
pio run -e atlas --target upload --upload-port COMx   # flash (no port is pinned; see below)
pio device monitor                    # serial, 115200 baud
```
Portal at `192.168.4.1` on the `TurnHub-Atlas` AP.

**One-button flash:** `tools\flash-all.cmd` runs `tools\scripts\flash-all.ps1 -Sign` (options `-NoPull`, `-DryRun`, `-Key`): it pulls the repo when the working copy is clean (with uncommitted changes it signs them unpulled), builds, signs with the local key (see "Local signing"), verifies and flashes each attached board by MAC from the git-ignored `tools\boards.local.md`. `tools\flash-all-unsigned.cmd` is the same without signing; it builds and uploads with `pio` and refuses to pull over uncommitted changes. Both skip the harness and spare boards, and offer board setup for unknown MACs (`-NoSetup` skips the offer). **Board setup:** `tools\setup-boards.cmd` (`-DryRun` to preview) identifies attached boards (CH340 = Atlas; a CP210x Sigil's running firmware reports its GPIO4 strap, `SIGIL|HW|EINK`/`OLED`), asks what each new one is, then lets you rename, retype, retire (`spare:<firmware>`), return to service or delete any recorded board. It writes only `tools\boards.local.md`. The committed `BOARD_INVENTORY.md` is a format example with made-up MACs: never put real boards in it.

**Local signing:** the git root is `D:\TurnHub\Include`; `D:\TurnHub\Private` (outside git) holds `TurnHub-keys`, `TurnHub-backups` and `TurnHub-builds`. Never move keys, backups or builds into the repo. `tools\sign-local.cmd` (`-Products atlas,sigil-eink,sigil-oled`; the PowerShell behind the launchers is in `tools\scripts\`) builds from the working copy and signs with the single `.pem` in `Private\TurnHub-keys`, then writes verified `.thfw` packages to `Private\TurnHub-builds\local-<time>-<commit>`. Install them from the Atlas portal's `/update` page, the Sigil firmware page, or the app. OTA accepts the same or a newer version only; raise `PATCH` in `firmware_version.h` to update over a running build, and a downgrade needs USB. Needs python `cryptography` 46.0.7 in PlatformIO's Python. Never print or open the key.

**Serial ports:** never hard-code COM numbers (in `platformio.ini`, docs or scripts). They change whenever the PC restarts. Find the board each time (Atlas is the CH340 port, Sigils are CP210x; see "Identifying boards" below), pass `--upload-port` / `--port`, and ask the owner if several candidates are attached.

### Atlas host regression tests (no hardware)
Linux / GitHub Actions: `bash Atlas/tests/host/run-linux.sh` and
`bash Sigil/tests/host/run-linux.sh` run the existing suites with sanitizers.
Keep their source lists aligned with the Windows runners when sources change.
See `Documentation/engineering/CONTINUOUS_INTEGRATION.md` for the workflow,
build artifacts and check names.

These compile the **real** `main.cpp` and application modules, `GameEngine`, `Lobby`, `IntentDispatcher`, HTTP handlers, the statistics bridge and storage code against stubs in `tests/host/stubs/` and `tests/host/storage_stubs/`. They produce three executables: `scenarios` (gameplay/login/recovery), `storage_scenarios` and `profile_store_scenarios`.
```
Atlas\tests\host\run-gcc.ps1          # PowerShell, uses PlatformIO's toolchain-gccmingw32 (or -Compiler <g++>)
Atlas\tests\host\run.cmd              # from an x64 VS Native Tools prompt (MSVC)
python Atlas/tests/host/audit_adapters.py          # guard: adapters must not bypass the dispatcher
python Atlas/tests/host/check_client_contract.py   # after the host suite: validate build/client-*.json + protocol/examples against schemas
node Atlas/tests/host/portal_smoke.cjs             # optional Playwright + Edge browser smoke
node Atlas/tests/host/counter_smoke.cjs            # optional two-context life/Commander UI smoke
node Atlas/tests/host/basic_portal_smoke.cjs       # optional: the basic portal kept in flash for a failed microSD card
python3 Atlas/web/build.py                         # SD portal pack (PORTAL_PACK.md), the full portal; the first two smokes render it
```
There is no per-test filter. To run one group, build the single executable (the command lines are in `tests/host/README.md`).
- **Adding a new `src/*.cpp` to Atlas:** add it to the source lists in `run.cmd`, `run-gcc.ps1` and the README command lines too. Those lists also carry the `shared/include` path. The exception is firmware-only code that needs a hardware library: `atlas_display.cpp` and `atlas_art.cpp` (LovyanGFX), `atlas_speaker.cpp` (ESP32 DAC), `sd_card.cpp` (Arduino SD), `factory_reset.cpp` (NVS erase + restart) and `secure_link_backend.cpp` (mbedTLS) are left out, and `test_globals.cpp` stubs them.
- **Python and `pio`:** PlatformIO's `%USERPROFILE%\.platformio\penv\Scripts` is on the user PATH ahead of the Microsoft Store stub (added 2026-09-28), so new shells resolve `python` and `pio` there. Shells started before that, and the Bash tool if it doesn't see it, need the full path. That interpreter runs both `.py` checks with the standard library only.
- **Packed-struct flag:** the GCC runner uses `-mno-ms-bitfields` and C++14 to preserve the packed radio packet layout. Keep it.
- **Test ordering:** the recovery scenario runs last on purpose (the recovery store is a one-way process-wide latch).

### Sigil firmware (run from `Sigil/`)
```
pio run -e sigil                      # E-ink Sigil with analog joystick
pio run -e sigil-oled                 # OLED Sigil (same thumbstick, ring and GPIOs; only the display differs)
pio run -e sigil-wokwi                # Wokwi simulation build (ESP-NOW replaced by wokwi_espnow_shim.h, which acts as a fake Atlas)
```
Wokwi serial-console commands for driving the simulated Atlas are listed in `Sigil/WOKWI.md`.

### Android (run from `Android/`)
```
./gradlew assembleDebug
./gradlew testDebugUnitTest           # plain JVM tests
./gradlew testDebugUnitTest --tests "com.turnhub.android.data.HttpAtlasRepositoryTest"
```
- **Environment:** no `local.properties` is checked in, and `java` isn't on PATH. Set `JAVA_HOME` to Android Studio's bundled `C:\Program Files\Android\Android Studio\jbr` and `ANDROID_HOME` to `%LOCALAPPDATA%\Android\Sdk`.
- **Test fixtures:** tests load the shared fixtures from `protocol/examples/` via `testing/Fixtures.kt`, so changing those files affects Android tests too.
- **Layering:** wire DTOs and the strict parser live in `protocol/`, UI aggregates in `domain/`, and networking only below `AtlasRepository` in `data/`. Production uses `HttpAtlasRepository`; there is no mock repository.
- **Cleartext HTTP:** `res/xml/network_security_config.xml` allows it only to `192.168.4.1`.
- **Android 17 local network permission:** apps targeting API 37 need the `ACCESS_LOCAL_NETWORK` ("Nearby devices") runtime permission for any LAN traffic. Without it, connections to Atlas just time out with no clear error. `MainActivity` requests it on Connect.
- **Joining Atlas's Wi-Fi:** the app joins it itself via `TargetedAtlasWifiLink` (`WifiNetworkSpecifier`, API 29+). It tries a saved password, then the shipped default, then prompts. The default passphrase `TurnHub-Setup` exists in two places that must stay in sync: `Atlas/include/config.h` (`WIFI_DEFAULT_PASSWORD`) and `WifiCredentials.DEFAULT_ATLAS_PASSPHRASE`. It's documented in `protocol/http-v1.md`.
- **Phone testing:** with USB debugging on, `adb` is at `%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe`. Drive the UI with `uiautomator dump` plus `input tap`. `screencap` needs a display ID on the Pixel Fold. Never type passwords for the user.
- **Identifying boards:** Atlas (LCDwiki E32R28T 2.8" display board) uses a CH340C USB bridge; Sigils use CP210x. To confirm a board, read its MAC with `pio pkg exec -p tool-esptoolpy -- esptool.py --port COMx read_mac`; Atlas's MAC is its `THA-` ID. Reading the MAC resets the board.

### Test harness (run from `TestHarness/`)
```
pio run -e harness --target upload --upload-port COMx   # the harness is a CP210x port too; ask which one
```
Serial console (115200): `status`, `pair` (tap Menu → Pair a Sigil on Atlas), `test` (premade tests), `run game [players] [turns]`, `pace <ms>`. It pairs as two Sigils (its station and soft-AP MACs), and the first advertises `CAPABILITY_HARNESS` so Atlas shows **Tests** under the lobby's Menu.

## Architecture: rules that span files

**Atlas owns all canonical state.** Sigils, browsers, Android, simulators and the Atlas touchscreen capture input and render state. They never decide game outcomes. Read `Documentation/engineering/ARCHITECTURAL_INVARIANTS.md` before structural work; violating it is treated as a regression.

**Intent pipeline (Atlas):**
```
transport adapter (ESP-NOW packet / HTTP handler / GPIO)
  -> TurnHub::Intent {type, actor, payload}        include/intent.h
  -> IntentDispatcher (fixed table, one handler per IntentType; a second bind is rejected)
  -> handle*Intent (Atlas/src/*_intents.cpp) -> GameEngine / Lobby mutation
  -> observer hook -> checkpointGame() (recovery record)
```
- **Adapters stay thin.** Adapters such as `handleWebControl`, `handlePass` and `processSigilEvents` must only build Intents. `audit_adapters.py` fails if an adapter calls `game.*`/`lobby.*` mutators, assigns `hubState`/`pendingPass` and similar state, or calls transition helpers directly. Adapters live in `sigil_input.cpp`, `web_adapters.cpp`, `front_panel.cpp` and `touch_controls.cpp`; list any new one in `audit_adapters.py`. New gameplay actions need an `IntentType`, a handler bound in `main.cpp`'s `configureIntentHandlers()`, and host scenarios.
- **Ownership layout:** `include/atlas_app.h` declares the shared runtime objects and table-decision state (`namespace TurnHubAtlas`) and maps each module: `main.cpp` (object definitions, handler bindings, networking, `setup`/`loop`), `app_context.cpp` (seat lookups, Intent builders), `gameplay_intents.cpp` (PASS, pause, concede, win, life/Commander, timer cues), `table_intents.cpp` (lobby, start, reset, elimination, pairing, settings, lifecycle transitions), `moderation_intent.cpp`, `front_panel.cpp` (pairing window and table presence codes: a signed-in phone asks for a code, the Atlas screen shows it, and entering it verifies that profile at the table for 10 minutes; that is the physical-presence proof for first-Admin setup, network settings, device names, OTA, Return to lobby and factory reset. It replaced the 3 s Unlock admin hold on 2026-09-25), `touch_controls.cpp` (the TFT's screen model and touch-button adapter, host-tested), `atlas_display.cpp` (the built-in TFT: panel setup, bit-banged XPT2046 touch reads and calibration, firmware-only), `atlas_art.cpp` (the Brass drawing of the screen model onto any LovyanGFX target; `bash Atlas/tests/host/render-atlas-screens.sh` renders it to PNGs on Linux and checks incremental redraws; fonts come from `tools/fonts/make_fonts.py`), `atlas_speaker.cpp` (the on-board speaker; `AudioController` routes table-wide cues to it through `ATLAS_SPEAKER_MASK`, firmware-only) and `sigil_accessibility.cpp` (applies seated players' accessibility preferences to each Sigil's LEDs, buzzer and hold timing). `GameEngine` owns turn, timer, life and win state. `Lobby` owns participants, seats and starter selection. One game's `Lobby`, `GameEngine`, `ClientState` and table decisions are grouped in a `GameTable` (`tables[MAX_GAME_TABLES]`, one entry today; the old global names such as `game`, `lobby` and `hubState` are references into `tables[0]`). This is groundwork for the venue model (several games per Atlas, see `STAGED_CHANGES.md`). The HTTP surface is `web_api.h`: `web_api.cpp` holds the route table and callbacks, with handlers in `web_session.cpp`, `web_profile_api.cpp`, `web_game_api.cpp` and `web_admin_api.cpp` (shared private state in `web_api_internal.h`). `sigil_bus.cpp` holds the ESP-NOW transport.
- **Serial output:** Atlas code logs through `TurnHub::serialLog` (`serial_log.h`), not `Serial.print*`, so every line also reaches the RAM log a Developer can download (`GET /api/diagnostics/log`). Optional SD diagnostics drain the same redacted ring in a separate worker after a successful card self-test; see `SD_DIAGNOSTICS.md`. Keep secrets out of both with `printlnRedacted`. Future SD consumers must serialize with that worker.
- **Host tests and `main.cpp`:** `scenarios.cpp` `#include`s `main.cpp` and links the other application modules, so tests see the same `TurnHubAtlas` globals as firmware.

**Identity model:** Profile (persistent: ID, name, PIN hash, stats) → Participant (one per person at the current table) → controller assignments (physical Sigil seat A/B, browser sessions, future app). Hardware identity is never player identity. Changing controllers must not replace the participant or move its stats. Multiple browser sessions can control one participant.

**Persistence:** profiles, PIN data, seat bindings, statistics, game settings, profile policy, per-player accessibility preferences, AP config, the pairing window, the speaker volume (`spkvol`), the first-run setup stage (`setup`, see `FIRST_RUN_SETUP.md`) and the touchscreen calibration (`atlas-touch/cal2`) live in NVS (`profile_store`, `profile_stats_storage`, `nvs_blob_store`, `game_settings_store`, `optional_preferences`). Sessions, table participation and live game state are RAM-only, apart from the in-progress interrupted-match recovery record (`game_checkpoint`/`game_recovery*`). That record must restore **paused** and must never replay the stats-completion callback. The engine's game-completed event invokes `profile_stats_bridge` once per match, whatever the ending path; the durability limit is described below.

**microSD card:** optional; play never depends on it. Luxury records (detailed statistics, `s<profileId>`) and the rotating diagnostics log live there; the NVS core record `c<profileId>` keeps games played and won without a card. All card access goes through `sd_card.cpp`: application code uses only `sdBlobStore()` (from the application task), whose calls take the same card lock as the background log worker. Never call the Arduino `SD` library directly elsewhere.

**Completion ordering:** `profile_stats_bridge.cpp` must successfully commit the
finished-match checkpoint before incrementing profile statistics. Failed or
uncertain checkpoint writes skip statistics and log `SKIPPED_CHECKPOINT`.
Restoration never replays completion; a cut during later profile writes can leave
partial/missing results. Do not claim crash-safe exactly-once statistics until
durable completion receipts and replay-safe persistence exist. See
`COMPLETION_RECOVERY.md` and `PROTOTYPE_V1_VERIFICATION.md` in the engineering docs.

**No compatibility before release:** every Atlas and Sigil is a prototype the owner reflashes together. Backward compatibility (older protocol versions, older firmware, OTA upgrade paths) applies only once hardware is released, and saved data (profiles, stats, pairings) needs no migrations until the owner says TurnHub is saving live stats: a changed layout means a factory reset. Remove code that only served retired devices or formats rather than keeping fallbacks.

**Radio contract:** `shared/include/protocol.h` is the single source for Atlas and Sigil (both `platformio.ini` files and the host test runners add `-I../shared/include`). Put any value both firmwares must agree on there, e.g. `PAIRING_WINDOW_MS` (60 s, the minimum on every device) or the Hello capability bits (only what varies between Sigils: OLED vs e-paper and the test harness; Atlas assumes the rest and reads the byte through `helloCapabilities()`). Never recreate per-project copies (Invariant 4). Changing it means reflashing both device types. The packet structs are packed, and host tests check their sizes (7-byte control, 110-byte display).

**Client contract:** live HTTP is `GET /api/v1/state`, `GET /api/v1/info` and form-based session controls such as `POST /api/control/pass`. State carries a `revision` scoped to `atlasId` + `bootId`. The JSON Intent envelope (`intent-v0.1.schema.json`), `POST /api/v1/intent` and the events WebSocket are **drafts, not implemented**. Android `protocol/` models mirror these JSON schemas, not the C++ types.

## Project process rules (from `Documentation/engineering/README.md`)

- **Feature gate:** before a significant feature, define:
  1. state owner
  2. Intent
  3. validator
  4. persistence owner
  5. rendering clients
  6. protocol/contract change
  7. third-party dependency impact (update `Documentation/legal/`)
  8. accessibility impact (see `ACCESSIBILITY.md`)
- **Structural changes:** review the engineering index, `ARCHITECTURAL_INVARIANTS.md`, `STAGED_CHANGES.md` and the affected reference docs first. If code would contradict the docs, resolve the docs explicitly rather than drifting.
- **Where work is tracked:** agreed but unimplemented work goes in `Documentation/engineering/STAGED_CHANGES.md`, not long-lived branches. Abandoned experiments are marked historical, not deleted from history.
- **Accessibility:** no essential info or action may rely on only color, sound or LED cadence. Accessible alternatives use the same Intent path. Web UIs target WCAG 2.2 AA.
- **Size history:** on firmware-version bumps and large feature commits, append a snapshot to `SIZE_AND_CHANGE_HISTORY.md`.
- **Confidence labels:** engineering docs label claims *Verified*, *Reconstructed*, *Planned*, *Experimental* or *Needs verification*. Host tests passing is not hardware acceptance, so don't mark hardware behavior as verified.
