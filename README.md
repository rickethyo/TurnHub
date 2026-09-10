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