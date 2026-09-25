#include "status_ring.h"

#if TURNHUB_STATUS_RING

#include <Adafruit_NeoPixel.h>
#include <new>

namespace TurnHubSigil {
namespace {
// Each RGBW pixel draws up to ~80 mA with all four chips at full, about
// 560 mA for the ring: more than a USB port supplies. 48/255 keeps the
// worst case this firmware can request (white, RGB only) near 70 mA.
constexpr uint8_t RING_BRIGHTNESS = 48;

Adafruit_NeoPixel *ring = nullptr;
LedFrame shown;
}  // namespace

void statusRingBegin(uint8_t pin) {
  if (ring) return;
  ring = new (std::nothrow) Adafruit_NeoPixel(LED_PIXELS, pin, NEO_GRBW + NEO_KHZ800);
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

#endif  // TURNHUB_STATUS_RING
