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

### 2026-09-24 - draw ending, forgetting pairings, pairing window (`android/testing`)

Sigil firmware `0.5.4-dev` -> `0.5.5-dev` (10-second Pair hold forgets; honors
`Unpair`); Atlas stays `0.6.0-dev`; radio protocol version 1 with a new
backward-compatible `Unpair = 12` packet.

- `EndMatch` (5 s master-button hold ends a match as a draw), `ForgetPairing`
  and `ConfigurePairing` Intents; `Draw` last-game result (byte 4, v1-compatible);
  recovery validator accepts a finished match without a winner
- `POST /api/device/forget`, `GET/POST /api/pairing`; portal Paired Sigils card;
  new header `Atlas/include/pairing_settings.h`

PlatformIO on Linux; "before" is `master` at `af5d770` built with the same
toolchain (it reproduces the previous entry's Atlas and Sigil figures):

| Build | Before | After |
| --- | ---: | ---: |
| Atlas RAM | 92,724 B (28.3%) | 92,780 B (28.3%) |
| Atlas flash | 1,111,241 B (84.8%) | 1,119,653 B (85.4%) |
| Sigil RAM | 48,360 B (14.8%) | 48,360 B (14.8%) |
| Sigil flash | 769,285 B (58.7%) | 770,509 B (58.8%) |
| Sigil (Wokwi) RAM | 48,368 B (14.8%) | 48,440 B (14.8%) |
| Sigil (Wokwi) flash | 795,441 B (60.7%) | 798,109 B (60.9%) |

Build only; nothing was flashed.

### 2026-09-24 - Atlas moves to the E32R28T display board (`android/testing`)

Large hardware change. Atlas stays `0.6.0-dev`, and the radio protocol is unchanged.

- New board pin map in `config.h`; Pair button and front-panel LEDs removed;
  BOOT (IO0) is the master button
- New `Atlas/src/atlas_display.cpp` (LovyanGFX 1.2.30, DejaVu fonts): TurnHub
  splash
- Partition table `default.csv` -> `min_spiffs.csv` (app slot 1,310,720 B ->
  1,966,080 B), so the percentages below use different denominators

Same PlatformIO toolchain as the entry above. "Before" is that entry's "after":

| Build | Before | After |
| --- | ---: | ---: |
| Atlas RAM | 92,780 B (28.3%) | 94,728 B (28.9%) |
| Atlas flash | 1,119,653 B (85.4% of 1.25 MB) | 1,220,625 B (62.1% of 1.875 MB) |

About 100 KB of the growth is LovyanGFX plus the two fonts. Build only; nothing
was flashed.

### 2026-09-24 - Atlas touchscreen controls (`android/testing`)

Feature change; Atlas stays `0.6.0-dev`, radio protocol unchanged.

- Screen flipped to rotation 3; status screen after the splash
- New `Atlas/src/touch_controls.cpp` (host-tested adapter + screen model):
  Pair, Pass, Pause/Resume, end-match hold, BOOT-hold countdown
- Bit-banged XPT2046 reads in `atlas_display.cpp`

| Build | Before | After |
| --- | ---: | ---: |
| Atlas RAM | 94,728 B (28.9%) | 95,000 B (29.0%) |
| Atlas flash | 1,220,625 B (62.1%) | 1,233,741 B (62.8%) |

Build only; nothing was flashed.

### 2026-09-24 - No master button, Atlas speaker, OLED one-player limit (`android/testing`)

Feature change; Atlas stays `0.6.0-dev`, Sigil `0.5.6-dev`. Radio protocol
version unchanged; new Hello capability bit `CAPABILITY_DISPLAY_OLED` (0x10).

- Master button removed: the touchscreen's 3 s Unlock admin hold opens a 60 s
  physical-presence window; End match hold replaces the BOOT draw hold
- New `Atlas/src/atlas_speaker.cpp` (DAC cosine tones) and the `ConfigureSpeaker`
  Intent / `spkvol` setting; table-wide cues play on Atlas
- Atlas refuses Seat B on OLED Sigils and blocks a start with a leftover one

| Build | Before | After |
| --- | ---: | ---: |
| Atlas RAM | 99,764 B (30.4%) | 99,788 B (30.5%) |
| Atlas flash | 1,300,721 B (66.2%) | 1,307,673 B (66.5%) |

"Before" is the microSD step-1 build. Build only; nothing was flashed.

### 2026-09-25 - Sigil-rendered light, joystick, status ring, speaker

Sigil `0.5.6-dev` -> `0.6.0-dev`; Atlas stays `0.6.0-dev`. Radio protocol
version unchanged; new `LedState = 25` packet and Hello capability bit
`CAPABILITY_LED_STATE` (0x20).

- Atlas sends semantic `LedState` to capable Sigils; the Sigil renders it
  (`sigil_led.cpp`) to an RGB LED and, on the E-ink build, the NeoPixel Jewel
- E-ink `sigil` build: analog joystick input and Jewel status ring
  (Adafruit NeoPixel 1.15.5); Pair moves to the DevKit BOOT button (GPIO0)
- Atlas speaker: LEDC square wave with louder volume steps instead of the DAC sine

| Build | After |
| --- | ---: |
| Atlas RAM | 99,860 B (30.5%) |
| Atlas flash | 1,308,245 B (66.5%) |
| Sigil E-ink (`sigil`) RAM | 48,808 B (14.9%) |
| Sigil E-ink (`sigil`) flash | 796,397 B (60.8%) |
| Sigil OLED (`sigil-oled`) RAM | 44,736 B (13.7%) |
| Sigil OLED (`sigil-oled`) flash | 798,969 B (61.0%) |

Atlas and the OLED Sigil were flashed; the E-ink Sigil was built only.

### 2026-09-25 - Sigil menus

Sigil `0.6.0-dev` -> `0.7.0-dev`; Atlas stays `0.6.0-dev`. Radio protocol
version unchanged; new `SelectAction = 13` and `MenuState = 26` packets and
Hello capability bit `CAPABILITY_MENU` (0x40).

- Atlas works out each menu Sigil's available actions (`sigil_menu.cpp`) and
  dispatches choices through the existing Intents (`handleSelectAction`)
- Sigil five-key input (joystick on E-ink, new five-button d-pad on OLED),
  e-ink compass legend and OLED menu list (`sigil_menu.cpp`), hold-to-confirm
  with ring progress

| Build | After |
| --- | ---: |
| Atlas RAM | 99,956 B (30.5%) |
| Atlas flash | 1,310,389 B (66.6%) |
| Sigil E-ink (`sigil`) RAM | 49,040 B (15.0%) |
| Sigil E-ink (`sigil`) flash | 799,973 B (61.0%) |
| Sigil OLED (`sigil-oled`) RAM | 44,944 B (13.7%) |
| Sigil OLED (`sigil-oled`) flash | 802,801 B (61.2%) |

All three were flashed (Atlas COM12, E-ink COM13, OLED COM3).

### 2026-09-25 - Touch fix, test harness, screen overhaul, statistics split (`android/testing`)

Firmware versions unchanged (Atlas `0.6.0-dev`, Sigil `0.7.0-dev`; harness
`0.1.0`). Radio protocol version 1, extended additively: `CAPABILITY_HARNESS`
(0x80), `HarnessReport = 14`, `HarnessCommand = 27`.

- Atlas touch read samples XPT2046 DOUT while the clock is high (it raced the
  chip's bit change); calibration key `atlas-touch/cal2`. *Verified* by the
  owner on hardware.
- `TestHarness/`: two virtual menu Sigils (station and soft-AP MACs) play whole
  games against a real Atlas; premade tests start from the Atlas touchscreen.
- Atlas screen overhaul: header with a NO SD CARD warning, names, life, turn
  clock, player chips, Info and QR code screens.
- Sigils share `sigil_icons.h`; the e-ink screens get the OLED's look.
- Statistics split: core counts in NVS (`c<profileId>`), detail on the microSD
  card, with a checked migration.

| Build | After |
| --- | ---: |
| Atlas RAM | 100,924 B (30.8%) |
| Atlas flash | 1,329,957 B (67.6%) |
| Sigil E-ink (`sigil`) RAM | 48,976 B (14.9%) |
| Sigil E-ink (`sigil`) flash | 803,053 B (61.3%) |
| Sigil OLED (`sigil-oled`) RAM | 45,056 B (13.8%) |
| Sigil OLED (`sigil-oled`) flash | 811,353 B (61.9%) |
| Test harness RAM | 43,736 B (13.3%) |
| Test harness flash | 751,501 B (57.3%) |

Measured NVS use on the owner's Atlas: 130 of 630 entries (one profile). The
largest flash items are web assets (portal HTML 103 KB uncompressed, theme CSS
19 KB, stats page 10 KB); see the microSD item in STAGED_CHANGES.

All four boards were flashed at the end of the session.


### 2026-09-25 - Optional SD diagnostics (experimental, `codex/sd-diagnostics`)

Atlas stays `0.6.0-dev`; Sigil firmware and radio protocol are unchanged.
Redacted serial capture can drain to four rotating SD logs, with boot markers,
explicit overflow reporting and self-test gating. Authoritative NVS data is
unchanged. The worker requests a 6,144-byte dynamic task stack, including a
2,048-byte drain buffer; this is not a measured total RAM delta. No new library.

Host storage and serial-stream scenarios cover retention, restarts, redaction,
overflow and injected failures. Firmware RAM/flash figures are unavailable in
this environment; a firmware build and physical SD acceptance are still required.

Merged into `android/testing` with the statistics split, which shares the card
through a lock in `sd_card.cpp` (2026-09-25). The combined Atlas build: RAM
100,980 B (30.8%), flash 1,333,357 B (67.8%). Physical SD acceptance is still
pending.

### 2026-09-25 - Touchscreen table actions and master pass

Atlas stays `0.6.0-dev`; Sigil firmware and radio protocol are unchanged. The
touchscreen gains Start, Cancel start, Rematch and Reset between games and an
in-game Table screen; its Pass button is replaced by a 2 s Master pass hold
(new `MasterPass` Intent, logged as `ATLAS|GAME|MASTER_PASS`). Each Atlas
speaker level is about 10% quieter. Atlas build: RAM 101,124 B (30.9%), flash
1,346,697 B (68.5%).

### 2026-09-25 - E-ink profile picker and Atlas screen flicker fix

Sigil `0.8.0-dev` (was `0.7.0-dev`); Atlas stays `0.6.0-dev`. Radio protocol
version stays 1, with two new packets: `PickerKey = 15` and the 51-byte
`ProfilePicker = 33`. Atlas gains `profile_picker.cpp` and the `PickProfile`
Intent; the TFT draws the turn clock, countdown bar and hold bars over old
pixels instead of clearing them. Atlas build: RAM 104,468 B (31.9%), flash
1,350,885 B (68.7%). E-ink Sigil build: RAM 49,032 B (15.0%), flash 806,165 B
(61.5%).

### 2026-09-26 - Prototype v1 completion guard and verification checklist

Branch: `codex/prototype-v1-stabilization`, based on `959a09b`.
Atlas remains `0.6.0-dev`; Sigil firmware and all storage/radio/HTTP schemas are
unchanged. The production completion bridge commits Game Over before profile
increments and skips statistics on failed/uncertain checkpoint writes. This
prevents the reproduced duplicate count but does not replay missing results.
The recovery owner reuses its existing buffers; no new persistent record or
third-party dependency is added to the firmware.

Source snapshot (production `.cpp` / `.h` files only): Atlas `src/`: 46 files,
660,368 bytes / 14,949 lines; Atlas `include/`: 53 files, 199,856 bytes / 4,568
lines. Host tests and documentation are excluded from these totals.

All six host suites, the 32-adapter audit and the 10-response/9-fixture contract
check pass. Atlas suites were rebuilt with GCC 13.3.0, C++14, ASan and UBSan;
LeakSanitizer is unavailable in this execution runtime. Hardware acceptance is
pending. Atlas firmware build passes for source commit `56c058f` (published as `199c467` with the identical source tree): RAM
**105,156 B (32.1%)**, flash **1,366,405 B (69.5% of the 1,966,080 B slot)**.
Environment: PlatformIO 6.2.0, Espressif32 7.1.3, Arduino framework
`4.20017.260907+sha.dcc1105b`, Xtensa GCC `8.4.0+2021r2-patch5`, LovyanGFX
1.2.30. Built locally with telemetry disabled; no board was flashed. These are
absolute measurements, not a before/after firmware delta.
See [the line-item checklist](PROTOTYPE_V1_VERIFICATION.md).

### 2026-09-29 - Brass display theme, Atlas Menu and screen previews

Branch: `claude/modest-mendel-c7ym34`, based on `db1e4f8`. Atlas remains
`0.6.0-dev`, Sigil firmware version unchanged; no radio, storage, HTTP or
schema change. The portal's Brass look now covers the Atlas TFT and both
Sigils. New: `Atlas/src/atlas_art.cpp` (drawing split out of
`atlas_display.cpp`), generated `brass_fonts.h` for Atlas (VLW, 71,280 B of
font data) and Sigil (GFXfont, 11,530 B), `tools/fonts/make_fonts.py`, and
host screen previews for both firmwares (also run in CI). The Atlas screen
model gains a Menu screen, the round, the match clock and per-player turn
time; the lobby's persistent QR code is gone.

Source snapshot (production `.cpp` / `.h`, excluding the generated
`brass_fonts.h`): Atlas `src/` + `include/`: 105 files, 896,810 bytes /
20,419 lines. Generated font headers: Atlas 438,366 bytes, Sigil 69,157 bytes
of source text.

Firmware builds (PlatformIO 6.2.0, same toolchain as 2026-09-26; no board
flashed), before -> after on this branch:
- Atlas: flash **1,384,437 -> 1,474,169 B (+89,732; 75.0% of 1,966,080)**,
  RAM 105,684 -> 107,140 B. Heap at run time also holds the header sprite
  (16,640 B), the gauge sprite (7,904 B) and the fonts' glyph tables (~5 KB).
- Sigil e-ink (`sigil`): flash 830,433 -> 844,725 B (+14,292; 64.4%), RAM
  unchanged at 49,576 B.
- Sigil OLED (`sigil-oled`): flash 840,681 -> 841,161 B (+480), RAM unchanged.

All Atlas and Sigil host suites, the 32-adapter audit and both screen
previews pass (the Atlas preview's nine incremental-redraw checks match full
redraws pixel for pixel). Hardware acceptance of the new look is pending.

### 2026-09-30 - OLED Sigil in the Brass fonts

Same branch, after `51e4028`. The OLED Sigil now uses Cinzel and Oswald where
they fit (header title, filled tickets, names, status big lines, life
figures), with the built-in font as the measured fallback. Four OLED fonts
are added to `tools/fonts/make_fonts.py` and `Sigil/include/brass_fonts.h`
regenerated: `OledHeader` (Cinzel Black 10 px), `OledName` (Cinzel 15 px),
`OledLifeMid` (Oswald 22 px), `OledLifeSmall` (Oswald 18 px). Sigil font data
grows from 11,530 to 15,250 bytes; Atlas fonts are unchanged byte for byte.

Firmware builds (local PlatformIO 6.2.0, same toolchain; no board flashed):
- Sigil OLED (`sigil-oled`): flash **841,161 -> 849,221 B (+8,060; 64.8% of
  1,310,720)**, RAM unchanged at 45,352 B. The CI build of `51e4028` was
  reported as 847,840 B; the local toolchain measures that commit at 841,161 B.
- Sigil e-ink (`sigil`) and `sigil-wokwi`: unchanged (844,725 B and
  852,281 B); the OLED fonts are not linked into them.

Sigil host suites (OLED stub now checks custom-font glyph bounds and
overlaps), Atlas host suites and the Sigil screen previews pass.

### 2026-09-30: Atlas RAM reduction

After the playtest crashes (see the playtest notes, section G), Atlas's RAM
use was cut. CI builds (PlatformIO 6.2.0, espressif32 7.1.3, Arduino-ESP32
2.0.17). Static RAM went from 109,748 to 90,196 B, and flash from 1,512,720 to
1,412,896 B, since the portal and CSS are now stored gzipped. Measured on Atlas
with no phones connected, free heap after start-up went from ~48 KB to 99 KB
(largest block 98 KB). Per-step costs are now: SD card 21 KB, Wi-Fi hotspot
54 KB, ESP-NOW 13 KB, web server 2.6 KB.

### 2026-09-30: First-run guided setup (branch `oobe-guided-setup`)

Setup stages, the Welcome and "You're all set" touchscreen screens,
`GET /api/setup` and `POST /api/setup/finish` (see
[First-run setup](FIRST_RUN_SETUP.md)). CI build of the branch (run
36679680047; PlatformIO 6.2.0, espressif32 7.1.3, Arduino-ESP32 2.0.17):
Atlas static RAM 90,228 B, flash 1,411,185 B of 1,966,080 (71.8%). The
previous entry's figures came from an earlier commit on `turnhub-integration`;
the setup code itself is small (a two-byte NVS record, one Intent, two routes,
one screen). No setup web page was added: the Android app is the setup client.

### 2026-09-30: Baseline Sigil, Sigil 0.9.0 (branch `oobe-polish`)

Atlas assumes the features every Sigil has; the Hello capability byte keeps
only OLED vs e-paper, d-pad vs thumbstick and the test harness
([Protocol and Pairing](PROTOCOL_AND_PAIRING.md), "Baseline Sigil"). Removed:
Atlas's three-button gesture adapter and pause-to-win arm, the per-channel LED
stream and Atlas's copy of the LED cadence tables, the old `MenuState`, every
capability and 0.8 firmware check; on the Sigil the three-button fallback, the
menu/picker/ring build switches, old `MenuState`, legacy LED channels and the
one-LED view. About 1,570 source lines out, 505 in (tests included). Local
builds (PlatformIO 6.2.0, espressif32 7.1.3, Arduino-ESP32 2.0.17):

| Build | Static RAM | Flash |
| --- | --- | --- |
| Atlas | 88,132 B (was 90,228) | 1,403,485 B (was 1,411,293) |
| Sigil e-ink (`sigil`) 0.9.0 | 53,620 B | 920,489 B |
| Sigil OLED (`sigil-oled`) 0.9.0 | 49,396 B | 924,957 B |
| `sigil-wokwi` (now joystick + ring) | 50,576 B | 878,173 B |
| Harness 0.9.0 | 44,544 B | 777,133 B |

Protocol version stays 2.

## 2026-09-30 BOOT button: pair, unpair, factory reset

Atlas `0.6.2-dev` -> `0.6.3-dev`, Sigil `0.9.2-dev` -> `0.9.3-dev`. One shared
`three_part_button.h` state machine drives the Sigil Pair button and Atlas's BOOT
button: quick press pairs (on release), 3 s unpairs, 10 s factory resets
(replacing the old 10 s forget-only hold). Atlas gains a BOOT-button adapter
(`updateBootButton`) and lets `AtlasHardware` forget all Sigils and factory reset
Atlas without an Admin; the OLED Sigil's menu list ends in a local Factory
reset. Local builds (PlatformIO 6.2.0, espressif32 7.1.3, Arduino-ESP32 2.0.17):

| Build | Static RAM | Flash |
| --- | --- | --- |
| Atlas 0.6.3 | 88,116 B | 1,399,941 B |
| Sigil e-ink (`sigil`) 0.9.3 | 53,628 B | 920,809 B |
| Sigil OLED (`sigil-oled`) 0.9.3 | 49,404 B | 925,217 B |
| `sigil-wokwi` | 50,584 B | 878,461 B |
| Harness | 44,552 B | 779,177 B |

Protocol version stays 3.

## 2026-10-02 Compass on both Sigils, device menu

Sigil `0.9.3-dev` -> `0.9.4-dev`. The OLED Sigil's scrolling action list is
replaced by the e-ink's compass (`MenuLayout` removed); the OLED shows the
legend one key at a time on its bottom row, stepped by the display task's idle
work. Both Sigils gain a device-local compass device menu (Menu on a free
Up/Down key outside a game: Factory reset held 5 s, Back), so the e-ink can
factory reset from its screen too. Undo pass moves to the click on both. The
OLED profile picker stays a list. Manual V0.7. Local builds (PlatformIO 6.2.0,
espressif32 7.1.3, Arduino-ESP32 2.0.17):

| Build | Static RAM | Flash |
| --- | --- | --- |
| Sigil e-ink (`sigil`) 0.9.4 | 53,548 B | 920,901 B |
| Sigil OLED (`sigil-oled`) 0.9.4 | 49,564 B | 932,721 B |
| `sigil-wokwi` | 50,512 B | 878,565 B |

Atlas and the harness are unchanged. Protocol version stays 3: nothing on the
radio changed.

## 2026-10-02 Unpair everywhere, joystick pairing, Atlas Device screen

Atlas `0.6.3-dev` -> `0.6.4-dev` (also covering the update-notice-everywhere
work merged before it, which kept 0.6.3), Sigil `0.9.5-dev` -> `0.9.6-dev`.
Sigil device menu: Unpair held 3 s on the click, Factory reset moved to Down
(5 s). An unpaired Sigil opens its pairing window when the thumbstick click is
held 3 s (BOOT may be inside a case); the Unpaired screens say so. Atlas's
touchscreen gains Menu > Device with held Unpair Sigils (3 s) and Factory reset
(10 s), the BOOT button's gestures as the same AtlasHardware Intents;
`MAX_TOUCH_BUTTONS` 6 -> 7. Manual V0.9. Local builds (PlatformIO 6.2.0,
espressif32 7.1.3, Arduino-ESP32 2.0.17):

| Build | Static RAM | Flash |
| --- | --- | --- |
| Atlas 0.6.4 | 88,244 B | 1,405,041 B |
| Sigil e-ink (`sigil`) 0.9.6 | 53,556 B | 921,553 B |
| Sigil OLED (`sigil-oled`) 0.9.6 | 49,572 B | 933,209 B |
| `sigil-wokwi` | 50,520 B | 879,213 B |

Protocol version stays 3.

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

Last updated: 2026-10-02
