# TurnHub

**Current ESP32 development:** see [Atlas](Atlas/README.md) for firmware
`0.6.0-dev`, profile login, phone-only/mixed tables and simultaneous phone/Sigil
control. The older Python/Raspberry Pi descriptions below remain historical
reference and do not describe the current Atlas build in every detail.

TurnHub is a local-first tabletop game-management platform for turn-based games. It began as a physical turn timer for Magic: The Gathering, but the current architecture supports fully virtual tables, official Atlas hardware with physical Sigils, and mixed tables where physical and browser-based controllers play together.

TurnHub is still a prototype. The current development Atlas is a Raspberry Pi with prototype wired hardware, while the software is being structured so the game engine does not depend on any particular controller or host device.

## Design goals

- **Local first and offline capable.** Core play does not require a cloud service, account, subscription, or Internet connection.
- **Hardware independent game state.** Players own game state; physical Sigils and browser-based Virtual Sigils are controllers for those players.
- **Repairable and long lived.** The product direction favors standard parts, documented interfaces, replaceable components, and graceful degradation rather than planned obsolescence.
- **Open software without counterfeit hardware.** TurnHub software and protocols can remain inspectable while future production Atlas hardware uses offline cryptographic identity to distinguish genuine hardware from third-party/community devices.

## Core model

TurnHub separates several concepts that used to be tied to a physical module:

```text
Profile (optional persistent person)
  -> Player (identity/state in one game)
    -> Controller (how that player interacts)
      -> Physical Sigil
      -> Virtual Sigil / browser
```

A saved profile is optional. Guests can play without creating one. Changing controllers does not change the logical player or erase that player's current game state.

## Current capabilities

### Game lifecycle

TurnHub tracks players, turn order, the active player, turn timing, warnings, pauses, eliminated players, victory claims, rematches, game statistics, and recoverable game state. The web UI includes game lifecycle controls so a completely virtual table does not depend on a physical host Sigil.

### Physical and Virtual Sigils

A player can join using a physical Sigil or play entirely through a browser. On Atlas-capable installations, the table can operate in **Physical + Virtual** mode or **Virtual Only** mode.

Physical profile joining still requires a real Action press on the selected Sigil. Browser controllers use server-issued tokens and server-side authorization rather than trusting controls rendered in the page. During a paused game, players can switch between physical and virtual controllers while preserving the underlying player state.

### Player profiles

TurnHub supports persistent local player profiles for recurring players. Profiles are stored locally under the TurnHub data directory and can contain:

- Display name
- Optional 4-12 digit PIN
- Avatar
- Virtual Sigil sound preference
- Vibration preference where the browser/device supports it
- Browser volume preference
- Game history

PINs are never stored in plaintext. They are stored as salted PBKDF2-SHA256 hashes. A profile with a PIN requires that PIN for virtual joining. Profiles remain optional, and guest play remains supported.

### Avatars

On a Home installation, a player can upload an avatar from the browser. The browser crops/resizes the image to 256x256 before upload and TurnHub stores it locally. PNG, JPEG, and WebP are supported by the profile backend.

Venue mode can disable player avatar uploads by setting `TURNHUB_VENUE_MODE=1`, avoiding unmoderated images on shared/public displays. The Home and Venue behavior uses the same TurnHub software with different capability/policy defaults.

### Virtual Sigil feedback

Virtual Sigils provide browser-side equivalents for physical feedback. Current browser feedback includes sound and, where supported, vibration for events such as turn passes, pause/resume, game over, targeted nudges, and table nudges. Preferences are stored per saved profile.

Browser audio is initialized after user interaction because mobile browsers may block autoplay before the user interacts with the page.

### Game profiles and life totals

The web UI currently includes profiles for:

- Generic
- Magic: The Gathering
- MTG Commander
- Yu-Gi-Oh!

The selected game profile and starting life are captured when a game starts. MTG profiles expose appropriate life controls, and Commander additionally tracks commander damage received from each opposing commander. Commander damage is informational and does not automatically eliminate a player.

Authenticated living players may adjust another living player's life. Cross-player changes take effect immediately and create a 30-second notice for the affected player, who may confirm the change, deny it to reverse that delta, or let it auto-confirm.

### Elimination and victory

Eliminated players remain in the game record without renumbering. Turn order skips them, their statistics remain available, and the last living player is declared the winner automatically.

Players may self-eliminate from their authenticated browser controller after confirmation. Physical elimination is also supported by the prototype controls.

A victory claim freezes game clocks while the table verifies the claim. Other living players can confirm or deny it, and a denial restores the prior running/paused state without charging claim-review time to the active turn.

### Nudge

Authenticated living players can nudge a specific player or the entire table. Nudges do not change game state and are rate limited. Physical hardware provides its available LED/audio feedback, while Virtual Sigils receive browser sound/vibration feedback.

## Local web interface

TurnHub serves a self-contained local web UI on port `8080` with no external web assets. On the prototype Raspberry Pi, a typical address is:

```text
http://turnhub.local:8080/
```

If mDNS is unavailable, use the host's LAN IP address on port 8080.

