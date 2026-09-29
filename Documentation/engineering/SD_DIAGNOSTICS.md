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

An absent card or a failed write/read self-test disables SD consumers until a
usable card mounts. No automatic formatting or deletion outside the logger's
four owned paths occurs. An unexpected oversized current log is preserved and
logging is disabled. Card capacity/usage in diagnostics is sampled at each
mount to avoid filesystem access from the HTTP task while the worker is writing.

### Hot-plug (2026-09-28)

Before this, the card was mounted once at boot: a pulled card stopped logging
and statistics, and a reinserted (or first) card needed a restart. Now the same
worker task handles hot-plug (`sd_card.cpp`, timing in `sd_hotplug.h`):

- **Removal.** A failed log write, or a raw sector read (every 3 s when
  nothing else touched the card), finds the card gone. The worker unmounts
  (`SD.end()`) and logs `ATLAS|SD|REMOVED`. A raw read reaches the card
  itself; a file check could be answered from the file system's cache.
- **Insertion.** With no card the worker tries to mount every 2 s (the
  "no card" line is logged once). A card that answers but fails the store check
  or self-test is retried every 30 s, not every 2 s.
- **Present but refusing writes** (full, read-only, oversized log): logging and
  the record store stop for that mount, as before, without remount loops; the
  probe still notices when it is pulled.
- **Statistics.** The application loop sees each mount/unmount
  (`sdCardGeneration()`) and points detailed statistics at the card, or at
  nothing (NVS core counts only), and moves any NVS detail onto a card that just
  arrived (`refreshSdLuxuryStore()` in `main.cpp`). The Atlas screen's
  "NO SD CARD" warning follows `sdCardReady()`.
- **Log.** A remount writes `ATLAS|SD|LOG|REMOUNTED` with the boot ID and
  uptime; text logged while the card was out appears as a lost-byte marker if
  the RAM ring overran.
- All mounting, probing and unmounting happen on the worker under the card
  lock; the store re-checks the card inside the lock, so a statistics write
  can't reach a card the worker just dropped. Gameplay never waits for it.
  Diagnostics add `mounts` (successful mounts since boot).

Host tests cover the timing policy (`storage_scenarios`). *Verified* on the
bench, 2026-09-28 (Atlas `B4:BF:E9:12:85:74`, 7580 MB SDHC card): pulling the
card logged `ATLAS|SD|REMOVED` and `ATLAS|SD|STATS|CORE_ONLY`; reinserting it
logged `MOUNTED`, `SELF_TEST|OK`, `STATS|CARD` and `LOG|READY` with no restart.
Still *Needs verification* (**D04**): statistics across a swap during and after
a game (no double counting), a different card, and the screen warning.

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

Update 2026-09-29: delivering an update no longer needs the card. Atlas stages
the one package being installed in its idle app slot (see
[Sigil OTA](SIGIL_OTA.md#where-the-sigil-package-is-staged)). What follows is
the optional history of older packages for reinstalling.

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
