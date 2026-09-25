# Sigil Rev A electrical drafts

Rev A is drawn as two KiCad projects in this folder. They share the symbol library (`Sigil.kicad_sym`) and differ only in the display interface:

| Project | Display | Display nets |
|---|---|---|
| `Sigil_EInk.kicad_pro` | Inland e-paper driver board (GxEPD2_213_B74), J2 `EPD_Module_Header` (8 pins) | EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY |
| `Sigil_OLED.kicad_pro` | Inland 1.3" OLED V2.0, J2 `OLED_Module_Header` (7 pins) | OLED_SCLK, OLED_MOSI, OLED_CS, OLED_DC, OLED_RST |

Both displays use the same sockets and GPIOs (A8/GPIO16 DC, A9/GPIO17 CS, A11/GPIO18 clock, A17/GPIO22 reset, A18/GPIO23 data); only the e-ink uses A14/GPIO21 BUSY. Both schematics also carry the buzzer interface J3 on J12/GPIO33, +3V3 from J19, and GND on A13, A19 and J6.

**Buttons and LEDs are not on either schematic** (removed 2026-09-24) while the controls are redesigned. Their GPIOs are marked NC.

**Joystick (E-ink only).** `Sigil_EInk` carries J4, an unmarked 5-pin analog thumbstick that replaces the Pass/Action/Pause buttons: GND, "+5V" (fed **3.3 V**, never 5 V, since VRX/VRY swing to the supply and the ESP32 ADC is 3.3 V max), VRX to J15/GPIO34, VRY to J14/GPIO35 and SW to J13/GPIO32. VRX/VRY are ADC1 input-only pins because ADC2 is unusable under ESP-NOW. Firmware: the Sigil `sigil` PlatformIO environment (the E-ink build) (click = PASS, right = Action, down = Pause/Win). *Verified* on the owner's breadboard (2026-09-25): both axes read reversed as mounted, so the firmware inverts X and Y.

**Status ring (E-ink only).** `Sigil_EInk` carries J5, an Adafruit NeoPixel Jewel 7 (RGBW): PWR from the DevKit's USB 5V (J1), GND, and DIN from J10/GPIO26 through R1 (330 ohm, placed at the ring); Data Output is unconnected. 3.3 V data into 5 V pixels usually works; add a 74AHCT125 level shifter if it glitches. A 100-1000 uF capacitor across PWR/GND is recommended but not fitted on the breadboard. Full RGBW draws about 560 mA, more than USB supplies, so the firmware caps brightness at 48/255. The ring draws the Sigil's status light from Atlas's LedState (player number as lit pixels, a shared seat as its ring half, the top overlay in the center); the same information is always on a screen as text. *Planned*: wired, not yet confirmed lit on hardware.

The breadboard wiring the current firmware uses is in the [hardware reference](../../../Documentation/engineering/HARDWARE_REFERENCE.md).

U1 is the removable, complete 38-pin ESP32 DevKit carrier interface. Its custom symbol and local symbol library use **A1–A19 and J1–J19 as pin numbers**, not ESP32 module pad numbers. No DevKit footprint is assigned. The PCB files are empty placeholders.

## Authority and orientation

- GPIO functions: `Sigil/include/epaper_display.h` and the explicit SPI configuration in `Sigil/src/epaper_display.cpp` (e-ink), `Sigil/include/oled_config.h` (OLED), and `BUZZER_PIN` in `Sigil/src/main.cpp`.
- Socket identity: user-supplied [SigilBackMarked.png](reference/SigilBackMarked.png), photographed from the **BACK**, and the user's transcribed sequence. The schematics deliberately show J1 (5V) top-left and A1 (CLK) top-right in that rear-reference view.
- [The cross-check tables](CROSS_CHECK.md) are verified from both exported netlists and the firmware by `tools/verify_schematic.py`.

The rear-reference arrangement is **not** a carrier component-side footprint drawing. Future footprint work must translate the views explicitly and verify insertion against the physical part. Preserve each socket ID; do not blindly copy or mirror the schematic into a footprint.

