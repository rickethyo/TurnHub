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

**Confidence:** Verified from current migration source where noted

### Controller

PlatformIO environment currently targets:

- Platform: Espressif32.
- Board definition: `esp32dev`.
- Framework: Arduino.
- Serial monitor: 115200 baud.

The physical development Atlas board model is known to differ from the Sigil development board. The exact commercial board model and board-specific header mapping should be added after a physical verification pass.

### Current Atlas I/O

| Function | GPIO | Confidence |
|---|---:|---|
| Master button | 32 | Verified |

Current firmware also creates a local access point named `TurnHub-Atlas`.

### Future Atlas hardware items to define

- Final ESP32-family module/SoC.
- USB-C power connector.
- USB-C data/firmware connector if kept as a separate port.
- Status indicator(s).
- Pairing/management control strategy.
- Power regulation.
- Optional battery/backup-power strategy.
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
| Buzzer | 33 | Current development wiring |

### E-ink interface

| Function | GPIO | Notes |
|---|---:|---|
| EPD CS | 17 | Verified in display class |
| EPD DC | 16 | Verified in display class |
| EPD RST | 22 | Verified in display class |
| EPD BUSY | 21 | Verified in display class |

The current display driver is `GxEPD2_213_B74`, a 2.13-inch-class monochrome e-ink target in the present implementation.

### Current control timing

- Debounce: 30 ms.
- Action long press: 2 seconds.
- Action win hold: 5 seconds.
- Pass acknowledgement green flash: 250 ms.

### Planned control revision

**Status:** Planned, not yet represented by the verified pin table above

The current product direction adds dedicated controls beyond the original two-button layout:

- **Pass** - primary turn-pass input.
- **Action** - contextual action with existing short/long behavior retained as needed.
- **Action/Win auxiliary button** - short press can represent the prior Action-long semantic while a long hold can initiate a victory claim, reducing the need to pause automatically before a win claim.
- **Pair button** - deliberate first-time pairing/re-pairing control.

Exact names, ergonomics, GPIO assignments, debounce rules, and hold times should be finalized after physical playtesting.

### Display orientation direction

**Status:** Planned

The current hardware/software originated with a landscape-oriented display concept. Product direction is now portrait orientation so the Sigil can remain narrow while showing player identity, turn state, life/game information, and at-a-glance statistics vertically.

The eventual display reference should document:

- Exact panel model.
- Active resolution.
- Physical dimensions.
- Rotation used in firmware.
- Full vs partial refresh behavior.
- SPI pin mapping including shared/default clock/data pins.
- Power requirements and sleep current.

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
