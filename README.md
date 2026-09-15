# TurnHub

TurnHub is a physical turn timer and game management system I am building primarily for Magic: The Gathering.

The project started with a pretty simple idea: give each player a button and make it obvious whose turn it is. It has grown into a central hub that keeps track of turn order, turn time, warnings, pauses, player assignments, and game state.

The current version is still a prototype and uses a Raspberry Pi as the main controller with an Arduino Uno handling the physical hardware.

## How It Works

The Raspberry Pi is the brain of TurnHub.

It keeps track of:

- Players
- Turn order
- Current player
- Turn timers
- Warning thresholds
- Paused time
- Game state
- Player statistics
- Winner
- Lobby and rematch state

The Arduino does not make any game decisions. It acts as an I/O controller for the buttons, LEDs, potentiometer, and piezo buzzer.

This keeps the game state in one place and will make replacing the wired prototype with wireless player modules much easier later.

## Current Hardware

The current breadboard prototype supports two players.

Each player has:

- One pushbutton
- Blue LED
- Red LED
- Green LED

The prototype also has:

- Raspberry Pi 3B
- OSEPP Uno R3 Plus
- Piezo buzzer
- Potentiometer for setting the turn warning time

## Player Controls

Each player only needs one button.

A short press by the active player ends their turn and passes to the next player.

Holding the button for 2 seconds pauses the game.

If the same hold continues to 5 seconds, that player declares victory.

While the game is already paused, holding a button for 2 seconds resumes the game.

## Starting a Game

TurnHub starts in a lobby instead of immediately starting a game.

The first module to press its button becomes Player 1 and the host.

The next module becomes Player 2, followed by Player 3, and so on as more modules are eventually supported.

After joining, a player can short press their button again to select themselves as the starting player.

The host starts the game by holding their button for 2 seconds and releasing it.

TurnHub then performs a 3 second countdown before starting the first player's timer.

If the host continues holding the button for 5 seconds instead of releasing it, the lobby is cleared.


## Shared-Module Players

A physical module can deliberately represent two adjacent logical players. In the lobby, join the module normally, then **hold Action and tap Pass** to add a second player. Repeat the same chord to remove that second player.

The chord does not also trigger the host randomizer or the normal Action-short command.

With two modules and four players, table order is naturally:

```text
Module 0: Player 1, Player 2
Module 1: Player 3, Player 4
```

Pass still advances one logical player at a time. A same-module handoff has its own LED pattern and two-note sound.

The starter randomizer chooses among all logical players equally. On a shared module, Slot A is shown with one repeating blue pulse and Slot B with two repeating blue pulses. Manual Action-short starter selection cycles between the two local seats.

## LED Behavior

The LEDs are intended to communicate most of the important game state without requiring a display.

### Lobby

Unjoined modules cycle through blue, red, and green.

Joined players use the red LED to indicate their assigned player number.

The host has a green LED.

The selected starting player has a blue LED.

### During a Game

The waiting player has a solid blue LED.

When a turn begins, the active player's blue LED flashes quickly for 3 seconds and then begins breathing.

When the active player reaches 75% of their warning time, their green LED turns on.

At 100%, the green LED turns off and the red LED begins flashing.

When the game is paused, all player modules breathe blue together.

### Game Over

The winner flashes blue and then remains solid blue.

The host's green LED also turns on so everyone knows which controller can start the next game.

The host can short press for a rematch with the same players or long press to clear the lobby completely.

## Warning Timer

The potentiometer controls when TurnHub begins warning a player about the length of their turn.

The warning can currently be adjusted from 5 seconds to 5 minutes in 5 second increments.

The value is locked when a turn begins.

This means changing the potentiometer during someone's turn does not suddenly change their current timer. The new setting takes effect when the next turn starts.

This also allows the warning time to be increased as a game gets longer and turns naturally become more complicated.

## Project Structure

The Raspberry Pi software is written in Python and has been split into several modules instead of keeping the entire program in one large file.

```text
TurnHub/
├── turnhub.py
├── config.py
├── serial_controller.py
├── game_engine.py
├── lobby.py
├── leds.py
├── audio.py
├── requirements.txt
└── README.md

## Read-only web portal

TurnHub serves a self-contained status page on the local network at port 8080.
No cloud service or external web assets are required, and the portal exposes no
game-control actions. On a typical Raspberry Pi hostname, browse to:

```text
http://turnhub.local:8080/
```

If mDNS is unavailable, use the Raspberry Pi's LAN IP address instead.
## Local web settings and recovery

The local web portal at `http://turnhub.local:8080/` (or the hub IP on port 8080) includes a basic Settings panel for persistent module and player/seat names. Names are keyed to physical module seats so they remain attached to the same person/seat if logical player numbers shift during lobby setup.

TurnHub stores user settings and the recoverable session outside the Git working tree under `~/.local/share/turnhub/`. Game state is saved after state changes and periodically during active play. If TurnHub restarts during a running game, the game is restored **paused** with player assignments, starter, active player, timers, and statistics preserved; downtime is not charged to a turn.


## Web settings, recovery, and paired Pass control

TurnHub serves its local portal on port `8080`. The portal can name modules and physical player seats, and TurnHub stores those settings outside the Git checkout under `~/.local/share/turnhub/`. Recoverable game state is autosaved there as well; an interrupted live game returns paused so reboot downtime is never charged to a turn.

A browser may optionally become a player controller. In the lobby, the browser chooses an exact physical seat and TurnHub requires a short Action press on that seat's physical module before issuing a random browser token. One browser claim is allowed per seat. The browser token is stored only in that browser; TurnHub persists only its SHA-256 hash. Once the game starts, normal player identity changes are locked.

The web Pass button is shown only when the paired browser's exact logical seat is active. The server independently verifies the bearer token, running state, and exact active `(module, slot)` before advancing the turn. A hidden or manually forged button cannot pass another player's turn.

If a phone is lost or dies during play, pause the game. A replacement browser may request reassignment to a seat, but the reassignment is not accepted until the physical host module approves it with a short Action press. Approval replaces the old token immediately.

The physical Pass button always remains available. TurnHub also suppresses the near-simultaneous duplicate physical Pass that could arrive immediately after a successful web Pass on a shared module.

## Player Elimination

Multiplayer games keep eliminated players in the game record without renumbering anyone. Turn order skips eliminated seats, their statistics remain available, and the last living player is declared the winner automatically.

Elimination is deliberately physical and only available while the game is paused:

1. Pause the game normally.
2. On the module containing the player to eliminate, **hold Action and tap Pass**.
3. TurnHub marks the selected seat in red. On a shared module, Action Short cycles between the living local seats. Seat A uses one repeating red pulse and Seat B uses two.
4. Press Pass on that module with Action released to confirm the elimination.
5. The game remains paused. Hold Action normally when the table is ready to resume.

Action Long cancels an armed elimination selection instead of resuming immediately. A confirmed elimination gives three red LED strikes and a distinct descending audio cue. Eliminated players remain visible in the web portal with an `ELIMINATED` marker and are included in game logs and recovered session state.
