# TurnHub Sigil Wokwi simulation

The Wokwi build runs the normal Sigil firmware with one deliberate substitution: Wokwi does not simulate ESP-NOW, so the `sigil-wokwi` environment force-includes `wokwi_espnow_shim.h`. The shim behaves like a tiny Atlas test harness and delivers normal TurnHub `Packet` objects to the existing Sigil receive handler.

The normal `sigil` PlatformIO environment is unchanged and continues to use real ESP-NOW.

## Build and run

From the `Sigil` directory:

```text
pio run -e sigil-wokwi
```

Then run **Wokwi: Start Simulator**. `wokwi.toml` loads the `sigil-wokwi` firmware and ELF files.

## Hardware in the diagram

`diagram.json` follows the Sigil Rev A schematic (`KiCad/PCB/Sigilv1`) and
`Documentation/engineering/HARDWARE_REFERENCE.md`:

| Part | Wokwi part | Wiring (DevKit socket from the schematic) |
| --- | --- | --- |
| ESP32 DevKit, 38 pins | `board-esp32-devkit-c-v4` | Same 38-pin layout as the real carrier socket |
| PASS button (`P`) | pushbutton | GPIO26 (A20) to GND (A24), INPUT_PULLUP |
| ACTION button (`A`) | pushbutton | GPIO25 (A19) to GND (A24) |
| PAUSE / WIN button (`W`) | pushbutton | GPIO32 (A17) to GND (A24) |
| PAIR button (`R`) | pushbutton | GPIO19 (J18) to GND (J17) |
| Red, green, blue LEDs (D1-D3) | LED + 330 ohm resistor (R1-R3) | GPIO13 (A25), GPIO14 (A22), GPIO27 (A21) -> 330R -> anode; cathode to GND (J11) |
| Buzzer | `wokwi-buzzer` | GPIO33 (A18) to GND (J11) |
| 2.13" e-paper | `chip-epaper-2in13` (local custom chip) | CLK 18, DIN 23, CS 17, DC 16, RST 22, BUSY 21, VCC 3V3, GND |

The e-paper is a local copy of Bonny Rais's SSD1680 e-paper chip model
(`wokwi/chips/`, MIT, release v0.0.5, the same binary Wokwi's own 2.9" board
uses) with its panel set to the real GDEM0213B74's controller geometry: 128 RAM
columns by 250 lines, portrait. The firmware drives it with the production
`GxEPD2_213_B74` driver, unchanged. The model ignores the temperature-sensor
command (`0x18`), which has no visible effect.

## Differences from the real devices

- **Radio:** ESP-NOW is replaced by the in-process fake Atlas, so range, packet
  loss, channel and real Atlas interoperability are not simulated.
- **Atlas:** the fake Atlas follows the real one's pairing and acknowledgement
  rules (`Atlas/src/sigil_bus.cpp`) but runs no game: LEDs, sounds and screens
  change only when you type console commands.
- **Display:** the 6 RAM columns the real panel does not show (122 of 128 are
  visible) appear here as a blank strip, and there is no e-ink ghosting or
  refresh flashing.
- **Buzzer:** the physical transducer on the Sigil is still unconfirmed
  (schematic J3); Wokwi's buzzer plays the tones directly.
- **Storage:** Wokwi normally starts each run from freshly flashed firmware, so
  the Sigil behaves like a new, unpaired device and must be paired every time.

## Pairing, as on real hardware

A new Sigil is unpaired: the screen says so and it ignores everything until it
pairs. As with a real Atlas, both sides must be in pairing mode:

1. Type `pair` in the serial console. This stands in for pressing Atlas's Pair
   button and opens the fake Atlas's 15-second window.
2. Press the Sigil's PAIR button (`R`) within those 15 seconds. The red LED
   blinks while the Sigil broadcasts `PairRequest`.
3. The fake Atlas answers `PairAccept` with the Sigil ID set by `id` (default 0).
   The Sigil stores it (`SIGIL|PAIR|SUCCESS`), sends Hello, and asks for player
   names.

Pressing PAIR without an open window ends in `SIGIL|PAIR|TIMEOUT`, as on real
hardware. After pairing the fake Atlas only acknowledges Hello, PASS, the
ACTION family and profile requests from that Sigil, exactly like the real one,
and sends Sigil 0.5.4+ its hold thresholds (`InputTiming`, default 2 s / 5 s).

### Forgetting a pairing

- Hold PAIR (`R`) for 10 seconds: the Sigil erases its saved pairing
  (`SIGIL|PAIR|FORGOTTEN|BUTTON`) and shows "Unpaired". A short press still
  only opens the pairing window. The fake Atlas keeps its record, as a real
  Atlas does until an admin forgets the Sigil in the portal.
- Type `forget`: the fake Atlas forgets the Sigil and sends `Unpair`, as an
  admin's Forget does on a real Atlas. Sigil 0.5.5+ erases its pairing
  (`SIGIL|PAIR|FORGOTTEN|ATLAS`). Type `pair` and press PAIR to pair again.

## Buttons

PAUSE / WIN switches GPIO 32 to GND and uses the internal pull-up. Tap to
pause/resume (the existing ACTION-long semantic), or hold for the win hold
(5 seconds by default) to claim a win. A win hold sends ACTION_LONG followed by
ACTION_WIN; it does not pause at the long-press threshold or send ACTION_SHORT
on release. GPIO 25 retains its existing ACTION behavior, including short-press
win confirmation. `timing 3000 6000` changes the thresholds the way a player's
accessibility setting does on a real table.

PAIR also switches to GND with the internal pull-up; the firmware takes GPIO 19
back from SPI MISO after the display starts.

## Atlas console commands

Type commands into the Wokwi serial console and press Enter:

```text
help
pair
id 3
forget
timing 3000 6000
blue 128
red 1
green 1
buzz 2000 250
name A Ricky
name B Micky
profile
state lobby 1 2 0 0x10
state starting 1 2 0 0x20
state running 1 2 4 0x08
state paused 1 2 4 0x80
state gameover 1 2 4 0x40
raw 20 255
```

`state` arguments are:

```text
state <mode> <primary player> <secondary player> <turn number> <flags>
```

Modes are `ready`, `lobby`, `starting`, `running`, `paused`, or `gameover`.

Useful display flags from `protocol.h` are:

- `0x08` active
- `0x10` host
- `0x20` starter
- `0x40` winner
- `0x80` attention

Flags can be combined, for example `0x18` means active + host.

## What this tests

The simulation exercises the production Sigil's button debounce and hold timing (including Atlas-sent thresholds), the manual pairing handshake and its 15-second window, packet creation, packet receive handling, ACK behavior, LED commands, buzzer commands, profile-name chunk assembly, display-state decoding, display-task scheduling, and the production 2.13" display driver against an SSD1680 model.

It does **not** test the ESP-NOW radio itself, RF behavior, peer discovery, packet loss, or real Atlas/Sigil wireless interoperability. Those still require physical ESP32 hardware.

Wokwi also does not currently support multiple microcontrollers in one simulation, which is why the Atlas harness lives inside the simulation transport rather than on a second virtual ESP32.
