#pragma once
// The board boundary: every GPIO the Sigil firmware drives. Master builds
// for a generic ESP32 dev board with the unified Sigil's parts (thumbstick,
// Jewel ring, buzzer) on these pins and no display; the ESP32-S3 Sigil board
// replaces this file. The Gen 1 boards' wiring stays on legacy/gen1.
#include <stdint.h>

namespace TurnHubSigilBoard {

// Status light: a 7-pixel NeoPixel ring's data input.
constexpr uint8_t STATUS_RING_PIN = 26;
// Analog thumbstick (3.3 V supply). X/Y must be ADC1 pins: ADC2 is unusable
// while ESP-NOW has the radio. Clicking it is Select and wakes from sleep, so
// it must be an RTC GPIO.
constexpr uint8_t JOYSTICK_X_PIN = 34;
constexpr uint8_t JOYSTICK_Y_PIN = 35;
constexpr uint8_t JOYSTICK_SELECT_PIN = 32;
constexpr uint8_t BUZZER_PIN = 33;
// The dev board's BOOT button (GPIO0). It is a strapping pin only at reset,
// so Pair works from boot onward.
constexpr uint8_t PAIR_BUTTON_PIN = 0;

}  // namespace TurnHubSigilBoard
