# TurnHub Hardware Reference

This is a working hardware reference, not yet a production schematic. Pin assignments marked verified come from surviving source files on the `atlas-esp32-port` branch. Planned controls and future production hardware are intentionally separated from implemented prototype wiring.

## Hardware naming

- **Atlas** - central authoritative table controller.
- **Sigil** - player-facing physical controller/display.
- **Virtual Sigil** - browser-based controller that represents the same logical controller role without physical hardware.

---

## Generation 0 standalone prototype

**Confidence:** Reconstructed

Known functional elements:

- Arduino-class controller.
- Pass button.
- Action/pause button.
- Red, green, and blue visual status.
- Piezo/buzzer feedback during later iterations.
- Adjustable turn-warning timing, initially through a potentiometer and later through fixed/DIP concepts.

The exact original pinout is not yet considered verified.

---

## Generation 1 wired Arduino module

**Confidence:** Verified from `Arduino/TurnHubArduino.ino`

Module identity in surviving firmware: `MODULE_ID = 1`

| Function | GPIO/pin | Notes |
|---|---:|---|
| Blue LED | 5 | PWM brightness supported |
| Red LED | 6 | Digital output |
| Green LED | 3 | Digital output |
| Pass button | 8 | Short physical button, `INPUT_PULLUP` |
| Action button | 7 | Tall physical button, `INPUT_PULLUP` |

Other verified details:

- Serial baud: 9600.
- Debounce: 30 ms.
- Long press: 2 seconds.
- Win hold: 5 seconds.
- Pass is emitted after a complete press/release cycle.
- Raspberry Pi owns game state.

This board should be treated as historical hardware, not as the target Sigil design.

---

## Generation 1 mixed wired prototype

**Confidence:** Partially verified / reconstructed

The bench system used a Raspberry Pi 3B as the central host with multiple wired microcontroller modules. Development notes identify:

- Module 0: ESP32, white wiring.
- Module 1: Arduino, red wiring.

The exact ESP32 wired-module pinout should be recovered from the surviving `ESP32/` firmware and/or original breadboard before being declared final in this document.

---

## Generation 2 development Atlas

**Confidence:** Pin map *Reconstructed* from the vendor pin allocation table
(LCDwiki "2.8inch ESP32-32E Display", E32R28T) and the board silkscreen; owner
photo 2026-09-24. Nothing below is hardware-verified yet.

### Controller board

Atlas now targets the LCDwiki **2.8" ESP32-32E display module, E32R28T**
(resistive touch; the E32N28T is the same board without touch). It replaces the
earlier bare `esp32dev` prototype and its external Pair button, master button,
status LED and Pair LED, none of which are carried over.

- Module: ESP32-32E N4, 4 MB QSPI flash, no PSRAM.
- USB-C through a CH340C USB-serial bridge (not CP210x like the Sigil boards).
- 2.8" 240x320 ILI9341V TFT, XPT2046 resistive touch controller.
- microSD slot, common-anode RGB LED, speaker amplifier with connector,
  battery connector with charging circuit and a battery-voltage ADC.
- RESET (EN) and BOOT (IO0) buttons. Both are for flashing and resets only;
  the firmware reads neither. The touchscreen is Atlas's only physical input.

PlatformIO (`Atlas/platformio.ini`): Espressif32, board `esp32dev`, Arduino,
115200 baud, partition table `min_spiffs.csv` (two 1.9 MB OTA app slots, NVS at
the default `0x9000`). Flashing a new partition table needs one USB upload; OTA
cannot change it. Display library: LovyanGFX.

### Atlas I/O (E32R28T)

