# Sigil OTA (Planned)

Owner direction, 2026-09-25: Sigil OTA is the next implementation priority, so
Sigils stop needing USB flashing and COM-port tracking. Transport accepted the
same day: Wi-Fi download from Atlas, with ESP-NOW transfer as the fallback. The
upload lives in the web portal (owner, 2026-09-28), next to the existing Atlas
firmware page. Status of everything below: *Planned* unless marked otherwise.

## Progress and resume point

The owner's dev PC crashes intermittently. Keep this list current and commit
after each step, so a new session can resume from the repository alone.

- [x] **Prerequisite:** the encrypted Atlas-Sigil link in
      [Secure Link](SECURE_LINK.md) (owner, 2026-09-28). Done 2026-09-29:
      pairing v2 with a code check, then protocol `VERSION` 2 with a secure
      session per Sigil and every packet sealed (AES-128-CCM). *Verified* on
      the bench: Atlas, both Sigils and the harness's two virtual Sigils boot
      into sealed sessions. What it means for OTA:
      - The offer that carries the Wi-Fi password goes out sealed like any
        packet (`SigilBus::sendSealed` on Atlas; the Sigil only handles frames
        that open in its session), so nobody listening can read it, and a
        forged or replayed offer doesn't open.
      - The offer (SSID 32 + password 64 + SHA-256 32 + size 4 + token 16 +
        version and variant, about 155 bytes) fits a sealed frame
        (`MAX_INNER_BYTES` 235), but not today's receive buffers: the Sigil's
        `ReceivedPacket::MAX_BYTES` and Atlas's `TxRequest` hold 125 bytes (a
        sealed game display). Step 1c grows them, with size asserts, and adds
        the new sealed size to `ReceivedPacket::acceptedSize`.
      - Pairing windows are now at least 60 s everywhere (owner, 2026-09-29);
        OTA doesn't pair, so no change here.
- [x] Step 1a: this design record and feature gate (2026-09-28).
- [ ] Step 1b: embedded firmware descriptor in Sigil builds (`sigil_firmware_descriptor`),
      shared descriptor layout and parser in `shared/include/`, host tests.
- [ ] Step 1c: `SigilUpdateOffer` / `SigilUpdateStatus` radio packets in
      `protocol.h`, size asserts, host tests.
- [ ] Step 1d: Sigil updater: offer handling, Wi-Fi join, HTTP download into
      the idle slot with SHA-256 check, deferred boot validation.
- [ ] Step 2: Atlas upload page and SD package catalog (presence code gated).
- [ ] Step 3: `UpdateSigil` Intent, per-Sigil update button and status, one
      Sigil end to end on hardware; then queuing, retries and reinstall.

## What is already in place (*Verified* from source, 2026-09-28)

- **Version and variant reporting.** Every Hello carries the firmware
  major/minor/patch and the capability byte (`encodeHelloInfo`). Atlas keeps
  them per Sigil (`SigilBus::updateHelloInfo`); the portal and Android already
  show the version. `CAPABILITY_DISPLAY_OLED` separates the OLED build from
  e-ink, and `CAPABILITY_HARNESS` marks the test harness, which must never be
  offered a Sigil image.
- **Flash layout.** Sigils use `board = esp32dev` with no custom partition
  table, so the Arduino default applies: `otadata` plus two 1.25 MB app slots
  (`app0`/`app1` at 0x10000/0x150000). No partition change, and so no
  full-erase reflash, is needed. Current images, built 2026-09-28: `sigil`
  812,293 bytes and `sigil-oled` 822,261 bytes of 1,310,720 (62%), leaving
  roughly 480 KB for the updater (HTTP client and `Update` add far less).
