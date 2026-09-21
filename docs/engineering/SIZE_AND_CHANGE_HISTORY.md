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

Last updated: 2026-09-21
