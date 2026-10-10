# TurnHub

TurnHub is a local-first tabletop game-management system: turn timer, lobby,
life totals and Commander damage, player profiles and statistics. An
authoritative **Atlas** table controller runs the game; player-facing
**Sigils**, phones and tablets (the Android app) and the Atlas
touchscreen request the same semantic actions from it.

**Current firmware:** Atlas **0.6.6**, Sigil **0.9.8** (protocol 3), on `master`.
Signed releases are published on GitHub Releases and installed from the
Android app or the Atlas portal.

Earlier Python/Raspberry Pi generations are recorded in
[Generation History](Documentation/engineering/history/GENERATION_HISTORY.md). They are
not part of this repository or the current runtime.

## Where to look

| Path | What |
|---|---|
| [`Atlas/`](Atlas/README.md) | ESP32 table controller: game engine, Wi-Fi AP, web portal and HTTP API, ESP-NOW radio, NVS and optional microSD storage |
| [`Sigil/`](Sigil/DISPLAY.md) | ESP32 player controller, e-ink and OLED variants ([Wokwi simulation](Sigil/WOKWI.md)) |
| [`Android/`](Android/README.md) | Native client: table view, player controls, guided setup and firmware updates |
| [`protocol/`](protocol/README.md) | Client contract: JSON schemas, [`http-v1.md`](protocol/http-v1.md), example responses |
| [`shared/include/`](shared/include) | Firmware headers shared by Atlas and Sigil (radio contract, firmware package format) |
| [`TestHarness/`](TestHarness/README.md) | Retired hardware test harness; historical source only, unsupported and excluded from builds |
| [`KiCad/`](KiCad/PCB/Sigilv1/README.md) | Sigil carrier PCB and schematics |
| [`Documentation/engineering/`](Documentation/engineering/README.md) | The engineering record: invariants, design references, staged work |
| [`Documentation/User Manual/`](Documentation/User%20Manual) | The user manual (newest `.docx`; the app bundles the same text) |

## Core architectural rule

**Atlas owns canonical game and table state.** Sigils, phones, the Atlas
touchscreen and simulators submit Intents and render Atlas state.
They never advance turns, resolve wins, own player identity or keep competing
game engines. See [Architectural Invariants](Documentation/engineering/ARCHITECTURAL_INVARIANTS.md).

```text
Profile (optional persistent person)
  -> Participant (one person at the current table)
    -> Controller assignment(s): physical Sigil seat, browser sessions, the app
```

Changing controllers never replaces the participant or moves its statistics.

## What works today

- Lobby with any-seated-player start (cancellable countdown), starter choice,
  turn passing with a three-second cancellable grace, pause/resume, concession,
  elimination, win claim/confirm, draw by the Atlas End match hold, rematch and reset.
- Game profiles (Generic, Magic, Commander, Yu-Gi-Oh!) with life totals,
  Commander damage and cross-player life requests the recipient approves.
- Turn timer with warning and expiry cues on Sigil lights and buzzers, the
  Atlas speaker and phones, with per-player accessibility preferences.
- Local profiles with PINs, phone-only and mixed phone/Sigil play, a Sigil
  profile picker, Admin/Game Master/Developer permissions, and table presence
  codes for protected actions.
- Deliberate pairing (60 s minimum window) over an encrypted Atlas-Sigil link,
  with forget/unpair and factory reset from menus, the portal and the BOOT button.
- Interrupted-match recovery: a power cut restores the match paused.
- Core statistics in NVS; detailed statistics, avatars and rotating
  diagnostics on an optional microSD card.
- Signed firmware updates for Atlas and both Sigil variants, delivered from
  the Android app or the portal, plus a first-run guided setup in the app.

Known limits are tracked in [Staged Changes](Documentation/engineering/STAGED_CHANGES.md).
The most important: statistics are not yet crash-safe exactly-once
([completion ordering](Documentation/engineering/STORAGE_AND_RECOVERY.md)).

## Persistence

Survives restart: profiles, PIN hashes, permissions and policy, core
statistics, seat bindings, pairings, game settings, accessibility
preferences, the Wi-Fi password, speaker volume, setup stage and touch
calibration (NVS); detailed statistics and diagnostics (microSD, optional).

RAM only: browser sessions, lobby participation and live game state, except
the interrupted-match recovery record, which restores paused and never
replays statistics.

## Building and testing

Commands for every target are in [`CLAUDE.md`](CLAUDE.md) and each
directory's README. CI (`.github/workflows/ci.yml`) runs the host suites,
contract checks, supported firmware builds and the Android tests; see
[Continuous Integration](Documentation/engineering/CONTINUOUS_INTEGRATION.md).

## Local-first

The core play path never needs a cloud service, subscription or internet
connection. Atlas serves the table on its own Wi-Fi. The broader ownership,
repairability, privacy and accessibility direction is in
[Product Principles](Documentation/PRODUCT_PRINCIPLES.md).

## Status

TurnHub is a prototype. Every Atlas and Sigil is reflashed together,
so there is no backward compatibility before release: a changed protocol or
storage layout means reflashing everything and, for storage, a factory reset.
