# Sigil OTA and Signed Firmware Updates (In progress)

Owner direction, 2026-09-25: Sigil OTA is the next implementation priority, so
Sigils stop needing USB flashing and COM-port tracking. Transport accepted the
same day: Wi-Fi download from Atlas, with ESP-NOW transfer as the fallback. The
upload lives in the web portal (owner, 2026-09-28), next to the existing Atlas
firmware page. OTA must work before devices leave the owner's hands
(owner, 2026-09-29).

Owner decision, 2026-09-29 (remote updates): accept this plan:

1. Sigil OTA through Atlas, with **signed firmware packages** checked by every
   device before it installs anything.
2. The **Android app carries updates**: it fetches releases while the phone is
   online, then installs them on Atlas and the Sigils at the table. Atlas never
   needs internet access.
3. *Later, optional:* Atlas joins the home Wi-Fi **only to fetch an update**,
   between games, then returns to its own network and channel. Tracked in
   [Staged Changes](STAGED_CHANGES.md). It reuses the release feed and packages
   below; what it adds is an HTTPS client on Atlas, a stored home Wi-Fi
   password and the channel switch (Sigils listen on channel 6 only).

Status of everything below: *Planned* unless marked otherwise.

## Progress and resume point

The owner's dev PC crashes intermittently. Keep this list current and commit
after each step, so a new session can resume from the repository alone.
Firmware builds run on GitHub Actions, not locally. Host suites can run locally.

- [x] **Prerequisite:** the encrypted Atlas-Sigil link in
      [Secure Link](SECURE_LINK.md). Done 2026-09-29 (protocol `VERSION` 2,
      every packet sealed; *Verified* on the bench at boot).
- [x] Step 1a: design record and feature gate (2026-09-28; signing and app
      delivery added 2026-09-29).
- [x] Step 1b: package format and descriptor. `shared/include/firmware_package.h`
      (embedded descriptor, `.thfw` header, parser, version rules), the
      descriptor built into Atlas and Sigil images, host tests.
- [x] Step 1c: signing tools and CI. `tools/firmware/` (key generation,
      packaging, verification), `firmware_signing_key.h` (public key), CI
      signs every firmware build when the secret exists, and a tag-triggered
      release workflow publishes packages and the release feed.
- [x] Step 1d: radio. `SigilUpdateOffer` / `SigilUpdateStatus` packets (sealed),
      bigger receive buffers, and the version-tolerant handshake (below).
- [x] Step 1e: Sigil updater: offer, Wi-Fi join, download into the idle slot,
      signature and hash check, deferred boot validation, update screen.
- [x] Step 1f: Atlas: signed Atlas OTA (raw `.bin` refused), Sigil package
      staging in the idle app slot, one-time download route.
- [x] Step 1g (software): `UpdateSigil` Intent and portal Sigil firmware page.
- [ ] Step 1g (bench): both display variants end to end on hardware, including
      interrupted transfers and rollback. See the checklist below.
- [ ] Step 2a: Android: read the release feed, download and verify packages.
- [ ] Step 2b: Android: install on Atlas, then the Sigils (presence-gated).

### Codex integration, 2026-09-29

Branch `codex/finish-sigil-ota` continues Claude's `5dd016b`. The checked
steps above mean source implementation, not hardware certification. The new
integration still needs a firmware CI build and the bench checks below.
Android delivery remains a separate step.

Local validation passed: Atlas application/service, storage and package/job
host suites; Sigil host suites; Python signing-tool tests; adapter audit;
client contract checks; portal JavaScript syntax. Host suites used ASan and
UBSan with leak detection disabled because this execution host blocks the
LeakSanitizer process inspection. Real mbedTLS/flash/network behavior remains
covered by the pending firmware build and bench gates, not by host stand-ins.

Device Settings links to `/sigil-update`: upload one signed `.thfw` package,
choose a matching online Sigil, and start its update. Verify at the table
before uploading. Raw `.bin` is now rejected by Atlas's own `/update` page;
use the signed Atlas `.thfw` artifact. An old Atlas without this change still
needs its usual `.bin` update to acquire the signed-package reader. Each Sigil
needs one USB flash of an updater-capable build before its first OTA.