- **Rollback support in the bootloader.** The installed Arduino core
  (framework-arduinoespressif32 2.0.17) is built with
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`. A newly written image boots in the
  pending-verify state; if the app restarts before marking itself valid, the
  bootloader returns to the previous slot. *Needs verification* on hardware.
- **Physical-presence gate and pattern.** Sigil factory reset already goes
  portal button -> `presenceFetch` -> `FactoryReset` Intent -> one ESP-NOW
  packet. The update command follows the same path.

## Feature gate

1. **State owner.** Atlas owns the package catalog (on SD) and each Sigil's
   update job (RAM only; lost on Atlas restart, which is safe because the Sigil
   side is self-validating). Each Sigil owns its own flash slots and boot
   validation. No game state changes.
2. **Intent.** New `UpdateSigil` (payload: Sigil ID; Atlas picks the catalog
   image for that Sigil's variant, or an explicitly chosen retained image).
   The firmware upload itself is an Admin file transfer like `/api/firmware`,
   not an Intent: it changes no table state until an update is started.
3. **Validator.** In the `UpdateSigil` handler: presence-verified Admin; table
   in Lobby or GameOver (as Atlas OTA); Sigil paired and heard from recently;
   Sigil not the harness; a valid image exists for its variant; Sigil firmware
   is at least the first OTA-capable release; no other update running (one at
   a time at first). Upload validation: descriptor present and well formed,
   variant known, size fits a 1.25 MB slot, ESP32 image magic byte, SHA-256
   computed while streaming to a temporary file.
4. **Persistence owner.** Packages and their metadata on the microSD card
   through `sdBlobStore()` / `sd_card.cpp` (serialized with the log worker).
   No NVS changes on Atlas. On the Sigil: the OTA slots and `otadata` only.
   Without a card, the page says a card is required; play is unaffected.
5. **Rendering clients.** Web portal: new Sigil firmware section in Device
   Settings (upload, catalog, per-Sigil Update button and status). Atlas
   touchscreen: none at first. Sigil: an "Updating..." screen and a distinct
   status-light state while downloading. Android: shows versions already;
   update controls later, if wanted.
6. **Protocol/contract change.** `protocol.h`: new `SigilUpdateOffer`
   (Atlas -> Sigil) and `SigilUpdateStatus` (Sigil -> Atlas) packets, and the
   shared firmware descriptor layout. Both go sealed (protocol `VERSION` 2),
   and a Sigil without the updater ignores the unknown type once opened. HTTP: new admin routes (upload, catalog, start
   update, status) and a one-time image download URL for the Sigil. These are
   admin portal routes, not part of the `protocol/` client contract. Both
   device types need one last USB flash to carry the updater.
7. **Third-party dependencies.** Arduino-ESP32 `Update`, `HTTPClient` and
   `mbedtls` SHA-256, all already in the core Atlas/Sigil use (LGPL-2.1 /
   Apache-2.0). No new libraries; note their use in `Documentation/legal/`.
8. **Accessibility.** Every stage and failure is shown as text in the portal
   with a live region; nothing relies on color. On the Sigil, the update state
   is text on the display plus a status-light pattern, never the light alone.

## Design

### Firmware descriptor (identifying an uploaded image)

Arduino builds don't give the ESP-IDF `esp_app_desc_t` usable version or
project fields, so each Sigil build embeds its own constant descriptor, kept
in the image by `__attribute__((used))`:

```
magic "THSIGFW1" (8) | descriptor version (1) | variant (1: 1 e-ink, 2 OLED)
| firmware major/minor/patch (3) | radio protocol VERSION (1) | reserved
```

Atlas scans the uploaded `firmware.bin` for the magic while streaming it (one
match required), so the owner uploads the plain PlatformIO output with no
extra packaging step. Variant and version come from the same build flags that
select the display, so a mislabeled image is not possible without editing the
source. The Wokwi build has no variant and is rejected.

### Update sequence

1. Admin chooses **Update** for a Sigil and enters the table presence code.
2. `UpdateSigil` is validated; Atlas creates a job with a random one-time
   token and sends `SigilUpdateOffer` sealed in that Sigil's secure session
   (resent until acknowledged or timeout): AP SSID and password, image size,
   SHA-256, target version, token. Only an offer that opens in the session
   counts, so it came from the paired Atlas; the Sigil also checks that the
   variant and version are sane.
3. The Sigil shows "Updating", reports `Accepted`, joins the Atlas AP on the
   same channel (ESP-NOW stays up) and downloads
   `http://192.168.4.1/api/sigil-image?token=...`, writing it straight into the
   idle slot with `Update`, hashing as it goes and reporting progress in 10%
   steps.
4. Hash or size mismatch, or a failed download: the Sigil aborts the write
   (the running slot is untouched), reports the failure and restarts into
   its current firmware. Atlas shows the reason; the owner can retry.
5. On success the Sigil sets the new slot to boot and restarts. The new image
   starts pending-verify (`verifyRollbackLater()` returns true), rejoins
   Atlas over ESP-NOW and marks itself valid only after Atlas acknowledges its
   Hello. If it hasn't within 60 s, it restarts, and the bootloader rolls back.
6. Atlas reports success when the Sigil's Hello shows the target version, or
   failure if it shows the old one (rolled back) or goes silent past a timeout.

The token is single-use, expires with the job and names the image, so the
download route serves nothing to anyone without a live job.

### Limits and risks

- **Rollback covers crashes and failed reconnects after the app starts.** A
  fault before the app code runs at all is caught by the bootloader only if
  the app never reaches the point of marking itself valid, which is the case
  here. Do not claim automatic rollback until an interrupted and a
  deliberately broken image have been tested on hardware (**U** items in
  `PROTOTYPE_V1_VERIFICATION.md`).
- **Power loss during download** leaves the running slot intact (the idle
  slot is only made bootable after a full, hash-checked write).
- **Harness boards** advertise `CAPABILITY_HARNESS` and are refused.
- **The AP password crosses the radio**, sealed in the Sigil's secure session
  ([Secure Link](SECURE_LINK.md), protocol `VERSION` 2 since 2026-09-29). The
  radio peers stay `encrypt = false`; the encryption is TurnHub's own, per
  Sigil, with no 7-peer limit. A Sigil that downloads over Wi-Fi reconnects
  with a new SecureHello afterwards, as after any link loss.

## Open decisions

1. **AP password over the radio.** *Decided 2026-09-28:* encrypt the link
   first (owner chose this over sending it in the clear or ESP-NOW-only
   transfer). Application-layer encryption, not ESP-NOW's built-in kind; the
   reasons are in [Secure Link](SECURE_LINK.md).
2. **Retained package count** on SD (see
   [SD Diagnostics](SD_DIAGNOSTICS.md#sigil-update-packages-planned)).
   *Undecided*; start with the current image plus one previous per variant.
3. **Update authenticity.** The hash detects corruption, not a malicious
   image from an Admin with presence proof. Signed images are a later
   decision.
