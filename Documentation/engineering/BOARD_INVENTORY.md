# Board Inventory

Turnhub development board inventory, identified by factory MAC address. COM port
numbers change whenever the PC restarts, so identify a board by MAC, never by
port. Read a board's MAC (this resets the board) with:

```
pio pkg exec -p tool-esptoolpy -- esptool.py --port COMx read_mac
```

`tools\flash-all.cmd` reads this table after `tools\boards.local.md`, the
git-ignored list of boards on one PC that board setup writes. Run
`tools\setup-boards.cmd` (or answer yes when a flash run finds a new board): it
reads each attached board's MAC, suggests Atlas for a CH340 bridge and, for a
CP210x Sigil already running TurnHub firmware, the display type its GPIO4 strap
reports at boot (`SIGIL|HW|EINK` or `SIGIL|HW|OLED`), then asks you to confirm
and name it. A Sigil with no TurnHub firmware yet has no suggestion: check its
strap. It then lists every recorded board, shared or local, to rename, change
type, make spare, return to service or delete. Those changes go only to
`tools\boards.local.md` (a local row overrides this table on that PC), so copy a
row here when the change belongs in the shared development record.
*Planned*: written 2026-10-06 without a Windows run; not yet tried on hardware.

| Board | Firmware | USB bridge | MAC | Notes |
|---|---|---|---|---|
| Atlas | Atlas | CH340 | `B4:BF:E9:12:85:74` | LCDwiki E32R28T 2.8" display board; this MAC is its `THA-` ID |
| OLED Sigil | `sigil-oled` | CP210x | `4C:C3:82:28:1D:14` | New board, added 2026-10-01: ESP32-WROOM-32E 38-pin DevKit, USB-C (ESP32-D0WD-V3 rev 3.1). No carrier or breakout. 128x64 OLED, analog joystick, IO4 pin wired to GND. MAC differs from the E-ink Sigil's only in the last byte (`14` vs `84`) |
| E-ink Sigil | `sigil` | CP210x | `4C:C3:82:28:1D:84` | New board, added 2026-10-01: ESP32-WROOM-32E 38-pin DevKit, USB-C (ESP32-D0WD-V3 rev 3.1). No carrier or breakout. 122x250 e-ink, analog joystick, IO4 pin left open |
| TestHarness | `harness` | CP210x | `D4:E9:F4:B4:27:3C` | Also pairs as a second virtual Sigil on its soft-AP MAC `D4:E9:F4:B4:27:3D`. Never flash Sigil or Atlas firmware onto it. |

### Spare boards

Kept, but never flashed by `flash-all` (their firmware column is `spare`). To
bring one back, set its firmware column to `sigil` or `sigil-oled`.

| Board | Firmware | USB bridge | MAC | Notes |
|---|---|---|---|---|
| Spare OLED Sigil (old) | `spare` | CP210x | `20:E7:C8:94:49:80` | Was the OLED Sigil until 2026-10-01; last ran `sigil-oled`. 128x64 OLED, analog joystick, A7 strap to GND |
| Spare E-ink Sigil (old) | `spare` | CP210x | `F4:65:0B:C4:FF:38` | Was the E-ink Sigil until 2026-10-01; last ran `sigil`. 122x250 e-ink, analog joystick, A7 strap open |

Sigils also carry their display type on a strap: GPIO4 open on the E-ink
Sigil, wired to GND on the OLED Sigil. A build for the wrong display halts at
boot (see `Sigil/DISPLAY.md`). On the new boards, which have no carrier or
breakout, that means a jumper from the DevKit pin labelled `IO4` to any `GND`
pin on the OLED Sigil. `A7` is the same pin's position on the KiCad carrier.

The new DevKits have the same 38-pin layout and silkscreen labels as the old
ones (`IO34`/`IO35`/`IO32` for the joystick, `IO4` between `IO16` and `IO0`), a
USB-C connector and a CP2102 bridge whose USB serial number is the generic
`0001`. Windows may not set up a second one while the first is plugged in;
connect them one at a time if a board does not appear.

*Verified* 2026-09-25 by reading each MAC with `read_mac` and matching each
board's boot banner (`SIGIL|DISPLAY|OLED`, `SIGIL|DISPLAY|READY|122x250`,
`HARNESS|BOOT`). That covered Atlas, the TestHarness and the two Sigils now
listed as spares.

The new E-ink and OLED Sigils' MACs were read with `read_mac` on 2026-10-01
(E-ink plugged in first, OLED second). *Verified* 2026-10-01 (owner photo):
the new E-ink Sigil, built into its wooden case, shows the paired lobby screen
("Sigil 2 · Ready for game") with the status ring lit, so its firmware and
open strap are right. The new OLED Sigil is also up and running (owner,
2026-10-01), so its IO4-to-GND strap is right.