Atlas checks product/variant, version, online session, Admin and physical
presence, idle table, and absence of another update. The Sigil independently
checks product in the offer AND in the signed package before writing flash.
The packaging tool derives product from the embedded build descriptor, not
the filename. Rename tests therefore cannot bypass product checks.

The package is re-hashed when loaded from flash after an Atlas restart, so
an incomplete upload cannot become staged merely because its header survived.
Game starts, device forget/reset and network-password changes are refused while
Sigil OTA is active; Atlas OTA cannot overwrite its package then. Each job
allows one HTTP download. Retry starts a new job/token. Completion requires a
new secure session followed by a sealed Hello, so cached version data and a
handshake alone cannot claim success.

### Portal OTA bench checklist (not yet verified)

- [ ] CI builds Atlas, both Sigil variants, Wokwi and the harness; signed
      artifacts verify against the committed public key.
- [ ] Install updater-capable Sigils by USB; pair securely, verify at the table.
- [ ] Update OLED and e-ink individually through Device Settings. Observe
      progress, reboot, a fresh secure session, and `done` in the portal.
- [ ] Try e-ink on OLED and OLED on e-ink. Atlas refuses; also directly test
      the Sigil's own wrong-product rejection before flash writes.
- [ ] Reject a raw `.bin`, wrong signature, altered image and older version.
      Allow a correctly signed same-version reinstall.
- [ ] Drop Wi-Fi and cut Sigil power during transfer. Old firmware still boots;
      failure is readable and a new job can retry.
- [ ] Interrupt package upload, restart Atlas, confirm no corrupt package is staged.
- [ ] Start a game, replace the package, update Atlas, forget/reset a Sigil, or
      change network password during OTA. Each conflicting action is refused.
- [ ] Reuse the download token or use a wrong token: route returns 403.
- [ ] Test a signed image that deliberately fails to reconnect. Confirm the
      bootloader rolls back and Atlas reports failure, with no USB recovery.
- [ ] Restart Atlas during a Sigil job. Confirm the Sigil safely completes or
      fails and reconnects; Atlas job state is intentionally RAM-only.
- [ ] Check keyboard navigation, screen reader labels/status and both Sigil
      progress screens. Test a table with multiple paired Sigils present.

## What is already in place (*Verified* from source, 2026-09-28)

- **Version and variant reporting.** Every Hello carries the firmware
  major/minor/patch and the capability byte (`encodeHelloInfo`). Atlas keeps
  them per Sigil (`SigilBus::updateHelloInfo`); the portal and Android already
  show the version. `CAPABILITY_DISPLAY_OLED` separates the OLED build from
  e-ink, and `CAPABILITY_HARNESS` marks the test harness, which must never be
  offered a Sigil image.
- **Flash layout.** Sigils use `board = esp32dev` with no custom partition
  table: `otadata` plus two 1.25 MB app slots. Images are about 820 KB (62%).
  Atlas uses `min_spiffs.csv`: two 1.9 MB app slots and no usable data
  partition. No partition change, and so no full-erase reflash, is needed.
