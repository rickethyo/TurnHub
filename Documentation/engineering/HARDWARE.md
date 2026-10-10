# Hardware: Boards, Pins, Screens and Flashing

The current Generation 2 prototype hardware and the tools that flash it.
Earlier generations are in [Generation History](history/GENERATION_HISTORY.md).
The Sigil carrier PCB is in `KiCad/PCB/Sigilv1/`; Sigil screen layouts and
menus are in `Sigil/DISPLAY.md`.

Pin maps are in daily use and the owner reports them verified through
playtesting and bench testing (2026-10-02).

## Hardware development direction (owner, 2026-10-10)

Current ESP32 DevKit e-paper and OLED Sigil feature development is frozen.
Their accepted firmware, radio contract and 16-pixel preset assets remain the
baseline. New screen work targets planned ESP32-S3 LCD hardware with 2.8-inch
and 4-inch displays. Board/panel selection, GPIOs, orientation, exact display
resolution, PSRAM, power budget and the new screen layouts are still to be
confirmed; neither S3 firmware target exists yet.

Player artwork begins independently of those ports: vector defaults, 64-pixel
Atlas alpha masks, and square JPEG masters up to 512 pixels/48 KiB stored on
Atlas's SD card. Android previews provisional 64/128-pixel icon slots for the
2.8/4-inch layouts. Inch size does not determine pixel resolution. Today's
Atlas renders a small approved RGB332 thumbnail in its existing 16-pixel slot;
future layouts consume the master rather than enlarging that thumbnail.

## Atlas: LCDwiki E32R28T

2.8" ESP32-32E display module (resistive touch): ESP32 N4, 4 MB flash, no
PSRAM, **CH340C** USB bridge (Sigils use CP210x), ILI9341V 240x320 TFT,
XPT2046 touch, microSD slot, common-anode RGB LED, speaker amplifier, battery
connector and charger (see "Atlas battery" below), RESET and BOOT buttons.

PlatformIO `Atlas/platformio.ini`: `esp32dev`, Arduino, 115200 baud,
`min_spiffs.csv` (two 1.9 MB OTA app slots, NVS at `0x9000`; a new partition
table needs one USB upload), LovyanGFX.

| Function | GPIO | Notes |
|---|---:|---|
| TFT SCK / MOSI / MISO | 14 / 13 / 12 | HSPI, display only |
| TFT CS / DC | 15 / 2 | Reset is the shared EN line |
| TFT backlight | 21 | Active high, PWM |
| Touch SCK / MOSI / MISO | 25 / 32 / 39 | XPT2046, bit-banged (HSPI is the TFT's, VSPI the card's) |
| Touch CS / IRQ | 33 / 36 | Active low; IRQ also wakes Atlas from sleep |
| microSD SCK / MOSI / MISO / CS | 18 / 23 / 19 / 5 | VSPI at 4 MHz, FAT32, never formatted by Atlas |
| RGB LED red / green / blue | 22 / 16 / 17 | Active low. Red blinks 250/250 ms while pairing; blue blinks while an update is available |
| Speaker amp enable | 4 | Active low, on only while a chime sounds (+250 ms) |
| Speaker audio | 26 | DAC 2 streamed by I2S0 (`atlas_speaker.cpp`) |
| Battery ADC | 34 | Cell voltage through the board's divider (halved); `atlas_battery.cpp` |
| BOOT | 0 | Pair / unpair all / factory reset (quick, 3 s, 10 s); never a game action |

There is no master button (2026-09-24): game controls are on the touchscreen.

### Atlas battery

