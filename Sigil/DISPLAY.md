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

- Startup reads the existing saved Atlas binding before drawing the first
  screen. A valid binding shows **Booting**; without a valid saved binding,
  the screen shows **Unpaired** and the pairing instruction. Peer registration
  still happens after ESP-NOW initializes, followed by the existing Ready/state
  flow. Radio startup failure does not turn a loaded binding into Unpaired.
- Ready screens retain their ready text.
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

The display implementation/header, startup pairing-read order and display
documentation changed. The saved binding format and validation are unchanged;
the read now occurs before the first screen instead of inside radio startup.
Atlas, ESP-NOW packets, capabilities, GPIO assignments, button semantics and
debounce/hold timing are unchanged. Atlas remains the sole game
authority. Paged drawing and the existing display worker/change detection are
preserved. All screens currently use full refreshes following the failed
partial-refresh bench trial below. No animation or refresh timer is added.
The write-only SPI initialization still explicitly detaches GPIO19 MISO for Pair.

The display-ready diagnostic now reports the actual geometry and rotation:

```text
SIGIL|DISPLAY|READY|122x250|ROTATION|0
SIGIL|DISPLAY|POLICY|FULL_ONLY
```

### Partial-refresh bench result (disabled by default)

The supplied bench video shows progressive contrast loss during partial
updates, including static titles and life digits. Full refresh is restored as
the default; shortening the cleanup interval alone would not establish that
the waveform is appropriate. The partial implementation remains available for
a controlled follow-up after panel identification. This is a mitigation, not
a claim that the underlying partial-refresh problem is solved.

