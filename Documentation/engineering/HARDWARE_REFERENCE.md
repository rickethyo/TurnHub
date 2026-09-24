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
- RESET (EN) and BOOT (IO0) buttons.

PlatformIO (`Atlas/platformio.ini`): Espressif32, board `esp32dev`, Arduino,
115200 baud, partition table `min_spiffs.csv` (two 1.9 MB OTA app slots, NVS at
the default `0x9000`). Flashing a new partition table needs one USB upload; OTA
cannot change it. Display library: LovyanGFX.

### Atlas I/O (E32R28T)

| Function | GPIO | Notes |
|---|---:|---|
| Master button | 0 | On-board BOOT button, active low. Firmware only samples it after boot. |
| TFT SCK / MOSI / MISO | 14 / 13 / 12 | HSPI, display only |
| TFT CS / DC | 15 / 2 | Reset is the shared EN line |
| TFT backlight | 21 | Active high (PWM) |
| Touch SCK / MOSI / MISO | 25 / 32 / 39 | XPT2046, separate bus. Not used yet |
| Touch CS / IRQ | 33 / 36 | Active low. Not used yet |
| microSD SCK / MOSI / MISO / CS | 18 / 23 / 19 / 5 | VSPI, shared with the SPI header. Not used yet |
| SPI header CS | 27 | Header pins: IO23, IO19, IO18, IO27 |
| RGB LED red / green / blue | 22 / 16 / 17 | Common anode, active low. Held off at boot |
| Speaker amp enable | 4 | Active low. Held disabled at boot |
| Speaker audio (DAC) | 26 | Not used yet |
| Battery voltage ADC | 34 | Input only. Not used yet |

Master button (BOOT): holding it proves physical presence for system settings
and OTA (Lobby/Game Over only). Releasing it passes for the active player.
Holding it for 5 seconds (`MASTER_END_MATCH_HOLD_MS`) during a running or paused
match ends the match as a draw. The fast-blink hold warning used the old status
LED and has no output on this board yet. The Pair button is gone, so Atlas
cannot open a pairing window until a replacement control exists (see
`STAGED_CHANGES.md`). *Needs verification* on hardware.

Current firmware shows a TurnHub splash on the TFT and creates the
`TurnHub-Atlas` access point.

**Needs verification on hardware:** panel orientation (`setRotation(1)`),
color inversion and RGB/BGR order; 40 MHz TFT write clock; the pin table
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

**Confidence:** Verified from `Sigil/src/main.cpp` and `Sigil/include/sigil_display.h` unless otherwise noted

### Main I/O

| Function | GPIO | Notes |
|---|---:|---|
| Blue LED | 27 | Current development wiring |
| Green LED | 14 | Current development wiring |
| Red LED | 13 | Current development wiring |
| Pass button | 26 | Current development wiring |
| Action button | 25 | Current development wiring |
| Pause / Win button | 32 | Breadboard J13; closes to GND, INPUT_PULLUP; tap for Action-long, hold 5 seconds for win |
| Buzzer | 33 | Current development wiring |
| Pair button | 19 | Verified working firmware and rear-photo socket A12; closes to A13/GND, INPUT_PULLUP |

### E-ink interface

| Function | GPIO | Notes |
|---|---:|---|
| EPD CS | 17 | Verified in display class |
| EPD DC | 16 | Verified in display class |
| EPD RST | 22 | Verified in display class |
| EPD BUSY | 21 | Verified in display class |
| EPD SCLK | 18 | Explicit SPI.begin configuration; socket A11 |
| EPD MOSI | 23 | Explicit SPI.begin configuration; socket A18 |

GPIO19 is explicitly detached from SPI MISO for the Pair button; the display is write-only. See the [Rev A electrical draft and unresolved parts/mechanics](../../KiCad/PCB/Sigilv1/README.md) and [38-position socket / firmware cross-check tables](../../KiCad/PCB/Sigilv1/CROSS_CHECK.md). Rev A sockets the complete removable DevKit, not a bare ESP32-WROOM module. The authoritative socket photograph is a BACK view: J1 is top-left, A1 top-right; A12 is GPIO19 and A13 is GND.

The current display driver is `GxEPD2_213_B74`, a 2.13-inch-class monochrome e-ink target in the present implementation.

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

### Auxiliary control revision

**Status:** Pause / Win implemented in firmware on GPIO32; physical operation awaits a bench check

The current controls extend the original two-button layout:

- **Pass** - primary turn-pass input.
- **Action** - contextual action with existing short/long behavior retained as needed.
- **Pause / Win auxiliary button** - tap emits the prior Action-long semantic; a 5-second hold emits Action-long then Action-win once, without pausing at 2 seconds. Release after a win hold emits no additional action.
- **Pair button** - now implemented and verified on GPIO19; no longer a planned GPIO assignment.

The Pause / Win auxiliary control uses GPIO32 / J13 on the breadboard. The Rev A schematic now carries it as net BTN_PAUSE with switch SW5 to GND (2026-09-24, checked against firmware by `verify_schematic.py`; not a hardware check). The older GPIO4 auxiliary and GPIO32 display-detect labels are gone. Remaining ergonomics should be finalized after physical playtesting.

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