*Experimental* (2026-10-08). A single-cell LiPo on the board's battery
connector (the first one tried: a 502030 pouch, 250 mAh, with its own
protection board) is charged by the on-board charger from USB. Atlas has no
fuel-gauge chip, so the charge is estimated from the cell voltage alone:
`atlas_battery.cpp` reads GPIO 34 four times a second (8 ADC reads averaged, the
ESP32's calibrated millivolts) and scales it by the divider ratio in
`config.h` (`BATTERY_DIVIDER_NUMERATOR` / `DENOMINATOR`, 2/1). The pure logic
in `battery_gauge.h` smooths their per-second average (an exponential average over about 16 s),
maps it through a LiPo discharge curve (flat through the middle, steep at both
ends, not linear), holds the shown percent until it moves 2 points, and
raises a low warning at 15 % that clears at 20 %. Below 2.5 V there is no cell.

Shown in: the touchscreen header (at the right, a phone-style cell icon and
the percent, the Sigil count having moved to the header's middle; the icon
fills red and a red **LOW BATTERY** pill appears while low; a lightning bolt
over it while charging, gone once full at 100%) and Menu > Info ("SD card: ready, battery 82%",
", charging" added when it fits), the portal's Atlas line under Devices, the
basic portal, and `GET /api/devices` (`atlas.battery`: `percent`,
`millivolts`, `low`, `charging`, or `null` with no cell). Each 5-point
change, and each start or end of charging, is logged as
`ATLAS|BATTERY|<mV>|<percent>` (`|LOW`, `|CHARGING`) for checking the curve
against a timed discharge.

**Charging** (2026-10-08). The charger is a TP4054 (the schematic's U6; the
fitted part is marked LTH7, the LTC4054 equivalent): 4.2 V, stopping on its
own as the current tapers, at about 300 mA set by R27 (3.3 kOhm on PROG,
1000 V / R). An SL2305 MOSFET runs the board from USB while it is plugged in,
so all of that current goes into the cell. 300 mA is 1.2 C for the 250 mAh
cell, above the 0.5-1 C most small pouches are rated to charge at; R27 at
6.8 kOhm (about 150 mA) or 10 kOhm (about 100 mA), or a cell of 500 mAh or
more, would be gentler. `BATTERY_CAPACITY_MAH` and `BATTERY_CHARGE_MA` in
`battery_gauge.h` must follow either change.

Plugged in, the reading is the charger's, not the cell's: the cell plus the
charge current through its resistance (about 210 mV on the bench), then a
flat 4.21 V at the charger's limit, and a flat 4.10 V once it has stopped
(measured 2026-10-08). Read through the curve that is "full" at once (it
jumped 60 % to 100 %). So while charging the percent is counted instead: it
starts from the last on-battery percent, never falls, and climbs one point
per 30 s (300 mA into 250 mAh), stopping at 90 % until the reading has held
steady at 4.08 V or more for 4 minutes (the charger's limit; climbing to it,
the reading rises 30 mV or more in that time). Then it climbs one point a
minute and shows 100 % only after 20 minutes there. Started on USB, with no
on-battery percent, it takes the cell as 210 mV below the reading, and as at
least 85 % once at the limit. Unplugged, it reads the curve again. Checked on
the bench (unplug, replug: the percent held and crept up with the bolt
showing); the counted rate is *Needs verification* against a timed charge.

**Screen off on battery** (2026-10-08). The cell is a backup that keeps the
game, the radio and the portal running through a pulled cable, not a power
source for play (it lasts minutes under Atlas's load), so on battery the
backlight, Atlas's biggest load, is off; it comes back on with USB. A touch
lights it for 15 s after the last touch (that first press presses nothing).
Nothing essential is lost while it is dark: the Sigils and the portal carry
all game state (`ACCESSIBILITY.md`).

The board has no USB-sense pin, so `PowerSourceTracker` (`battery_gauge.h`)
reads the power source from the raw samples. Measured on the 502030 with a
temporary probe (2026-10-08, four samples a second, two pulls and replugs):
pulling USB dropped the reading 220-240 mV within one sample (4.10 V to
3.83-3.87 V), plugging it back raised it 200-220 mV, sample noise stayed
within about 30 mV, and on the cell alone it fell about 100 mV in 45 s. So a
sample 100 mV below the highest of the last second is USB pulled, and 100 mV
above the lowest is USB back. Atlas starts as if on USB (screen on); if it
was started on the cell, a steady 60 mV fall within a minute (which the
charger never allows) switches it to battery, in about 20 s on the measured
trace, and a steady 60 mV rise switches it back. With no cell it is always
USB. Changes are logged as `ATLAS|POWER|BATTERY` / `ATLAS|POWER|USB`, and the
charge estimate restarts from the new level.

**Auto sleep on battery** (2026-10-08, owner: keep the cell from running
flat). On the cell, Atlas goes into the menu's deep sleep by itself
(`serviceAutoSleep`, `table_intents.cpp`; a System-origin `Sleep` Intent):
- between games, after `BATTERY_IDLE_SLEEP_MS` (5 min) with no player action:
  any Intent from a Sigil, phone or the touchscreen, or a touch;
- at any time, once the shown charge has stayed at or below
  `BATTERY_EMPTY_PERCENT` (3%, about 3.58 V under load) for 30 s. Mid-game
  the interrupted-match record brings the game back paused. A touch or BOOT
  doesn't wake an empty Atlas; only USB does.

On USB it never sleeps by itself, and an update, Sigil update or factory reset
under way holds it off. Asleep on the cell, the RTC timer wakes the chip 5 s
after it sleeps (the resting-cell reading) and then each minute
(`stayAsleepWithoutUsb`, `atlas_display.cpp`, before anything else in
`setup()`): a reading 80 mV over the last one, or 4.15 V and up, is the
charger, so Atlas starts; otherwise it is back asleep within a few
milliseconds. Deep sleep still leaves the board's regulator, CH340 and
power LED drawing a few mA, so a sleeping Atlas still drains the cell, only
far slower; the cell's protection board is the last stop.

*Needs verification:*
- Auto sleep's USB check: that plugging in raises the resting cell by more
  than `USB_STEP_MV` (80 mV), at any charge, and that noise never does.
- Sleep current on the cell, and how long a sleeping Atlas lasts.
- The divider ratio: compare the portal's voltage with a meter at the plug
  and correct `config.h` if they differ.
- The curve under Atlas's load (TFT backlight, Wi-Fi AP: roughly 1 C on a
  250 mAh cell), which sags the voltage so the estimate reads low.
- With USB in and no cell the charger output can look like a full battery.
  The board has no charge-status or USB-sense pin, so Atlas can't tell these
  apart from voltage alone.
- When the cell is too low for the 3.3 V regulator; the curve's 0 % (3.45 V)
  may need raising so Atlas warns before it browns out.

### Device theme choices

Each device owns its appearance independently. Atlas uses Menu > Device >
Theme to select a named theme; each Sigil uses Menu > Device > Theme to cycle
Graphite, Daylight, Brass and High contrast. The current Sigil theme is shown
in the OLED row and e-paper Device subtitle. Its menu stays open after a theme
tap. Local device menus also work while unpaired or Atlas is unreachable.
The joystick hold for pairing applies only outside the open device menu.

Choices are saved in the device's `display` NVS namespace (`theme`), preserved
by unpairing and cleared by factory reset. Missing/invalid choices use Graphite
on Atlas/OLED and Daylight on e-paper; failed writes retain the old theme.
Rendering consumes the new choice on the existing display task, and e-paper
invalidates its cache and partial-refresh frame so the first redraw is full.
No theme packets or new game state are introduced.

OLED Daylight uses dark ink on a light ground. Graphite, Brass and High contrast
use light ink on dark. E-paper keeps black ink on white in all themes: the
modern palettes converge on their one-bit typography and simple cards, Brass
keeps the serif/ornament family, and High contrast adds a second banner frame.
Monochrome panels cannot reproduce the color palettes or translucent surfaces.
Inter digits use equal advances; measured fallbacks preserve long names and
large/signed life totals. All status meanings remain in words and shapes.
Physical-panel readability, OLED current and e-paper ghosting need device
acceptance; host previews demonstrate source rendering only.

### Atlas touchscreen

Landscape, rotation 3. A 2 s splash, then the status screen in the selected
Graphite (default), Daylight, Brass or High contrast theme (`atlas_art.cpp`).
`design/tokens.json` generates RGB888 roles in `shared/include/device_theme.h`;
the TFT displays them at RGB565. Modern themes use Inter bitmaps from
`tools/fonts/make_modern_fonts.py`; Brass keeps Cinzel and Oswald from
`tools/fonts/make_fonts.py`. Modern themes use flat surfaces, an outlined turn
clock and teal turn emphasis; Brass retains its plates, gears and gauge.

- **Header:** state (LOBBY, STARTING, PLAYING, PAUSED, GAME OVER or the open
  screen), Sigils online, a red **NO SD CARD** pill, round and match time,
  and "Update available" when it applies.
- **Hero:** whose turn it is, a detail line (pairing countdown, pending pass,
  whom a claim waits on), and a turn clock: time left with a gauge and
  countdown tube, or elapsed time with the timer off.
- **Chips:** one per player (up to 8): name, life, the player's total turn
  time, and a tag in words (TURN, OUT, WINNER, STARTS, CONFIRM).

| State | Buttons |
|---|---|
| Lobby | Start (two or more players), Clear (hold), Menu. Tap a chip for the Player screen: Earlier/Later, Remove (hold 2 s) and the B side |
| Menu (between games) | Pair a Sigil (lobby), QR codes, Info, Device, Back |
| Device (between games) | Theme, Unpair Sigils (hold 3 s), Factory reset (hold 10 s), Sleep |
| Theme | Graphite, Daylight, Brass, High contrast, Back; the selected theme is framed |
| Starting | Cancel start |
| Running / Paused | Pause or Resume, Table. Tap a chip for the Player screen (-5/-1/+1/+5, Concede) |
| Table screen | Master pass (hold 2 s, running), End match (hold 5 s), Back |
| Game over | Rematch, Reset, Menu |
| Presence code / pairing code | The six-digit code and QR with Cancel; or Pair Sigil N with the code, Codes match, Reject |
| Setup | Welcome or "You're all set" ([First-run Setup](FIRST_RUN_SETUP.md)) |

Buttons are at least 60 px tall; holds say "hold", count down in words and
fill a bar. Taps act on release inside the button; a press may drift 12 px
(`TOUCH_SLOP_PX`) and contact gaps under 60 ms count as one press. Every
button dispatches an Intent with `IntentOrigin::AtlasHardware`
(`touch_controls.cpp`, host-tested); screens like Info and QR codes change no
state. The display redraws only regions whose part of the screen model
changed; `bash Atlas/tests/host/render-atlas-screens.sh` renders it to PNGs
and checks that incremental redraws match full ones.

**Calibration.** With nothing valid saved, a 4-point calibration runs after
the splash. Hold anywhere for 10 s in the lobby to recalibrate. Saved in
`atlas-touch/cal2`; `config.h` values are only the fallback. The XPT2046 is
sampled while the clock is high, with the first conversion dropped and a
median of three (the 2026-09-25 fix for misplaced touches).

**Speaker.** A small chime synthesizer on core 0 (22.05 kHz, up to four
voices, pentatonic, soft clip). Volume Off/Low/Medium/High (`spkvol`,
Admin). Loudness and tone on the real speaker are *Needs verification*.

## Sigils: ESP32 DevKit

Both Sigil types use a 38-pin ESP32-WROOM-32E DevKit (**CP210x**; the CP2102
reports serial `0001`, so connect two at a time if Windows misses one), the
same thumbstick, the NeoPixel Jewel 7 ring and a passive buzzer. Only the
display differs. Flash: `esp32dev` default partitions (two 1.25 MB app
slots).

| Function | GPIO | Notes |
|---|---:|---|
| Thumbstick X / Y / click | 34 / 35 / 32 | Directions are Up/Down/Left/Right, click is Select. Click (and BOOT) wake from sleep |
| Jewel 7 RGBW ring data | 26 | Via 330 Ω; powered from USB 5 V. The only status light |
| Buzzer | 33 | Passive piezo |
| Pair | 0 | DevKit BOOT button: quick press pairs, 3 s unpairs, 10 s factory resets |
| Display strap | 4 | Open = e-ink, wired to GND = OLED. A build for the wrong display halts at boot. On a bare DevKit, jumper `IO4` to `GND` for OLED (`A7` on the carrier) |
| Display SPI SCLK / MOSI / CS / DC / RST | 18 / 23 / 17 / 16 / 22 | Shared by both displays |
| E-ink BUSY | 21 | E-ink only; GPIO19 is detached from SPI MISO |
| Front LED strip (reserved) | 13 | J6 on the Rev A carrier, unused ([Planned Designs](PLANNED_DESIGNS.md)) |

- **E-ink Sigil** (`sigil`): 2.13" GDEM0213B74 (SSD1680) via
  `GxEPD2_213_B74`, portrait 122x250, rotation 0. Partial refresh with
  periodic full clean-ups; a trial with a driver-loaded waveform is on the
  bench (`Sigil/DISPLAY.md`).
- **OLED Sigil** (`sigil-oled`): Inland 1.3" 128x64 SH1106 over 4-wire SPI at
  3.3 V.
- **Wokwi** (`sigil-wokwi`) keeps an RGB LED on 27/14/13 and a Pair
  pushbutton on GPIO19; see `Sigil/WOKWI.md`.
- **Spare** (`sigil-spare`): inert image for either board
  ([Firmware Updates](FIRMWARE_UPDATES.md)).

**Controls** (one rule set since Sigil 0.9.10; details in `Sigil/DISPLAY.md`,
"Menus"): the click is the obvious next step (Join, Start, Pass, Undo pass,
Resume, Rematch, Confirm); Left/Right change life in a game (seat B and next
starter in the lobby, answers in a decision); Down switches seat on a shared
Sigil; Up always opens **Menu** with every other action, then Device (Sleep,
Unpair, Factory reset, Theme). The OLED shows Menu as a scrolling list, the e-ink as
compass pages. Holds use the seated players' times: long press 2 s default
(1-4 s), win hold 5 s (3-10 s, at least 1 s longer); the ring fills while
held. An unpaired Sigil also opens pairing with a 3 s click hold.

## Identifying and flashing boards

Never hard-code COM numbers; they change whenever the PC restarts. Boards are
identified by factory MAC:

```
pio pkg exec -p tool-esptoolpy -- esptool.py --port COMx read_mac
```

(Reading the MAC resets the board.) Each PC keeps its own list in the
git-ignored `tools\boards.local.md`; never put real boards in a committed
file. `tools\setup-boards.cmd` writes it: it reads each attached board's MAC,
suggests a type (CH340 = Atlas; a Sigil running TurnHub firmware reports its
strap as `SIGIL|HW|EINK` or `SIGIL|HW|OLED`), blinks the board in question
white for 10 s (the serial `identify` command), and lets you rename, retype,
make spare, return to service or delete (erases an attached board's flash).

The list's format, with made-up MACs (`02:` is locally administered, so no
real board has one):

| Board | Firmware | USB bridge | MAC | Notes |
|---|---|---|---|---|
| Atlas | `atlas` | CH340 | `02:00:00:00:00:01` | E32R28T; its MAC is its `THA-` ID |
| E-ink Sigil | `sigil` | CP210x | `02:00:00:00:00:02` | GPIO4 open |
| OLED Sigil | `sigil-oled` | CP210x | `02:00:00:00:00:03` | GPIO4 to GND |

`atlas`, `sigil` and `sigil-oled` are flashed; `spare` or
`spare:<firmware>` gets the spare image; anything else (the retired
`harness`) is listed and never flashed. `tools\flash-all.cmd` and the
signing tools are described in `CLAUDE.md`.

## Rules and open items

- **Hardware stops at the hardware boundary.** GPIO numbers, polarity and
  driver quirks never reach game rules; hardware code turns physical input
  into semantic events. A PCB revision must be able to move a pin without
  touching the engine.
- **Revision names:** `PROTO-G2-ESP` for today's boards. Reserve
  `ATLAS-REV-A` / `SIGIL-REV-A` for the first reproducible schematic + BOM,
  not a breadboard that happens to work.
- **Open:** the Sigil carrier PCB layout, DRC and a test fit (the OLED PCB is
  a placeholder); power measurements (idle, radio, display, buzzer, capped
  LEDs, sleep) and USB supply behavior; Atlas battery runtime and calibration
  (above), final Atlas module, enclosure
  and docking decisions. Tracked in [Staged Changes](STAGED_CHANGES.md).

### Display inversion and header marks (2026-10-09)

Atlas 0.7.5 and Sigil 0.9.17 add a separate saved `display/inverted` bool.
On Atlas use Menu > Device > Theme > Invert: on/off; on either Sigil use
Device > Invert. Changing theme retains inversion, unpairing retains it and
factory reset clears it. Saves must succeed before the visible setting changes.
Atlas complements the RGB palette; Sigils swap foreground/background. Atlas
QR codes retain conventional black-on-white modules and quiet zones for scanning.
LED colors/preferences are independent. Each theme has a header emblem on all
three displays: diamond, sun, gear or boxed hourglass. Normal/inverted host
previews and firmware checks do not establish physical panel acceptance.
