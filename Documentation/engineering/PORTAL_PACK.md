# Web Portal Pack

The V1 web portal ships as a signed pack that Atlas unpacks onto its microSD
card and serves at `/portal`. It is the only portal: the built-in flash portal
was removed on 2026-10-02 (owner decision, to save flash). Without a pack,
`/portal` serves a small install page (`web_pages.cpp` `INSTALL_PORTAL_HTML`):
sign in, verify at the table, upload the pack. The TurnHub app does not need
the pack.

Status: **Implemented** in source (2026-10-02, V1 phase 1). Host tests cover the
installer and both portals in the browser smoke checks. Install, serving and
fallback on Atlas hardware **need verification**.

This is presentation only: no Intent, validator or game state changes. The pack
needs no protocol change for Sigils or Android.

## Why a pack

Every Atlas ships with a card (owner decision 2026-10-02), so the portal's
fonts, icons and stylesheets no longer have to fit in flash. A pack is updated
separately from firmware: a portal change does not need an Atlas flash.

## Source and build

| Path | What |
|---|---|
| `Atlas/web/src/` | Portal pages (`index.html`, `login.html`, `stats.html`, `update.html`, `sigil-update.html`, `dev.html`) and their local files: `portal.css` styles every page from the design tokens, `theme-boot.js` sets the theme before first paint, `manifest.webmanifest` and `icons/` let a phone add the portal to its home screen |
| `Atlas/web/VERSION` | Pack version, `major.minor.patch`; raise it for every pack you install over another |
| `Atlas/web/build.py` | Builds, checks, signs and previews the pack (standard library only) |
| `design/` | Tokens, components, fonts and icons the pages pull in (`design/README.md`) |

```
python3 Atlas/web/build.py                  # dist/site/ (unpacked) and dist/portal-<version>.bin
python3 Atlas/web/build.py --check          # CI: build and check, write nothing
python3 Atlas/web/build.py --serve          # preview at http://127.0.0.1:8080 against a real Atlas (192.168.4.1)
tools\sign-local.cmd -Products portal       # signed dist package in Private\TurnHub-builds
node Atlas/tests/host/portal_smoke.cjs   # smoke checks against the pack (built first when missing)
PORTAL_RENDERS=1 node Atlas/tests/host/counter_smoke.cjs   # also screenshots every theme to tests/host/build/render-*.png
```

Stylesheets and local files referenced from a page become content-hashed
assets under `/assets/`; `build.py`'s docstring lists the markers. Text files
are stored gzip with a fixed timestamp, so a build is reproducible.

## Look (V1 phase 2)

The pack portal kept the former flash portal's markup, element IDs, labels and
scripts, so both smoke checks run against it unchanged; only the styling and
the theme choice differ. Themes: Automatic (follow the device: Graphite when
dark, Daylight when light, High contrast when it asks for more), Graphite,
Daylight, Brass and High contrast. A saved Midnight or Parchment choice from
the flash portal maps to Graphite or Daylight. The turn hero is a ring in the
modern themes and the original brass gauge in Brass.

## Pages (V1 phases 3 and 4)

Implemented 2026-10-02. Host smoke checks cover the portal and sign-in pages;
the other pages and the first-run steps **need verification** on Atlas.

- **Players:** one row per player with life and turn state. The QR codes open
  in an Invite players sheet. Connect a Sigil is a list of numbered steps whose
  ticks come from what Atlas reports (signed in, a Sigil paired, a seat on your
  profile).
- **My Account:** a profile header with roles and a win-rate summary from
  `/api/session/stats`. Tapping a theme previews it; Save appearance keeps it.
- **Device Settings:** an inline Verify at the table card (show the code,
  type it, stop), Atlas facts with links to the update pages, Wi-Fi, paired
  Sigils, Atlas hardware, a Reset group and account permissions.
- **First-run setup:** while `GET /api/setup` reports `welcome`, the Game view
  opens with the same steps as the app: account, presence code (first Admin),
  and the table's own Wi-Fi password through `POST /api/setup/finish`. Pairing
  and updates stay on the Atlas screen and in the app. Skip for now hides it
  for the browser session.
- **Sign-in, statistics, updates and Developer:** the built-in pages
  restyled, with the same element IDs and scripts. Sign-in adds a Sign in /
  Create account switch, Show buttons, a live PIN-or-password check
  (`validPin`'s rule) and remembers the last account on that phone.
- **Home screen and screen-on:** the manifest and Apple meta tags let a phone
  add TurnHub to its home screen. Android only opens it full screen, and the
  browser only grants the screen wake lock the Game view asks for during a
  match, over HTTPS (secure context). Until Atlas serves HTTPS (planned) both
  quietly fall back: a home-screen shortcut, and the phone's own screen
  timeout.

## Package and archive

The pack is a `.thfw` package (`SIGIL_OTA.md`, "Firmware package") with
product 4, `portal`. It is signed with the same key and checked against the
same public key as firmware. Its image is an archive (`Atlas/include/portal_pack.h`):

```
Descriptor   16 bytes  "THFWDSC1", product 4, version, radio 0, reserved
"THWEBAR1"    8 bytes
u16 fileCount (1..512), u16 reserved (0)
per file: u8 pathLength (1..96), u8 flags (bit 0: gzip), u32 size, path, data
```

Paths use letters, digits, `.`, `_`, `-` and `/`, with no empty, `.` or `..`
part. `VERSION` is reserved for Atlas, and `index.html` is required. A whole
pack is at most 16 MB.

## Installing

Upload the `.thfw` on the Atlas `/update` page. Atlas recognizes the product
and sends it to `POST /api/portal/install`. The rules match firmware updates:
an Admin account, verified at the table, with the table in Lobby or Game Over.

Atlas checks the signature and hash while it streams the archive into
`/turnhub/portal/stage`. Only when everything checks out does it write
`VERSION` and swap the folders: `live` becomes `old`, `stage` becomes `live`,
then `old` is removed. If a swap step fails, the previous `live` is put back.
A failed or abandoned upload leaves the installed portal untouched. As with
firmware, a pack must be the same version as the installed one or newer; to
roll back, build the older source with a raised `VERSION`.

`GET /api/portal` reports `{card, installed, version}`. Factory reset keeps
`/turnhub/portal` (`wipeSdCard`).

## Serving

| Route | Served from |
|---|---|
| `/portal` | the pack's `index.html` (no-cache), else the install page |
| `/login`, `/update` | the pack's `login.html`, `update.html`, else the built-in page kept in flash so a pack can be installed (`/update` after the Admin check) |
| `/stats`, `/sigil-update`, `/dev` | the pack's copy, else the install page (`/sigil-update` and `/dev` after the Admin or Developer check) |
| any of those with `?classic=1` | the built-in page or install page, skipping the pack (a damaged pack can still be replaced) |
| `/assets/...` | the pack, `Cache-Control: immutable` (names are content-hashed) |

A gzip copy is sent with `Content-Encoding: gzip`. Card reads take the same
lock as the diagnostics log worker (`sd_card.cpp`).
