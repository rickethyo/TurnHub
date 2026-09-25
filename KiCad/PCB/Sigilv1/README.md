# Sigil Rev A electrical draft

U1 is the removable, complete 38-pin ESP32 DevKit carrier interface. Its custom symbol and local symbol library use **A11–A29 and J11–J29 as pin numbers** (breadboard positions after the owner rotated the breadboard 180° on 2026-09-24: old A*n* is now J*(30−n)*, old J*n* is now A*(30−n)*), not ESP32 module pad numbers. No DevKit footprint is assigned. The PCB file has not been routed or changed.

## Authority and orientation

- GPIO functions: `Sigil/src/main.cpp`, `Sigil/include/epaper_display.h`, and explicit SPI configuration in `Sigil/src/epaper_display.cpp`.
- Socket identity: user-supplied [SigilBackMarked.png](reference/SigilBackMarked.png), photographed from the **BACK**, and the user's transcribed sequence. The photo still carries the pre-rotation A1–A19 / J1–J19 labels; apply the mapping above to read it. The schematic shows A29 top-left and J29 top-right in that rear-reference view.
- Existing values: R1–R3 remain **330R**, as specified in the previous schematic. The hardware reference contains no additional verified passive values or peripheral part numbers.
- [Both cross-check tables](CROSS_CHECK.md) are verified from the exported netlist and firmware by `tools/verify_schematic.py`.

The schematic's rear-reference arrangement is **not** a carrier component-side footprint drawing. Future footprint work must translate the views explicitly and verify insertion against the physical part. Preserve each socket ID; do not blindly copy or mirror the schematic into a footprint.

## Electrical corrections and discrepancies

The old U1 was a bare ESP32-WROOM-32E symbol with labels on incorrect electrical pins. For example its exported netlist assigned LED_BLUE to IO5, LED_GREEN to IO25, and LED_RED to IO23, contrary to the working firmware's GPIO27, GPIO14, and GPIO13. These now terminate on A21, A22, and A25 respectively. The old LED resistor outputs and LED cathodes were unconnected; the new circuits connect GPIO → 330R → LED anode, with cathode → GND.

PAIR was described as future work in old schematic/hardware notes. It is implemented: `PAIR_BUTTON = 19`, U1 J18, net PAIR, SW4 to common GND including U1 J17. Firmware detaches SPI MISO from GPIO19 and configures INPUT_PULLUP after display initialization. Released is HIGH; pressed is LOW. Pass and Action also close to GND with internal pull-ups.

The old GPIO4 Action/Win auxiliary and GPIO32 DISPLAY_DETECT assignments have no current firmware implementation and were removed; J23 is explicitly NC. GPIO32 / A17 is now the Pause / Win button (`PAUSE_WIN_BUTTON = 32`): net BTN_PAUSE, SW5 to GND, INPUT_PULLUP like the other buttons. The owner first added the BTN_PAUSE label by hand in KiCad without a switch; the script now draws the complete circuit. SW3's reference stays retired (it was the old GPIO4 auxiliary) rather than renumbering the existing Pair switch SW4.

All three ground positions J17, J11, and A24 connect to GND. A11 supplies the +3V3 rail. This draft assumes power through the DevKit's own USB connector; A29/5V has no carrier connection. No additional regulator, USB-UART, BOOT, EN/reset, or other DevKit support circuitry is reproduced.

## Unresolved electrical details

The **known GPIO network is connected and internally consistent**, but the complete peripheral implementation cannot be released from the information currently in the repository:

- **Display J2:** a logical signal interface only, with semantic pin IDs VCC/GND/DIN/CLK/CS/DC/RST/BUSY. These are not a verified physical connector order. Firmware selects GxEPD2_213_B74; the exact breakout, connector, supply rating/current, and onboard support components need confirmation. The +3V3 connection carries forward the old schematic's intended supply and is pending module verification. The display has no carrier MISO connection.
- **Buzzer J3:** a logical SIG/GND interface only. Firmware proves GPIO33 tone output, but not whether the physical load is a passive piezo, magnetic transducer, or driven module. Confirm part, wiring, voltage/current, driver, bias and protection before implementing the load. No direct GPIO-drive rating is assumed.
- J2/J3 are excluded from PCB and BOM, have no footprints, and are explicitly marked logical-only on the sheet. Their complete internal circuitry is not represented. They must be replaced with verified physical interfaces/circuits before PCB work.
- Confirm LED parts, operating current and resistor ratings; 330R is retained from the old schematic, not claimed measured. Confirm the DevKit regulator can supply the total carrier/display load.

## Mechanical release hold

No reliable dimensional drawing or measured dimensions were found in the repository. The photograph is sufficient for sequence and orientation, not mechanics. Measure or obtain a reliable drawing for:

- Actual pin pitch (likely 2.54 mm, not yet verified), row center spacing and pin locations.
- Exact board width/length, USB-C overhang and cable insertion envelope.
- Mounting-hole positions/diameters, if used.
- Female socket dimensions/height, insertion depth, underside and component keepouts.
- BOOT and EN/reset access plus antenna clearance requirements for the exact installed DevKit.

Use two 1x19 female socket rows so the complete DevKit remains removable. The eventual silkscreen must make A29/J29 and insertion orientation obvious. Keep USB-C accessible for flashing/debugging and leave BOOT and EN/reset operable. **No fabrication-ready DevKit footprint exists in this revision.**

## Validation and editing

KiCad 10.0.6 CLI ERC (standard `Device`/`Switch` libraries installed): **0 errors, 0 warnings**, without adding ERC exclusions (2026-09-24, after adding SW5). Exported netlist checks verify all 38 socket names, all 14 firmware signal mappings, ground positions, button topology, LED chains, logical peripheral connections and unused-pin NC markers. A rendered schematic was visually reviewed. These checks do not resolve the electrical or mechanical holds above.

Open `Sigilv1.kicad_sch` in KiCad. The project had existing editor lock files during this update; reload the schematic from disk before editing so an older open copy does not overwrite it.

`tools/build_schematic.py` rebuilds the draft deterministically, preserving embedded standard R/LED/SW_Push definitions. When `kicad-cli` is on PATH it then re-saves the file with `kicad-cli sch upgrade --force`, so the output is byte-identical to what the KiCad editor writes (pin UUIDs are generated deterministically too). It overwrites schematic and custom library; do not rerun it after manual edits unless those edits have been incorporated into the script. To validate an edited schematic, export KiCad XML netlist and run `python tools/verify_schematic.py path/to/export.xml` (for example after `kicad-cli sch export netlist --format kicadxml -o export.xml Sigilv1.kicad_sch`); this refreshes CROSS_CHECK.md only after checks pass.
