# Web Portal Pack

The V1 web portal ships as a signed pack that Atlas unpacks onto its microSD
card and serves at `/portal`. The portal in flash (`web_pages.cpp`
`PORTAL_HTML`) stays as the fallback, so a table never loses its portal to a
missing card or a bad pack.

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
| `Atlas/web/src/` | Portal pages (`index.html`) and their local files; `portal.css` styles the portal from the design tokens |
| `Atlas/web/VERSION` | Pack version, `major.minor.patch`; raise it for every pack you install over another |
| `Atlas/web/build.py` | Builds, checks, signs and previews the pack (standard library only) |
| `design/` | Tokens, components, fonts and icons the pages pull in (`design/README.md`) |

```
python3 Atlas/web/build.py                  # dist/site/ (unpacked) and dist/portal-<version>.bin
python3 Atlas/web/build.py --check          # CI: build and check, write nothing
python3 Atlas/web/build.py --serve          # preview at http://127.0.0.1:8080 against a real Atlas (192.168.4.1)
tools\sign-local.cmd -Products portal       # signed dist package in Private\TurnHub-builds
PORTAL_PACK=1 node Atlas/tests/host/portal_smoke.cjs   # smoke checks against the pack
PORTAL_PACK=1 PORTAL_RENDERS=1 node Atlas/tests/host/counter_smoke.cjs   # also screenshots every theme to tests/host/build/render-*.png
```

Stylesheets and local files referenced from a page become content-hashed
assets under `/assets/`; `build.py`'s docstring lists the markers. Text files
are stored gzip with a fixed timestamp, so a build is reproducible.

## Look (V1 phase 2)

The pack portal keeps the flash portal's markup, element IDs, labels and
scripts, so both smoke checks run against it unchanged; only the styling and
the theme choice differ. Themes: Automatic (follow the device: Graphite when
dark, Daylight when light, High contrast when it asks for more), Graphite,
Daylight, Brass and High contrast. A saved Midnight or Parchment choice from
the flash portal maps to Graphite or Daylight. The turn hero is a ring in the
modern themes and the original brass gauge in Brass. The footer links to
`/portal-classic`.

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
| `/portal` | the pack's `index.html` (no-cache), else the flash portal |
| `/portal-classic` | always the flash portal |
| `/assets/...` | the pack, `Cache-Control: immutable` (names are content-hashed) |

A gzip copy is sent with `Content-Encoding: gzip`. Card reads take the same
lock as the diagnostics log worker (`sd_card.cpp`).
