# Optional SD diagnostics

Status: Experimental implementation, 2026-09-25. Hardware acceptance pending.

## Feature gate

- State owner: Atlas diagnostic service; records are disposable observations.
- Intent/validator: no gameplay action or new Intent. Only the already-redacted
  serial capture is eligible; the SD self-test gates every storage consumer.
- Persistence owner: Atlas SD service, bounded text files under `/turnhub`.
- Presentation: existing Developer diagnostics reports logger state and loss;
  retained text can be read from the removed card after powering down.
- Contract: additive SD diagnostics fields only; no radio/client-state changes.
- Dependencies: existing Arduino SD and ESP32 FreeRTOS only.
- Accessibility: diagnostic state is text, with no change to gameplay controls.

## Behavior

The card remains optional. A low-priority worker drains the redacted serial ring;
callbacks and the gameplay loop never perform diagnostic file writes. It retains
`diagnostics.log` and three archives (`.1` newest through `.3` oldest), each at
most 256 KiB, for at most 1 MiB of file contents. Filesystem overhead is extra.
Each boot appends a firmware/reset-reason/random boot-ID marker. Timestamps are
uptime, not wall-clock time. The current file is appended across reboots.

The worker copies at most 2 KiB once per second. If the RAM ring overtakes it,
the next write includes an explicit lost-byte marker. Existing secret redaction
also applies to the card. Other diagnostic data, including identifiers and game
activity, is readable by anyone with physical access to the card; these are not
profile statistics or a match-history database. Existing browser permissions and
the RAM-only HTTP log download are unchanged.

An absent card or failed boot write/read test disables SD consumers. File errors
(including a full or removed card) stop logging until restart. No automatic
formatting, background retries, or deletion outside the logger's four owned
paths occurs. An unexpected oversized current log is preserved and logging is
disabled. Card capacity/usage in diagnostics is sampled at startup to avoid
filesystem access from the HTTP task while the worker is writing.

Writes are flushed and closed, but logs are best effort. Power loss can lose
buffered text, interrupt rotation, or damage FAT metadata. CRC/read-back and
temporary/backup files in the separate blob store are not a filesystem-level
transaction guarantee. Profiles, settings, core statistics (games played and
won) and recovery stay in NVS. Detailed statistics live in the blob store on
the card (see [Identity and storage](IDENTITY_AND_STORAGE.md)). The store and
this worker share one card lock in `sd_card.cpp`, so their SD library calls
never overlap. A logging I/O error also makes the store report Unavailable,
and statistics then fall back to the NVS core counts.

## Verification

Verified on the host (2026-09-25, GCC C++14): gameplay, storage and profile-store
scenario executables pass, including retention, append across restarts, rotation
failure, short writes, self-test gating and redacted ring draining/overflow.
The adapter-boundary audit and generated client-contract checks also pass.
PlatformIO is unavailable here: no ESP32 firmware build or hardware test was run.
Physical checks still needed:
boot with no card, a read-only/full card, reboot markers across real restarts,
rotation on the actual card, and gameplay responsiveness during slow SD writes.

## Sigil update packages (Planned)

Owner direction, 2026-09-25: Sigil OTA is the next implementation priority,
ahead of session history and theme packs. Stage firmware on SD and retain a
small history for rollback. Keep packages separate from diagnostic-log retention.
Each package needs a verified checksum/hash, firmware version/build ID, target hardware
and display/input variant, size, and protocol/compatibility metadata. Download
to a temporary path and validate fully before offering an image for installation.
Retain the current known-good image and a configurable small number of previous
compatible releases; do not prune an image in use by an update or pinned for
recovery. The exact count is not decided yet.

This is storage for reinstalling an older compatible release. Automatic recovery
from a failed update still requires a Sigil-side OTA transport, suitable flash
partitions/boot validation, and firmware/data compatibility rules. Those are not
implemented by this diagnostics change. Future package/catalog access must share
one serialized SD owner with diagnostics rather than racing filesystem calls.
