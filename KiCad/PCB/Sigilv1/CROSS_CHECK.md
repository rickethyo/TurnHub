# Sigil Rev A cross-check tables

Verified against exported KiCad netlists, current firmware, and the user-supplied rear-photo sequence. YES means GPIO/socket/net consistency; it does not verify peripheral parts or mechanical dimensions.

Discrete LEDs and the old buttons are gone; their GPIOs are explicitly NC. The Jewel moves to its own adapter board (with C1, 470 uF) on a pigtail into J5; C2 (10 uF) on +3V3, and U2 (74AHCT1G125, decoupled by C3) lifts the ring data to 5 V. GPIO4 (A7) is the display-type strap: open on the E-ink board, tied to GND on the OLED board. The E-ink schematic carries the analog joystick (J4) and the NeoPixel status ring (J5) instead; the OLED schematic carries five discrete pushbuttons (SW1-SW5) and the same ring.

## E-ink (Sigil_EInk.kicad_sch)

| Function | GPIO | DevKit socket position | Schematic net | Firmware evidence | Match? |
|---|---|---|---|---|---|
| EPD DC | GPIO16 | A8 | EPD_DC | `EPD_DC = 16` | YES |
| EPD CS | GPIO17 | A9 | EPD_CS | `EPD_CS = 17` | YES |
| EPD clock | GPIO18 | A11 | EPD_SCLK | `SPI.begin(18, 19, 23, EPD_CS)` | YES |
| EPD BUSY | GPIO21 | A14 | EPD_BUSY | `EPD_BUSY = 21` | YES |
| EPD reset | GPIO22 | A17 | EPD_RST | `EPD_RST = 22` | YES |
| EPD data | GPIO23 | A18 | EPD_MOSI | `SPI.begin(18, 19, 23, EPD_CS)` | YES |
| Joystick SW (Select) | GPIO32 | J13 | JOY_SW | `32 /* SW (J13) */` | YES |
| Joystick VRY | GPIO35 | J14 | JOY_Y | `JOYSTICK_Y_PIN = 35` | YES |
| Joystick VRX | GPIO34 | J15 | JOY_X | `JOYSTICK_X_PIN = 34` | YES |
| Status ring data (via R1) | GPIO26 | J10 | RING_DIN | `STATUS_RING_PIN = 26` | YES |
| Buzzer signal | GPIO33 | J12 | BUZZER | `BUZZER_PIN = 33` | YES |
| Ground | — | A13, A19, J6 | GND | Hardware ground | YES |
| 3.3 V rail | — | J19 | +3V3 | DevKit supply; not a GPIO | N/A |
| Display-type strap (open = E-ink) | GPIO4 | A7 | NC | `HW_TYPE_STRAP_PIN = 4` | YES |
| USB 5 V (status ring) | — | J1 | +5V | DevKit USB supply; not a GPIO | N/A |

Unused (NC) sockets: A1, A2, A3, A4, A5, A6, A7, A10, A12, A15, A16, J2, J3, J4, J5, J7, J8, J9, J11, J16, J17, J18.

Display header J2, in physical order (pin 1 at the top of the module header). Wire colors are the breadboard jumpers in the owner photos (2026-09-24), not a harness specification.

| Header pin | Silkscreen | Net | Wire color |
|---|---|---|---|
| 1 | SDI | EPD_MOSI | blue |
| 2 | SCLK | EPD_SCLK | purple |
| 3 | CS | EPD_CS | gray |
| 4 | D/C | EPD_DC | white |
| 5 | RES | EPD_RST | black |
| 6 | BUSY | EPD_BUSY | brown |
| 7 | VCC | +3V3 | red |
| 8 | GND | GND | orange |

Joystick header J4, in module order. The "+5V" pin is fed from +3V3 on purpose: VRX/VRY swing to the supply and the ESP32 ADC must not see 5 V. Firmware: Sigil env `sigil`, the E-ink build; the directions and click are the five menu keys.

| Header pin | Module label | Net |
|---|---|---|
| 1 | GND | GND |
| 2 | +5V | +3V3 |
| 3 | VRX | JOY_X |
| 4 | VRY | JOY_Y |
| 5 | SW | JOY_SW |

Status ring cable J5 (JST-XH, 3 pins) to the Jewel adapter board, on USB 5 V. Data runs GPIO26 (J10, net RING_DIN) through U2 (74AHCT1G125, 5 V buffer, net RING_DIN_5V) and R1 (330 ohm) to DIN (net RING_DIN_R). Pin numbers are logical; the pads are labelled. Firmware caps brightness at 48/255.

| J5 pin | Signal | Net |
|---|---|---|
| 1 | +5V | +5V |
| 2 | DIN | RING_DIN_R |
| 3 | GND | GND |

