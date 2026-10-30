// WS2812 LED ring driver for the E32R28T (led_ring.h holds what it shows).
// The RMT peripheral shapes every bit in hardware, so Wi-Fi interrupts and
// flash cache stalls cannot stretch one (a bit-banged first try showed random
// colours on the bench, 2026-10-30). The whole frame fits in RMT memory and is
// sent in one go, without refill interrupts.
#include <Arduino.h>
#include <string.h>

#include "led_ring.h"
#include "sigil_board.h"

namespace TurnHubSigil {

namespace {
// Room for RGBW (SK6812) rings, which take a fourth, white byte per LED.
constexpr size_t RING_BITS = static_cast<size_t>(TurnHubSigilBoard::LED_RING_COUNT) * 32;
static_assert(RING_BITS + 1 <= 512, "The ring frame must fit in RMT memory (15 LEDs at most)");
rmt_obj_t *ringRmt = nullptr;
rmt_data_t ringData[RING_BITS + 1];
bool ringRgbw = TURNHUB_RING_RGBW;
// Bench test (serial "ring test"): LED 1 red, 2 green, 3 blue, 4 white, the
// rest off, for 20 s (5 s at boot). Shows the ring's byte order and RGB/RGBW at a glance.
uint32_t ringTestUntilMs = 0;
}  // namespace

bool ringHandleCommand(const char *line) {
  if (strcmp(line, "ring test") == 0) {
    ringTestUntilMs = millis() + 20000;
  } else if (strcmp(line, "ring rgbw") == 0) {
    ringRgbw = true;
  } else if (strcmp(line, "ring rgb") == 0) {
    ringRgbw = false;
  } else {
    return false;
  }
  Serial.printf("SIGIL|RING|%s|TEST|%u\n", ringRgbw ? "RGBW" : "RGB", ringTestUntilMs > millis() ? 1 : 0);
  return true;
}

void ringBegin() {
  const auto blocks = static_cast<rmt_reserve_memsize_t>((RING_BITS + 1 + 63) / 64);
  ringRmt = rmtInit(TurnHubSigilBoard::LED_RING_PIN, RMT_TX_MODE, blocks);
  if (ringRmt == nullptr) {
    Serial.println("SIGIL|RING|RMT|ERROR");
    return;
  }
  rmtSetTick(ringRmt, 100);  // 0.1 us ticks.
  ringTestUntilMs = millis() + 5000;  // The test pattern shows briefly at boot.
  Serial.printf("SIGIL|RING|READY|GPIO|%u|LEDS|%u\n", TurnHubSigilBoard::LED_RING_PIN,
      TurnHubSigilBoard::LED_RING_COUNT);
}

void ringShow(const RingPixel *frame, uint8_t count) {
  if (ringRmt == nullptr) return;
  if (count > TurnHubSigilBoard::LED_RING_COUNT) count = TurnHubSigilBoard::LED_RING_COUNT;
  const bool testing = static_cast<int32_t>(ringTestUntilMs - millis()) > 0;
  size_t n = 0;
  for (uint8_t i = 0; i < count; ++i) {
    RingPixel p = frame[i];
    uint8_t white = 0;
    if (testing) {
      p = {0, 0, 0};
      if (i == 0) p.r = 40;
      if (i == 1) p.g = 40;
      if (i == 2) p.b = 40;
      if (i == 3) { p = {40, 40, 40}; white = 40; }
    }
    const uint8_t bytes[4] = {p.g, p.r, p.b, white};  // GRB(W) on the wire.
    for (uint8_t k = 0; k < (ringRgbw ? 4 : 3); ++k) {
      const uint8_t byte = bytes[k];
      for (uint8_t bit = 0x80; bit; bit >>= 1) {
        const bool one = byte & bit;
        ringData[n].level0 = 1;
        ringData[n].duration0 = one ? 8 : 4;  // T1H 0.8 us, T0H 0.4 us.
        ringData[n].level1 = 0;
        ringData[n].duration1 = one ? 4 : 8;
        ++n;
      }
    }
  }
  // Holds the line low past the 50 us latch (the last item's low half).
  ringData[n].level0 = 0;
  ringData[n].duration0 = 600;
  ringData[n].level1 = 0;
  ringData[n].duration1 = 0;
  rmtWriteBlocking(ringRmt, ringData, n + 1);
}

}  // namespace TurnHubSigil
