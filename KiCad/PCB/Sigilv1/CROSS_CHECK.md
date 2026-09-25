# Sigil Rev A cross-check tables

Verified against exported KiCad netlists, current firmware, and the user-supplied rear-photo sequence. YES means GPIO/socket/net consistency; it does not verify peripheral parts or mechanical dimensions.

LEDs and the old buttons are not on either schematic while the controls are redesigned; their GPIOs are explicitly NC. The E-ink schematic carries the analog joystick (J4) and the NeoPixel status ring (J5) instead; the OLED schematic carries five discrete pushbuttons (SW1-SW5) and the same ring.

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
| USB 5 V (status ring) | — | J1 | +5V | DevKit USB supply; not a GPIO | N/A |

Unused (NC) sockets: A1, A2, A3, A4, A5, A6, A7, A10, A12, A15, A16, J2, J3, J4, J5, J7, J8, J9, J11, J16, J17, J18.

Display header J2, in physical order (pin 1 at the top of the module header). Wire colours are the breadboard jumpers in the owner photos (2026-09-24), not a harness specification.

| Header pin | Silkscreen | Net | Wire colour |
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

Status ring J5: Adafruit NeoPixel Jewel 7 RGBW on USB 5 V. Data runs GPIO26 (J10, net RING_DIN) through R1 (330 ohm, at the ring) to DIN (net RING_DIN_R). Pin numbers are logical; the pads are labelled. Firmware caps brightness at 48/255.

| Header pin | Pad label | Net |
|---|---|---|
| 1 | PWR | +5V |
| 2 | GND | GND |
| 3 | DIN | RING_DIN_R |
| 4 | DOUT | NC |

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
| Buzzer signal | GPIO33 | J12 | BUZZER | `BUZZER_PIN = 33` | YES |
| Ground | — | A13, A19, J6 | GND | Hardware ground | YES |
| 3.3 V rail | — | J19 | +3V3 | DevKit supply; not a GPIO | N/A |
| USB 5 V (status ring) | — | J1 | +5V | DevKit USB supply; not a GPIO | N/A |

Unused (NC) sockets: A1, A2, A3, A4, A5, A6, A7, A10, A15, A16, J2, J3, J4, J5, J7, J8, J14, J15, J16, J17, J18.

Display header J2, in physical order (pin 1 at the top of the module header). Wire colours are the breadboard jumpers in the owner photos (2026-09-24), not a harness specification.

| Header pin | Silkscreen | Net | Wire colour |
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

Status ring J5: Adafruit NeoPixel Jewel 7 RGBW on USB 5 V. Data runs GPIO26 (J10, net RING_DIN) through R1 (330 ohm, at the ring) to DIN (net RING_DIN_R). Pin numbers are logical; the pads are labelled. Firmware caps brightness at 48/255.

| Header pin | Pad label | Net |
|---|---|---|
| 1 | PWR | +5V |
| 2 | GND | GND |
| 3 | DIN | RING_DIN_R |
| 4 | DOUT | NC |

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

Validation: 38 unique socket positions per schematic; display, joystick and status ring (both), pushbuttons (OLED) and buzzer nets match firmware; power and all three grounds connected; every other socket explicitly NC; no buttons, LEDs or dangling named nets. Peripheral interfaces remain unresolved; see README.md.
