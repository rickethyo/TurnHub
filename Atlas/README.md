# TurnHub Atlas ESP32

This directory contains the ESP32 implementation of the TurnHub Atlas controller. The older Python controller in `Controller/` remains a behavioral reference while the ESP32 migration continues.

Current development firmware: **0.5.5-dev**.

## Current prototype hardware

- ESP32-WROOM-32 DevKit-style Atlas
- Atlas master button: GPIO 32 to GND using the ESP32 internal pull-up
- No Atlas buzzer
- ESP-NOW Sigil transport on Wi-Fi channel 6
- Local `TurnHub-Atlas` Wi-Fi access point and browser portal

## Current Atlas capabilities

The ESP32 Atlas currently provides:

- ESP-NOW discovery and communication with physical Sigils
- Lobby, shared-Sigil seats, starting-player selection, countdown, turn passing, pause/resume, elimination/concede, and confirmed win flow
- Local browser portal with authenticated player-seat sessions
- Persistent WPA2 access-point password and local network administration
- Physical Sigil naming and display-profile synchronization
- Durable local player profiles with stable profile IDs
- Persistent lifetime and most-recent-game statistics
- Authenticated statistics page at `/stats`
- Self-service text statistics export for the authenticated profile
- Browser feedback controls
- Atlas web OTA with physical master-button/state gating and post-restart verification

TurnHub remains local-first. Normal table control and player statistics do not require an internet or cloud connection.

## Profile and seat architecture

Player identity is deliberately separate from hardware identity.

A **profile** owns persistent player data such as:

- stable local profile ID
- player name
- PIN hash
- lifetime statistics
- most-recent-game statistics

A physical Sigil seat stores only a binding to a profile ID. Existing MAC-and-seat profile data is migrated as compatibility data when first encountered.

This separation is intentional groundwork for virtual Sigils. A future persistent or temporary virtual seat can bind to the same durable profile ID without moving, copying, or resetting the player's statistics. The current ESP game/lobby implementation still uses physical Sigil module IDs; virtual-Sigil gameplay integration remains future work.

## Statistics

Completed games are committed from the authoritative game-engine completion event, so win confirmation, concession, and elimination all use the same persistence path.

Tracked profile statistics currently include:

- games played and won
- games started first
- eliminations
- completed turns
- total and average completed-turn time
- fastest and longest completed turn
- total and average game time
- most recent game result and timing records

After a completed game, serial output includes:

```text
ATLAS|PROFILE_STATS|GAME_RECORDED|<count>
```

where `<count>` is the number of player profiles successfully updated.

Statistics endpoints are authenticated to the browser's current profile rather than accepting an arbitrary profile ID:

```text
GET /api/session/stats
GET /api/session/stats/export
```

The portal links to `/stats`, where the authenticated player can view and download their own statistics.

## Build and upload

This project uses PlatformIO with the Arduino ESP32 framework.

From the `Atlas` directory:

```text
pio run -e atlas
pio run -e atlas --target upload
pio device monitor
```

Serial monitor speed is 115200 baud.

A specific Windows upload port can be supplied explicitly, for example:

```powershell
C:\Users\ricke\.platformio\penv\Scripts\platformio.exe run -e atlas --target upload --upload-port COM7
```

After boot, connect a phone or computer to the Atlas access point and browse to the IP printed in the serial monitor. The ESP32 SoftAP address will normally be `192.168.4.1`.

## Migration direction

The ESP32 port is being moved toward clear modules rather than one large firmware file. Current major boundaries include the game engine, lobby, ESP-NOW Sigil bus, LED/audio renderers, OTA manager, web API/pages, profile store, and profile statistics service.

Next larger layers include virtual Sigil integration, richer gameplay data such as life totals and Commander damage, profile selection/rebinding in the portal, and continued efficiency/refactoring work.
