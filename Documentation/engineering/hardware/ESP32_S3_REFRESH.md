# ESP32-S3 prototype refresh

Status: **Planned hardware; verified vendor documents, not physical acceptance**.
Research date: 2026-10-11. Atlas candidate ES3C40P; Sigil candidate ES3C28P.
Require the capacitive **C** SKU: ES3N28P has no touch. An equivalent must match
controller, memory, connector, power and mechanical requirements individually.
The Gen 1 E32R28T Atlas and ESP32 DevKit Sigils remain the working baseline on
`legacy/gen1`. Master's `env:atlas-s3` (2026-10-28, *Experimental*) runs the full Atlas
firmware on the ES3C40P: native USB serial, microSD over 4-bit SDMMC, an
`ATLAS|BOARD|...` boot line, and the touchscreen (`atlas_display_s3.cpp`:
LovyanGFX ST7796S/FT6336U drivers, LVGL 9.3 drawing the `touch_controls.cpp`
screen model, laid out for the panel's native 320 × 480 portrait since
2026-10-28). First board, 2026-10-28 (*Verified* on one unit): ESP32-S3
rev 0.2, flash JEDEC 5E4018 = 16 MB (the 4-inch schematic's W25X40 is wrong),
8 MB PSRAM, SDHC card mounted, AP and ESP-NOW up, no resets. Rotation, colour
inversion and touch orientation are `LCD_ROTATION`/`LCD_INVERT` in `config.h`
and still need a look at the panel. The battery reading (GPIO 9, 1/2 divider,
`atlas_battery_s3.cpp`) landed 2026-10-29 with the Gen 1 gauge and curve, which
*need verification* against a meter and a timed discharge on this board's cell.
Audio and RGB drivers come next;
the Sigil target does not exist yet.

## Evidence and confidence

Sources are the [4-inch manufacturer page][40], [2.8-inch manufacturer page][28],
each board's schematic, user manual and dimension drawing linked there, the
[ESP32-S3 datasheet][s3], [SparkFun product][nav], [navigation schematic][navsch]
and [PCA9554 datasheet][pca]. Downloaded primary-source digests are in
[SOURCES.csv](SOURCES.csv). Sources are linked rather than redistributed.

**Confirmed in manufacturer documentation** means source review only. Hardware
revision, fitted components, continuity and actual performance remain unverified.
Manufacturer pages contain errors (including classic Bluetooth claims: ESP32-S3
supports BLE, not BR/EDR), so do not treat marketing as an electrical specification.

| Item | Atlas ES3C40P | Sigil ES3C28P |
|---|---|---|
| MCU implementation | Discrete ESP32-S3R8 SoC, PCB antenna; not a WROOM module | Discrete ESP32-S3R8 SoC, PCB antenna; not a WROOM module |
| Memory | Vendor: 16 MB external QSPI flash, 8 MB internal OPI PSRAM | Vendor: 16 MB external QSPI flash, 8 MB internal OPI PSRAM |
| Display | ST7796S IPS, native 320 × 480, 4-wire SPI | ILI9341V IPS, native 240 × 320, 4-wire SPI |
| Application orientation | 320 × 480 portrait (owner, 2026-10-28; was landscape) | 240 × 320 portrait |
| Capacitive controller | FT6336U, I²C 7-bit 0x38 | FT6336G, I²C 7-bit 0x38 |
| Audio | ES8311 codec 0x18, analog MEMS microphone, mono amplifier + speaker socket | Same documented functions; verify fitted amplifier and data pins separately |
| Storage | microSD, 4-bit SDIO/SDMMC (not legacy SPI wiring) | microSD, 4-bit SDIO/SDMMC |
| USB | USB-C native USB D− GPIO19 / D+ GPIO20; CC 5.1 kΩ resistors | Same documented USB functions |
| Charging | TP4054, R12 3.3 kΩ, vendor actual 290 mA (~303 mA nominal using 1000/R) | TP4054, R12 3.3 kΩ, same advertised current |
| Main/audio regulator | ME6217C33M5G 3.3 V LDOs, SL2305 supply-switch MOSFET | Same documented topology |
| Overall assembly | 61 × 112 × 10.8 mm vendor nominal | 50 × 86 × 10.6 mm vendor nominal |
| Display-only vendor load at 5 V | 240 mA / 1.2 W; backlight 143 mA | 140 mA / 0.7 W; backlight 79 mA |
| Vendor combined display/audio/charge load | 600 mA / 3 W | 560 mA / 2.8 W |

**Conflicts to resolve before a hardware target:** 4-inch schematic W1 says
W25X40 (not 16 MB), while its manual explicitly says 16 MB; 2.8-inch W1 says
25VQ128. Read JEDEC ID and flash size on arrival. Both schematics label amplifier
SC8002B while manuals link FM8002E; inspect markings and rating. The 2.8-inch web
pin table reverses I²S input/output relative to the schematic (see below). The
4-inch touch pull-ups are 10 kΩ, versus 4.7 kΩ on 2.8-inch. Battery dividers differ.
The 4-inch drawing's temperature range differs from its page. Neither board is
assumed electrically identical to the other.

Manufacturer Arduino examples target core 3.2.0 and LVGL 8.3/TFT_eSPI; they
explicitly exclude LVGL 9.x. ESP-IDF examples and an LVGL port guide are linked,
but their exact versions have not been qualified. The isolated desktop uses
LVGL 9.3 with its official SDL backend, not those vendor sketches.

## Pin and connector planning

These are **source-reviewed maps, not implemented firmware bindings**. GPIOs are
ESP32-S3 GPIO numbers, not SoC pad numbers. Each board schematic was inspected.

| Function | ES3C40P | ES3C28P | Constraints |
|---|---|---|---|
| LCD CS / DC / SCK / MOSI / MISO | 10 / 46 / 12 / 11 / 13 | 10 / 46 / 12 / 11 / 13 | Dedicated display SPI; DC 46 is a strap |
| LCD reset / backlight | EN / 45 | EN / 45 | Shared system reset; BL 45 is a strap, high = lit |
| Touch SDA / SCL / reset / IRQ | 16 / 15 / 18 / 17 | 16 / 15 / 18 / 17 | Shared I²C with codec and P4 |
| Codec MCLK / BCLK / LRCK | 4 / 5 / 7 | 4 / 5 / 7 | No ESP32 DAC on S3; use I²S codec |
| Codec data | Schematic GPIO6 = ESP DOUT → codec DSDIN; GPIO8 = ESP DIN ← codec ASDOUT | Schematic same; page calls 8 output / 6 input | Continuity + playback/capture test required; no binding yet |
| Amplifier enable | 1, active low | 1, active low | Default disabled |
| SD CLK / CMD / D0 / D1 / D2 / D3 | 38 / 40 / 39 / 41 / 48 / 47 | 38 / 40 / 39 / 41 / 48 / 47 | Board has 10 kΩ pull-ups; use SDMMC driver |
| Board RGB data | 42 | 42 | WS2812-class RGB, separate from Jewel RGBW |
| Battery ADC | 9; 100 kΩ / 100 kΩ divider | 9; 200 kΩ / 200 kΩ divider | Both nominal 1/2; characterize settling and ADC calibration |
| BOOT / reset | 0 / EN | 0 / EN | Preserve physical recovery access |
| UART | GPIO43 TX / GPIO44 RX in schematics | Same | Web tables reverse TX/RX; trust net tracing, then verify |
| P3 expansion pin 1 / 2 / 3 / 4 | 2 / 3 / 14 / 21 | 2 / 3 / 14 / 21 | No power or ground in this 4-pin connector |
| P4 I²C pin 1 / 2 / 3 / 4 | 3.3 V / GND / SCL15 / SDA16 | Same | Pin numbering from schematic, not plug-view guess |
| JP1 battery pin 1 / 2 | BAT+ / GND | Same | Polarity-test actual mating harness |
| JP3 speaker pin 1 / 2 | SP− / SP+ | Same | Differential amplifier output: neither lead to GND |
| P2 UART pin 1 / 2 / 3 / 4 | switched supply / GND / TX / RX | Same | Supply Q5 path is not a guaranteed battery 5 V output |

Manufacturer specifies 1.25 mm pitch for P2/P3/P4 (4P) and battery/speaker (2P).
**Exact connector family, housing, key and mating orientation are unverified**;
1.25 mm alone does not prove JST-GH, Molex PicoBlade or a generic clone.
Obtain the included harness or measure the actual socket before ordering plugs.

GPIO21 is the preferred Jewel output: exposed, no documented onboard peripheral,
not a strap. GPIO14 is the preferred navigation INT/wake input; GPIO2 remains a
candidate optional LED output. GPIO3 is a JTAG-related strapping pin: avoid loads
that set its boot level. Other S3 straps are 0, 45, 46. Leave LCD strap circuitry
alone. PSRAM/flash pins are not spare GPIO; native USB uses 19/20. Keep all new
bindings in per-board drivers after verification, never game logic.

## Five-way navigation

Select SparkFun **PRT-27576** for prototypes (listed $8.75 USD, 2026-10-11).
PCA9554 default 7-bit address 0x20, selectable 0x20–0x27: no address collision
with touch 0x38 or ES8311 0x18. Operate at 3.3 V; no 5 V bus pull-ups.
Schematic IO0/1/2/3/4 = Up/Down/Right/Left/Select, active low, each with 10 kΩ
pull-up. IO5–7 drive optional non-addressable RGB, which is not a Jewel replacement.
Configure input bits and initialize output LEDs off; read the input register to
clear active-low open-drain INT. Debounce edges in the input adapter; preserve
simultaneous-key handling and release/hold semantics outside widgets.

The Qwiic board has 2.2 kΩ bus pull-ups and a removable I²C jumper. Effective
bus pull-up with 2.8-inch 4.7 kΩ is ~1.50 kΩ; with 4-inch 10 kΩ is ~1.80 kΩ,
before any codec pull-ups. At 3.3 V this is about 2.2/1.8 mA sink; measure rise
time and total pull-ups. Start at 100 kHz, use one serialized bus owner for touch,
codec and navigation, then qualify 400 kHz. Remove duplicate pull-ups if needed.

Qwiic is **4-pin 1.0 mm JST-SH**, pin 1 GND, 2 3.3 V, 3 SDA, 4 SCL.
Adapter wiring: LCD P4 **1→Qwiic2, 2→Qwiic1, 3→Qwiic4, 4→Qwiic3**.
Use keyed housings, explicit pin labels, strain relief and a continuity test;
never a straight-through pitch adapter. Run a separate ground-referenced INT lead
from its 0.1-inch PTH to proposed P3 GPIO14. Alternatively solder 3V3/GND/SDA/SCL
and INT directly to the board's PTHs; these are bus/INT pads, not five individual
switch contacts. This reduces connector cost but makes service harder.

PCA9554 can assert INT without polling while powered. GPIO14 is an RTC-capable
S3 pin and can be a low-level deep-sleep wake source. Keep expander supply and
INT pull-up powered; read/clear a pending interrupt before sleeping to avoid a
wake loop. Test held switch, release, simultaneous presses and sleep entry race.
Touch IRQ GPIO17 is also RTC-capable, but wake depends on controller power and
its sleep/interrupt configuration; touch wake is **not yet verified**.

Future production alternative: bare SF303GJ26 or equivalent five-way switch,
PCA9554/PCA9534 with explicit contact pull-ups, decoupling, address straps and
INT pull-up on one interface PCB. Quote assembly/test costs before concluding it
is cheaper. Direct five-GPIO wiring is constrained by available non-strap pins.
LVGL receives a semantic keypad adapter; no PCA9554 registers enter UI code.

## RGBW Jewel and interface PCB

Retain Adafruit NeoPixel Jewel 7 **RGBW**, matching the existing white-temperature
variant. Reuse existing status animations, LedState semantics, preferences and
hold-progress cues when hardware firmware begins. Do not drive onboard RGB as RGBW.

Planned circuit: GPIO21 → 74AHCT1G125 at regulated 5 V (OE low, 100 nF at VCC)
→ 330 Ω series resistor → Jewel DIN. Jewel has its own DIN resistor; verify edge
quality with the actual combined cable/resistors. Place 470 µF ≥10 V bulk at ring
power (existing adapter choice; 500–1000 µF is also an option), + to 5 V, common
GND for board, booster, buffer and ring. Use a short data/ground pair. Choose a
buffer with documented partial-power behavior or gate OE/power so an unpowered
LED rail is not back-fed. Never take ring current from a GPIO or the 3.3 V LDO.

Seven RGBW pixels worst-case planning bound is 7 × 80 mA = **560 mA at 5 V**.
Start with existing brightness ceiling 48/255: linear worst-case ~105 mA plus
quiescent load, not a measured limit. Add an aggregate channel/current cap and
measure patterns; dim further on battery and use no full-white boot flash.
An optional 8-pixel second RGBW strip at the same cap adds ~120 mA: ~225 mA
combined plus idle, requiring its own budget, not an automatic BOM purchase.
Can chain Jewel DOUT (shared timing) or use proposed GPIO2 with another buffer.

Neither board has a documented 5 V boost. Its net called **+5 is cell voltage
on battery**, roughly 3.0–4.2 V, and diode-reduced USB voltage on USB. A 74AHCT
buffer needs its specified 4.5–5.5 V supply: use a separately regulated 5 V rail
for reliable battery RGBW operation. Plan a protected 1S-to-5 V boost with enable,
≥1 A transient capability for ring-only worst case, thermal/efficiency checks,
input fuse and no USB back-feed. Qualify a USB/battery mux or run the booster
from BAT+ without placing loads across charge termination; topology is still open.
If rail design is deferred, ring must be explicitly disabled on battery.

A new small **S3 interface PCB** is recommended: navigation bus/INT adapter,
level buffer + decoupling, resistor, LED power routing, booster/load switch and
keyed power/data harness. Reuse the legacy Jewel adapter's mechanical concept,
not its unverified pad geometry or USB-only power assumption. Preserve all three
legacy KiCad projects. No new fabrication-ready PCB is claimed here.

## Battery and power

Use a protected **1S 3.7 V LiPo (4.2 V charge)**, not LiFePO4 or a 4.35 V cell.
Vendor says 290 mA configured charge; R12 implies ~303 mA nominal. Its page also
says 4.24 V saturation and 62°C: verify actual termination, tolerance and thermal
behavior against the fitted IC and cell rating before locking battery choice.
The TP4054 is a linear charger, not a fuel gauge or temperature-managed system.
Schematics do not show pack-protection circuitry; cell protection is required.
CHRG is not routed to the MCU, and no reliable dedicated USB/presence detection
is exposed. Do not copy legacy voltage-step charging estimates as facts for S3.

D8 isolates USB, Q3 disconnects cell supply when USB is present; source analysis
supports charging while operating, with system powered from USB. This is a
simple discrete load-sharing path, not a qualified seamless power-path IC.
Check low/full cell, removal/insertion, audio/radio bursts and rapid USB bouncing
with an oscilloscope for resets and reverse current. At low cell voltage the
3.3 V LDO loses headroom; brownout can precede pack cutoff. Add firmware low-voltage
shutdown after measuring loaded voltage. Sleeping SoC does not imply low board
current: LDOs, dividers, power LEDs, touch, codec and booster remain loads.

### Preliminary engineering budget (assumptions, not measurements)

| Load | Atlas | Sigil | Basis |
|---|---:|---:|---|
| Display/system baseline | 1.2 W | 0.7 W | Vendor display-only figures at 5 V; includes some MCU activity |
| Additional radio headroom | 0.3–0.8 W | 0.3–0.8 W | Planning allowance, not added chip measurement; avoid double-counting after bench test |
| Speaker burst | 0–2.5 W | 0–2.5 W | Engineering allowance pending amplifier/load; playback not continuously budgeted |
| Capped Jewel | — | ~0.525 W + idle | 105 mA at regulated 5 V |
| Charge (USB only) | ~1.5 W input allowance | ~1.5 W input allowance | ~0.3 A charger input; heat and losses included approximately |

ESP32-S3 datasheet Table 5-7 reports **340 mA peak TX** (802.11b, 21 dBm)
and **88–91 mA RX** at 3.3 V, under its stated CPU/radio conditions. At 340 mA,
the 3.3 V chip rail is ~1.12 W; the 5 V LDO input can consume ~1.7 W before
other loads. ESP-NOW shares this radio: duty cycle lowers average, not burst
requirements. These chip figures are not complete-board measurements. Reserve
that burst separately from the average-radio allowance above.

Typical screen/radio planning: Atlas **1.5–2 W**, Sigil + capped ring **1.5–2 W**.
Peak source allocation: Atlas ~7 W incl. charge/audio/radio; Sigil ~7 W capped,
~10 W uncapped ring fault envelope. These are design headroom, not certified maxima.
Use a quality 5 V / 2 A USB supply per prototype; the Type-C CC resistors do not
prove a 2 A negotiation contract. Confirm source advertisement/cable ratings and
board path current/temperature; limit loads to the negotiated/available supply.

Provisional fit candidates: Atlas **2000–4000 mAh**, Sigil **1500–2500 mAh**, with
≥1 A continuous and ≥2 A short-burst cell rating as initial candidates (uncapped
fault + audio at low cell can exceed 3 A; enforce a verified combined load cap).
Do not buy a pack by capacity alone. Runtime example with 80% usable energy:
`t_hours = 3.7 × capacity_Ah × 0.8 / average_W` (includes a conservative combined
usable-energy/loss factor): at 1.5–2 W, 2000 mAh gives **3.0–3.9 h**, 2500 mAh
**3.7–4.9 h**, 4000 mAh **5.9–7.9 h**. Screen dimming, radio duty, audio and ring
change this substantially. Approximate 2 Ah charge time at 0.29 A is 6.9 h plus
CV taper. Final cell dimensions, charge/discharge ratings, cutoff and connector
await samples. No battery runtime or sleep current has been measured.

## Mechanical changes and release checklist

Reviewed `3D/README.md` and both print sets (now on `legacy/gen1`), plus KiCad EInk/OLED/JewelAdapter
schematics, PCB nets and existing fit/DRC holds. Current Atlas shell 98 × 90.95 ×
43.34 mm and Sigil shell 64 × 167.28 × 73.70 mm are legacy envelopes, not proof
new boards fit. The rotated 4-inch board is 112 mm wide, exceeding the legacy
Atlas shell's 98 mm external width, so it requires a new/wider mounting envelope.
Preserve them; start new revision directories when measured CAD
exists. Existing OLED PCB remains a placeholder; EInk routing is not repurposed.

Plan a 4-inch portrait Atlas bezel and a 2.8-inch portrait Sigil bezel with
unobstructed capacitive edges. Vendor drawings show four mounting holes (2.8-inch
Ø3.2 with Ø5.6 clearance); verify coordinates, diameter, component-side clearance
and drawing origin on real samples before transferring to CAD. Account for LCD
stack, flex, backlight and component heights, not only PCB rectangle. Put the
25.4 mm-square navigation board below the Sigil screen with full five-way travel,
exposed center and direction labels; verify cap retention and finger clearance.
Position Jewel for table/player visibility without glare on glass; provide a
diffuser. Locate protected battery away from screw tips, speaker magnet/heat and
board edges; leave pouch swelling and removal space, no compression.

Provide USB-C insertion envelope, accessible SD, BOOT and RESET, strain-relieved
P4/P3/power cables with no sharp bends, and a serviceable base. Include a speaker
cavity/grille only after included speaker contents and sound performance are
known. Keep antenna clear of battery, LED copper and metal. Print bezel, USB,
joystick and mounting coupons first; do not manufacture from nominal web sizes.

- [ ] Verify exact SKU/revision, flash JEDEC/size, PSRAM and fitted audio IC.
- [ ] Verify each pin table/connector polarity and actual 1.25 mm family; continuity-test adapters.
- [ ] Scan shared I²C addresses; measure pull-ups, debounce, IRQ and touch/joystick wake.
- [ ] Scope SPI/SDMMC/audio; resolve 2.8-inch I²S and UART table contradictions.
- [ ] Measure charging/termination/thermal behavior and cell protection/cutoff.
- [ ] Scope USB↔cell transitions, brownout, LED boost stability and back-feed prevention.
- [ ] Measure idle, screen brightness sweep, ESP-NOW/Wi-Fi bursts, audio, capped LEDs and sleep.
- [ ] Validate BOOT+RESET USB recovery with failed display/touch; factory-reset hold is physical fallback, no external Pair button.
- [ ] Caliper-check bezel, mounting holes, connectors, PCB height, battery and joystick; test-fit before CAD release.
- [ ] Confirm supplier complete-board prices, ordinary quantity/MOQ, speaker/harness contents and freight.

[40]: https://www.lcdwiki.com/4.0inch_ESP32-S3_Display
[28]: https://www.lcdwiki.com/2.8inch_ESP32-S3_Display
[s3]: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
[nav]: https://www.sparkfun.com/sparkfun-qwiic-navigation-switch.html
[navsch]: https://docs.sparkfun.com/SparkFun_Qwiic_Navigation_Switch/assets/board_files/SparkFun_Qwiic_Navigation_Switch_v11_Schematic.pdf
[pca]: https://docs.sparkfun.com/SparkFun_Qwiic_Navigation_Switch/assets/component_documentation/pca9554.pdf