Parts and footprints (U1 is from published Inland DevKit dimensions; caliper-check before ordering):

| Ref | Value | Footprint |
|---|---|---|
| C2 | 10uF | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` |
| C3 | 100nF | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` |
| J2 | INLAND E-PAPER HEADER (8-PIN) | `Connector_PinSocket_2.54mm:PinSocket_1x08_P2.54mm_Vertical` |
| J3 | BUZZER 2-PIN HEADER | `Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical` |
| J4 | ANALOG JOYSTICK HEADER (5-PIN) | `Connector_PinSocket_2.54mm:PinSocket_1x05_P2.54mm_Vertical` |
| J5 | STATUS RING CABLE (JST-XH 3) | `Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical` |
| R1 | 330R | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal` |
| U1 | REMOVABLE ESP32 DEVKIT / 2 x 19 | `Sigil:ESP32_DevKit_38_Socket_Row22.86mm` |
| U2 | 74AHCT1G125 | `Package_TO_SOT_SMD:SOT-23-5` |

## OLED (Sigil_OLED.kicad_sch)

| Function | GPIO | DevKit socket position | Schematic net | Firmware evidence | Match? |
|---|---|---|---|---|---|
| OLED DC | GPIO16 | A8 | OLED_DC | `c.dc = 16` | YES |
| OLED CS | GPIO17 | A9 | OLED_CS | `c.cs = 17` | YES |
| OLED clock | GPIO18 | A11 | OLED_SCLK | `c.sclk = 18` | YES |
| OLED reset | GPIO22 | A17 | OLED_RST | `c.reset = 22` | YES |
| OLED data | GPIO23 | A18 | OLED_MOSI | `c.mosi = 23` | YES |
| Up button (SW1) | GPIO25 | J11 | KEY_UP | `25 /* Up, J11 */` | YES |
| Down button (SW2) | GPIO27 | J9 | KEY_DOWN | `27 /* Down, J9 */` | YES |
| Left button (SW3) | GPIO19 | A12 | KEY_LEFT | `19 /* Left, A12 */` | YES |
| Right button (SW4) | GPIO21 | A14 | KEY_RIGHT | `21 /* Right, A14 */` | YES |
| Select button (SW5) | GPIO32 | J13 | KEY_SELECT | `32 /* Select, J13 */` | YES |
| Status ring data (via R1) | GPIO26 | J10 | RING_DIN | `STATUS_RING_PIN = 26` | YES |
| Display-type strap (to GND = OLED) | GPIO4 | A7 | GND | `HW_TYPE_STRAP_PIN = 4` | YES |
| Buzzer signal | GPIO33 | J12 | BUZZER | `BUZZER_PIN = 33` | YES |
| Ground | — | A13, A19, J6 | GND | Hardware ground | YES |
| 3.3 V rail | — | J19 | +3V3 | DevKit supply; not a GPIO | N/A |
| USB 5 V (status ring) | — | J1 | +5V | DevKit USB supply; not a GPIO | N/A |

Unused (NC) sockets: A1, A2, A3, A4, A5, A6, A10, A15, A16, J2, J3, J4, J5, J7, J8, J14, J15, J16, J17, J18.

Display header J2, in physical order (pin 1 at the top of the module header). Wire colors are the breadboard jumpers in the owner photos (2026-09-24), not a harness specification.

| Header pin | Silkscreen | Net | Wire color |
|---|---|---|---|
| 1 | GND | GND | olive |
| 2 | VCC | +3V3 | black |
| 3 | CLK | OLED_SCLK | white |
| 4 | MOSI | OLED_MOSI | gray |
| 5 | RES | OLED_RST | purple |
| 6 | DC | OLED_DC | blue |
| 7 | CS | OLED_CS | green |

Menu keys SW1-SW5: five discrete momentary pushbuttons, each from its GPIO (pin 1) to one shared GND rail (pin 2), using the ESP32 internal pull-ups; no resistors. On a 4-leg tactile switch, pins 1 and 2 are legs on opposite sides (across the gap). Firmware: Sigil env `sigil-oled`, `KEY_PINS` in `main.cpp`.

| Switch | Key | GPIO | DevKit socket (breadboard) | Net (pin 1) | Pin 2 |
|---|---|---|---|---|---|
| SW1 | Up | GPIO25 | J11 | KEY_UP | GND |
| SW2 | Down | GPIO27 | J9 | KEY_DOWN | GND |
| SW3 | Left | GPIO19 | A12 | KEY_LEFT | GND |
| SW4 | Right | GPIO21 | A14 | KEY_RIGHT | GND |
| SW5 | Select | GPIO32 | J13 | KEY_SELECT | GND |

Status ring cable J5 (JST-XH, 3 pins) to the Jewel adapter board, on USB 5 V. Data runs GPIO26 (J10, net RING_DIN) through U2 (74AHCT1G125, 5 V buffer, net RING_DIN_5V) and R1 (330 ohm) to DIN (net RING_DIN_R). Pin numbers are logical; the pads are labelled. Firmware caps brightness at 48/255.

| J5 pin | Signal | Net |
|---|---|---|
| 1 | +5V | +5V |
| 2 | DIN | RING_DIN_R |
| 3 | GND | GND |

Parts and footprints (U1 is from published Inland DevKit dimensions; caliper-check before ordering):

| Ref | Value | Footprint |
|---|---|---|
| C2 | 10uF | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` |
| C3 | 100nF | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` |
| J2 | INLAND 1.3" OLED HEADER (7-PIN) | `Connector_PinSocket_2.54mm:PinSocket_1x07_P2.54mm_Vertical` |
| J3 | BUZZER 2-PIN HEADER | `Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical` |
| J5 | STATUS RING CABLE (JST-XH 3) | `Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical` |
| R1 | 330R | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal` |
| SW1 | UP (GPIO25, J11) | `Button_Switch_THT:SW_PUSH_6mm` |
| SW2 | DOWN (GPIO27, J9) | `Button_Switch_THT:SW_PUSH_6mm` |
| SW3 | LEFT (GPIO19, A12) | `Button_Switch_THT:SW_PUSH_6mm` |
| SW4 | RIGHT (GPIO21, A14) | `Button_Switch_THT:SW_PUSH_6mm` |
| SW5 | SELECT (GPIO32, J13) | `Button_Switch_THT:SW_PUSH_6mm` |
| U1 | REMOVABLE ESP32 DEVKIT / 2 x 19 | `Sigil:ESP32_DevKit_38_Socket_Row22.86mm` |
| U2 | 74AHCT1G125 | `Package_TO_SOT_SMD:SOT-23-5` |

