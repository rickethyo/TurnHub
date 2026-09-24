# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

TurnHub is a local-first tabletop game-management system (turn timer, lobby, life totals, profiles and statistics). It is a multi-target monorepo:

| Dir | What | Toolchain |
|---|---|---|
| `Atlas/` | ESP32 table controller: **the authoritative game engine**, Wi-Fi AP, web portal and HTTP API, ESP-NOW radio to Sigils, NVS persistence | PlatformIO, Arduino ESP32, C++ |
| `Sigil/` | ESP32 player controller: buttons, LEDs, buzzer, e-ink (GxEPD2), ESP-NOW | PlatformIO, Arduino ESP32, C++ |
| `Android/` | Native client bootstrap (Compose UI over `MockAtlasRepository`, no networking yet) | Gradle, Kotlin |
| `shared/include/` | Firmware headers shared by Atlas and Sigil (currently `protocol.h`, the ESP-NOW radio contract) | C++ |
| `protocol/` | Transport-neutral client contract: JSON schemas, `http-v1.md`, example responses | — |
| `PiLogger/` | Optional Raspberry Pi telemetry recorder (non-authoritative; must never be required for gameplay) | Python |
| `KiCad/` | Sigil PCB/schematic, plus Python scripts in `tools/` that build and verify the schematic | KiCad, Python |
| `Documentation/engineering/` | The durable engineering record: design decisions, invariants, staged work, verification backlog | — |

Earlier Python/Raspberry Pi generations are deliberately **not in this repo**; they survive only as history in `Documentation/engineering/GENERATION_HISTORY.md`. Don't reintroduce references to a `Controller/` directory, or its tooling (root `requirements.txt`, the Visual Studio Python project, the pySerial dependency).

`Documentation/User Manual/*.docx` is the user-facing manual. When a user-visible timing or behavior changes (e.g. the 15 s pairing window, the 15 s life-change approval `LIFE_APPROVAL_MS`), keep the code, its UI/API messages, the engineering docs and the manual in agreement.

The owner also uses GitHub Desktop, which can switch branches and auto-stash uncommitted work as `!!GitHub_Desktop<branch>`. If files suddenly revert mid-session, check `git branch --show-current` and `git stash list` before redoing anything.

## Commands

### Atlas firmware (run from `Atlas/`)
```
pio run -e atlas                      # build
pio run -e atlas --target upload      # flash (add --upload-port COM7 on Windows if needed)
pio device monitor                    # serial, 115200 baud
```
Portal at `192.168.4.1` on the `TurnHub-Atlas` AP.

### Atlas host regression tests (no hardware)
These compile the **real** `main.cpp`, `GameEngine`, `Lobby`, `IntentDispatcher`, HTTP handlers and storage code against stubs in `tests/host/stubs/` and `tests/host/storage_stubs/`. They produce three executables: `scenarios` (gameplay/login/recovery), `storage_scenarios` and `profile_store_scenarios`.
```
Atlas\tests\host\run-gcc.ps1          # PowerShell, uses PlatformIO's toolchain-gccmingw32 (or -Compiler <g++>)
Atlas\tests\host\run.cmd              # from an x64 VS Native Tools prompt (MSVC)
python Atlas/tests/host/audit_adapters.py          # guard: adapters must not bypass the dispatcher
python Atlas/tests/host/check_client_contract.py   # after the host suite: validate build/client-*.json + protocol/examples against schemas
node Atlas/tests/host/portal_smoke.cjs             # optional Playwright + Edge browser smoke
node Atlas/tests/host/counter_smoke.cjs            # optional two-context life/Commander UI smoke
```
There is no per-test filter. To run one group, build the single executable (the command lines are in `tests/host/README.md`).
- **Adding a new `src/*.cpp` to Atlas:** add it to the source lists in `run.cmd`, `run-gcc.ps1` and the README command lines too. Those lists also carry the `shared/include` path.
- **Python:** `python` isn't on PATH on this machine (it hits the Microsoft Store stub). Use PlatformIO's bundled interpreter, `%USERPROFILE%\.platformio\penv\Scripts\python.exe`, which runs both `.py` checks with the standard library only.
- **Packed-struct flag:** the GCC runner uses `-mno-ms-bitfields` and C++14 to preserve the packed radio packet layout. Keep it.
- **Test ordering:** the recovery scenario runs last on purpose (the recovery store is a one-way process-wide latch).