| Function | GPIO | Notes |
|---|---:|---|
| TFT SCK / MOSI / MISO | 14 / 13 / 12 | HSPI, display only |
| TFT CS / DC | 15 / 2 | Reset is the shared EN line |
| TFT backlight | 21 | Active high (PWM) |
| Touch SCK / MOSI / MISO | 25 / 32 / 39 | XPT2046, read by bit-banged SPI (HSPI is the TFT's, VSPI is kept for microSD) |
| Touch CS / IRQ | 33 / 36 | Active low |
| microSD SCK / MOSI / MISO / CS | 18 / 23 / 19 / 5 | VSPI, shared with the SPI header. Mounted at boot at 4 MHz (`SD_SPI_HZ`), never formatted; FAT32 cards only. Optional storage, see [Identity and storage](IDENTITY_AND_STORAGE.md#optional-microsd-storage). *Verified* on the board by the owner (2026-09-24): the card mounts and registers |
| SPI header CS | 27 | Header pins: IO23, IO19, IO18, IO27 |
| RGB LED red / green / blue | 22 / 16 / 17 | Common anode, active low. Held off at boot |
| Speaker amp enable | 4 | Active low. On only while a tone plays |
| Speaker audio | 26 | LEDC channel 4 square wave (`atlas_speaker.cpp`); see [Atlas speaker](#atlas-speaker) |
| Battery voltage ADC | 34 | Input only. Not used yet |

**No master button (owner decision, 2026-09-24).** The firmware no longer
reads BOOT (IO0) or any other button. Everything the master button did now
happens on the touchscreen:

- **Physical presence:** hold **Unlock admin** for 3 seconds
  (`ADMIN_UNLOCK_HOLD_MS`) in the Lobby or at Game Over. That opens a 60-second
  admin unlock window (`ADMIN_UNLOCK_WINDOW_MS`), shown as a countdown on the
  screen and on the portal's Developer status. While it is open, an account
  with the right permission can make itself the first Admin, save network
  settings, rename devices, start an OTA update (Lobby/Game Over only), or
  **Return table to lobby** (Admin; ends a match in progress as a draw, then
  empties the table).
  Tapping **Admin unlocked: tap to lock** closes it early. The window only
  proves someone is at the table; the account permission checks still apply.
- **Pass** for the active player: the touchscreen's Pass button.
- **End a match as a draw:** hold **End match** for 5 seconds (`END_MATCH_HOLD_MS`).

*Needs verification* on hardware.

### Atlas speaker

The on-board amplifier and speaker play the **table-wide** cues, so players
without a Sigil (a browser or phone) hear them too: the start countdown and its
cancellation, game start, each turn change, the turn-timer warning and expiry,
pause and resume, eliminations, win claim/confirm/deny/cancel, and game over.
Per-Sigil feedback (joining, shared seats, starter selection, nudges, decisions
waiting on one Sigil) stays on that Sigil's buzzer. Every cue also shows on the
Atlas screen, Sigils and portal, so sound is never the only signal
([Accessibility](ACCESSIBILITY.md)).

`AudioController` sends a cue's notes to the Sigils in its target mask; the
speaker is the mask's bit 15 (`ATLAS_SPEAKER_MASK`). `atlas_speaker.cpp`
(firmware-only) plays each note as an LEDC square wave on IO26 and keeps
the amplifier enabled (IO4 low) only while a note plays, so an idle speaker does
not hiss. Volume is an Admin setting in the portal (System), saved as the
`spkvol` NVS blob: Off, Low, Medium (default) or High. The duty cycle sets
loudness: about 1/2, 3/4 and full amplitude (the fundamental scales with
sin(pi x duty); 50% is loudest). Until 2026-09-25 the speaker used the DAC's
sine generator at 1/8, 1/4 and full scale, which was too quiet on the default
Medium; a square wave is also much louder on a small speaker. Off silences only the Atlas speaker; each Sigil still
follows its seated players' sound preference.

*Needs verification* on hardware: loudness at each level, the square wave's
tone quality, and that the amplifier stays quiet between notes.

### Atlas touchscreen

The TFT is landscape, rotation 3 (`TFT_ROTATION`), so the connector pigtails on
the board's left edge leave from the top of the screen. The firmware shows the
TurnHub splash for 2 seconds and then a status screen: table state, a detail
line (pairing countdown, player and Sigil counts, whose turn it is, time left,
a pending pass, or the result), a line for action messages, and touch buttons.
The display (`atlas_display.cpp`) only draws. `touch_controls.cpp` builds the
screen and holds the touch adapter, which dispatches Intents with
`IntentOrigin::AtlasHardware`. The one exception is Unlock admin, which only
opens the physical-presence window in `front_panel.cpp` and changes no table
state:

| State | Buttons | Intent |
|---|---|---|
| Lobby | Pair a Sigil | `PairRequest` (replaces the Pair button) |
| Lobby, Game Over | Hold to unlock admin (3 s), then Admin unlocked: tap to lock | Opens/closes the admin unlock window; no Intent |
| Running | Pass, Pause | `Pass` / `Pause` for the active seat |
| Paused | Resume | `Resume` for the active seat |
| Running, Paused | Hold to end match (draw) | `EndMatch` after `END_MATCH_HOLD_MS` (5 s), with an on-screen countdown |

Taps act on release inside the same button, and sliding off cancels. A press
that started on a button stays on it within `TOUCH_SLOP_PX` (12 px) of its
edge, so jitter and a rolling fingertip don't cancel it; a press has to start
inside a button to pick it. A contact
gap shorter than `TOUCH_RELEASE_MS` (60 ms) counts as the same press, because
resistive panels drop out briefly. Touches during the splash are ignored.
Buttons are at least 60 px tall. A pressed button inverts and gets a heavier
border, so the press does not rely on color alone. The screen gives no player
names yet, only player numbers.

**Touch calibration.** Resistive panels vary from unit to unit. On the first
E32R28T, the borrowed defaults registered touches about one button-height
below the drawn button. So Atlas calibrates on the device:

- If no calibration is saved, a 4-point calibration runs after the splash,
  before any button is offered. Press and release each cross; the targets are
  inset 24 px from the corners.
- To recalibrate, hold anywhere on the screen for 10 seconds while the table is
  in the lobby (`touchCalibrationAllowed()`). The hold never triggers the
  button under it.
- `touch_calibration.h` (host-tested) works out whether the axes are swapped
  or inverted, plus each axis's raw range extended to the screen edges. It
  refuses presses that don't span the panel or whose axes don't separate,
  then asks again.
- The result is saved in NVS (`atlas-touch/cal2`). The `config.h` values
  (`TOUCH_RAW_*`, `TOUCH_SWAP_XY`, `TOUCH_INVERT_*`) are only the fallback when
  nothing valid is saved or calibration times out (30 s without a touch).
- **Touch read fix (2026-09-25, *Needs verification* on hardware).** The
  bit-banged XPT2046 read sampled DOUT just after the falling clock edge,
  which is when the chip changes that line. Each bit could come from either
  side of the race, so positions were scrambled (a doubled, wrapped value
  whenever the new bit won). That is the likeliest cause of the touches that
  didn't line up with the buttons, and of calibrations solved from them. DOUT
  is now sampled while the clock is high. Each axis also throws away its
  first conversion after the plates switch, then takes the median of three.
  Pressure is checked before and after the position reads, so samples taken
  while a finger lands or lifts are dropped. The calibration key moved to
  `cal2`, so each Atlas recalibrates once after this update.
- Serial logs `ATLAS|TOUCH|CALIBRATION|LOADED/DEFAULT/SAVED|...` with the
  values, and each new press logs `ATLAS|TOUCH|RAW|x|y|SCREEN|x|y`.

**Needs verification on hardware:** panel orientation (rotation 3 puts the
pigtail at the top), the on-device calibration and the accuracy it gives, color inversion and
RGB/BGR order; 40 MHz TFT write clock; the pin table
above, especially the small 3-pin header (silkscreen appears to read IO35/IO22/GND).
Also confirm that GPIO0 reads high when BOOT is released and that the RGB LED
really is common-anode.

### Future Atlas hardware items to define

- Final ESP32-family module/SoC (the E32R28T is a development board).
- Status indicator(s) (on-board RGB LED and TFT available).
- Pairing/management control strategy (touchscreen Pair control staged).
- Battery/backup-power strategy (board has a battery connector and charger).
- Venue-display interface if required.
- Secure device identity hardware if retained for production authenticity.
- Docking/power contacts for Sigils if pursued.

---

## Generation 2 development Sigil

**Confidence:** Verified from `Sigil/src/main.cpp` and `Sigil/include/epaper_display.h` unless otherwise noted

### Main I/O

| Function | GPIO | Notes |
|---|---:|---|
| Status LED blue / green / red (Wokwi only) | 27 / 14 / 13 | One RGB LED or three LEDs; PWM on all three. The hardware Sigils use the Jewel ring instead |
| Menu keys, E-ink (`sigil`) | 34 VRX, 35 VRY, 32 SW | Analog joystick: directions are Up/Down/Left/Right, click is Select. Stick powered from 3.3 V |
| Menu keys, OLED (`sigil-oled`) | 25 Up, 27 Down, 19 Left, 21 Right, 32 Select | Five-button d-pad, each a switch to GND with INPUT_PULLUP. *Planned*: not yet wired |
| Status ring, both hardware Sigils | 26 | NeoPixel Jewel 7 RGBW Data Input via 330 ohm; PWR from USB 5V (J1). The only status light (no separate LED since 2026-09-25) |
| Pass / Action / Pause-Win buttons (Wokwi only) | 26 / 25 / 32 | The three-button gesture layout; closes to GND, INPUT_PULLUP |
| Buzzer | 33 | Current development wiring |
| Pair button | 0 (DevKit BOOT) | Since 2026-09-25 the DevKit's onboard BOOT button is Pair on both hardware builds; no carrier wiring. GPIO0 is a strap only at reset (holding BOOT through a reset enters the ROM downloader). *Needs verification* on hardware. The Wokwi build keeps its Pair pushbutton on GPIO19 (A12). |

### E-ink interface

| Function | GPIO | Notes |
|---|---:|---|
| EPD CS | 17 | Verified in display class |
| EPD DC | 16 | Verified in display class |
| EPD RST | 22 | Verified in display class |
| EPD BUSY | 21 | Verified in display class |
| EPD SCLK | 18 | Explicit SPI.begin configuration; socket A11 |
| EPD MOSI | 23 | Explicit SPI.begin configuration; socket A18 |

GPIO19 is explicitly detached from SPI MISO; it was the Pair button until 2026-09-25 (still Pair in Wokwi) and the display is write-only. See the [Rev A electrical draft and unresolved parts/mechanics](../../KiCad/PCB/Sigilv1/README.md) and [38-position socket / firmware cross-check tables](../../KiCad/PCB/Sigilv1/CROSS_CHECK.md). Rev A sockets the complete removable DevKit, not a bare ESP32-WROOM module. The authoritative socket photograph is a BACK view: J1 (5V) is top-left, A1 (CLK) top-right; A12 is GPIO19 and A13 is GND. Rev A is drawn twice, `Sigil_EInk` and `Sigil_OLED`, which differ only in the display interface. Since 2026-09-24 neither schematic carries the buttons or LEDs, which are being redesigned; the breadboard wiring in this table is the current firmware's.

The current display driver is `GxEPD2_213_B74`, a 2.13-inch-class monochrome e-ink target in the present implementation.

### Experimental OLED display variant

A separate `sigil-oled` build selects the OLED renderer while `sigil` and
`sigil-wokwi` keep e-paper. The owner's 2026-09-24 photos show an Inland
1.3-inch OLED V2.0 board with IIC/SPI markings. It runs over 4-wire SPI on the
e-paper's GPIOs (CLK 18, MOSI 23, RES 22, DC 16, CS 17; GPIO21 unused) at 3.3 V,
as selected in `Sigil/include/oled_config.h`; the owner verified that wiring and
a working image on 2026-09-24. The SH1106 128x64 controller is inferred from the
KS0056 vendor example and still **Needs verification**. The OLED header order and
wire colours are in the `Sigil_OLED` schematic. See
[display selection, sources, configuration and verification](../../Sigil/DISPLAY.md).

**Limitation (owner decision, 2026-09-24): the OLED Sigil is single-player only.**
Seat one player on it; shared seating (Seat A and Seat B on one Sigil) is not
supported on the OLED variant. Use an e-paper Sigil for a shared seat.
Atlas enforces it (2026-09-24): the OLED build reports
`CAPABILITY_DISPLAY_OLED` (0x10) in its Hello, and Atlas then refuses Seat B on
that Sigil (the Action + PASS chord, or any Seat B join) with "This Sigil has an
OLED display and seats one player; use an e-paper Sigil to share a seat". It also
refuses to start a game while an OLED Sigil still has a Seat B joined before it
reported its display (for example, one reflashed while seated). The portal's
device list shows each Sigil's display: "OLED: 1 player" or "E-paper: up to 2
players". Sigils without the bit (e-paper, and older firmware) keep shared
seating, so an OLED Sigil must run Sigil firmware 0.5.6 or later. Host-tested;
*Needs verification* on hardware.

### Current control timing

- Debounce: 30 ms.
- Action long press: 2 seconds by default; 1-4 seconds per player (Sigil firmware 0.5.4+).
- Action win hold: 5 seconds by default; 3-10 seconds per player, always at least
  1 second longer than the long press. The Pause / Win button uses the same win hold.
  Atlas sends the seated players' choice (`InputTiming`, see
  [Protocol and Pairing](PROTOCOL_AND_PAIRING.md)); the Sigil keeps it in RAM only.
  *Needs verification* on hardware; host tests only cover Atlas's side.
- Pass acknowledgement green flash: 250 ms.
- Pair: a press opens the 15-second pairing window; holding it for 10 seconds
  erases the Sigil's saved pairing (Sigil 0.5.5+). *Needs verification* on hardware.

### Menu controls (2026-09-25)

**Status:** Implemented in firmware and host-tested; *Needs verification* on hardware

Both hardware Sigils have five keys (Up, Down, Left, Right, Select) and show
Atlas's action menu instead of the button gestures below. Atlas sends which
actions each Sigil may use now (`MenuState`), the Sigil sends the one chosen
(`SelectAction`), and Atlas dispatches the same Intents the gestures used
(see [Protocol and Pairing](PROTOCOL_AND_PAIRING.md)).

- **E-ink (joystick): compass.** Every action has a fixed key, listed at the
  bottom of the screen, so the panel redraws only when the menu changes. Click
  is the likely action (Pass on your turn, Join, Start, Confirm win, Rematch);
  Up pauses or resumes; Down holds for Claim win or Reset table; Left is no,
  back or cancel; Right is yes or next. Link phone takes the first free key.
- **OLED (d-pad): list.** Any key opens the list at the likely action; Up and
  Down move, Select or Right choose, Left closes, and ten idle seconds close it.
- **Deliberate actions** (Claim win: the win hold; Confirm out, Reset table:
  the long press) are sent only once the key is held for the seated players'
  thresholds. The E-ink ring fills in white while held; the OLED row says HOLD.
- Until Atlas sends a menu (an older Atlas), the keys fall back to the gestures:
  Select is PASS, Right is Action, Down is Pause / Win.

### Auxiliary control revision

**Status:** Historical on the hardware Sigils (superseded by the menu controls above); still the Wokwi layout

The current controls extend the original two-button layout:

- **Pass** - primary turn-pass input.
- **Action** - contextual action with existing short/long behavior retained as needed.
- **Pause / Win auxiliary button** - tap emits the prior Action-long semantic; a 5-second hold emits Action-long then Action-win once, without pausing at 2 seconds. Release after a win hold emits no additional action.
- **Pair button** - implemented and verified on GPIO19; moved to the DevKit's onboard BOOT button (GPIO0) on 2026-09-25.

The Pause / Win auxiliary control uses GPIO32 / J13 on the breadboard. The Rev A schematics no longer draw it (buttons and LEDs were removed on 2026-09-24 pending the controls redesign). The older GPIO4 auxiliary and GPIO32 display-detect labels are gone. Remaining ergonomics should be finalized after physical playtesting.

### Display orientation direction

**Status:** Portrait firmware implemented; physical mounting direction awaits a bench check.

The `GxEPD2_213_B74` driver targets GDEM0213B74 / SSD1680 with a **122 x 250**
visible portrait area (128 controller RAM columns). Sigil now uses library
rotation **0**, replacing rotation 1's 250 x 122 landscape layout. If the rewired
panel is upside down, rotation **2** is the opposite portrait orientation.

The screen stacks player identity, life, shared-player information, received
Commander damage and turn status. Other lifecycle screens stack shared seats
vertically and retain host/starter/attention/winner indicators. Full refreshes
are currently enabled for all screens: the partial-update bench trial caused
progressive contrast loss. The owner identifies an unmarked Inland module from
Micro Center; the panel revision remains unconfirmed. The FPC-A002 marking and
the similar Keyestudio module's GDEM0213B74 example give conflicting identification
leads; verify the panel and rear switch settings before another partial trial.
The configured driver still targets GDEM0213B74. The pin mapping
above is unchanged. See
[Sigil portrait layout and verification](../../Sigil/DISPLAY.md).

Exact physical panel dimensions, supply requirements and sleep current still
need a hardware verification pass. At-a-glance statistics remain future display
content; the current protocol does not supply them.

---

## Hardware abstraction rule

Game behavior should not depend directly on GPIO numbers. Firmware should expose semantic controls and devices such as:

- `BTN_PASS`
- `BTN_ACTION`
- `BTN_PAIR`
- `BTN_AUX`
- `STATUS_LED_RED`
- `STATUS_LED_GREEN`
- `STATUS_LED_BLUE`
- `DISPLAY`
- `BUZZER`
- `BATTERY_STATUS`

A future PCB revision should be able to move a signal to a different pin without changing game-engine behavior.

---

## Proposed hardware revision naming

Until formal PCB revisions exist, use development labels cautiously:

- `PROTO-G0` - earliest standalone timer prototype.
- `PROTO-G1-PI` - Raspberry Pi + wired modules.
- `PROTO-G2-ESP` - ESP32 Atlas/Sigil migration hardware.
- `ATLAS-REV-A` - reserve for the first intentionally documented Atlas electrical design.
- `SIGIL-REV-A` - reserve for the first intentionally documented Sigil electrical design.

Do not assign `REV-A` merely because a breadboard happens to work. The revision should correspond to a reproducible wiring/schematic/BOM package.

Last reconstructed: 2026-09-19