## Socket positions

| Socket position | DevKit silkscreen pin |
|---|---|
| A1 | CLK |
| A2 | SD0 |
| A3 | SD1 |
| A4 | GPIO15 |
| A5 | GPIO2 |
| A6 | GPIO0 |
| A7 | GPIO4 |
| A8 | GPIO16 |
| A9 | GPIO17 |
| A10 | GPIO5 |
| A11 | GPIO18 |
| A12 | GPIO19 |
| A13 | GND |
| A14 | GPIO21 |
| A15 | RXD0 |
| A16 | TXD0 |
| A17 | GPIO22 |
| A18 | GPIO23 |
| A19 | GND |
| J1 | 5V |
| J2 | CMD |
| J3 | SD3 |
| J4 | SD2 |
| J5 | GPIO13 |
| J6 | GND |
| J7 | GPIO12 |
| J8 | GPIO14 |
| J9 | GPIO27 |
| J10 | GPIO26 |
| J11 | GPIO25 |
| J12 | GPIO33 |
| J13 | GPIO32 |
| J14 | GPIO35 |
| J15 | GPIO34 |
| J16 | SVN |
| J17 | SVP |
| J18 | EN |
| J19 | 3V3 |

Unused means no carrier connection; onboard flash, UART, BOOT and EN circuitry may still use these signals.

Validation: 38 unique socket positions per schematic; display, joystick and status ring (both), pushbuttons (OLED) display strap, U2/R1/C2/C3 and buzzer nets match firmware; power and all three grounds connected; every part has a footprint; every other socket explicitly NC; no dangling named nets. Peripheral interfaces remain unresolved; see README.md.

## Jewel adapter (Sigil_JewelAdapter.kicad_sch)

The Jewel is soldered on pins on this board, LEDs up, with C1 (470 uF), a 3-wire JST-XH pigtail in J2 whose order matches J5 on the Sigil boards, and an optional chain-out J3 (+5V, DOUT, GND) for more pixels. Pad positions: Adafruit-NeoPixel-Jewel-7 board file.

| Ref | Pin | Name | Net |
|---|---|---|---|
| C1 | 1 | ~_1 | +5V |
| J1 | 1 | PWR_1 | +5V |
| J2 | 1 | +5V_1 | +5V |
| J3 | 1 | +5V_1 | +5V |
| C1 | 2 | ~_2 | GND |
| J1 | 2 | GND_2 | GND |
| J1 | 5 | GND_5 | GND |
| J2 | 3 | GND_3 | GND |
| J3 | 3 | GND_3 | GND |
| J1 | 3 | DIN_3 | RING_DIN_R |
| J2 | 2 | DIN_2 | RING_DIN_R |
| J1 | 4 | DOUT_4 | RING_DOUT |
| J3 | 2 | DOUT_2 | RING_DOUT |
