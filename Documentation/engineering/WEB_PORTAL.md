# Web Portal

The administration interface Atlas serves at `192.168.4.1`. Personal and shared-tablet play, player preferences/statistics and Game Master moderation live in the Android app. It is presentation only:
every action goes through the same HTTP handlers and Intents as the app, and
Atlas validates everything. The shared design system (tokens, icons, fonts,
components) is `design/` (see `design/README.md`).

## Firmware owns the portal

Atlas 0.7.7-dev embeds the complete administration UI in its application image.
There is one signed Atlas update for firmware and web UI. The microSD card is
optional for administration, including initial-Admin setup, Developer hardware
tests, diagnostics and local signed Atlas/Sigil uploads. No portal pack, separate
portal version or installer is needed; old files on a card are never served.
Factory reset may erase them with the rest of the card without affecting the UI.
Reload existing browser tabs after updating firmware.

| Route | Embedded page / policy |
|---|---|
| `/portal` | `index.html`, `Cache-Control: no-store` |
| `/login` | administrator sign-in, no-store |
| `/update`, `/sigil-update` | signed firmware upload, after Admin check |
| `/dev` | diagnostics/hardware tests, after Developer check |
| `/tablet`, `/stats` | same administration home; browser play/statistics retired |
| `/assets/...` | content-hashed assets, `public, max-age=31536000, immutable` |

Restricted pages first serve an authentication shell; their contents require
the session header. Page gzip bytes are sent directly from flash with explicit
lengths and content types, without decompression or SD reads. Fonts, icons,
styles and font licenses are embedded too. An unknown asset returns 404.
`?classic=1` no longer selects another UI. `/api/portal` and
`/api/portal/install` are retired. HTTP gameplay/tablet APIs remain available
for native clients; permissions stay Atlas-owned.

## Building and previewing

| Path | Purpose |
|---|---|
| `Atlas/web/src/` | Single source for pages, styles, scripts, manifest and icons |
| `Atlas/web/build.py` | Builds/checks assets and generates the C++ header; standard library only |
| `Atlas/tools/embed_web.py` | PlatformIO pre-build hook; writes `generated/web_assets.h` in the build directory |
| `Atlas/src/web_pages.cpp` | Serves generated PROGMEM assets with binary-safe lengths |

```
pio run -d Atlas -e atlas              # generates and embeds all assets automatically
python3 Atlas/web/build.py             # dist/site/ for browser checks
python3 Atlas/web/build.py --check      # CI: validate in memory, write nothing
python3 Atlas/web/build.py --serve      # local preview; APIs proxied to a real Atlas
```

Builds gzip text with a fixed timestamp when compression saves space. Content
hashes and stored bytes are reproducible. The generated header is written only
when contents change, so unchanged assets do not force recompilation. Host
runners generate the same header; browser checks rebuild the preview each time
to avoid testing stale assets. Firmware partition-size checks enforce fit in
both existing 1,966,080-byte OTA app slots. No new filesystem partition is used.

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
- **Test contract:** `portal_smoke.cjs` covers the actual embedded HTML with/without a card:
  Admin/ordinary account visibility, bootstrap, explicit hardware save,
  unsaved-choice preservation and absence of gameplay requests.
  `maintenance_smoke.cjs` checks local firmware uploads, per-Sigil selection
  and Developer hardware controls with authenticated requests. Both use
  Playwright/Chromium; `PLAYWRIGHT_EXECUTABLE_PATH` selects the browser.
  The former browser gameplay/Commander/tablet smokes are retired; Atlas host
  and Android tests cover those contracts.
- **Dependencies:** no web fonts or frameworks are downloaded from the
  internet; fonts and icons ship in firmware from `design/`. No third-party browser script is required.

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
Atlas firmware to use these controls.

## Status

Host and browser checks cover flash asset serving, restricted pages, administration and cardless bootstrap/update access.
Still open: golden screenshot tests for the app and portal, theme packs from
the card, and HTTPS ([Staged Changes](STAGED_CHANGES.md)).
