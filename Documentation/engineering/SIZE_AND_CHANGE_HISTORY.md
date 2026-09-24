# TurnHub Size and Change History

This document tracks engineering growth over time without replacing Git history.
Git commits remain the authoritative record of exact changes. This file captures
repeatable size snapshots, firmware footprint measurements, and notable milestones
so growth can be reviewed without reconstructing every checkpoint manually.

## What to track

At meaningful development checkpoints, record:

- commit SHA and firmware version
- raw source-file sizes for Atlas and Sigil firmware
- largest or fastest-growing source files
- PlatformIO RAM and flash usage when a real build is available
- major added/removed behavior at that checkpoint
- notable commit diff size for large milestones

Raw source sizes are Git blob byte sizes, not compiled firmware size. They are useful
for identifying concentration and growth, while PlatformIO RAM/flash measurements
remain the relevant device-capacity figures.

Update this file at release candidates, firmware-version changes, major architecture
milestones, and unusually large feature commits. It does not need an entry for every
small documentation or bug-fix commit.

## Baseline snapshot: 2026-09-21

Checkpoint: `810731439c85b5a9d6c3d1295df5583c3edeee1c`
Atlas firmware line: `0.6.0-dev`

### Source footprint

| Scope | Files | Raw bytes |
| --- | ---: | ---: |
| `Atlas/src` | 18 | 257,579 |
| `Sigil/src` | 2 | 29,349 |

Largest current implementation files:

| File | Raw bytes |
| --- | ---: |
| `Atlas/src/main.cpp` | 62,912 |
| `Atlas/src/web_api.cpp` | 43,176 |
| `Atlas/src/web_pages.cpp` | 42,129 |
| `Sigil/src/main.cpp` | 18,014 |
| `Atlas/src/game_engine.cpp` | 13,851 |
| `Atlas/src/led_renderer.cpp` | 13,448 |
| `Atlas/src/sigil_bus.cpp` | 12,517 |
| `Sigil/src/sigil_display.cpp` | 11,335 |
| `Atlas/src/ota_manager.cpp` | 11,180 |
| `Atlas/src/profile_store.cpp` | 10,771 |

`main.cpp`, `web_api.cpp`, and `web_pages.cpp` are the main concentration points to
watch during hardening. File size alone is not a refactor trigger, but continued
growth should prompt a boundary review before adding another unrelated responsibility.

### Comparison with ESP32-port merge baseline

Checkpoint: `6844f94910c6e25d4e4ddd044119a4b7afeb8d2a`

| Scope | Merge baseline | 2026-09-21 | Change |
| --- | ---: | ---: | ---: |
| `Atlas/src` raw bytes | 228,180 | 257,579 | +29,399 (+12.9%) |
| `Atlas/src` files | 14 | 18 | +4 |
| `Atlas/src/main.cpp` | 56,159 | 62,912 | +6,753 |
| `Atlas/src/web_api.cpp` | 36,199 | 43,176 | +6,977 |
| `Atlas/src/web_pages.cpp` | 39,868 | 42,129 | +2,261 |
| `Atlas/src/lobby.cpp` | 8,090 | 9,408 | +1,318 |
| `Atlas/src/profile_store.cpp` | 9,269 | 10,771 | +1,502 |

The growth from the merge baseline primarily reflects profile-independent browser
login/participation, logical controller registration, storage boundaries, and related
verification support rather than duplication of the game engine.

## Historical firmware footprint records

These values are preserved from engineering verification documents and should not be
silently compared across toolchain changes without noting the build environment.

| Checkpoint | RAM | Flash | Notes |
| --- | ---: | ---: | --- |
| Intent migration verification, 2026-09-19 | 47,908 bytes | 924,281 bytes | Clean Atlas build before LED follow-up |
| Pairing-LED mock follow-up | 47,924 bytes | 924,797 bytes | Five-second visual PairRequest mock added |
| Identity/storage foundation | 47,940 bytes | 925,265 bytes | Blob/NVS statistics boundary and identity groundwork |
| Current `0.6.0-dev` profile-login build | not yet recorded here | not yet recorded here | Record next confirmed PlatformIO build output |
| Local profile-policy slice, 2026-09-21 | 51,628 bytes | 953,345 bytes | Uncommitted changes based on `0cbb707`, still `0.6.0-dev`; Atlas build only, no flash |
| Local game profiles and life totals, 2026-09-21 | 51,748 bytes | 967,273 bytes | Includes profile-policy slice and diagnostics; same toolchain, uncommitted `0.6.0-dev` based on `0cbb707`; build only, no flash |
| Local accounts, moderation and portal cleanup, 2026-09-21 | 51,764 bytes | 983,505 bytes | Includes prior local slices; same toolchain and baseline; focused tests/build passed, not flashed |
| Local life approval and Commander counters, 2026-09-22 | 59,036 bytes | 1,012,233 bytes | Based on `34a0363`, branch `codex/manual-v02-gameplay`; same toolchain, native/storage/browser checks and adapter audit passed; not flashed |

