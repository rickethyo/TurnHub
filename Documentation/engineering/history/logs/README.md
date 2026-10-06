# Captured device logs

Serial and other device captures from bench sessions, kept as dated evidence
for the *Verified* labels in the other engineering docs. Host tests passing is
not hardware acceptance; these files are.

Conventions:

- File name: `YYYY-MM-DD-<what>-<board>.log`. Name boards by role (`atlas`,
  `sigil-eink`, `sigil-oled`, `harness`), never by COM port. Boards are matched
  by MAC per [Board Inventory](../../HARDWARE.md).
- Each line is prefixed with seconds since capture start, added by the capture
  script. Serial lines can be split across reads, so a few lines are cut mid-word.
- Scan every capture for secrets (Wi-Fi passwords, keys, tokens) before
  committing, and replace them with `<redacted>`.

## Index

| Date | Files | What |
|---|---|---|
| 2026-09-30 | `2026-09-30-boot-*.log` | Reset of all four boards over USB (RTS), 45 s each, Atlas first then the Sigils and the harness. Atlas 0.6.0-dev, both Sigils 0.8.1-dev, harness 0.8.0, protocol 2. Sigils reported to Atlas over the secure link (`SIGIL|INFO`, `FW|0.8.1`). Nothing was paired or played. |
| 2026-09-30 | `2026-09-30-ota-*.log` | First real Sigil OTA through the portal, Atlas 0.6.0-dev, packages signed with key `bd3b55e0`. Passive capture (no resets), 160 s. E-ink Sigil 0.8.1 -> 0.8.2 (job 1) and OLED Sigil 0.8.1 -> 0.8.2 (job 0), each: presence code, package upload and stage, offer, accept, Wi-Fi join, download (about 0.5 s), `installed`, reboot (`SW_CPU_RESET`), `OTA\|BOOT\|PENDING_VERIFY` then `VALID` once the secure session was up, Atlas `done`. Atlas's Hello showed 0.8.2 for both. Observations: each Sigil logged `ATLAS\|LOST` then `RESTORED` during the Atlas package upload (Atlas busy writing flash, about 8 s). The Sigil boot banner still said `0.8.1-dev` because only `PATCH` had been bumped; fixed in the source afterwards. Rollback, interrupted transfers and the negative tests in [Sigil OTA](../../FIRMWARE_UPDATES.md) were not run. |
