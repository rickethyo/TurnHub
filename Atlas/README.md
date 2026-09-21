# TurnHub Atlas ESP32

This directory contains the ESP32 implementation of the TurnHub Atlas controller. The older Python controller in `Controller/` remains a behavioral reference while the ESP32 migration continues.

Current development firmware: **0.6.0-dev**.

## Profile login and phone play

Open **Profiles** in the portal to create a local profile or sign in with its PIN.
No physical Sigil is required. After signing in, choose **Join table**. The first
participant is host and can start a game once two players have joined. Phones
support starter selection, start/cancel countdown, pass/cancel pass, pause/resume,
concession, win confirmation/denial, rematch and reset through Atlas's existing
Intent handlers. Saved statistics remain profile-owned.

Multiple phones can sign into one profile; they control the same participant.
In the lobby, **System → Device manager → Attach to my profile**, followed by a
physical Action press, attaches an unused Sigil to that participant. Phone and
Sigil then both work. A phone-only participant keeps its table position and
participant identity during attachment. Existing physical players can sign in
with their profile PIN for companion control; shared A/B seats remain supported.
New attachment currently targets the primary seat of an unjoined Sigil.

Logout revokes that browser session without removing a player mid-game. Signing
in again reconnects to the current participant. Profiles survive Atlas restart;
sessions and current games remain RAM-only. Profiles without a saved PIN retain
the existing physical sign-in path; set a PIN afterward for independent login.
Legacy hardware-derived PIN hashes migrate through the existing physical-seat
PIN login, or can be replaced after physically signing in.

Current limits: 16 table participants, 32 browser sessions, 64 profiles in the
login directory/new-registration path. The existing AP configuration permits
eight Wi-Fi clients; table capacity is not a promise of 16 direct phone clients.
PIN changes revoke other browser sessions for that profile. Five failed PIN
attempts per profile within 30 seconds trigger temporary throttling.

See [Profile login and virtual play](../docs/engineering/PROFILE_LOGIN_AND_VIRTUAL_PLAY.md)
for ownership, compatibility and acceptance checks.

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
- Three-second cancellable pass grace for physical, browser, and Atlas-master turn passes
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

The lobby supports physical and browser controller registrations. Browser
authentication identifies a profile independently of any seat; Atlas resolves it
to one participant. The engine receives logical controller handles and a captured
profile ID, and the statistics completion bridge never consults radio discovery.

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

## Turn-pass grace

A turn pass is now provisional for three seconds.

When the active player presses Pass, Atlas records the original pass timestamp and arms a pending transition. During the grace window, another Pass or an Action press from that physical Sigil cancels the pending transition. Browser Pass and the Atlas master-button pass use the same pending-pass path.

If the grace expires, the game engine commits the pass using the original request timestamp. This keeps the outgoing player's recorded turn from gaining an artificial extra three seconds, while the incoming player's clock includes the elapsed grace period. A cancelled pass leaves the original turn uninterrupted.

The status API exposes the pending player and remaining grace time through `passPending` and `passGraceMs` for future portal/Sigil presentation.

## Planned gameplay event model

Richer gameplay data such as life totals should preserve raw events separately from derived statistics.

For life changes, Atlas should record each input event with the affected profile, actor/source, timestamp, old value, new value, and delta. Lifetime statistics such as life gained and life lost should then be derived from those raw events.

Rapid opposite-direction edits should be treated as **potential corrections** rather than automatically counted as independent gain/loss events. An initial heuristic should consider collapsing edits when they:

- affect the same player
- come from the same actor or input source
- occur within one second
- reverse direction
- have no conflicting intervening life event
- look like a small correction to the preceding edit

For example, `40 -> 48 -> 47` entered rapidly by the same player should normally aggregate as a net `+7` life gain rather than `+8 gained` and `1 lost`. The raw `+8` and `-1` events should still remain in the game log, marked as a correction relationship, so the audit history stays truthful.

This distinction lets TurnHub later provide life graphs, per-game life gained/lost, Commander-damage history, undo/revert handling, and spectator timelines without rewriting the source data model.

## First-run provisioning direction

Atlas is moving toward an out-of-box setup flow rather than assuming a permanently preconfigured standalone access point. The detailed design is in [`OOBE.md`](OOBE.md).

The intended direction is:

- first boot enters a deliberate Setup Mode
- setup creates the initial owner/admin profile using the same profile/permission system as normal users
- the owner chooses Standalone mode or optional Home/LAN mode
- Home/LAN mode lets Atlas join an existing 2.4 GHz Wi-Fi network
- Atlas always retains a physical-button recovery path and local fallback network
- home-network support is not enabled as a default until Sigils can dynamically follow Atlas to the infrastructure Wi-Fi channel required by ESP-NOW

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

## Atlas front-panel LEDs

The status LED (GPIO25 / J8) flashes three times after startup, then stays on.
The pairing LED (GPIO26 / J7) is off outside mock pairing mode. Press Pair
(GPIO32 / J10) to flash it every 250 ms for five seconds; the mode then exits
automatically. Repeated presses during that window do not extend it.

This is a visual prototype only: no devices are paired or forgotten and nothing
is persisted. Serial reports `ATLAS|PAIRING|MOCK|ENTER|DURATION_MS|5000` and
`ATLAS|PAIRING|MOCK|EXIT`. LED timing is nonblocking.

## Regression verification

Current gameplay controls enter Atlas's authoritative IntentDispatcher, including
win decisions, lobby/lifecycle, join/leave, elimination, and deferred timers.
Optional NVS absence returns existing defaults without silencing other errors.

See [native regression tests](tests/host/README.md) for repeatable scenarios and
[verification status / hardware checklist](../docs/engineering/ATLAS_INTENT_VERIFICATION.md)
for exact build evidence and post-flash checks. The user reports the gameplay
migration checks complete. The new LED behavior still needs a physical check.

## Migration direction

The ESP32 port is being moved toward clear modules rather than one large firmware file. Current major boundaries include the game engine, lobby, ESP-NOW Sigil bus, LED/audio renderers, OTA manager, web API/pages, profile store, and profile statistics service.

Next larger layers include virtual Sigil integration, richer gameplay data such as life totals and Commander damage, profile selection/rebinding in the portal, first-run provisioning/network modes, and continued efficiency/refactoring work.