The local profile-policy build used Espressif32 7.1.3, Arduino ESP32
`4.20017.260907+sha.dcc1105b`, and Xtensa toolchain `8.4.0+2021r2-patch5`.
It is not a measurement of the unchanged profile-login baseline above.

A future automated build should capture RAM/flash usage directly into CI or a generated
artifact so this ledger does not depend on manual transcription.

## Notable change ledger

### `6844f94` - Atlas ESP32 port merged

- ESP32 Atlas becomes the accepted `master` baseline.
- Authoritative Intent migration, ESP-NOW Sigil transport, portal, profile/statistics,
  OTA, LED/audio, and native regression framework are integrated.

### `a9b0555` - profile login, virtual play, identity/storage expansion

Large feature commit: 2,256 additions and 429 deletions across the repository.

- firmware version advances to `0.6.0-dev`
- browser profile registration/login no longer requires a physical Sigil
- phone-only and mixed physical/phone games share Atlas Intent handlers
- controller identity is separated from physical radio module identity internally
- NVS blob/storage boundaries and additional identity groundwork are introduced
- native and portal smoke verification are expanded
- physical profile selection is explicitly documented as the next remaining reuse gap

### 2026-09-21 documentation sync

The engineering record was brought into line with current `0.6.0-dev` behavior:

- Prototype 1.0 field-test priority lane added
- interrupted-match recovery target documented
- current persistence limitations clarified
- `controllerId` terminology corrected in Intent documentation
- pairing/recovery verification items added
- root and Atlas READMEs updated to stop describing the Raspberry Pi as the current Atlas

### 2026-09-24 - turn timer and LED/audio cue layers (`android/testing`, uncommitted)

Large feature change; firmware version unchanged (`0.6.0-dev`).

- Atlas-owned turn timer (Off/presets/custom), 10 s warning, non-gameplay expiry cue
- LED selection separated from styling (`led_cues.h`); audio cue profile with mute
- `gamecfg` schema 2; HTTP state/settings additions; no radio-contract change
- Android player slice UI (sign in, join, PASS, pause/resume, sign out, host timer)

Atlas `pio run -e atlas`, same toolchain, measured against the parent commit `cac3a68`:

| Build | RAM | Flash |
|---|---:|---:|
| `cac3a68` (before) | 73,860 B (22.5%) | 1,046,965 B (79.9%) |
| This change | 74,780 B (22.8%) | 1,053,261 B (80.4%) |

The RAM growth is the two static default cue tables.

### 2026-09-24 - web portal redesign and selectable themes (`claude/tender-cerf-84yd8v`)

Large presentation change; firmware version unchanged (`0.6.0-dev`). No gameplay,
Intent, storage or protocol change.

- New shared `/theme.css` (`THEME_CSS`) linked by portal, login, stats, dev and update pages
- Four per-browser themes: Brass (default steampunk), Midnight, Parchment, High contrast
- Portal relayout: brass turn gauge, life tiles, phone bottom tab bar, themed dialogs
- See [Web Portal Design System](WEB_PORTAL_DESIGN.md)

Embedded page text (raw-string literals in `web_pages.cpp`, `profile_login_page.cpp`,
`stats_page.cpp`, `ota_manager.cpp`): 92,995 B → 131,098 B (+38,103 B, all in flash).
Compiled RAM/flash was **not measured** for this change: no PlatformIO build was
possible in the authoring environment. Record a `pio run -e atlas` figure before
treating the ~3-point flash increase over 80.4% as confirmed.
The first measured build that contains this change is in the reorganization
entry below (83.7% flash).

### 2026-09-24 - code review and module reorganization (`claude/code-review-cleanup-cn4zoy`)

Major architecture milestone; firmware version unchanged (`0.6.0-dev`). Behavior,
Intents, storage schemas and the radio contract are unchanged, apart from removing
the unused `/api/device/persistence` endpoint and the always-false `persistentA`
device field (see [Physical Profile Selection](PHYSICAL_PROFILE_SELECTION.md)).

