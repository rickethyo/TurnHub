#include "status_ring.h"

#if defined(TURNHUB_BOARD_E32R28T)
#include <Arduino.h>

#include "sigil_board.h"

// The touch Sigil stand-in has no ring: its one on-board RGB LED (common
// anode, so each channel is active low) shows the frame's average colour.
namespace TurnHubSigil {
namespace {
constexpr uint8_t CHANNELS[3] = {0, 1, 2};  // LEDC; the buzzer is on 7, the backlight on 4.
constexpr uint8_t PINS[3] = {TurnHubSigilBoard::RGB_RED_PIN, TurnHubSigilBoard::RGB_GREEN_PIN,
    TurnHubSigilBoard::RGB_BLUE_PIN};
// The LED is bright and close to the eye.
constexpr uint8_t LED_BRIGHTNESS = 96;
bool started = false;
Rgb shown;
}  // namespace

void statusRingBegin(uint8_t) {
  for (uint8_t i = 0; i < 3; ++i) {
    ledcSetup(CHANNELS[i], 5000, 8);
    ledcAttachPin(PINS[i], CHANNELS[i]);
    ledcWrite(CHANNELS[i], 255);  // Off.
  }
  started = true;
  shown = Rgb();
}

void statusRingShow(const LedFrame &frame) {
  if (!started) return;
  uint32_t sum[3] = {};
  for (uint8_t i = 0; i < LED_PIXELS; ++i) {
    sum[0] += frame.pixels[i].r;
    sum[1] += frame.pixels[i].g;
    sum[2] += frame.pixels[i].b;
  }
  const Rgb mean{static_cast<uint8_t>(sum[0] / LED_PIXELS), static_cast<uint8_t>(sum[1] / LED_PIXELS),
      static_cast<uint8_t>(sum[2] / LED_PIXELS)};
  if (mean == shown) return;
  shown = mean;
  const uint8_t level[3] = {mean.r, mean.g, mean.b};
  for (uint8_t i = 0; i < 3; ++i) ledcWrite(CHANNELS[i], 255 - level[i] * LED_BRIGHTNESS / 255);
}

}  // namespace TurnHubSigil

#else

#include <Adafruit_NeoPixel.h>
#include <new>

namespace TurnHubSigil {
namespace {
// Each RGBW pixel draws up to ~80 mA with all four chips at full, about
// 560 mA for the ring: more than a USB port supplies. 48/255 keeps the
// worst case this firmware can request (white, RGB only) near 70 mA.
constexpr uint8_t RING_BRIGHTNESS = 48;
constexpr neoPixelType RING_TYPE = NEO_GRBW + NEO_KHZ800;  // The Jewel 7 is RGBW.

Adafruit_NeoPixel *ring = nullptr;
LedFrame shown;
}  // namespace

void statusRingBegin(uint8_t pin) {
  if (ring) return;
  ring = new (std::nothrow) Adafruit_NeoPixel(LED_PIXELS, pin, RING_TYPE);
  if (!ring) return;
  ring->begin();
  ring->setBrightness(RING_BRIGHTNESS);
  ring->clear();  // Start dark: no inrush from a full-brightness first frame.
  ring->show();
  shown = LedFrame();
}

void statusRingShow(const LedFrame &frame) {
  if (!ring) return;
  bool changed = false;
  for (uint8_t i = 0; i < LED_PIXELS; ++i) {
    if (frame.pixels[i] == shown.pixels[i]) continue;
    const Rgb &p = frame.pixels[i];
    ring->setPixelColor(i, Adafruit_NeoPixel::Color(p.r, p.g, p.b, 0));
    shown.pixels[i] = p;
    changed = true;
  }
  if (changed) ring->show();
}

}  // namespace TurnHubSigil

#endif  // TURNHUB_BOARD_E32R28T
