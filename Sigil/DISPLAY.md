# Sigil portrait display

The physical Sigil now renders in **122 x 250 portrait**, using
`DISPLAY_ROTATION = 0` in `include/sigil_display.h`. The previous layout used
rotation 1 (250 x 122 landscape). GxEPD2 handles rotation; drawing coordinates
remain ordinary top-left portrait coordinates.

The installed GxEPD2 1.6.9 driver is `GxEPD2_213_B74` (GDEM0213B74 / SSD1680).
Its visible width is 122 pixels; the 128-column controller buffer includes six
non-visible columns. Layout uses `display_.width()`, not the RAM width. See the
[upstream panel definition](https://github.com/ZinggJM/GxEPD2/blob/master/src/epd/GxEPD2_213_B74.h).

Rotation 0 assumes the panel's native top edge is at the top of the new mounting.
That physical direction has not been verified on the rewired Sigil. If the whole
screen is upside down, change only `DISPLAY_ROTATION` to **2**, the opposite
portrait orientation; coordinates and layout do not need to change.

## Layout

All text uses the existing Adafruit GFX built-in monochrome font (6 x 8 pixel
cells). Automatic wrapping is disabled. Names retain all 12 protocol characters
at size 2 across up to two centered lines; wrapping prefers a word boundary,
with a character split for longer unbroken names. Six-pixel side margins leave
110 pixels for content. Life numbers reduce font size to fit, preserving the
sign and every digit through the supported -1,000,000 to 1,000,000 range.

### Running game snapshot

| Vertical area | Contents |
| --- | --- |
| y=4-36 | Game/Commander title, Sigil number, HOST flag, turn number, divider |
| y=40-59 | YOUR TURN banner, or WAITING FOR TURN, directly above the primary player |
| y=66-99 | Primary player's full transmitted name, up to two lines |
| y=104-163 | Large life total and LIFE label (shared view uses a shorter primary block) |
| y=152-211 shared / y=188-247 single, Commander only | CMD TAKEN heading, omitted-source count, up to three source names and received damage values |
| y=216-248 shared Commander / y=149-181 shared other games | Divider, other seat letter/name, other life total |

The primary life total uses size 6 for ordinary single-player values and size 4
for ordinary shared values; larger numbers scale down. Shared secondary names
use size 1 and their life totals use up to size 2. The primary LIFE label names
its seat, and the secondary name is prefixed with the other seat letter.

Atlas chooses the primary/focused participant. A shared running view can put
Seat B first when Atlas focuses it; names, life, seat labels and received
Commander damage follow that supplied snapshot. In shared Commander games,
damage stays directly below the primary player's life; the secondary player's
name and life sit beneath the damage section, separated by a divider.
Commander rows use two size-1
lines per source so all 12 name characters and both damage values fit. A lone
second-commander value keeps `(C2)`; paired values remain `C1/C2`. `+N` in the
CMD TAKEN heading means additional sources were omitted by Atlas. With no damage,
the screen says "No commander damage received" across two lines.

YOUR TURN uses white size-2 text on black in addition to its explicit wording.
It sits directly above the primary player's name, so it stays associated with
the active participant supplied by Atlas even when that participant is Seat B.
Waiting status occupies the same area, keeping the rest of the layout stable.
The display remains understandable without the LED colors or audio.

### Other supported states

- Unpaired and ready screens retain their pairing instruction and ready text.
- Lobby, starting, legacy running, paused and game-over states retain their
  state title, identity, Sigil/host/turn metadata and relevant starter, active,
  attention or winner indication.
- Shared state-only screens stack Seat A above Seat B in the existing seat
  order. A black seat-label strip preserves the existing focus emphasis;
  GO FIRST, YOUR TURN, ACTION NEEDED and WINNER also name the relevant seat.
- Longer status messages such as ACTION NEEDED and GAME COMPLETE use two lines.

Life and Commander damage remain available only in the existing running-game
snapshot. Paused and game-over packets do not carry those values; the renderer
does not invent or retain them as current state. No unsupported clocks, battery
indicators, statistics or new pairing states are added.

## Boundaries and refresh

Only the display implementation/header and display documentation changed.
Atlas, ESP-NOW packets, capabilities, pairing/storage, GPIO assignments, button
semantics and debounce/hold timing are unchanged. Atlas remains the sole game
authority. Full-window paged refresh and the existing display worker/change
detection are preserved; no partial-refresh or animation behavior is introduced.
The write-only SPI initialization still explicitly detaches GPIO19 MISO for Pair.

The display-ready diagnostic now reports the actual geometry and rotation:

```text
SIGIL|DISPLAY|READY|122x250|ROTATION|0
```

## Verification and physical review

From `Sigil`, build with:

```text
pio run -e sigil -e sigil-wokwi
```

Local validation for this change:

- Production `sigil` build and simulation `sigil-wokwi` build pass.
- A temporary host rendering harness compiled the actual `sigil_display.cpp`
  against a framebuffer adapter and the installed library's classic font.
  2,595 rendered cases had no text-cell overlaps or drawing outside 122 x 250.
  Cases include every state/flag combination, both shared focus orders, empty
  and maximum-length names, positive/negative life limits, maximum damage,
  C2-only damage, omitted sources and turn 255.
- Fifteen representative framebuffer previews were visually reviewed, including
  shared Commander with no damage and with all three source rows. These
  checks validate layout, not SPI, panel rotation wiring, e-ink refresh or radio.
- Generated review files live under ignored `.pio/portrait-review/` locally;
  they are not firmware sources or committed test infrastructure.

Before accepting the mounting on hardware:

1. Boot and check upright orientation, all four edges, Unpaired/Ready and Pair.
2. Check a named single player and shared A/B players through lobby/start,
   active/waiting turns, pause/attention and winner/game-complete states.
3. Check life values and Commander rows, including C2-only and both commanders.
4. Confirm Pass/Action still work during refresh and that GPIO19 Pair is usable.

No firmware upload or physical-panel verification was performed for this change.