This draft assumes power through the DevKit's own USB connector; J1/5V has no carrier connection. No additional regulator, USB-UART, BOOT, EN/reset, or other DevKit support circuitry is reproduced.

## Unresolved electrical details

- **Display J2 (both):** pin numbers follow the module's own header, pin 1 at the top, with the silkscreen labels as pin names. Order and the jumper wire colour on each wire come from the owner's photos of the breadboard wiring (2026-09-24); the colours are annotations, not a harness specification. Tables are in [CROSS_CHECK.md](CROSS_CHECK.md). J2 has no footprint yet.
- **E-ink J2:** SDI, SCLK, CS, D/C, RES, BUSY, VCC, GND. The Inland driver board has two slide switches, P1 (3 / 0.47) and P2 (5VIN / 3.3VIN), whose positions are not recorded; with the carrier's 3.3 V supply, check P2. Supply current and the panel itself need confirmation. The display has no carrier MISO connection.
- **OLED J2:** GND, VCC, CLK, MOSI, RES, DC, CS. The owner verified the SPI wiring, 3.3 V supply and a working image on 2026-09-24; the SH1106 controller and 128x64 geometry are inferred from the vendor example (see `Sigil/DISPLAY.md`).
- **Buzzer J3:** a logical SIG/GND interface only. Firmware proves GPIO33 tone output, but not whether the physical load is a passive piezo, magnetic transducer, or driven module. Confirm part, wiring, voltage/current, driver, bias and protection before implementing the load. No direct GPIO-drive rating is assumed.
- J2/J3 are excluded from PCB and BOM and have no footprints; J3 is marked logical-only on the sheet. Both need verified physical connectors/circuits before PCB work.
- Buttons, LEDs and their passives must be redrawn once the controls are decided. Confirm the DevKit regulator can supply the total carrier/display load.

## Mechanical release hold

No reliable dimensional drawing or measured dimensions were found in the repository. The photograph is sufficient for sequence and orientation, not mechanics. Measure or obtain a reliable drawing for:

- Actual pin pitch (likely 2.54 mm, not yet verified), row center spacing and pin locations.
- Exact board width/length, USB-C overhang and cable insertion envelope.
- Mounting-hole positions/diameters, if used.
- Female socket dimensions/height, insertion depth, underside and component keepouts.
- BOOT and EN/reset access plus antenna clearance requirements for the exact installed DevKit.

Use two 1x19 female socket rows so the complete DevKit remains removable. The eventual silkscreen must make A1/J1 and insertion orientation obvious. Keep USB-C accessible for flashing/debugging and leave BOOT and EN/reset operable. **No fabrication-ready DevKit footprint exists in this revision.**

## Validation and editing

KiCad 10.0.6 CLI ERC on both schematics: **0 violations**, without ERC exclusions (2026-09-24). Exported netlist checks verify all 38 socket names, the display and buzzer GPIOs against firmware, each display header pin's number, label and net, power and ground positions, that only U1/J2/J3 are present, and that every other socket is NC. Rendered schematics were visually reviewed. These checks do not resolve the electrical or mechanical holds above.

`tools/build_schematic.py` rebuilds both schematics and the symbol library deterministically. When `kicad-cli` is on PATH it then re-saves each file with `kicad-cli sch upgrade --force`, so the output matches what the KiCad editor writes (pin UUIDs are deterministic too). It overwrites both schematics and the library; do not rerun it after manual edits unless those edits have been incorporated into the script. Close the projects in KiCad first, or reload from disk afterwards, so an open copy does not overwrite the rebuild.

To validate, export each netlist and run the verifier; it refreshes CROSS_CHECK.md only after both pass:

```sh
kicad-cli sch export netlist --format kicadxml -o eink.xml Sigil_EInk.kicad_sch
kicad-cli sch export netlist --format kicadxml -o oled.xml Sigil_OLED.kicad_sch
python tools/verify_schematic.py eink.xml oled.xml
```