- **Rollback support in the bootloader.** The Arduino core (2.0.17) is built
  with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`. A new image boots in the
  pending-verify state if the app asks for it (`verifyRollbackLater()`);
  otherwise the core marks it valid at startup. *Needs verification* on
  hardware.
- **Physical-presence gate.** Admin actions that change devices go portal (or
  app) -> presence code -> Intent -> radio. Android already has the Admin
  console and the presence-code flow (`AtlasAdminConsole`, `PresenceCode`).

## Feature gate

1. **State owner.** Atlas owns the staged Sigil package and each Sigil's
   update job (RAM only; lost on Atlas restart, which is safe because the Sigil
   side is self-validating). Each Sigil owns its flash slots and boot
   validation. The app owns its downloaded release cache. No game state
   changes.
2. **Intent.** New `UpdateSigil` (payload: Sigil ID). Uploading a package is an
   Admin file transfer like `/api/firmware`, not an Intent: it changes no table
   state until an update starts.
3. **Validator.** Upload: presence-verified Admin, table in Lobby or GameOver,
   a valid signature from the TurnHub key, the right product, size fits, the
   SHA-256 of the streamed image matches the signed header, and the version is
   not older than what it replaces. `UpdateSigil`: the same Admin and table
   rules; the Sigil paired, heard from recently and not the harness; a staged
   package for its variant; no other update running (one at a time).
4. **Persistence owner.** The staged Sigil package lives in Atlas's idle app
   slot (below). No NVS changes on Atlas. On the Sigil: its OTA slots and
   `otadata` only. The app keeps packages in its private files directory.
5. **Rendering clients.** Portal: a Sigil firmware section in Device Settings
   (upload, staged package, per-Sigil Update and status). Android: an Updates
   screen (available release, Atlas and Sigil versions, Install). Atlas
   touchscreen: none at first. Sigil: an "Updating" screen with progress and a
   distinct status-light state.
6. **Protocol/contract change.** `protocol.h`: the two update packets and the
   version-tolerant handshake. New shared header `firmware_package.h`. HTTP:
   admin routes (Sigil package upload, status, start update) and a one-time
   package download URL for the Sigil; `/api/firmware` takes `.thfw`
   packages. These are admin routes, not part of the `protocol/` client
   contract. The release feed (`turnhub-firmware.json`) is a new contract,
   documented below. Every device needs one last USB flash to carry the
   updater and the public key.
7. **Third-party dependencies.** Arduino-ESP32 `Update`, `HTTPClient`, and
   mbedTLS SHA-256 and ECDSA, all in the core already (LGPL-2.1 / Apache-2.0).
   Android uses `java.security` only. The tools use Python `cryptography`
   (Apache-2.0/BSD), developer-side only. Note these in `Documentation/legal/`.
8. **Accessibility.** Every stage and failure is text in the portal and app
   (live regions / accessibility announcements); nothing relies on color. On
   the Sigil, the update state is text on the display plus a light pattern,
   never the light alone.

## Design

### Firmware package (`.thfw`)

Every update, for Atlas or a Sigil, is one file: a 128-byte signed header
followed by the plain PlatformIO `firmware.bin`. All integers little-endian.

```
off size field
  0   8  magic "THFWPKG1"
  8   1  header format (1)
  9   1  product: 1 Atlas, 2 Sigil e-ink, 3 Sigil OLED
 10   3  firmware major, minor, patch
 13   1  radio protocol VERSION the image speaks
 14   2  flags (0; reserved)
 16   4  image size in bytes
 20  32  image SHA-256
 52   8  build ID (first 8 bytes of the git commit, for display)
 60   4  key ID (first 4 bytes of SHA-256 of the public key)
 64  64  ECDSA P-256 signature (r || s) over SHA-256 of bytes 0-63
