# TurnHub Hardware Test Harness

**Status:** *Experimental* (2026-09-25). A virtual-Sigil emulator and a full
game scenario run against a real Atlas. The firmware builds and pairs over the
air; treat a scenario's PASS as a check of Atlas's radio and menu paths, not as
hardware acceptance of a physical Sigil.

One ESP32 (any `esp32dev` board; the owner's is the former Atlas DevKit, with
no buttons or LEDs besides BOOT) plays **two menu Sigils** against a real Atlas
over ESP-NOW:

- **V1** is the board's Wi-Fi station MAC and **V2** its soft-AP MAC. Wi-Fi
  runs in AP+STA mode, so both MACs are live at once and Atlas pairs them as
  two separate Sigils (two of Atlas's eight slots). The soft-AP is hidden and
  password-protected; it exists only to own the second MAC on channel 6.
- Each virtual Sigil can take Seat B too, so one board seats up to **four
  players**.
- They advertise `CAPABILITY_MENU` in Hello and act only through
  `SelectAction`, choosing from the `MenuState` Atlas sends: exactly the path a
  real menu Sigil (OLED d-pad or E-ink joystick) uses.

## Architectural boundary

- Atlas remains the sole authority for table and game state. The harness reads
  the menus Atlas offers and picks from them; it never decides an outcome.
- Shared ESP-NOW packet definitions come from `../shared/include/protocol.h`;
  do not copy them into this project.
- Pairing goes through Atlas's real pairing window (tap **Pair a Sigil** on the
  touchscreen); the harness never edits Atlas's pairing storage.
- It must not depend on Atlas GPIO or display hardware.

## Feature gate

1. **State owner:** Atlas. The harness owns only transient run state and its
   own pairing record (NVS `th_harness/pair`: Atlas MAC and the two Sigil IDs).
2. **Intent/request:** the existing Sigil packets (PairRequest, Hello,
   SelectAction). No test-only gameplay semantics.
3. **Validator:** Atlas's normal transport adapters and Intent handlers.
4. **Persistence:** none for canonical state.
5. **Presentation:** serial console, machine-readable lines.
6. **Protocol change:** none.
7. **Third-party impact:** none (Arduino ESP32 core only).
8. **Accessibility impact:** none to gameplay; developer tooling only.

## From the Atlas touchscreen

V1 advertises `CAPABILITY_HARNESS`, so while the harness is online the Atlas
lobby shows **Tests** beside Pair a Sigil. It opens the premade tests: **Radio**
(radio check), **2p game**, **4p game**, **Rematch** (3 players: a game, a
rematch and a second game) and **Soak x5**. Tap one and the harness plays it
through its Sigils. The title and detail lines show the test, the current
checkpoint and the result in words, and **Stop test** aborts it. The test
screen stays up through the game until **Back**. Atlas and the harness speak
`HarnessCommand` and `HarnessReport` (see
`Documentation/engineering/PROTOCOL_AND_PAIRING.md`).

## Serial commands (115200 baud)

| Command | What it does |
|---|---|
| `status` | Radio, Atlas MAC, each virtual Sigil's MAC, ID, online state and current menu |
| `pair` | Sends PairRequest from every unpaired virtual Sigil for 30 s. Tap **Pair a Sigil** on Atlas during that time |
| `forget` | Forgets the pairing on the harness only. Forget the two Sigils in the portal's Device Settings as well |
| `sigils <1\|2>` | Use one or both virtual Sigils |
| `pace <ms>` | Pause before each menu choice (default 1500 ms, 0-10000, kept in NVS) so a run can be followed on the Atlas screen |
| `verbose <on\|off>` | Log every packet sent and received |
| `test [n]` | Runs premade test n, as the Atlas touchscreen does; with no number, lists them |
| `menu` | Print what Atlas currently offers each virtual Sigil |
| `select <V1\|V2> <action>` | Send one menu choice by hand (`join`, `start`, `pass`, `claim-win`, ...) |
| `run smoke` | Each virtual Sigil is paired, answers Hello and has a menu |
| `run game [players] [turns] [rematch]` | A whole game (defaults: 4 players, 6 turns), below |
| `run soak [games] [players]` | Repeats `run game` (4 turns each) until one fails |
| `x` | Aborts a running scenario |

`run game` needs the table in its lobby. Any seated Sigil may start (there is
no table host since 2026-09-25), so V1 starts it. Its steps:

1. `LOBBY`, then `JOIN` for V1 and V2, then `SEAT_B` for as many extra players
   as asked for (V1 B, then V2 B).
2. `HOST` (a harness Sigil offered Start; the step name is historical) and
   `START`: V1 picks Start, and the countdown ends in a running game.
3. `TURN` × N: the active Sigil picks Pass, sees Cancel pass while Atlas holds
   the pass for its 3 s grace, and the pass commits.
4. `PAUSE` and `RESUME`.
5. With 3+ players, `ELIMINATE`: V1 pauses, says "I'm out" and eliminates one
   of its seats, then the table resumes.
6. `CLAIM_WIN` by the active player, `CONFIRM_WIN` from every other living
   player in the order Atlas asks, then `GAME_OVER`.
7. `RESET_TABLE` back to an empty lobby (or `REMATCH_LOBBY` with `rematch`).

Every step prints `HARNESS|PASS|<step>|...` or `HARNESS|FAIL|<step>|...` with
the menus at that moment, and each run ends with
`HARNESS|SUMMARY|<scenario>|passed=N|failed=N`. Games end in a guest win, and
Atlas records statistics only for signed-in profiles.

## Build and flash

From `TestHarness/` (the COM number changes between PC restarts; the harness
is a CP210x port, like the Sigils, so check which is which first):

```text
pio run -e harness
pio run -e harness --target upload --upload-port COMx
pio device monitor --port COMx
```
