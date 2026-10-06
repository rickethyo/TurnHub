# Firmware Updates, Signing and Spare Sigils

How Atlas, Sigils and the portal pack are packaged, signed, released and
installed over the air, and how a spare Sigil is parked and brought back.
USB flashing and board identification are in [Hardware](HARDWARE.md).

## Overview

- Every update is a signed `.thfw` package. Each device checks the signature
  itself before installing anything.
- **Atlas** installs its own firmware and the portal pack from the portal's
  `/update` page or the app. **Sigils** update through Atlas: Atlas stages the
  package and the Sigil downloads it over Atlas's Wi-Fi.
- The **Android app carries updates**: it reads the public release feed while
  the phone is online, then installs on Atlas and each Sigil at the table.
  Atlas never needs the internet.
- All updates need an Admin verified at the table and a table in Lobby or
  Game Over, one job at a time.
- Signed releases have shipped since `v0.9.1` (2026-09-30). Ordinary updates
  of every variant, and refusal of wrong-variant, unsigned, altered and older
  packages, are verified in use (owner, 2026-10-02). Interrupted transfers and
  rollback (**U06**) are still to test.

## Package format (`.thfw`)

A 128-byte signed header, then the plain `firmware.bin` (or the portal
archive). Little-endian. Code: `shared/include/firmware_package.h`,
`tools/firmware/thfw.py`.

```
off size field
  0   8  magic "THFWPKG1"
  8   1  header format (1)
  9   1  product: 1 Atlas, 2 Sigil e-ink, 3 Sigil OLED, 4 portal pack
 10   3  version major, minor, patch
 13   1  radio protocol VERSION (0 for the portal)
 14   2  flags (0)
 16   4  image size
 20  32  image SHA-256
 52   8  build ID (first 8 bytes of the commit)
 60   4  key ID (first 4 bytes of SHA-256 of the public key)
 64  64  ECDSA P-256 signature (r || s) over SHA-256 of bytes 0-63
128   -  image
```

Each build also embeds a 16-byte descriptor (`THFWDSC1`, product, version,
radio protocol); the packaging tool requires exactly one and copies product
and version from it, so renaming a file can't relabel an image. Wokwi,
harness and spare builds carry none and can't be packaged.

A device checks magic, format, key ID, signature, product, the version rule
and size, then hashes the image as it streams into flash. The new slot is
made bootable only after all of them pass. ECDSA P-256 was chosen because
mbedTLS in the Arduino core, Python `cryptography` and Android
`java.security` all handle it.

## Version rules

- A device refuses a package **older** than what it runs; the same version
  reinstalls. Downgrades need USB. To update over a running build, raise
  `PATCH` in `firmware_version.h` (or `Atlas/web/VERSION` for the portal).
