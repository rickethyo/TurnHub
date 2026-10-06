# Web Portal

The browser interface Atlas serves at `192.168.4.1`. It is presentation only:
every action goes through the same HTTP handlers and Intents as the app, and
Atlas validates everything. The shared design system (tokens, icons, fonts,
components) is `design/` (see `design/README.md`).

## Two portals

- **The V1 portal pack** lives on the microSD card: a signed pack built from
  `Atlas/web/` and `design/`, updated separately from firmware (owner,
  2026-10-02). Every Atlas ships with a card.
- **The basic portal** stays in flash (`web_pages.cpp` `BASIC_PORTAL_HTML`,
  about 14 KB, gzip) so a failed or missing card still leaves the essentials:
  game status and the player's own controls (join, start, pass, pause, life,
  concede, rematch), accessibility preferences, device settings (verify at the
  table, speaker, pairing window, Wi-Fi password, forget Sigils, return to
  lobby, factory reset), links to the firmware pages and the pack installer.
  Flash also keeps the sign-in, firmware and Sigil firmware pages and the
  shared `/theme.css`.

| Route | Served from |
|---|---|
| `/portal` | the pack's `index.html` (no-cache), else the basic portal |
| `/login`, `/update`, `/sigil-update` | the pack's copy, else the built-in page (`/update` and `/sigil-update` after the Admin check) |
| `/stats`, `/dev` | the pack's copy, else the basic portal (`/dev` after the Developer check) |
| any of those with `?classic=1` | the built-in page, skipping the pack, so a damaged pack can be replaced |
| `/assets/...` | the pack, `Cache-Control: immutable` (content-hashed names) |
| `/theme.css` | flash (`THEME_CSS`) |

Restricted pages first serve an authentication shell; their contents need the
session header. Card reads take the SD card lock.

## Building and installing the pack

| Path | What |
|---|---|
| `Atlas/web/src/` | Pages (`index.html`, `login.html`, `stats.html`, `update.html`, `sigil-update.html`, `dev.html`), `portal.css`, `theme-boot.js`, the web manifest and icons |
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

Upload it on `/update`; Atlas routes it to `POST /api/portal/install`
(Admin verified at the table, Lobby or Game Over). Atlas checks signature and
hash while streaming into `/turnhub/portal/stage`, then swaps `live` → `old`,
`stage` → `live`, and removes `old`; a failed step puts the previous `live`
back. Same version or newer only. `GET /api/portal` reports
`{card, installed, version}`. Factory reset keeps the pack.

## Look and layout

- **Themes:** Automatic (follow the device: Graphite when dark, Daylight when
  light, High contrast when it asks for more), Graphite, Daylight, Brass and
  High contrast. The choice is per browser (`localStorage`), never sent to
  Atlas. Themes are only token sets on `html[data-theme]`; components never
  hard-code colors. The turn hero is a ring in the modern themes and the
  brass gauge in Brass. Decorative graphics are `aria-hidden`; the same
  information is in text.
- **Phones:** one column, a bottom tab bar, the primary action (Join / Pass
  turn / Resume) as a large button in My seat. **Wide screens (≥1000 px):**
  main column (stage, life, Commander damage) plus a side column. The sticky
  header must not use `backdrop-filter` on phones (it would capture the fixed
  tab bar).
- **Where things live:** sign-in and the account menu at top right; table
  actions only in My seat; game setup under the stage in the lobby; roster,
  invite QR codes, Sigil attachment and Game Master tools on Players; name,
  secret, privacy, theme, motion, Sigil accessibility and statistics on My
  Account; Wi-Fi, devices, permissions and firmware on Device Settings
  (Admin), which also has an inline Verify at the table card.
- **First-run setup:** while `GET /api/setup` reports `welcome`, the Game
  view opens the same steps as the app (account, presence code, Wi-Fi
  password). Pairing and updates stay on the Atlas screen and in the app.
- **Home screen:** a manifest and Apple meta tags let a phone add the portal
  to its home screen. Full-screen launch and the screen wake lock need HTTPS,
  which Atlas doesn't serve yet; both quietly fall back.

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
- **Test contract:** `portal_smoke.cjs` and `counter_smoke.cjs` drive the
  pack by element IDs, accessible names and a few page globals
  (`refreshAll`, `sessionInfo`, `gameSettingsData`, `counterData`). Keep them
  stable when restyling. `basic_portal_smoke.cjs` covers the flash portal.
  `PORTAL_RENDERS=1 node Atlas/tests/host/counter_smoke.cjs` screenshots every
  theme.
- **Dependencies:** no web fonts or frameworks are downloaded from the
  internet; fonts and icons ship in the pack from `design/`. The vendored
  `qrcode-generator` 1.4.4 (MIT) is the only third-party script.

## Status

Host smoke checks cover both portals and the installer; the pack is the portal in daily use.
Still open: golden screenshot tests for the app and portal, theme packs from
the card, and HTTPS ([Staged Changes](STAGED_CHANGES.md)).
