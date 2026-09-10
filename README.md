TurnHub
TurnHub is a physical turn-management system for tabletop games, currently focused on Magic: The Gathering.
The prototype uses a Raspberry Pi as the authoritative game controller and an Arduino Uno as a simple hardware I/O controller. The Arduino reports button and potentiometer events and receives LED and sound commands from the Pi.
Current prototype
The current build supports two physical player modules.
Each module has:
Blue status LED
Red warning LED
Green status LED
One momentary pushbutton
The shared prototype hardware also includes:
Piezo buzzer
Potentiometer for selecting the warning threshold
Project structure
`turnhub.py`  
Main application loop and top-level orchestration.
`config.py`  
Serial, timing, LED, warning, and application constants.
`serial_controller.py`  
Arduino serial connection, reconnect handling, input parsing transport, and output commands.
`game_engine.py`  
Game timing, turns, pause/resume behavior, victory state, and player statistics.
`lobby.py`  
Player joining, host assignment, starter selection, held-button tracking, and lobby reset behavior.
`leds.py`  
Lobby animations, active/waiting player LEDs, warning indicators, pause breathing, and game-over display.
`audio.py`  
All TurnHub chirps, tones, countdown sounds, and victory melody.
Serial protocol
Arduino to Raspberry Pi:
```text
READY
POT|<warning_ms>
BUTTON|<module>|DOWN
BUTTON|<module>|UP
BUTTON|<module>|SHORT
BUTTON|<module>|LONG
BUTTON|<module>|WIN
```
Raspberry Pi to Arduino:
```text
BLUE|<module>|<0-255>
RED|<module>|<0|1>
GREEN|<module>|<0|1>
SOUND|<frequency>|<duration_ms>
OFF
```
Lobby behavior
The first physical module to short-press becomes Player 1 and the host.
Additional modules join in the order they short-press.
Once joined, a short press selects that player as the starting player.
The host controls game start:
Hold for two seconds to arm start.
Release before five seconds to begin the three-second countdown.
Continue holding to five seconds to clear the lobby.
At least two players are required to start.
During a game
Short press from the active player passes the turn.
A two-second hold pauses the game.
If that same continuous hold reaches five seconds, that player declares victory.
While paused, a new two-second hold resumes the game.
The warning threshold is locked at the beginning of each turn. Adjusting the potentiometer during a turn changes the threshold for the next turn.
At 75 percent of the warning threshold, the active player's green LED turns on.
At 100 percent, green turns off and the active player's red LED begins blinking.
After a game
The winner remains blue.
The host's green LED turns on so the table can identify the controller that owns the post-game controls.
Host short press starts a rematch lobby while preserving player assignments.
Host long press clears the player assignments and returns to an empty lobby.
Raspberry Pi setup
Install the Python dependency:
```bash
python3 -m pip install -r requirements.txt
```
On Raspberry Pi OS, pyserial can also be installed with:
```bash
sudo apt install -y python3-serial
```
Run TurnHub:
```bash
python3 turnhub.py
```
The default serial device is:
```text
/dev/ttyUSB0
```
You can override it with the `TURNHUB_SERIAL_PORT` environment variable. For example, during Windows testing:
```powershell
$env:TURNHUB_SERIAL_PORT="COM3"
python turnhub.py
```
Planned direction
The final architecture is intended to use a headless central hub, a phone app over Bluetooth Low Energy, and wireless player modules. The hub will remain authoritative for game state so gameplay can continue even if the phone disconnects.