- Before release, protocol versions must match exactly (protocol 3).
- **Needed for release:** freeze the secure-session handshake and update
  packets so a newer Atlas can still update an older Sigil (show it as "needs
  update", never seat it). Without that, updating Atlas first strands every
  Sigil until USB.

## Keys and signing

- **Private key:** held by the owner outside the repository
  (`D:\TurnHub\Private\TurnHub-keys`), and as the GitHub Actions secret
  `TURNHUB_FIRMWARE_SIGNING_KEY`. Never commit, print or open it.
- **Public key:** `shared/include/firmware_signing_key.h` and the Android
  app. Rotating it needs a USB flash of every device, or an update signed by
  the old key that carries the new one (last rotated 2026-10-05, `e89210a`).
- **CI** signs every firmware build when the secret is available (fork PRs
  stay unsigned). **Releases:** pushing a `v*` tag runs `release.yml`, which
  builds, signs and publishes the `.thfw` files and `turnhub-firmware.json`.
  Cloud sessions can't push tags; the owner pushes them.
- **Local:** `tools\sign-local.cmd` builds from the working copy and writes
  verified packages to `Private\TurnHub-builds`; `tools\flash-all.cmd -Sign`
  signs and flashes over USB (see `CLAUDE.md`).

## Release feed

`https://github.com/rickethyo/TurnHub/releases/latest/download/turnhub-firmware.json`
(public, no token):

```json
{"schema": 1, "release": "0.9.1",
 "packages": [{"product": "atlas", "version": "0.6.1", "radioProtocol": 3,
   "file": "atlas-0.6.1.thfw", "size": 1234567, "sha256": "...", "buildId": "..."}]}
```

`size` and `sha256` cover the whole `.thfw` file. The feed isn't signed; a
tampered feed can only point at a package that fails its signature check.
The web portal pack is listed too, as product `portal` with the
`Atlas/web/VERSION` version. The app's update step installs it first
(`POST /api/portal/install`, no restart) when Atlas has a microSD card and an
older or no pack, then Atlas, then the Sigils. Readers skip products they
don't know.

## Updating a Sigil

1. An Admin verified at the table uploads a Sigil package on the portal's
   `/sigil-update` page (or the app does). Atlas checks it fully while writing
   it to **its own idle app slot** (1.9 MB, raw `esp_partition_*`), so no card
   is needed. One package at a time: with mixed displays, update one variant,
   then stage the other. Atlas's own update overwrites it.
2. **Update** (`UpdateSigil`): the Sigil must be paired, recently heard, not
   a harness, and match the staged variant. Atlas makes a job with a one-time
   token and sends `SigilUpdateOffer` sealed in that Sigil's session: product,
   version, size, token, AP SSID and password.
3. The Sigil checks product and version, shows "Updating", joins Atlas's AP
   (same channel, ESP-NOW stays up), downloads
   `http://192.168.4.1/api/sigil-package?token=...` into its idle slot,
   hashing as it goes, and reports progress in 10% steps.
4. On failure it aborts (the running slot is untouched) and reports why;
   retry makes a new job. On success it restarts into the new image in
   pending-verify state and marks it valid only once a secure session is up;
   otherwise after 60 s it restarts and the bootloader rolls back.
5. Atlas reports success when the Sigil's sealed Hello shows the new version.

While a Sigil job runs, game starts, forget/reset and Wi-Fi password changes
are refused, and Atlas OTA can't overwrite the staged package. A package
reloaded after an Atlas restart is re-hashed before use.

**Atlas OTA** takes signed `.thfw` only (raw `.bin` is refused) and has no
boot validation yet: a bad Atlas image needs USB. A board new from the
factory needs one USB flash before its first OTA.

## The app and update notices

- The app (`FirmwareReleases.kt`, `AtlasUpdateWatcher`) reads the feed at
  most daily (every minute in debug builds) while connected, verifies each
  download's size and hash, and reports the newest versions to Atlas with
  `POST /api/updates/latest` (public, information only).
- Atlas (`update_notice.h`, RAM only) counts devices running something older.
  It blinks its on-board LED blue, shows "Update available" (or "... for
  Atlas") on its screen, sends `UpdateNotice` so each Sigil says it too, and
  reports the count on `GET /api/updates`.
- The app shows an **Update now** card (and a notification when in the
  background): Admin sign-in, one table code, Atlas first, wait for it to
  return, sign in again and get a new code, then each Sigil in turn. First-run
  setup uses the same steps.

## Spare Sigils

A spare is a paired Sigil kept out of play (owner, 2026-10-06; host-tested,
not yet tried on hardware). It runs `sigil-spare`, one inert image for
either display.

1. `tools\setup-boards.cmd` > **Make spare** records the board as
   `spare:<firmware it ran>` in `tools\boards.local.md`.
2. The next `flash-all` uploads `sigil-spare` over USB (never packaged or
   signed). Pairing and NVS are kept.
3. The spare announces `CAPABILITY_SPARE`, with the OLED bit reporting its
   GPIO4 strap. To bring it back, stage the matching package on the portal's
   Sigil firmware page (the device shows `spare`) and update it. The package
   must be at least the spare's version.
4. The next `flash-all` with that board attached hears the normal boot line,
   records it as `sigil` or `sigil-oled` and flashes it as that.

An unpaired spare pairs with `PairRequestSpare`, without the code check, and
stays spare-only ([Pairing and Secure Link](PAIRING_AND_SECURE_LINK.md)).
Atlas drops everything from a spare except Hello and update status, and the
picker, the first-run "Sigil paired" check and the update count skip it. The
spare draws nothing (an e-ink panel keeps its last image), plays nothing and
sends no input; its ring shows only pairing and update animations. Serial
says `SIGIL|SPARE|EINK` or `SIGIL|SPARE|OLED`.

## Limits

- Rollback covers crashes and failed reconnects after the new app starts.
  Don't claim automatic rollback until **U06** passes.
- Downloads are plain HTTP on Atlas's network: readable (the image is public
  anyway) but not alterable without breaking the signature. The AP password
  crosses the radio sealed.
- A leaked signing key lets anyone make installable firmware.
- Keeping older packages on the card for reinstalling is undecided;
  delivery doesn't depend on it. Atlas fetching updates over home Wi-Fi is
  staged.
