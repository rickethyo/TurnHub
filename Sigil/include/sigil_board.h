#pragma once
// Touchscreen-only Sigil board boundary. The E32R28T is the supported
// touch target until the S3 Sigil driver is implemented. The Gen 1 Sigils'
// firmware remains on legacy/gen1.
#include <stdint.h>
#if !defined(TURNHUB_BOARD_E32R28T)
#error "Select a supported Sigil touchscreen board"
#endif
namespace TurnHubSigilBoard {
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
// Optional WS2812 LED ring (led_ring.h): data on IO27, the SPI connector's CS
// (free: the case has no microSD); 5V and GND from the UART header.
#ifndef TURNHUB_RING_LEDS
#define TURNHUB_RING_LEDS 7  // The bench jewel: one centre LED and six round it.
#endif
#ifndef TURNHUB_RING_RGBW
#define TURNHUB_RING_RGBW 1  // SK6812 RGBW (four bytes per LED), the bench ring; 0 for WS2812.
#endif
#ifndef TURNHUB_RING_CENTRE
#define TURNHUB_RING_CENTRE 1  // LED 0 is the jewel's centre.
#endif
constexpr uint8_t LED_RING_PIN = 27;
constexpr uint8_t LED_RING_COUNT = TURNHUB_RING_LEDS;
constexpr bool LED_RING_CENTRE = TURNHUB_RING_CENTRE;

}  // namespace TurnHubSigilBoard
