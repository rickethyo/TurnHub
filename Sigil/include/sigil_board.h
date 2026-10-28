#pragma once
// The board boundary: every GPIO the Sigil firmware drives. Master builds
// for a generic ESP32 dev board with the unified Sigil's parts (thumbstick,
// Jewel ring, buzzer) on these pins and no display; the ESP32-S3 Sigil board
// replaces this file. The Gen 1 Sigils' wiring stays on legacy/gen1.
#include <stdint.h>

namespace TurnHubSigilBoard {

#if defined(TURNHUB_BOARD_E32R28T)
// The Gen 1 Atlas board (E32R28T: classic ESP32, 2.8-inch ILI9341 240 x 320
// with an XPT2046 resistive touch panel) as a stand-in for the S3 Sigil. The
// screen's keys replace the thumbstick (sigil_touch_pad.h), the on-board RGB
// LED stands in for the ring, and the speaker amp is the buzzer. Pins from
// legacy/gen1's Atlas config.h.
constexpr bool HAS_THUMBSTICK = false;
constexpr uint8_t STATUS_RING_PIN = 22;  // The RGB LED's red channel (status_ring.cpp).
constexpr uint8_t RGB_RED_PIN = 22;       // Common anode: active low.
constexpr uint8_t RGB_GREEN_PIN = 16;
constexpr uint8_t RGB_BLUE_PIN = 17;
constexpr uint8_t JOYSTICK_X_PIN = 34;    // Unused (HAS_THUMBSTICK).
constexpr uint8_t JOYSTICK_Y_PIN = 35;
// Select is a screen key like the directions (main.cpp's virtual pins).
constexpr uint8_t JOYSTICK_SELECT_PIN = 0xE4;
constexpr uint8_t BUZZER_PIN = 26;        // The amp's input.
constexpr int8_t AUDIO_ENABLE_PIN = 4;    // The amp's enable, active low.
constexpr uint8_t PAIR_BUTTON_PIN = 0;    // BOOT.
constexpr int8_t TFT_SCLK_PIN = 14;
constexpr int8_t TFT_MOSI_PIN = 13;
constexpr int8_t TFT_MISO_PIN = 12;
constexpr int8_t TFT_CS_PIN = 15;
constexpr int8_t TFT_DC_PIN = 2;
constexpr int8_t TFT_BACKLIGHT_PIN = 21;
constexpr uint8_t TOUCH_SCLK_PIN = 25;
constexpr uint8_t TOUCH_MOSI_PIN = 32;
constexpr uint8_t TOUCH_MISO_PIN = 39;
constexpr uint8_t TOUCH_CS_PIN = 33;
constexpr uint8_t TOUCH_IRQ_PIN = 36;
#else
constexpr bool HAS_THUMBSTICK = true;
constexpr int8_t AUDIO_ENABLE_PIN = -1;

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
#endif

}  // namespace TurnHubSigilBoard
