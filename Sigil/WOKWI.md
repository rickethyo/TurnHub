# TurnHub Sigil Wokwi simulation

The Wokwi build runs the normal Sigil firmware with one deliberate substitution: Wokwi does not simulate ESP-NOW, so the `sigil-wokwi` environment force-includes `wokwi_espnow_shim.h`. The shim behaves like a tiny Atlas test harness and delivers normal TurnHub `Packet` objects to the existing Sigil receive handler.

The simulation is the E-ink Sigil (`sigil`) with the same analog thumbstick and NeoPixel status ring; only the radio and the Pair button differ.

## Build and run

From the `Sigil` directory:

```text
pio run -e sigil-wokwi
```

Then run **Wokwi: Start Simulator**. `wokwi.toml` loads the `sigil-wokwi` firmware and ELF files.

## Hardware in the diagram

Since 2026-09-30 (Sigil 0.9.0) every Sigil has five keys, Atlas's action menu
and the Jewel ring, so the simulation wires the E-ink Sigil's parts (see
`platformio.ini` and `Documentation/engineering/HARDWARE_REFERENCE.md`). The
old Rev A three-button layout and discrete LEDs are gone with the firmware
path that used them.

| Part | Wokwi part | Wiring |
| --- | --- | --- |
| ESP32 DevKit, 38 pins | `board-esp32-devkit-c-v4` | Same 38-pin layout as the real carrier socket |
| Thumbstick | `wokwi-analog-joystick` | HORZ GPIO34 (VRX), VERT GPIO35 (VRY), SEL GPIO32 (click = Select), VCC 3V3 |
| Status ring | `wokwi-led-ring`, 7 pixels | DIN GPIO26, VCC 5V. RGB here; the real Jewel is RGBW (`status_ring.cpp` picks the format) |
| PAIR button (`R`) | pushbutton | GPIO19 to GND, standing in for the DevKit's BOOT button (GPIO0) |
| Buzzer | `wokwi-buzzer` | GPIO33 to GND |
| 2.13" e-paper | `chip-epaper-2in13` (local custom chip) | CLK 18, DIN 23, CS 17, DC 16, RST 22, BUSY 21, VCC 3V3, GND |

The simulated stick reads the right way round, so this build skips the real
E-ink Sigil's rotated-mount axis swap.
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
  rules (`Atlas/src/sigil_bus.cpp`) but runs no game: the ring, sounds, menus and screens
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

1. Type `pair` in the serial console. This stands in for tapping **Pair a
   Sigil** on the Atlas touchscreen and opens the fake Atlas's 60-second window.
2. Press the Sigil's PAIR button (`R`) within those 60 seconds. The ring
   blinks red while the Sigil broadcasts `PairRequest2` (pairing v2, see
   `Documentation/engineering/SECURE_LINK.md`).
3. The fake Atlas answers `PairAccept2` with the Sigil ID set by `id` (default
   0) and prints the pairing code (`WOKWI|ATLAS|PAIRING|V2|CODE|0427`). The
   Sigil's screen shows the same code.
4. Type `confirm` (the Atlas screen's **Codes match**) or `reject`. On
   `confirm` the Sigil stores Atlas and the pair key
   (`SIGIL|PAIR|SUCCESS|SECURE`), sends Hello, and asks for player names. On
   `reject`, or no answer for about a minute, nothing is stored.
5. The Sigil then starts a secure session (`SecureHello`; the fake Atlas logs
   `WOKWI|ATLAS|SECURE|SESSION_READY`) and everything after that travels sealed,
   as with a real Atlas. The fake Atlas keeps the pair key in RAM only: after
   restarting the simulation, `pair` and `confirm` again (the Sigil forgets its
   old pairing when told, or hold PAIR for 10 seconds).

Pressing PAIR without an open window ends in `SIGIL|PAIR|TIMEOUT`, as on real
hardware. After pairing the fake Atlas acknowledges Hello, menu choices
(`SelectAction`, logged as `WOKWI|ATLAS|MENU|SELECT|<action>|REVISION|<n>`),
picker keys, life packets and profile requests from that Sigil, like the real
one, and sends its hold thresholds once (`InputTiming`, default 2 s / 5 s).

### Forgetting a pairing

- Hold PAIR (`R`) for 10 seconds: the Sigil erases its saved pairing
  (`SIGIL|PAIR|FORGOTTEN|BUTTON`) and shows "Unpaired". A short press still
  only opens the pairing window. The fake Atlas keeps its record, as a real
  Atlas does until an admin forgets the Sigil in the portal.
- Type `forget`: the fake Atlas forgets the Sigil and sends `Unpair`, as an
  admin's Forget does on a real Atlas. The Sigil erases its pairing
  (`SIGIL|PAIR|FORGOTTEN|ATLAS`). Type `pair` and press PAIR to pair again.

## Keys

The thumbstick is the Sigil's five keys: push Up, Down, Left or Right, and
click it for Select. Until the fake Atlas sends a menu (`menu`), the keys do
nothing, as on a real Sigil whose Atlas has sent none. Deliberate choices
(Leave, Eliminate, Reset, Claim win) must be held; `timing 3000 6000` changes
the thresholds the way a player's accessibility setting does on a real table.

PAIR switches to GND with the internal pull-up; the firmware takes GPIO 19
back from SPI MISO after the display starts.
## Atlas console commands

Type commands into the Wokwi serial console and press Enter:

```text
help
pair
id 3
forget
timing 3000 6000
menu 0xA1 0
led 5 0 1 0
buzz 2000 250
name A Ricky
name B Micky
profile
state lobby 1 2 0 0x10
state starting 1 2 0 0x20
state running 1 2 4 0x08
state paused 1 2 4 0x80
state gameover 1 2 4 0x40
raw 38 1
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

`menu <mask> [default]` offers actions as a `MenuState2`: each bit is a
`SigilAction` number from `protocol.h` (0 Join, 1 CycleStarter, 5 StartGame,
7 Pass, ...), so `menu 0xA1 0` offers Join, StartGame and Pass with Join
first. `led <cue> [overlays] [player] [style]` sends a `LedState` (cues 1-10
from `LedCue`; style 0 default, 1 reduced motion, 2 monochrome-safe), for
example `led 5 0 1 0` for Your turn.

## What this tests

The simulation exercises the production Sigil's thumbstick input, the action menu and its holds (including Atlas-sent thresholds), the manual pairing handshake and its 60-second window, packet creation, packet receive handling, ACK behavior, the status ring's cues and styles, buzzer commands, profile-name chunk assembly, display-state decoding, display-task scheduling, and the production 2.13" display driver against an SSD1680 model.

It does **not** test the ESP-NOW radio itself, RF behavior, peer discovery, packet loss, or real Atlas/Sigil wireless interoperability. Those still require physical ESP32 hardware.

Wokwi also does not currently support multiple microcontrollers in one simulation, which is why the Atlas harness lives inside the simulation transport rather than on a second virtual ESP32.
