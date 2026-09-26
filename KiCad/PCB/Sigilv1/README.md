# Sigil Rev A electrical drafts

Rev A is drawn as two KiCad projects in this folder, plus `Sigil_JewelAdapter` (the status ring's small board, below). They share the symbol library (`Sigil.kicad_sym`) and differ only in the display interface:

| Project | Display | Display nets |
|---|---|---|
| `Sigil_EInk.kicad_pro` | Inland e-paper driver board (GxEPD2_213_B74), J2 `EPD_Module_Header` (8 pins) | EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY |
| `Sigil_OLED.kicad_pro` | Inland 1.3" OLED V2.0, J2 `OLED_Module_Header` (7 pins) | OLED_SCLK, OLED_MOSI, OLED_CS, OLED_DC, OLED_RST |

Both displays use the same sockets and GPIOs (A8/GPIO16 DC, A9/GPIO17 CS, A11/GPIO18 clock, A17/GPIO22 reset, A18/GPIO23 data); only the e-ink uses A14/GPIO21 BUSY. Both schematics also carry the buzzer interface J3 on J12/GPIO33, +3V3 from J19, and GND on A13, A19 and J6.

**Discrete LEDs and the old buttons are gone** (removed 2026-09-24). Their GPIOs are marked NC.

**Display-type strap (both, 2026-09-26).** A7/GPIO4 tells the firmware which display the board carries (`HW_TYPE_STRAP_PIN`, `Sigil/DISPLAY.md`): left NC on `Sigil_EInk` (the internal pull-up reads E-ink), tied to GND on `Sigil_OLED`. A build for the other display halts at boot, so a carrier with the wrong strap won't run.

**First-PCB footprints (2026-09-26).** Every carrier part now has a through-hole KiCad standard-library footprint and is in the BOM: J2 a 1x8 (E-ink) or 1x7 (OLED) 2.54 mm socket, J4 a 1x5 socket, J5 a JST-XH 3-pin socket for the Jewel adapter pigtail, J3 a 1x2 header for the buzzer leads, R1 an axial 0207 resistor, C2 a 10 uF ceramic on +3V3 beside the headers (5 mm disc footprint), SW1-SW5 6 mm tactile switches. *Planned*: footprints are chosen, no board is laid out.

**Joystick (E-ink only).** `Sigil_EInk` carries J4, an unmarked 5-pin analog thumbstick that replaces the Pass/Action/Pause buttons: GND, "+5V" (fed **3.3 V**, never 5 V, since VRX/VRY swing to the supply and the ESP32 ADC is 3.3 V max), VRX to J15/GPIO34, VRY to J14/GPIO35 and SW to J13/GPIO32. VRX/VRY are ADC1 input-only pins because ADC2 is unusable under ESP-NOW. Firmware: the Sigil `sigil` PlatformIO environment (the E-ink build); the directions and click are the five menu keys (on-screen compass). *Verified* on the owner's breadboard (2026-09-25): both axes read reversed as mounted, so the firmware inverts X and Y.

**Pushbuttons (OLED only).** `Sigil_OLED` carries SW1-SW5, five discrete momentary pushbuttons (no d-pad module; replaced J4 on 2026-09-25), each from its GPIO (pin 1) to one shared GND rail (pin 2) using the ESP32's internal pull-ups, no resistors: SW1 Up J11/GPIO25, SW2 Down J9/GPIO27, SW3 Left A12/GPIO19, SW4 Right A14/GPIO21 (the e-ink BUSY pin, unused by the OLED) and SW5 Select J13/GPIO32. On a 4-leg tactile switch use legs on opposite sides of the gap. They drive the OLED menu list; firmware unchanged. *Planned*: being wired.

**Status ring (both), on its own adapter board (2026-09-26).** The Adafruit NeoPixel Jewel 7 (RGBW) no longer sits on the Sigil board. `Sigil_JewelAdapter` is a small board with five pin holes where the Jewel's pads are (positions from Adafruit's [NeoJewel 7 board file](https://github.com/adafruit/Adafruit-NeoPixel-Jewel-7)): pins stand up from it and the Jewel is soldered on top, LEDs up, like it was on the breadboard. It carries C1 (470 uF bulk capacitor), the Jewel's two M2 mounting holes, an optional unfitted chain-out J3 (+5V, DOUT, GND) for more pixels after the Jewel, and J2, three holes for the bare end of a 3-wire JST-XH pigtail. The pigtail plugs into J5 on the Sigil board, a JST-XH 3-pin socket (1 +5V, 2 DIN, 3 GND, same order at both ends). On the Sigil board, GPIO26 (J10) goes through U2 and R1 (330 ohm, at the source; the Jewel has its own 470 ohm on DIN) to J5, and +5V comes from the DevKit's USB 5V (J1). Full RGBW draws about 560 mA, more than USB supplies, so the firmware caps brightness at 48/255. The ring draws the Sigil's status light from Atlas's LedState; the same information is always on a screen as text. *Verified* lit on the breadboard (owner, 2026-09-26); the adapter's pad positions are *Needs verification* (test-fit a Jewel against a 1:1 print).

The breadboard wiring the current firmware uses is in the [hardware reference](../../../Documentation/engineering/HARDWARE_REFERENCE.md).

U1 is the removable, complete 38-pin ESP32 DevKit carrier interface (Inland ESP32-WROOM-32D, micro-USB). Its custom symbol and local symbol library use **A1–A19 and J1–J19 as pin numbers**, not ESP32 module pad numbers. Its footprint, `Sigil.pretty/ESP32_DevKit_38_Socket_Row25.4mm` (from `build_schematic.py`), uses the published dimensions: 2.54 mm pitch, rows 25.4 mm (1.0 in) apart, a 55.0 x 27.5 mm board. It is drawn from the carrier's top side with the DevKit face up and micro-USB at the top, which mirrors the rear photo: the A row is on the left, the J row on the right, and A1/J1 at the USB end. *Needs verification*: caliper-check the row spacing and test-fit sockets before ordering. The PCB files are empty placeholders.

## Authority and orientation

- GPIO functions: `Sigil/include/epaper_display.h` and the explicit SPI configuration in `Sigil/src/epaper_display.cpp` (e-ink), `Sigil/include/oled_config.h` (OLED), and `BUZZER_PIN` in `Sigil/src/main.cpp`.
- Socket identity: user-supplied [SigilBackMarked.png](reference/SigilBackMarked.png), photographed from the **BACK**, and the user's transcribed sequence. The schematics deliberately show J1 (5V) top-left and A1 (CLK) top-right in that rear-reference view.
- [The cross-check tables](CROSS_CHECK.md) are verified from both exported netlists and the firmware by `tools/verify_schematic.py`.

The rear-reference arrangement is **not** a carrier component-side footprint drawing. Future footprint work must translate the views explicitly and verify insertion against the physical part. Preserve each socket ID; do not blindly copy or mirror the schematic into a footprint.

This draft assumes power through the DevKit's own USB connector; J1/5V has no carrier connection. No additional regulator, USB-UART, BOOT, EN/reset, or other DevKit support circuitry is reproduced.

## Unresolved electrical details

- **Display J2 (both):** pin numbers follow the module's own header, pin 1 at the top, with the silkscreen labels as pin names. Order and the jumper wire color on each wire come from the owner's photos of the breadboard wiring (2026-09-24); the colors are annotations, not a harness specification. Tables are in [CROSS_CHECK.md](CROSS_CHECK.md). J2 has no footprint yet.
- **E-ink J2:** SDI, SCLK, CS, D/C, RES, BUSY, VCC, GND. The Inland driver board has two slide switches, P1 (3 / 0.47) and P2 (5VIN / 3.3VIN), whose positions are not recorded; with the carrier's 3.3 V supply, check P2. Supply current and the panel itself need confirmation. The display has no carrier MISO connection.
- **OLED J2:** GND, VCC, CLK, MOSI, RES, DC, CS. The owner verified the SPI wiring, 3.3 V supply and a working image on 2026-09-24; the SH1106 controller and 128x64 geometry are inferred from the vendor example (see `Sigil/DISPLAY.md`).
- **Buzzer J3:** a logical SIG/GND interface only. Firmware proves GPIO33 tone output, but not whether the physical load is a passive piezo, magnetic transducer, or driven module. Confirm part, wiring, voltage/current, driver, bias and protection before implementing the load. No direct GPIO-drive rating is assumed.
- J3 is a plain 2-pin header (pin 1 SIG, pin 2 GND). The owner reports cheap passive piezo buzzers, which GPIO33 drives directly; a magnetic transducer would need a transistor driver and flyback diode.
- Confirm the DevKit regulator can supply the carrier/display load on +3V3 (the ring runs from USB 5 V).
- **Ring level shifter (2026-09-26):** 3.3 V data worked on the breadboard but is below the SK6812's ~3.5 V input spec, so the PCB adds U2, a 74AHCT1G125 (SOT-23-5) on +5V with C3 (100 nF): GPIO26 -> U2 -> R1 -> DIN (nets RING_DIN, RING_DIN_5V, RING_DIN_R).

## Mechanical release hold

No reliable dimensional drawing or measured dimensions were found in the repository. The photograph is sufficient for sequence and orientation, not mechanics. Measure or obtain a reliable drawing for:

- Actual pin pitch (likely 2.54 mm, not yet verified), row center spacing and pin locations.
- Exact board width/length, USB-C overhang and cable insertion envelope.
- Mounting-hole positions/diameters, if used.
- Female socket dimensions/height, insertion depth, underside and component keepouts.
- BOOT and EN/reset access plus antenna clearance requirements for the exact installed DevKit.

Use two 1x19 female socket rows so the complete DevKit remains removable. The eventual silkscreen must make A1/J1 and insertion orientation obvious. Keep USB-C accessible for flashing/debugging and leave BOOT and EN/reset operable. **No fabrication-ready DevKit footprint exists in this revision.**

## Validation and editing

KiCad 10.0.6 CLI ERC on both schematics: **0 violations**, without ERC exclusions (2026-09-24). **ERC needs a re-run** after the 2026-09-26 changes: they were built and checked in a session without KiCad, using `tools/offline_netlist.py` in place of the netlist export, so the files are in the builder's compact form until KiCad re-saves them. Exported netlist checks verify all 38 socket names, the display and buzzer GPIOs against firmware, each display header pin's number, label and net, power and ground positions, that only U1/J2/J3 are present, and that every other socket is NC. Rendered schematics were visually reviewed. These checks do not resolve the electrical or mechanical holds above.

`tools/build_schematic.py` rebuilds both schematics and the symbol library deterministically. When `kicad-cli` is on PATH it then re-saves each file with `kicad-cli sch upgrade --force`, so the output matches what the KiCad editor writes (pin UUIDs are deterministic too). It overwrites both schematics and the library; do not rerun it after manual edits unless those edits have been incorporated into the script. Close the projects in KiCad first, or reload from disk afterwards, so an open copy does not overwrite the rebuild.

To validate, export each netlist and run the verifier; it refreshes CROSS_CHECK.md only after both pass:

```sh
kicad-cli sch export netlist --format kicadxml -o eink.xml Sigil_EInk.kicad_sch
kicad-cli sch export netlist --format kicadxml -o oled.xml Sigil_OLED.kicad_sch
kicad-cli sch export netlist --format kicadxml -o adapter.xml Sigil_JewelAdapter.kicad_sch
python tools/verify_schematic.py eink.xml oled.xml adapter.xml
```

Without KiCad, `python tools/offline_netlist.py Sigil_EInk.kicad_sch eink.xml` (and the same for OLED) writes an equivalent netlist from the generated files. It only understands what `build_schematic.py` writes and is not ERC.