128   -  image
```

Why ECDSA P-256: mbedTLS in the Arduino core verifies it with no new library
(Ed25519 is not in mbedTLS 2.28), Python `cryptography` signs it, and Android
verifies it with `java.security`. Verification takes well under a second and
runs only when an update is checked, not at boot.

A device checks, in order: magic and format, key ID, signature, product,
version rule, size fits its slot, then the SHA-256 of the image as it streams
into flash. The new slot becomes bootable only after all of them pass.

### Embedded descriptor

Each build also carries a 16-byte descriptor in its image, kept by
`__attribute__((used))`:

```
magic "THFWDSC1" (8) | product (1) | major, minor, patch (3)
| radio protocol VERSION (1) | reserved (3)
```

The packaging tool finds it (exactly one match required) and copies product
and version into the header, so a package can't be labeled as something the
image isn't without editing the source. The product comes from the same
build flags that pick the display. The Wokwi and harness builds carry no
descriptor and can't be packaged.

### Version rules

- A device refuses a package **older** than what it runs. The same version is
  allowed (reinstall). Downgrades need USB.
- Atlas firmware gains major/minor/patch constants (it only had a string).
- **Before hardware is released, protocol versions must match.** Owner,
  2026-09-30: backward compatibility applies only to released hardware, so
  `MIN_UPDATABLE_VERSION` and the frozen handshake are gone (protocol 3): a
  handshake, update offer or update status from another protocol version is
  refused, and a protocol bump means reflashing every board together.
- *Needed for release:* old Sigils must stay updatable after Atlas moves on.
  Freeze the secure-session handshake and the update packets, have a later
  Atlas still accept a SecureHello from an older Sigil and send it an update
  offer, show that Sigil as "needs update" and never seat it. Without this,
  updating Atlas first would strand every Sigil until someone used USB. The
  recommended order is then Atlas first, then the Sigils.

### Keys and signing

- **Private key:** held by the owner outside the repository, and as the
  GitHub Actions secret `TURNHUB_FIRMWARE_SIGNING_KEY` (PEM). Never committed.
- **Public key:** `shared/include/firmware_signing_key.h` (Atlas and Sigil)
  and the Android app. Rotating it needs a USB flash of every device, or an
  update signed by the old key that carries the new one.
- **CI signs every firmware build** on this repository when the secret exists
  (pull requests from forks get no secrets and stay unsigned). So any build of
  a pushed branch is installable: only trusted people may push.
- **Releases:** pushing a tag `v*` runs a release workflow that builds,
  packages and signs Atlas, Sigil e-ink and Sigil OLED, and publishes a GitHub
  Release with the three `.thfw` files and the feed.

### Release feed (`turnhub-firmware.json`)

Published as a release asset, so the app reads
`https://github.com/rickethyo/TurnHub/releases/latest/download/turnhub-firmware.json`
(the repository is public; no API token or rate limit):

```json
{
  "schema": 1,
  "release": "0.9.0",
  "packages": [
    {"product": "atlas", "version": "0.7.0", "radioProtocol": 2,
     "file": "atlas-0.7.0.thfw", "size": 1234567, "sha256": "...",
     "buildId": "1548b96..."}
  ]
}
```

`sha256` and `size` are of the whole `.thfw` file, so the app can check a
download before parsing it. The feed is not signed; the packages are, and the
app checks each one before offering it. A tampered feed can only point at a package that fails the check.

### Where the Sigil package is staged

Atlas keeps **one** Sigil package in its idle app slot (1.9 MB), written raw
with `esp_partition_*`. Nothing else uses that slot between Atlas updates, and
the core marks the running Atlas image valid at startup, so the idle slot is
never a rollback target. Atlas's own update overwrites it, which is fine.

