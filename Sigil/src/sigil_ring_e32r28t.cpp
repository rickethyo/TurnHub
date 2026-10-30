// WS2812 LED ring driver for the E32R28T (led_ring.h holds what it shows).
// The RMT peripheral shapes every bit in hardware, so Wi-Fi interrupts and
// flash cache stalls cannot stretch one (a bit-banged first try showed random
// colours on the bench, 2026-10-30). The whole frame fits in RMT memory and is
// sent in one go, without refill interrupts.
#include <Arduino.h>

#include "led_ring.h"
#include "sigil_board.h"

namespace TurnHubSigil {

namespace {
constexpr size_t RING_BITS = static_cast<size_t>(TurnHubSigilBoard::LED_RING_COUNT) * 24;
static_assert(RING_BITS + 1 <= 512, "The ring frame must fit in RMT memory (21 LEDs at most)");
rmt_obj_t *ringRmt = nullptr;
rmt_data_t ringData[RING_BITS + 1];
}  // namespace

void ringBegin() {
  const auto blocks = static_cast<rmt_reserve_memsize_t>((RING_BITS + 1 + 63) / 64);
  ringRmt = rmtInit(TurnHubSigilBoard::LED_RING_PIN, RMT_TX_MODE, blocks);
  if (ringRmt == nullptr) {
    Serial.println("SIGIL|RING|RMT|ERROR");
    return;
  }
  rmtSetTick(ringRmt, 100);  // 0.1 us ticks.
  Serial.printf("SIGIL|RING|READY|GPIO|%u|LEDS|%u\n", TurnHubSigilBoard::LED_RING_PIN,
      TurnHubSigilBoard::LED_RING_COUNT);
}

void ringShow(const RingPixel *frame, uint8_t count) {
  if (ringRmt == nullptr) return;
  if (count > TurnHubSigilBoard::LED_RING_COUNT) count = TurnHubSigilBoard::LED_RING_COUNT;
  size_t n = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t bytes[3] = {frame[i].g, frame[i].r, frame[i].b};  // GRB on the wire.
    for (uint8_t byte : bytes) {
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
