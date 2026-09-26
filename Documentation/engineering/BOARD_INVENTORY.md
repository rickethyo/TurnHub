# Board Inventory

The owner's development boards, identified by factory MAC address. COM port
numbers change whenever the PC restarts, so identify a board by MAC, never by
port. Read a board's MAC (this resets the board) with:

```
pio pkg exec -p tool-esptoolpy -- esptool.py --port COMx read_mac
```

| Board | Firmware | USB bridge | MAC | Notes |
|---|---|---|---|---|
| Atlas | Atlas | CH340 | `B4:BF:E9:12:85:74` | LCDwiki E32R28T 2.8" display board; this MAC is its `THA-` ID |
| OLED Sigil | `sigil-oled` | CP210x | `20:E7:C8:94:49:80` | 128x64 OLED, five-button d-pad |
| E-ink Sigil | `sigil` | CP210x | `F4:65:0B:C4:FF:38` | 122x250 e-ink, analog joystick |
| TestHarness | `harness` | CP210x | `D4:E9:F4:B4:27:3C` | Also pairs as a second virtual Sigil on its soft-AP MAC `D4:E9:F4:B4:27:3D`. Never flash Sigil or Atlas firmware onto it. |

*Verified* 2026-09-25 by reading each MAC with `read_mac` and matching each
board's boot banner (`SIGIL|DISPLAY|OLED`, `SIGIL|DISPLAY|READY|122x250`,
`HARNESS|BOOT`).
