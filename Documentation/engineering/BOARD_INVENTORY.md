# Board Inventory

Turnhub development board inventory, identified by factory MAC address. COM port
numbers change whenever the PC restarts, so identify a board by MAC, never by
port. Read a board's MAC (this resets the board) with:

```
pio pkg exec -p tool-esptoolpy -- esptool.py --port COMx read_mac
```

| Board | Firmware | USB bridge | MAC | Notes |
|---|---|---|---|---|
| Atlas | Atlas | CH340 | `B4:BF:E9:12:85:74` | LCDwiki E32R28T 2.8" display board; this MAC is its `THA-` ID |
| OLED Sigil | `sigil-oled` | CP210x | `4C:C3:82:28:1D:14` | New board, added 2026-10-01 (ESP32-D0WD-V3 rev 3.1). 128x64 OLED, analog joystick, A7 strap to GND. MAC differs from the E-ink Sigil's only in the last byte (`14` vs `84`) |
| E-ink Sigil | `sigil` | CP210x | `4C:C3:82:28:1D:84` | New board, added 2026-10-01 (ESP32-D0WD-V3 rev 3.1). 122x250 e-ink, analog joystick, A7 strap open |
| TestHarness | `harness` | CP210x | `D4:E9:F4:B4:27:3C` | Also pairs as a second virtual Sigil on its soft-AP MAC `D4:E9:F4:B4:27:3D`. Never flash Sigil or Atlas firmware onto it. |

### Spare boards

Kept, but never flashed by `flash-all` (their firmware column is `spare`). To
bring one back, set its firmware column to `sigil` or `sigil-oled`.

| Board | Firmware | USB bridge | MAC | Notes |
|---|---|---|---|---|
| Spare OLED Sigil (old) | `spare` | CP210x | `20:E7:C8:94:49:80` | Was the OLED Sigil until 2026-10-01; last ran `sigil-oled`. 128x64 OLED, analog joystick, A7 strap to GND |
| Spare E-ink Sigil (old) | `spare` | CP210x | `F4:65:0B:C4:FF:38` | Was the E-ink Sigil until 2026-10-01; last ran `sigil`. 122x250 e-ink, analog joystick, A7 strap open |

Sigils also carry their display type on a strap: header A7 (GPIO4) open on
the E-ink Sigil, wired to GND on the OLED Sigil. A build for the wrong display
halts at boot (see `Sigil/DISPLAY.md`).

*Verified* 2026-09-25 by reading each MAC with `read_mac` and matching each
board's boot banner (`SIGIL|DISPLAY|OLED`, `SIGIL|DISPLAY|READY|122x250`,
`HARNESS|BOOT`). That covered Atlas, the TestHarness and the two Sigils now
listed as spares.

The new E-ink and OLED Sigils' MACs were read with `read_mac` on 2026-10-01
(E-ink plugged in first, OLED second). *Needs verification:* their display
straps and boot banners have not been checked yet.
