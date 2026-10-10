# Web Portal

The administration interface Atlas serves at `192.168.4.1`. Personal and shared-tablet play, player preferences/statistics and Game Master moderation live in the Android app. It is presentation only:
every action goes through the same HTTP handlers and Intents as the app, and
Atlas validates everything. The shared design system (tokens, icons, fonts,
components) is `design/` (see `design/README.md`).

## Two portals

- **The v2 administration pack** lives on the microSD card: a signed pack built from
  `Atlas/web/` and `design/`, updated separately from firmware (owner,
  2026-10-02). Every Atlas ships with a card.
- **The basic administration portal** stays in flash (`web_pages.cpp`
  `BASIC_PORTAL_HTML`) for missing/failed cards and old packs. It provides
  initial-Admin bootstrap, device/network settings, signed firmware/portal
  installation and recovery. It has no personal gameplay/accessibility UI.
  Flash also keeps administrator sign-in and signed update pages.

Cutover: Atlas 0.7.6+ serves only portal packs with major version 2 or later,
including assets. An installed v1 pack stays on the card but is never served;
install a signed v2 pack through the fallback. The installer also requires
v2.0.0 or the installed version, whichever is newer. Update firmware and pack
together and reload existing tabs. HTTP gameplay and tablet APIs remain available
for native clients; this is a presentation cutover, not a new authorization rule.

| Route | Served from |
|---|---|
| `/portal` | the pack's `index.html` (no-cache), else the basic portal |
| `/login`, `/update`, `/sigil-update` | the pack's copy, else the built-in page (`/update` and `/sigil-update` after the Admin check) |
| `/dev` | the pack’s copy, else the basic portal, after the Developer check |
| `/tablet`, `/stats` | flash administration fallback; retired browser play/statistics |
| any of those with `?classic=1` | the built-in page, skipping the pack, so a damaged pack can be replaced |
| `/assets/...` | the pack, `Cache-Control: immutable` (content-hashed names) |
| `/theme.css` | flash (`THEME_CSS`) |

Restricted pages first serve an authentication shell; their contents need the
session header. Card reads take the SD card lock.

## Building and installing the pack

| Path | What |
|---|---|
| `Atlas/web/src/` | Pages (`index.html`, `login.html`, `update.html`, `sigil-update.html`, `dev.html`), `portal.css`, `theme-boot.js`, the web manifest and icons |
| `Atlas/web/VERSION` | Pack version; raise it for every pack installed over another |
| `Atlas/web/build.py` | Builds, checks, signs and previews (standard library only) |

```
python3 Atlas/web/build.py              # dist/site/ and dist/portal-<version>.bin
python3 Atlas/web/build.py --check      # CI: build and check, write nothing
python3 Atlas/web/build.py --serve      # preview at 127.0.0.1:8080 against a real Atlas
tools\sign-local.cmd -Products portal   # signed package in Private\TurnHub-builds
```

The pack is a `.thfw` package, product 4 ([Firmware Updates](FIRMWARE_UPDATES.md)),
signed with the firmware key. Its image is an archive
(`Atlas/include/portal_pack.h`): the descriptor, `THWEBAR1`, a file count
(1-512), then per file a path (letters, digits, `.`, `_`, `-`, `/`; no `..`),
a gzip flag, a size and the data. `index.html` is required; at most 16 MB.
Text files are gzipped with a fixed timestamp, so builds are reproducible.

Upload it on `/update`, or let the app's update step install it from the
release feed ([Firmware Updates](FIRMWARE_UPDATES.md)); either way it reaches `POST /api/portal/install`
(Admin verified at the table, Lobby or Game Over). Atlas checks signature and
hash while streaming into `/turnhub/portal/stage`, then swaps `live` → `old`,
`stage` → `live`, and removes `old`; a failed step puts the previous `live`
back. Same version or newer only. `GET /api/portal` reports
`{card, installed, version}`. Factory reset keeps the pack.

## Look and layout

The home page is administration, with an Android installation link and Admin
sign-in. Ordinary signed-in profiles see a message directing play to the app.
Admin content includes network/device settings, pairing decisions, account roles
and archive/restore, and explicit game selection for table resets. No personal
seat claims, life/Commander edits, timers, invitations to web play or moderation
controls are generated. Initial-Admin account creation remains available before
bootstrap is complete. Existing authentication/permission checks on APIs remain
Atlas-owned. Developer diagnostics and hardware tests require Developer, including
for an Admin; the portal links them only when that bit is present.

Automatic/Graphite/Daylight/Brass/High contrast themes remain browser-local. Setup
uses the existing account, presence and Wi-Fi endpoints. All signed package file
pickers and device-specific update selection remain on the maintenance pages.

## Rules

- **Settings never save on change.** A control stages a choice; an explicit
  Save sends it, confirms in words (status line and toast) and says where it
  was stored. Background refreshes must not overwrite a staged, unsaved
  choice (use a dirty flag). The Android app follows the same rule.
- **Accessibility:** every colored state also has text; touch targets at least
  44 px; visible focus; a skip link; real labelled `<input>`s; help text via
  `aria-describedby`; `prefers-reduced-motion` and the per-browser Reduce
  motion switch; `forced-colors` support. A design target toward WCAG 2.2 AA,
  not a conformance claim.
- **Test contract:** `portal_smoke.cjs` covers the actual SD and flash HTML:
  Admin/ordinary account visibility, bootstrap, explicit hardware save,
  unsaved-choice preservation and absence of gameplay requests. It uses
  Playwright/Chromium; `PLAYWRIGHT_EXECUTABLE_PATH` selects the browser.
  The former browser gameplay/Commander/tablet smokes are retired; Atlas host
  and Android tests cover those contracts.
- **Dependencies:** no web fonts or frameworks are downloaded from the
  internet; fonts and icons ship in the pack from `design/`. The vendored
  `qrcode-generator` 1.4.4 (MIT) is the only third-party script.

## Hardware tests

Open Developer (`/dev`) and select Atlas or an online Sigil. Buzzer Test
plays a short tone; Screen Test shows solid fills and a checkerboard before
restoring the current screen. Atlas also shows red, green and blue fills.
Sigil Status Light Test cycles the Jewel ring through red, green, blue and
white for six seconds. Screen tests ignore normal input until controls are
released. These are visual/audible checks: inspect the physical device to
judge the result. E-paper takes several full refreshes.

Apps may use the same Developer-authenticated endpoint:
`POST /api/device/test?target=atlas&test=screen` or
`POST /api/device/test?target=sigil&module=0&test=lights`.
Tests are `buzzer`, `screen`, and (Sigil only) `lights`; module IDs are
zero-based. Offline, spare and harness Sigils are rejected. HTTP success
means the request was started/sent, not that the hardware passed. Update
both the firmware and the SD portal pack to use these controls.

## Status

Host smoke checks cover both portals and the installer; the pack and cardless administration share the cutover checks.
Still open: golden screenshot tests for the app and portal, theme packs from
the card, and HTTPS ([Staged Changes](STAGED_CHANGES.md)).
