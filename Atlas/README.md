# TurnHub Atlas ESP32

The ESP32 table accessory. Since 2026-11-04 the hub phone or tablet runs
every game ([hosted-v1.md](../protocol/hosted-v1.md)); Atlas shows it, relays
its Sigils' and touchscreen's input to the hub, and keeps profiles,
statistics, match history and settings. It hosts the `TurnHub-Atlas` Wi-Fi
access point and the HTTP API (it serves no web pages), runs the ESP-NOW radio
to Sigils and persists to NVS and the optional microSD card. Firmware
version: `include/firmware_version.h`.

Module layout and the Intent pipeline are described in the repository's
`CLAUDE.md` and in [Software Architecture](../Documentation/engineering/ARCHITECTURE.md).
First-run setup is in [First-run setup](../Documentation/engineering/FIRST_RUN_SETUP.md);
the longer-term provisioning direction (home Wi-Fi, recovery mode) is in [`OOBE.md`](OOBE.md).

## Hardware

- One build, `atlas-s3`, for the 4-inch ES3C40P S3 Atlas: touchscreen,
  optional microSD card, no speaker or battery driver yet; the BOOT button
  pairs, unpairs and factory resets. The Gen 1 E32R28T build is on
  `legacy/gen1`.
- ESP-NOW Sigil transport and the WPA2 access point, both on Wi-Fi channel 6.

Pins and the board boundary are in the
[Hardware Reference](../Documentation/engineering/HARDWARE.md).

## Profiles and phone play

Use the Android app to create a local profile or sign in with its PIN; no
Sigil is needed. The hub seats its players (its own accounts, accounts copied
from Atlas, or guests); a Sigil joins the hub's setup from its own menu.
Guests never create saved accounts or statistics.

Logout revokes only that app session. PIN changes revoke the profile's other
sessions, and five failed PIN attempts within 15 seconds throttle that profile.

Limits: 8 physical Sigils (two seats each), 32 HTTP sessions, 64 profiles,
8 Wi-Fi stations at once.

Details: [Players and Accounts](../Documentation/engineering/PLAYERS_AND_ACCOUNTS.md).

## Statistics

A finished hosted game reaches Atlas through the game import
(`POST /api/standalone/import`), once per record ID
([completion ordering](../Documentation/engineering/STORAGE_AND_RECOVERY.md)).
NVS keeps a core record per profile (games played and won, last result and
game type); the detailed record (turn counts and times, game times, first
starts, eliminations) and the match history live on the microSD card. Imports
log `ATLAS|STANDALONE|IMPORT|...`.

Statistics are served for the signed-in profile only:

```text
GET /api/session/stats
GET /api/session/stats/export
```

The Android app displays and exports profile statistics and holds Atlas's
administration settings (the embedded portal was removed on 2026-10-30).

## Setup Wi-Fi

Only a new or factory-reset Atlas starts TurnHub-Atlas. Welcome shows a
Wi-Fi QR carrying its password. A phone camera joins without typing. The app
provisions home Wi-Fi and pins the HTTPS key before finishing. Atlas then
restarts with its hotspot off; outages never bring it back. See
[Atlas setup](../Documentation/engineering/FIRST_RUN_SETUP.md).

## Serial log and browser download

Atlas writes pipe-separated `ATLAS|...` lines to USB serial (115200 baud)
through `TurnHub::serialLog`. The same output goes to an 8 KB RAM ring, each
line stamped with uptime (`[     12.345]`), and is drained to the microSD card
when one is present ([SD diagnostics](../Documentation/engineering/DIAGNOSTICS.md)).
A Developer can download the RAM log from the app or:

```text
GET /api/diagnostics/log     (X-TurnHub-Token; Developer permission)
```

The file starts with the Atlas ID, boot ID, firmware version and uptime, and
says how many bytes were dropped. Secrets are redacted (`printlnRedacted`).
Framework messages (`log_e`, ESP-IDF) bypass the Atlas log and are not captured.

## Build, flash and test

From `Atlas/`:

```text
pio run -e atlas-s3
pio run -e atlas-s3 --target upload --upload-port COMx
pio device monitor
```

Find the port each time (the S3 Atlas is its native USB port, 303A:1001);
never hard-code it. Signed
local builds and OTA updates are
described in the repository's `CLAUDE.md` and in
[Sigil OTA](../Documentation/engineering/FIRMWARE_UPDATES.md). The HTTP API
is at `192.168.4.1` on the `TurnHub-Atlas` access point.

Host regression tests (no hardware) are in [`tests/host/`](tests/host/README.md).
