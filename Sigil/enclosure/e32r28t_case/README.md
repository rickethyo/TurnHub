# Sigil stand-in case: E32R28T phone style

*Experimental, not yet printed.* A portrait, rounded case for the 2.8-inch
LCDWIKI E32R28T board (the old Gen 1 Atlas board, now standing in for a
Sigil), in the same style as the
[Atlas S3 case](../../../Atlas/enclosure/s3_phone_case/README.md). It measures
**63.8 × 91.4 mm**: 16.4 mm thick when flat, or a 5° wedge from 16.4 mm at the
bottom to 24.4 mm at the top, which tips the screen toward the player.
Source: [`sigil_e32r28t_case.scad`](sigil_e32r28t_case.scad).

![Tilted case, front](preview_front_tilt5.png)

| Part | File | Print |
|---|---|---|
| Tub, flat | [`sigil_e32r28t_tub_flat.stl`](sigil_e32r28t_tub_flat.stl) | Back face down, no supports |
| Tub, 5° wedge | [`sigil_e32r28t_tub_tilt5.stl`](sigil_e32r28t_tub_tilt5.stl) | Already rotated so the wedge lies flat |
| Bezel (fits both) | [`sigil_e32r28t_bezel.stl`](sigil_e32r28t_bezel.stl) | Front face down (already flipped) |
| Light bar (fits both) | [`sigil_e32r28t_light_bar.stl`](sigil_e32r28t_light_bar.stl) | Clear PETG or PLA, on its side (already rotated), 100% infill |

Print one tub and the bezel. Hardware: four **M3 × 10** screws from the back,
through the standoffs and the board's holes, self-tapping into the bezel
(pilot 2.5 mm; `screw_pilot = 4.0` for heat-set inserts).

![Tub with the light bar](preview_light_bar.png)

## Layout

USB-C is at the bottom and the screen is portrait. If the picture runs the
wrong way round, change the display rotation in firmware, not the case.

- **Display:** a 46 × 60.6 mm window over the touch area. The lip sits 0.4 mm
  above the resistive touch film so it can never press a phantom touch.
- **Status light:** the board's only RGB LED faces the back, so a clear light
  bar carries it to a glowing slot under the USB-C port. The bar lies on the
  tub floor between two low rails; a post rises to 1 mm below the LED and a
  45° face under it turns the light toward the slot. The back cavity is 2 mm
  deeper than the board needs to make room for it. The LED position (about
  mid-width, 41.5 mm from the USB end) comes from a photo
  (*Needs verification*); move `led_c` if the post misses it.
- **microSD:** a slot on the left side with a fingertip recess and a guide
  shelf; the card sits about 6 mm in.
- **RESET / BOOT:** paper-clip holes in the back near the bottom.
- **Edge connectors (speaker, SPI, UART, battery):** a 4.5 mm gutter down each
  side leaves room for the plugs.
- **Angle:** set `tilt` (degrees) and re-export the tub; the bezel stays the same.

## Measurements

From the [E32R28T outline drawing][size] (V1.0, 2024-08-31): PCB 50 × 86 ×
1.6 mm, R3.5 corners, Ø3.2 holes 4 mm in from the edges (42 × 78 apart),
4.0 mm of display and touch above the PCB, back parts up to 5.09 mm. Not yet
checked with calipers on the board.

```
openscad -D 'part="tub"'             -o sigil_e32r28t_tub_flat.stl  sigil_e32r28t_case.scad
openscad -D 'part="tub"' -D tilt=5   -o sigil_e32r28t_tub_tilt5.stl sigil_e32r28t_case.scad
openscad -D 'part="bezel"'           -o sigil_e32r28t_bezel.stl     sigil_e32r28t_case.scad
openscad -D 'part="pipe"'            -o sigil_e32r28t_light_bar.stl sigil_e32r28t_case.scad
```

[size]: https://www.lcdwiki.com/res/E32R28T/E32R28T_Size.pdf