The visible flex marking is `FPC-A002`. The
[Zephyr Waveshare panel table](https://docs.zephyrproject.org/latest/boards/shields/waveshare_epaper/doc/index.html)
associates that marking with **GDEY0213B74**, whereas the configured
`GxEPD2_213_B74` targets **GDEM0213B74**. This is evidence to check the exact
panel model/revision, not proof of the cause. The installed library has a
separate `GxEPD2_213_GDEY0213B74` driver. No driver or voltage/waveform registers
have been changed based only on the marking.

The owner identifies the module as an unmarked Inland screen from Micro Center.
The [Inland 2.13-inch listing](https://www.microcenter.com/product/632694/inland-213-inch-e-ink-lcd-display-screen)
does not establish the installed panel revision. A possible hardware reference is
the [Keyestudio KS0461 documentation](https://wiki.keyestudio.com/KS0461_Keyestudio_Electronic_Ink_Screen_Module_2.13_Inch),
but equivalence to this Inland unit remains unconfirmed. For a matching board,
Keyestudio specifies rear switch P1 at `3`, warns against `0.47`, and requires P2
to match the supplied voltage (`3.3VIN` or `5VIN`). The owner confirmed P1 is
already at `3`, so an incorrect P1 position does not explain this trial's result.
P2 and actual supply voltage have not been confirmed. Check the actual board
labels and positions before changing anything; disconnect power before moving
switches.

Keyestudio's [official resource archive](https://github.com/keyestudio/KS0461-Keyestudio-Electronic-Ink-Screen-Module-2.13-Inch/blob/master/docs/Resource.7z)
contains `GDEM0213B74_Arduino.ino`, supporting the existing driver family as a
candidate rather than proving that GDEY is required. Its partial example resets
the controller, sets border control to `0x80`, and uses update control `0xFF`;
the installed GxEPD2 B74 partial path uses `0xFC`. The example recommends a full
cleanup after five partial updates. These differences are investigation leads,
not a validated replacement sequence for this unmarked panel. No vendor code
or controller-register changes were incorporated into the firmware.

Inspection of the installed GxEPD2 paged path confirms it writes both image
buffers after refresh; Sigil calls `powerOff()` only after that completes.
The video alone cannot distinguish panel-waveform mismatch, electrical effects
or other panel-specific behavior. Host rendering tests cannot validate any of
those optical/electrical effects.

The installed `GxEPD2_213_B74` driver advertises both partial and fast partial
updates. Its timing constants are 3,600 ms for full refresh and 500 ms for partial
refresh. These are driver values, not measured end-to-end Sigil latencies. See
the [upstream driver](https://github.com/ZinggJM/GxEPD2/blob/master/src/epd/GxEPD2_213_B74.h).

Refresh selection is local to `SigilDisplay`; no protocol or Atlas change is
needed. The existing worker coalesces arriving snapshots and avoids drawing
identical game snapshots. If the experimental switch is re-enabled, its policy is:

1. Booting, Unpaired, Ready and all state-only screens use full refreshes. The
   first running-game snapshot also uses a full refresh to establish a baseline.
   Returning from pause or another screen, changing Sigil ID, switching between
   single/shared layouts or changing Commander mode forces a fresh full baseline.
   Reinitializing the display clears the remembered baseline.
2. Subsequent running-game snapshots with the same layout use
   `setPartialWindow(0, 0, display_.width(), display_.height())` with the same
   `firstPage()` / `nextPage()` drawing loop. This uses the partial waveform over
   the whole display, retaining one atomic snapshot and clearing old text even
   when shared-seat focus, name length or digit count changes.
3. After ten partial updates, the **next changed snapshot** uses a full refresh,
   resets the counter and starts another cycle. The ten-update limit is an
   initial trial setting, not a panel specification or a hardware-validated
   ghosting limit. Idle screens do not refresh to clear the counter.
4. After each game frame finishes, `powerOff()` disables the panel's generated
   driving voltages. The partial library path otherwise leaves them enabled.
   This happens after the image buffers have been synchronized, without a
   reset or hibernate that would discard the differential baseline.

`ENABLE_GAME_PARTIAL_REFRESH` in `include/sigil_display.h` is currently `false`,
so every frame uses full refresh. Boot reports `POLICY|FULL_ONLY` to identify
the fallback build. Setting the switch to `true` reports `POLICY|PARTIAL_TRIAL`.
`MAX_PARTIAL_REFRESHES` sets the cleanup cadence. A driver without fast partial
support also falls back to full updates.

Each running-game refresh emits its chosen waveform, elapsed milliseconds
(drawing, refresh and power-off) and partial count, for example:

```text
SIGIL|DISPLAY|REFRESH|PARTIAL|MS|<measured>|PARTIALS|1
SIGIL|DISPLAY|REFRESH|FULL|MS|<measured>|PARTIALS|0
```

With the trial enabled, the sequence after a full baseline is PARTIAL counts 1
through 10, then FULL count 0. With the current default, all game refreshes log
FULL count 0. This lets the bench distinguish expected cleanup flashes
from a screen/layout transition or an unexpectedly slow partial update.

The library handles the byte-aligned horizontal window (122 visible columns
round up to 128 RAM columns) and writes the image to both controller buffers for
fast differential refresh. Its implementation and examples are in
[GxEPD2_BW.h](https://github.com/ZinggJM/GxEPD2/blob/master/src/GxEPD2_BW.h) and
[showPartialUpdate](https://github.com/ZinggJM/GxEPD2/blob/master/examples/GxEPD2_Example/GxEPD2_Example.ino).

Smaller changed regions can be evaluated later, but updating only the life digits
would miss coupled turn/name/Commander changes. A whole-screen partial waveform
is the simpler first trial; smaller regions should not be assumed to shorten
the panel's waveform time proportionally.

Bench acceptance should measure BUSY/refresh latency, inspect alternating turn
banners, 40-to-9 digit shrinkage, negative life and shared A/B focus changes, and
check accumulated ghosting through the periodic full refresh. Also check cold
power-up and button/radio responsiveness during refresh. Actual speed, ghosting
and electrical behavior still need verification on the physical Sigil.

## Verification and physical review

From `Sigil`, build with:

```text
pio run -e sigil -e sigil-wokwi
```

Local validation for this change:

- Production `sigil` build and simulation `sigil-wokwi` build pass.
- A host regression of the current full-only default passes across 24 changing
  game snapshots, shared focus/turn/life changes and pause/resume: no partial
  refresh calls, with power-off after completed game drawing.
- Earlier host checks of the enabled trial passed for the full/ten-partial/full
  sequence, screen/layout/ID/reinitialization baselines, whole-snapshot windows,
  power-off after drawing, and fallback when fast partial support is absent.
  These use a driver adapter and do not simulate e-ink waveforms or ghosting.
- The added Booting screen was separately rendered and visually checked with
  the host framebuffer adapter; no text overlap or out-of-bounds drawing.
  Saved/unsaved pairing boot sequences still require the physical checks below.
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

1. Boot without a saved binding: confirm Unpaired and Pair. Reboot a paired
   Sigil: confirm Booting appears first, without an Unpaired flash, then the
   existing Ready/Atlas state. Repeat with Atlas powered off; the saved binding
   must not be presented as unpaired. Check orientation and all four edges.
2. Check a named single player and shared A/B players through lobby/start,
   active/waiting turns, pause/attention and winner/game-complete states.
3. Check life values and Commander rows, including C2-only and both commanders.
4. Confirm Pass/Action still work during refresh and that GPIO19 Pair is usable.

No firmware upload or physical-panel verification was performed for this change.