The top-level UI is organized into:

- **Game** - game lifecycle, starter selection, pause/resume, rematch/reset, and live game state
- **Players** - saved profiles, guests, physical/virtual joining, player names, controller assignment, life and player tools
- **Settings** - game profile, starting life, turn timer, and table-interface mode
- **System** - persistence/runtime information and Atlas host controls when the runtime supports them

The same web application is intended to serve phones/tablets on a Home Atlas and the built-in kiosk display of a future Venue/Tournament Atlas.

## Persistence and recovery

TurnHub stores local data outside the Git working tree under:

```text
~/.local/share/turnhub/
```

This includes settings, recoverable session state, saved profiles, and avatar files. Active game state is autosaved. If TurnHub restarts during a live game, the session is restored **paused** so downtime is not charged to a player.

Profile-to-player associations are included in recovery so a saved player's identity can survive a service/device restart during a game.

## Turn timer

The turn-warning setting is currently configured in the web UI rather than by the old prototype potentiometer. Current modes are:

- **Disabled** - no red warning limit, but the active player receives the green five-minute indication
- **5 minutes**
- **10 minutes**

The setting for a game is captured by the game engine rather than being continually changed by hardware input. Physical timer controls can be reintroduced later through the same interface without coupling the game engine to them.

## Current prototype hardware

The development setup uses a Raspberry Pi 3B as the host and prototype microcontroller hardware for physical I/O. Prototype work has included Arduino and ESP32 modules while the design moves toward wireless Sigils.

Physical Sigil concepts include:

- Pass control
- Action control
- Blue, green, and red status LEDs
- Audio feedback where hardware supports it
- Future persistent low-power display support

The Raspberry Pi owns game state. Microcontrollers act as I/O devices and do not make game-rule decisions.

### Prototype LED behavior

The current prototype uses LEDs to communicate state such as lobby/host/starter selection, active and waiting players, timer caution/warning, pause, elimination, and game over.

The current wired prototype exposes red and green inverted below the LED controller. TurnHub compensates once at the serial boundary with `SWAP_RED_GREEN_OUTPUTS = True`, keeping game logic semantic. Future corrected hardware can disable that compatibility flag without changing the game engine.

### Shared physical modules

The prototype can represent two adjacent logical players on one physical module. Pass still advances one logical player at a time, and the LED patterns distinguish the local seats. This is primarily a prototype capability and does not change the higher-level Player/Controller architecture.

## Runtime modes

Platform selection is intentionally separate from game logic. `TURNHUB_MODE` currently supports:

- `virtual` - browser/Virtual Sigil operation only
- `development-atlas` - explicitly enables current prototype physical hardware
- `auto` - reserved for future production Atlas identity verification and currently fails safely into Virtual Mode

For bench compatibility, the prototype currently defaults to `development-atlas`. A public software release should default to `auto` or `virtual` so generic hardware is never silently treated as an official Atlas.

Future production Atlas authentication is intended to be entirely offline: a secure element holds a non-extractable device private key, TurnHub verifies a manufacturer-signed device certificate and challenge signature locally, and authentication failure falls back to TurnHub Virtual rather than bricking the software.

## Project structure

The controller software is Python and deliberately split into focused modules:

```text
TurnHub/
├── Controller/
│   ├── turnhub.py             # application orchestration
│   ├── config.py              # runtime constants
│   ├── game_engine.py         # game state and rules
│   ├── lobby.py               # lobby/player construction
│   ├── player.py              # logical player-seat model
│   ├── player_profiles.py     # persistent profiles, PINs, avatars, history
│   ├── web_control.py         # browser/controller authorization and assignment
│   ├── web_portal.py          # self-contained local web application/API
│   ├── persistence.py         # settings and recoverable session state
│   ├── platform_identity.py   # Virtual/Atlas runtime selection
│   ├── serial_controller.py   # physical-module transport
│   ├── leds.py                # semantic LED rendering
│   ├── audio.py               # physical audio output
│   ├── game_log.py            # completed-game logs
│   └── status_monitor.py      # hardware/status monitoring
├── Arduino/                   # Arduino prototype firmware
├── ESP32/                     # ESP32 prototype firmware
├── requirements.txt
└── README.md
```

The important architectural rule is that `game_engine.py` should not care whether an action came from a physical Sigil, a Virtual Sigil, or a future controller implementation.

## Product direction

TurnHub is evolving toward one software platform with multiple deployment profiles rather than separate incompatible products:

- **TurnHub Virtual** - software-only local table management
- **Home Atlas** - headless first-party host with physical and/or Virtual Sigils, managed from players' devices
- **Venue/Tournament Atlas** - first-party multi-game host with an integrated display running the same web UI in kiosk mode
- **Community/third-party hardware** - compatible implementations that can participate without being represented as genuine TurnHub Atlas hardware

Multi-game tournament/venue management and production Atlas secure-element verification are future work, not current prototype capabilities.

## Status

TurnHub is under active development and the hardware, APIs, and UX may change. The current priority is keeping the core model clean while adding first-party features without making physical hardware mandatory.
