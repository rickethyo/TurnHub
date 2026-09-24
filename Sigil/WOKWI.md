# TurnHub Sigil Wokwi simulation

The Wokwi build runs the normal Sigil firmware with one deliberate substitution: Wokwi does not simulate ESP-NOW, so the `sigil-wokwi` environment force-includes `wokwi_espnow_shim.h`. The shim behaves like a tiny Atlas test harness and delivers normal TurnHub `Packet` objects to the existing Sigil receive handler.

The normal `sigil` PlatformIO environment is unchanged and continues to use real ESP-NOW.

## Build and run

From the `Sigil` directory:

```text
pio run -e sigil-wokwi
```

Then run **Wokwi: Start Simulator**. `wokwi.toml` loads the `sigil-wokwi` firmware and ELF files.

The simulated Atlas automatically acknowledges the Sigil Hello packet, assigns Sigil ID 0, acknowledges Sigil control packets, and responds to a display-profile request with default names for Seat A and Seat B.

The Wokwi diagram maps:

- `P` to the PASS button on GPIO 26
- `A` to the ACTION button on GPIO 25
- `W` to the PAUSE / WIN button on GPIO 32 (breadboard J13)
- blue LED to GPIO 27
- green LED to GPIO 14
- red LED to GPIO 13
- buzzer to GPIO 33
- e-paper SPI/control wiring to the production Sigil pins

PAUSE / WIN switches GPIO 32 to GND and uses the internal pull-up. Tap to
pause/resume (the existing ACTION-long semantic), or hold 5 seconds to claim
a win. A win hold sends ACTION_LONG followed by ACTION_WIN at 5 seconds;
it does not pause at 2 seconds or send ACTION_SHORT on release. GPIO 25
retains its existing ACTION behavior, including short-press win confirmation.

## Atlas console commands

Type commands into the Wokwi serial console and press Enter:

```text
help
id 3
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

The simulation exercises the production Sigil's button debounce and hold timing, packet creation, packet receive handling, ACK behavior, LED commands, buzzer commands, profile-name chunk assembly, display-state decoding, and display-task scheduling.

It does **not** test the ESP-NOW radio itself, RF behavior, peer discovery, packet loss, or real Atlas/Sigil wireless interoperability. Those still require physical ESP32 hardware.

Wokwi also does not currently support multiple microcontrollers in one simulation, which is why the Atlas harness lives inside the simulation transport rather than on a second virtual ESP32.
