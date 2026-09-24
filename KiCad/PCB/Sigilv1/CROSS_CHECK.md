# Sigil Rev A cross-check tables

Verified against exported KiCad netlist, current working firmware, and the user-supplied rear-photo sequence. YES means GPIO/socket/net consistency; it does not verify peripheral parts or mechanical dimensions.

## Table 1

| Function | GPIO | DevKit socket position | Schematic net | Firmware definition | Match? |
|---|---|---|---|---|---|
| PAIR | GPIO19 | J18 | PAIR | `PAIR_BUTTON` = 19 | YES |
| Pass | GPIO26 | A20 | BTN_PASS | `PASS_BUTTON` = 26 | YES |
| Action | GPIO25 | A19 | BTN_ACTION | `ACTION_BUTTON` = 25 | YES |
| Pause / Win | GPIO32 | A17 | BTN_PAUSE | `PAUSE_WIN_BUTTON` = 32 | YES |
| Red LED | GPIO13 | A25 | LED_RED | `RED_LED` = 13 | YES |
| Green LED | GPIO14 | A22 | LED_GREEN | `GREEN_LED` = 14 | YES |
| Blue LED | GPIO27 | A21 | LED_BLUE | `BLUE_LED` = 27 | YES |
| Buzzer signal | GPIO33 | A18 | BUZZER | `BUZZER_PIN` = 33 | YES |
| EPD DC | GPIO16 | J22 | EPD_DC | `EPD_DC` = 16 | YES |
| EPD CS | GPIO17 | J21 | EPD_CS | `EPD_CS` = 17 | YES |
| EPD clock | GPIO18 | J19 | EPD_SCLK | `SPI.begin(18, 19, 23, EPD_CS)` | YES |
| EPD BUSY | GPIO21 | J16 | EPD_BUSY | `EPD_BUSY` = 21 | YES |
| EPD reset | GPIO22 | J13 | EPD_RST | `EPD_RST` = 22 | YES |
| EPD data | GPIO23 | J12 | EPD_MOSI | `SPI.begin(18, 19, 23, EPD_CS)` | YES |
| Ground | — | J17, J11, A24 | GND | Hardware ground | YES |
| 3.3 V rail | — | A11 | +3V3 | DevKit supply; not a GPIO | N/A |

**PAIR: J18 / GPIO19 / PAIR; SW4 closes to GND, including J17. INPUT_PULLUP: released HIGH, pressed LOW. GPIO19 is detached from SPI MISO.**

**Pause / Win: A17 / GPIO32 / BTN_PAUSE; SW5 closes to GND. INPUT_PULLUP: released HIGH, pressed LOW. SW3 stays retired (it was the old GPIO4 auxiliary).**

## Table 2

| Socket position | DevKit silkscreen pin | Connected Sigil function | Used/Unused |
|---|---|---|---|
| J29 | CLK | — | Unused (carrier NC) |
| J28 | SD0 | — | Unused (carrier NC) |
| J27 | SD1 | — | Unused (carrier NC) |
| J26 | GPIO15 | — | Unused (carrier NC) |
| J25 | GPIO2 | — | Unused (carrier NC) |
| J24 | GPIO0 | — | Unused (carrier NC) |
| J23 | GPIO4 | — | Unused (carrier NC) |
| J22 | GPIO16 | EPD DC | Used |
| J21 | GPIO17 | EPD CS | Used |
| J20 | GPIO5 | — | Unused (carrier NC) |
| J19 | GPIO18 | EPD clock | Used |
| J18 | GPIO19 | PAIR | Used |
| J17 | GND | Ground | Used |
| J16 | GPIO21 | EPD BUSY | Used |
| J15 | RXD0 | — | Unused (carrier NC) |
| J14 | TXD0 | — | Unused (carrier NC) |
| J13 | GPIO22 | EPD reset | Used |
| J12 | GPIO23 | EPD data | Used |
| J11 | GND | Ground | Used |
| A29 | 5V | — | Unused (carrier NC) |
| A28 | CMD | — | Unused (carrier NC) |
| A27 | SD3 | — | Unused (carrier NC) |
| A26 | SD2 | — | Unused (carrier NC) |
| A25 | GPIO13 | Red LED | Used |
| A24 | GND | Ground | Used |
| A23 | GPIO12 | — | Unused (carrier NC) |
| A22 | GPIO14 | Green LED | Used |
| A21 | GPIO27 | Blue LED | Used |
| A20 | GPIO26 | Pass | Used |
| A19 | GPIO25 | Action | Used |
| A18 | GPIO33 | Buzzer signal | Used |
| A17 | GPIO32 | Pause / Win | Used |
| A16 | GPIO35 | — | Unused (carrier NC) |
| A15 | GPIO34 | — | Unused (carrier NC) |
| A14 | SVN | — | Unused (carrier NC) |
| A13 | SVP | — | Unused (carrier NC) |
| A12 | EN | — | Unused (carrier NC) |
| A11 | 3V3 | 3.3 V rail | Used |

Unused means no carrier connection; onboard flash, UART, BOOT and EN circuitry may still use these signals.

Validation: 38 unique socket positions; 14 firmware signal mappings; all four active-low switches; three 330R resistor/anode/cathode chains; all display logical signals; buzzer logical interface; all unused carrier pins explicitly NC. No dangling named nets. Peripheral interfaces remain unresolved; see README.md.
