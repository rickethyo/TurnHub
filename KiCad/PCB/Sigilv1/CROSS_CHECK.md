# Sigil Rev A cross-check tables

Verified against exported KiCad netlist, current working firmware, and the user-supplied rear-photo sequence. YES means GPIO/socket/net consistency; it does not verify peripheral parts or mechanical dimensions.

## Table 1

| Function | GPIO | DevKit socket position | Schematic net | Firmware definition | Match? |
|---|---|---|---|---|---|
| PAIR | GPIO19 | A12 | PAIR | `PAIR_BUTTON` = 19 | YES |
| Pass | GPIO26 | J10 | BTN_PASS | `PASS_BUTTON` = 26 | YES |
| Action | GPIO25 | J11 | BTN_ACTION | `ACTION_BUTTON` = 25 | YES |
| Red LED | GPIO13 | J5 | LED_RED | `RED_LED` = 13 | YES |
| Green LED | GPIO14 | J8 | LED_GREEN | `GREEN_LED` = 14 | YES |
| Blue LED | GPIO27 | J9 | LED_BLUE | `BLUE_LED` = 27 | YES |
| Buzzer signal | GPIO33 | J12 | BUZZER | `BUZZER_PIN` = 33 | YES |
| EPD DC | GPIO16 | A8 | EPD_DC | `EPD_DC` = 16 | YES |
| EPD CS | GPIO17 | A9 | EPD_CS | `EPD_CS` = 17 | YES |
| EPD clock | GPIO18 | A11 | EPD_SCLK | `SPI.begin(18, 19, 23, EPD_CS)` | YES |
| EPD BUSY | GPIO21 | A14 | EPD_BUSY | `EPD_BUSY` = 21 | YES |
| EPD reset | GPIO22 | A17 | EPD_RST | `EPD_RST` = 22 | YES |
| EPD data | GPIO23 | A18 | EPD_MOSI | `SPI.begin(18, 19, 23, EPD_CS)` | YES |
| Ground | — | A13, A19, J6 | GND | Hardware ground | YES |
| 3.3 V rail | — | J19 | +3V3 | DevKit supply; not a GPIO | N/A |

**PAIR: A12 / GPIO19 / PAIR; SW4 closes to GND, including A13. INPUT_PULLUP: released HIGH, pressed LOW. GPIO19 is detached from SPI MISO.**

## Table 2

| Socket position | DevKit silkscreen pin | Connected Sigil function | Used/Unused |
|---|---|---|---|
| A1 | CLK | — | Unused (carrier NC) |
| A2 | SD0 | — | Unused (carrier NC) |
| A3 | SD1 | — | Unused (carrier NC) |
| A4 | GPIO15 | — | Unused (carrier NC) |
| A5 | GPIO2 | — | Unused (carrier NC) |
| A6 | GPIO0 | — | Unused (carrier NC) |
| A7 | GPIO4 | — | Unused (carrier NC) |
| A8 | GPIO16 | EPD DC | Used |
| A9 | GPIO17 | EPD CS | Used |
| A10 | GPIO5 | — | Unused (carrier NC) |
| A11 | GPIO18 | EPD clock | Used |
| A12 | GPIO19 | PAIR | Used |
| A13 | GND | Ground | Used |
| A14 | GPIO21 | EPD BUSY | Used |
| A15 | RXD0 | — | Unused (carrier NC) |
| A16 | TXD0 | — | Unused (carrier NC) |
| A17 | GPIO22 | EPD reset | Used |
| A18 | GPIO23 | EPD data | Used |
| A19 | GND | Ground | Used |
| J1 | 5V | — | Unused (carrier NC) |
| J2 | CMD | — | Unused (carrier NC) |
| J3 | SD3 | — | Unused (carrier NC) |
| J4 | SD2 | — | Unused (carrier NC) |
| J5 | GPIO13 | Red LED | Used |
| J6 | GND | Ground | Used |
| J7 | GPIO12 | — | Unused (carrier NC) |
| J8 | GPIO14 | Green LED | Used |
| J9 | GPIO27 | Blue LED | Used |
| J10 | GPIO26 | Pass | Used |
| J11 | GPIO25 | Action | Used |
| J12 | GPIO33 | Buzzer signal | Used |
| J13 | GPIO32 | — | Unused (carrier NC) |
| J14 | GPIO35 | — | Unused (carrier NC) |
| J15 | GPIO34 | — | Unused (carrier NC) |
| J16 | SVN | — | Unused (carrier NC) |
| J17 | SVP | — | Unused (carrier NC) |
| J18 | EN | — | Unused (carrier NC) |
| J19 | 3V3 | 3.3 V rail | Used |

Unused means no carrier connection; onboard flash, UART, BOOT and EN circuitry may still use these signals.

Validation: 38 unique socket positions; 13 firmware signal mappings; all three active-low switches; three 330R resistor/anode/cathode chains; all display logical signals; buzzer logical interface; all unused carrier pins explicitly NC. No dangling named nets. Peripheral interfaces remain unresolved; see README.md.
