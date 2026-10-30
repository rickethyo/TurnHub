# Atlas S3 phone-style case

*Experimental; second revision after the first test print.* A flat, rounded case for the 4-inch LCDWIKI
ES3C40P Atlas board in portrait, about **74.8 × 117.6 × 19.4 mm**.
Source: [`atlas_s3_phone_case.scad`](atlas_s3_phone_case.scad) (OpenSCAD 2021.01 or newer).

![Closed case, front](preview_front.png)

| Part | File | Print |
|---|---|---|
| Tub: back, walls, standoffs | [`atlas_s3_tub.stl`](atlas_s3_tub.stl) | Back face down, no supports |
| Bezel: front frame and window | [`atlas_s3_bezel.stl`](atlas_s3_bezel.stl) | Front face down, no supports (already flipped) |

![Tub with the speaker cradle](preview_tub.png)

Hardware: four **M3 × 10** screws from the back. They pass through the tub
standoffs and the board's own holes, then self-tap into the bezel bosses
(pilot 2.5 mm). The heads sit in deep counterbores in the standoffs. For M3 × 5.7 heat-set inserts, set `screw_pilot = 4.0`.

## Layout

The case lies flat with USB-C facing away from the player, so the cable
leaves from the back of the table and the microphone end is nearest the
player. The case itself is unchanged; the firmware draws the UI rotated
180° for it (`LCD_ROTATION = 2` in `Atlas/include/config.h` since
2026-10-29; touch follows it). The vendor drawing places the mic hole and
the back-side mic part at the same corner. If the UI draws upside down in
the case, change `LCD_ROTATION`, not the case. Feature positions below are given
as if looking at the board right way up, with USB-C at the bottom.

- **USB-C:** a 13 × 7 mm slot at the bottom, centred on the receptacle.
- **microSD:** a slot on the left side (looking at the screen) with a
  fingertip recess. A shelf guides the card across the side gutter. The card
  sits about 6 mm in, so push it home with a fingernail or a coin edge.
- **Microphone:** a 1.6 × 2.6 mm port in the bezel, above the window.
- **BOOT / RESET:** 2.2 mm paper-clip holes in the back near the bottom
  corners. Uploads over USB don't need them.
- **Edge connectors (battery, speaker, UART, I²C, expansion):** they all face
  the board's long edges, so the tub has a 4.5 mm gutter down each side for
  the plugs. Wires turn under the board into the back cavity.
- **Speaker:** the board kit's oval speaker (40.1 × 28.3 × 9.7 mm, measured)
  lies face down and lengthwise in a cradle on the back plate, firing through
  six slots. It sits under the "4.0\" LCD Display" silkscreen, the one clear
  patch on the board's back: 25–65 mm from the USB end, 26–54 mm in from the
  edge with the SPEAKER header. Both cradle ends are notched on the header
  side, so turn the speaker whichever way its lead reaches. The cavity behind
  the board is 10.3 mm deep for it. Stick small rubber feet on the back
  corners so the slots aren't muffled on a table.
- **Battery:** no room yet; it would need more depth again.

## First print (2026-10-28)

The board fit, but two things needed fixing. The window started 1.5 mm too
high and showed the board above the glass, so it now starts at 9.8 mm, which
matches the vendor drawing's glass position. The speaker had been across the
case, where it hit the touch flex and the SPEAKER header and its lead pointed
away from the socket, so it now runs lengthwise nearer the middle.

## Measurements and fit risks

Board numbers come from the [ES3C40P Specification V1.0][spec], figure 5.1
(PCB 61.04 × 112.21 × 1.6 mm, R3.5 corners, Ø3.2 holes at 53.04 × 104.21 mm,
4 mm from the edges, 11.45 mm overall). Ricky's calipers agree:
112.2 × 61.0 mm, 104.0 / 53.1 mm across the holes and 10.1 mm thick.

The first print settled the glass position: it follows the drawing (94.57 mm
long, starting 9.0 mm from the mic end), not the earlier photo estimate. The
window is 58 × 92.7 mm, the pocket under the lip takes the whole board, and
the screw bosses stop 5.6 mm from each end (`boss_clip`).

Every opening and clearance is a named parameter at the top of the `.scad` file.
To regenerate the STLs:

```
openscad -D 'part="tub"'   -o atlas_s3_tub.stl   atlas_s3_phone_case.scad
openscad -D 'part="bezel"' -o atlas_s3_bezel.stl atlas_s3_phone_case.scad
```

[spec]: https://www.lcdwiki.com/res/ES3C40P/ES3C40P_Specification_V1.0.pdf