- `main.cpp` split along the Intent boundary into the modules declared in
  `atlas_app.h`; `handleTableIntent` becomes focused per-IntentType handlers.
  `main_internal_fwd.h` and its `-include` flag are gone.
- `web_api.cpp` split into session, profile, game and admin modules behind
  `web_api_internal.h`; shared JSON escaping in `json_text.h`.
- Duplicates merged (debounce, Wi-Fi password store, JSON escapers, reconnect,
  error responses, life bound `LIFE_LIMIT`); unused `SigilBus::injectEvent` removed.
- `audit_adapters.py` now scans the adapter modules and rejects direct handler calls.

Measured against the branch base `fdd8659` (Git blob bytes):

| Scope | `fdd8659` | This change |
| --- | ---: | ---: |
| `Atlas/src` | 25 files, 433,424 B | 36 files, 443,177 B |
| `Atlas/include` | 36 files, 116,388 B | 39 files, 142,494 B |
| `Sigil/src` | 2 files, 38,058 B | 2 files, 38,882 B |
| `Atlas/src/main.cpp` | 91,005 B | 14,803 B |
| `Atlas/src/web_api.cpp` | 72,178 B | 7,477 B |

The largest logic file is now `table_intents.cpp` (27,131 B); `web_pages.cpp`
(115,176 B of embedded portal text) is unchanged apart from one dead function.
Source growth is interface documentation and one-statement-per-line formatting.
Compiled RAM/flash, measured afterwards on the same branch (after a C++11
constructor fix in `front_panel.cpp`, the only compile error the reorganization
left), with the same toolchain as above (Espressif32 7.1.3, Arduino ESP32
`4.20017.260907+sha.dcc1105b`, Xtensa `8.4.0+2021r2-patch5`). The Atlas figure
also includes the portal redesign and the 16 KB serial-log ring, neither of
which had been measured, so it is not a pure delta for the reorganization:

| Build | RAM | Flash |
|---|---:|---:|
| Atlas `pio run -e atlas` | 91,340 B (27.9%) | 1,097,033 B (83.7%) |
| Sigil `pio run -e sigil` | 48,360 B (14.8%) | 769,037 B (58.7%) |
| Sigil `pio run -e sigil-wokwi` | 48,336 B (14.8%) | 793,713 B (60.6%) |

Build only; nothing was flashed.

### 2026-09-24 - per-player Sigil accessibility (`claude/code-review-cleanup-cn4zoy`)

Large feature change. Sigil firmware `0.5.3-dev` -> `0.5.4-dev` (adjustable hold
timing needs a Sigil reflash); Atlas stays `0.6.0-dev`; radio protocol version 1
with a new backward-compatible `InputTiming = 24` packet.

- Per-player Sigil sound, light style and hold times stored with the profile
  (`x<profileId>`), merged per Sigil; reduced-motion and monochrome-safe LED
  profiles; per-Sigil mute; `ActionRequired` emitted
- `GET/POST /api/session/accessibility`; portal Sigil accessibility card,
  reduce-motion switch and OS-contrast default; Android editor and high-contrast theme
- New `Atlas/src/sigil_accessibility.cpp`, `Atlas/include/accessibility_prefs.h`

Same toolchain as the reorganization entry above, measured before and after:

| Build | Before | After |
| --- | ---: | ---: |
| Atlas RAM | 91,340 B (27.9%) | 92,724 B (28.3%) |
| Atlas flash | 1,097,033 B (83.7%) | 1,111,241 B (84.8%) |
| Sigil RAM | 48,360 B (14.8%) | 48,360 B (14.8%) |
| Sigil flash | 769,037 B (58.7%) | 769,285 B (58.7%) |
| Sigil (Wokwi) flash | 793,713 B (60.6%) | 793,957 B (60.6%) |

The Atlas RAM growth is the two extra static LED profiles and per-Sigil state;
the flash growth is mostly portal text. Build only; nothing was flashed.

## Tracking rules

1. Git history is authoritative; this document is a milestone ledger, not a substitute.
2. Source byte growth is diagnostic, not a quality score.
3. Record compiled RAM/flash only from a known build and include toolchain context when it changes.
4. Do not optimize only to make numbers smaller; preserve architectural boundaries first.
5. When a large file grows because multiple responsibilities accumulated, review whether a
   documented module boundary already exists before refactoring.
6. Preserve historical measurements even when a later refactor reduces them.
7. At Prototype 1.0 release-candidate time, record a fresh Atlas/Sigil source snapshot,
   compiled RAM/flash usage, protocol version, and the exact release commit/tag.

Last updated: 2026-09-24