This replaces staging on the microSD card (the 2026-09-25 plan) for delivery:
a table without a card can still update its Sigils. Keeping a history of
older packages on the card for reinstalling stays planned (see
[SD Diagnostics](SD_DIAGNOSTICS.md#sigil-update-packages-planned)). With a
mixed table, update one variant, then stage the other.

### Update sequence (portal or app)

1. An Admin verifies at the table (presence code) and uploads a Sigil package.
   Atlas checks it fully while writing it to the idle slot.
2. The Admin chooses **Update** for a Sigil (the app can queue all Sigils of
   that variant). `UpdateSigil` is validated; Atlas creates a job with a
   random one-time token and sends `SigilUpdateOffer` sealed in that Sigil's
   session, resent until answered: product, version, package size, token, AP
   SSID and password.
3. The Sigil checks the product and version rule, shows "Updating", reports
   `Accepted`, joins the Atlas AP (same channel; ESP-NOW stays up) and
   downloads `http://192.168.4.1/api/sigil-package?token=...`. It checks the
   header, then writes the image into its idle slot with `Update`, hashing as
   it goes and reporting progress in 10% steps.
4. On any failure the Sigil aborts the write (the running slot is untouched),
   reports the reason and returns to normal. Atlas shows it; retry is allowed.
5. On success the Sigil makes the new slot bootable and restarts. The new
   image starts pending-verify, rejoins Atlas and marks itself valid only
   after a secure session is up. If that hasn't happened within 60 s, it
   restarts and the bootloader rolls back.
6. Atlas reports success when the Sigil's Hello shows the new version, or
   failure if it shows the old one (rolled back) or goes silent past a timeout.

The token is single-use, expires with the job, and the download route serves
nothing without a live job.

### App delivery (Android)

- **Check:** when the phone has internet, the app reads the feed (on request,
  and at most daily in the background later) and downloads any package newer
  than what it has. It verifies each signature and hash before keeping it.
- **Install:** at the table, the Updates screen lists Atlas and each Sigil
  with running and available versions. **Install** asks for the presence code
  once, updates Atlas (`/api/firmware`), waits for it to come back, then
  stages each Sigil package and updates those Sigils one at a time, showing
  each step.
- The app joins Atlas's Wi-Fi with a network specifier, which doesn't give the
  phone internet. Downloading happens before joining (or the app uses the
  phone's default network for the feed and Atlas's for the install).

### Update notice (2026-10-01, owner)

Atlas has no internet, so it learns about new releases from the app.

1. **State owner:** Atlas, RAM only (`front_panel.cpp`, `update_notice.h`):
   the newest version of each product as last reported. Lost at reboot; the
   app reports again on each Atlas boot.
2. **Request:** `POST /api/updates/latest` with `atlas`, `sigilEink` and
   `sigilOled` (`major.minor.patch`, any omitted). Not an Intent: it is
   information, not table state, like the presence codes.
3. **Validator:** version syntax only. Public on purpose (the feed is
   public); a false report can only light the LED.
4. **Persistence:** none.
5. **Rendering:** Atlas counts itself and every paired Sigil that has said
   its version (never the harness) running something older. While any is,
   the on-board LED blinks blue in the pairing rhythm (pairing's red blink
   wins), and the Atlas screen's Menu and Info say "Updates for N devices:
   use the app". `GET /api/updates` reports the same for the app and portal.
6. **Contract:** the two routes above (`protocol/http-v1.md`).
7. **Dependencies:** none.
8. **Accessibility:** the blink is never the only cue: the screen says it in
   words.

The app (`AtlasUpdateWatcher`) reads the feed at most once per interval while
connected (every minute in debuggable builds, daily in release builds) and
reports whenever the feed, the Atlas or its boot changes. Installing is still
the deliberate update flow. *Host-tested only; needs verification on
hardware* (the blue channel is GPIO17 on the E32R28T).

### Limits and risks

- **Rollback covers crashes and failed reconnects after the app starts.** Do
  not claim automatic rollback until an interrupted and a deliberately broken
  image have been tested on hardware (**U** items in
  `PROTOTYPE_V1_VERIFICATION.md`). Atlas itself has no boot validation yet
  (the core marks it valid at once); a bad Atlas image needs USB.
- **Power loss during download** leaves the running slot intact.
- **Harness boards** advertise `CAPABILITY_HARNESS` and are refused.
- **The AP password crosses the radio** sealed in the Sigil's secure session;
  nobody listening can read it.
- **Downloads are plain HTTP** on the Atlas network, so a listener with the
  Wi-Fi password can read the image (it is public anyway) but can't alter it
  without breaking the signature.
- **A leaked signing key** lets someone make installable firmware. Keep it off
  shared machines; rotating needs the steps above.

## Open decisions

1. **AP password over the radio.** *Decided 2026-09-28:* encrypt the link
   first. Done 2026-09-29 ([Secure Link](SECURE_LINK.md)).
2. **Update authenticity.** *Decided 2026-09-29:* signed packages (above).
3. **Retained package history** on SD for reinstalling older releases.
   *Undecided*; delivery no longer depends on it.
4. **Atlas home Wi-Fi updates** (option 3). *Accepted as later, optional.*
