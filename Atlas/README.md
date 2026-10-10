# TurnHub Atlas ESP32

The ESP32 table controller: the authoritative game engine, the
`TurnHub-Atlas` Wi-Fi access point and web portal, the HTTP API, the ESP-NOW
radio to Sigils and NVS/microSD persistence. Firmware version: see
`include/firmware_version.h` (0.6.6 at the time of writing).

Module layout and the Intent pipeline are described in the repository's
`CLAUDE.md` and in [Software Architecture](../Documentation/engineering/ARCHITECTURE.md).
First-run setup is in [First-run setup](../Documentation/engineering/FIRST_RUN_SETUP.md);
the longer-term provisioning direction (home Wi-Fi, recovery mode) is in [`OOBE.md`](OOBE.md).

## Hardware

- LCDwiki E32R28T 2.8" display module (ESP32-32E). The touchscreen is the only
  physical input, plus the DevKit BOOT button for pair / unpair / factory reset.
- On-board speaker for table-wide cues; optional microSD card.
- ESP-NOW Sigil transport and the WPA2 access point, both on Wi-Fi channel 6.

Pins and the touchscreen layout are in the
[Hardware Reference](../Documentation/engineering/HARDWARE.md).

## Profiles and phone play

Use the Android app to create a local profile or sign in with its
PIN; no Sigil is needed. Signing in and joining the table are separate: choose
**Join game** after signing in. Any seated player may start (with a
countdown any seated player can cancel), choose the starter and rematch.
Phones support every game action through the same Intent handlers as Sigils.

Several phones may sign into one profile and control the same participant. In
the lobby, **Connect a Sigil → Attach seat A to my profile**, confirmed with
**Link phone** on the Sigil, attaches a Sigil to the signed-in participant.
A Sigil can also join on its own through its profile picker. Unassigned Sigils
and Guest play as guests, which never create saved accounts or statistics.

Logout revokes only that app session; signing in again reconnects to the
same participant. PIN changes revoke the profile's other sessions, and five
failed PIN attempts within 15 seconds throttle that profile.

Limits: 16 table participants (8 physical Sigils, two seats each), 32 HTTP
sessions, 64 profiles, 8 Wi-Fi stations at once.

Details: [Players and Accounts](../Documentation/engineering/PLAYERS_AND_ACCOUNTS.md).

## Turn-pass grace

A player's pass is provisional for three seconds. During the grace a second
Pass from the same player cancels it. When it expires, the engine commits the
pass at the original request time, so the outgoing player's turn doesn't gain
three seconds. The status API exposes `passPending` and `passGraceMs`. The
touchscreen's Table screen has a 2 s **Master pass** hold for a stuck turn,
which passes at once and logs `ATLAS|GAME|MASTER_PASS`.

## Statistics

Every ending (win, concession, elimination, End match draw) reaches statistics
through the engine's single game-completed event, after the finished-match
checkpoint commits ([completion ordering](../Documentation/engineering/STORAGE_AND_RECOVERY.md)).
NVS keeps a core record per profile (games played and won, last result and
game type); the detailed record (turn counts and times, game times, first
starts, eliminations) lives on the microSD card. After a completed game the
serial log shows `ATLAS|PROFILE_STATS|GAME_RECORDED|<count>`.

Statistics are served for the signed-in profile only:

```text
GET /api/session/stats
GET /api/session/stats/export
```

The Android app displays and exports profile statistics. The browser portal
is for administration and is embedded in Atlas firmware; it needs no card.

## Wi-Fi access point password

Atlas hosts `TurnHub-Atlas` (WPA2, channel 6). Until an owner sets a password
(the portal's Atlas Wi-Fi security card, after verifying at the table, or the last
step of first-run setup), Atlas uses the shipped passphrase `TurnHub-Setup`
from `config.h`, which lets the Android app join a new Atlas without asking.
The default is never written to NVS, so a factory reset returns to it. Boot
logs `ATLAS|WIFI_AP|PASSWORD_STORE|DEFAULT` or `|LOADED`, and the portal shows
"Factory default (change it)" while it is in use.

## Serial log and browser download

Atlas writes pipe-separated `ATLAS|...` lines to USB serial (115200 baud)
through `TurnHub::serialLog`. The same output goes to an 8 KB RAM ring, each
line stamped with uptime (`[     12.345]`), and is drained to the microSD card
when one is present ([SD diagnostics](../Documentation/engineering/DIAGNOSTICS.md)).
A Developer can download the RAM log from the Developer page (`/dev`) or:

```text
GET /api/diagnostics/log     (X-TurnHub-Token; Developer permission)
```

The file starts with the Atlas ID, boot ID, firmware version and uptime, and
says how many bytes were dropped. Secrets are redacted (`printlnRedacted`).
Framework messages (`log_e`, ESP-IDF) bypass the Atlas log and are not captured.

Lines that make a log readable on its own:

```text
ATLAS|GAME|START|<starter>|PROFILE|<generic|mtg|mtg_commander|yugioh>|LIFE|<n>|TIMER_MS|<0=off>|PLAYERS|<n>
ATLAS|LOBBY|JOIN|BROWSER|<controller>|PLAYER|<n>|PROFILE|<profileId>
ATLAS|LOBBY|LEAVE|BROWSER|<controller>|SLOT|<1|2>|PROFILE|<profileId>
ATLAS|LOBBY|EMPTY|RESET|ORIGIN|<SIGIL|BROWSER|SYSTEM|...>[|CONTROLLER|<id>]|FROM|<state>
ATLAS|TIMER|<WARNING|EXPIRED>|PLAYER|<n>
```

Browser and phone seats use controller IDs from 8 upward (shown as "Sigil 8A"
in `GAME|START`).

## Build, flash and test

From `Atlas/`:

```text
pio run -e atlas
pio run -e atlas --target upload --upload-port COMx
pio device monitor
```

Find the port each time (Atlas is the CH340 port); never hard-code it. Signed
local builds, the one-button `tools\flash-all.cmd` (run `tools\setup-boards.cmd`
first on a new PC or with new boards) and OTA updates are
described in the repository's `CLAUDE.md` and in
[Sigil OTA](../Documentation/engineering/FIRMWARE_UPDATES.md). The portal is at
`192.168.4.1`.

Host regression tests (no hardware) are in [`tests/host/`](tests/host/README.md).