### Sigil firmware (run from `Sigil/`)
```
pio run -e sigil                      # real hardware
pio run -e sigil-wokwi                # Wokwi simulation build (ESP-NOW replaced by wokwi_espnow_shim.h, which acts as a fake Atlas)
```
Wokwi serial-console commands for driving the simulated Atlas are listed in `Sigil/WOKWI.md`.

### Android (run from `Android/`)
```
./gradlew assembleDebug
./gradlew testDebugUnitTest           # plain JVM tests
./gradlew testDebugUnitTest --tests "com.turnhub.android.data.MockAtlasRepositoryTest"
```

### PiLogger
`python -m turnhub_logger.main --config config.toml` (copy from `config.example.toml`).

## Architecture: rules that span files

**Atlas owns all canonical state.** Sigils, browsers, Android, simulators and the Atlas master button capture input and render state. They never decide game outcomes. Read `Documentation/engineering/ARCHITECTURAL_INVARIANTS.md` before structural work; violating it is treated as a regression.

**Intent pipeline (Atlas):**
```
transport adapter (ESP-NOW packet / HTTP handler / GPIO)
  -> TurnHub::Intent {type, actor, payload}        include/intent.h
  -> IntentDispatcher (fixed table, one handler per IntentType; a second bind is rejected)
  -> handle*Intent in main.cpp  -> GameEngine / Lobby mutation
  -> observer hook -> checkpointGame() (recovery record)
```
- **Adapters stay thin.** Adapters such as `handleWebControl`, `handlePass` and `processSigilEvents` must only build Intents. `audit_adapters.py` fails if an adapter calls `game.*`/`lobby.*` mutators, assigns `hubState`/`pendingPass` and similar state, or calls transition helpers directly. New gameplay actions need an `IntentType`, a handler bound in `main.cpp` setup, and host scenarios.
- **Ownership layout:** handler bindings live in `main.cpp` (the large, still-monolithic translation unit). `GameEngine` owns turn, timer, life and win state. `Lobby` owns participants, seats and starter selection. `web_api.cpp` holds the HTTP surface, and `sigil_bus.cpp` holds the ESP-NOW transport.
- **Forward declarations:** `main_internal_fwd.h` is force-included via `build_src_flags` so handlers can reference helpers defined later in `main.cpp`.

**Identity model:** Profile (persistent: ID, name, PIN hash, stats) → Participant (one per person at the current table) → controller assignments (physical Sigil seat A/B, browser sessions, future app). Hardware identity is never player identity. Changing controllers must not replace the participant or move its stats. Multiple browser sessions can control one participant.

**Persistence:** profiles, PIN data, seat bindings, statistics, game settings, profile policy and AP config live in NVS (`profile_store`, `profile_stats_storage`, `nvs_blob_store`, `game_settings_store`, `optional_preferences`). Sessions, table participation and live game state are RAM-only, apart from the in-progress interrupted-match recovery record (`game_checkpoint`/`game_recovery*`). That record must restore **paused** and must never replay the stats-completion callback. Stats are committed once from the engine's game-completed event (`profile_stats_bridge`), whatever the ending path.

**Radio contract:** `shared/include/protocol.h` is the single source for Atlas and Sigil (both `platformio.ini` files and the host test runners add `-I../shared/include`). Put any value both firmwares must agree on there, e.g. `PAIRING_WINDOW_MS` (15 s). Never recreate per-project copies (Invariant 4). Changing it means reflashing both device types. The packet structs are packed, and host tests check their sizes (7-byte control, 110-byte display).

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
