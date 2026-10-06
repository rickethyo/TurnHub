# Board Inventory

The flash scripts identify boards by factory MAC address, never by COM port
(port numbers change whenever the PC restarts). Each PC keeps its own list in
`tools\boards.local.md`, which git ignores. `tools\setup-boards.cmd` writes it:
run it once with your boards plugged in, or answer yes when `flash-all` finds a
board it doesn't know.

Board setup reads each attached board's MAC (this resets the board) and
suggests what it is: Atlas for a CH340 USB bridge, and for a CP210x Sigil
already running TurnHub firmware, the display type its GPIO4 strap reports at
boot (`SIGIL|HW|EINK` or `SIGIL|HW|OLED`). A Sigil with no TurnHub firmware
yet has no suggestion, so check its strap. A board running TurnHub firmware is
also told to blink white for 10 s (the Sigil's ring, Atlas's on-board RGB LED),
and so is an attached board you pick from the list, so you can see which one
the script means; send `identify` on a serial console for the same. You
confirm and name each board.
Setup then lists every recorded board to rename, change type, make spare,
return to service or delete. To read a MAC by hand:

```
pio pkg exec -p tool-esptoolpy -- esptool.py --port COMx read_mac
```

## Example

`tools\boards.local.md` uses this table. These rows are made up (`02:` MACs
are locally administered, so no real board has one); they show one entry per
device type.

| Board | Firmware | USB bridge | MAC | Notes |
|---|---|---|---|---|
| Atlas | `atlas` | CH340 | `02:00:00:00:00:01` | Example: LCDwiki E32R28T 2.8" display board; its MAC is its `THA-` ID |
| E-ink Sigil | `sigil` | CP210x | `02:00:00:00:00:02` | Example: ESP32 DevKit, 122x250 e-ink, GPIO4 left open |
| OLED Sigil | `sigil-oled` | CP210x | `02:00:00:00:00:03` | Example: ESP32 DevKit, 128x64 OLED, GPIO4 wired to GND |

Firmware values:

- `atlas`, `sigil` (E-ink) and `sigil-oled` are flashed by `flash-all`.
- `spare`, or `spare:<firmware it ran>` after **Make spare**: a Sigil gets the
  inert spare firmware ([Spare Sigil](SPARE_SIGIL.md)) at the next flash; a spare
  Atlas is left alone. **Return to service** suggests the firmware it ran. A spare
  brought back over the air is noticed at the next flash and recorded.
- **Delete** erases an attached board's whole flash (firmware, pairing, settings)
  after you type `y`, then forgets it; a board that isn't attached is only
  forgotten.
- Anything else (the retired test harness's `harness`, for example) is listed
  and never flashed.

## Hardware notes

Sigils carry their display type on a strap: GPIO4 open on an E-ink Sigil,
wired to GND on an OLED Sigil. A build for the wrong display halts at boot
(see `Sigil/DISPLAY.md`). On a bare DevKit with no carrier or breakout, that
means a jumper from the pin labelled `IO4` to any `GND` pin on the OLED Sigil.
`A7` is the same pin's position on the KiCad carrier.

The ESP32-WROOM-32E 38-pin DevKits used for Sigils have the joystick on
`IO34`/`IO35`/`IO32` and `IO4` between `IO16` and `IO0`. Their CP2102 bridge
reports the generic USB serial number `0001`, so Windows may not set up a
second one while the first is plugged in; connect them one at a time if a
board does not appear.
