// WS2812 LED ring driver for the E32R28T (led_ring.h holds what it shows).
// Bit-banged against the CPU cycle counter, so it needs no library or RMT
// channel; a 12-LED frame holds interrupts on this core for about 0.4 ms.
#include <Arduino.h>
#include <soc/gpio_struct.h>

#include "led_ring.h"
#include "sigil_board.h"

namespace TurnHubSigil {

void ringBegin() {
  pinMode(TurnHubSigilBoard::LED_RING_PIN, OUTPUT);
  digitalWrite(TurnHubSigilBoard::LED_RING_PIN, LOW);
}

void ringShow(const RingPixel *frame, uint8_t count) {
  static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  const uint32_t mask = 1u << TurnHubSigilBoard::LED_RING_PIN;
  const uint32_t mhz = getCpuFrequencyMhz();
  const uint32_t t0h = mhz * 400 / 1000, t1h = mhz * 800 / 1000, period = mhz * 1250 / 1000;
  portENTER_CRITICAL(&mux);
  uint32_t start = ESP.getCycleCount();
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t bytes[3] = {frame[i].g, frame[i].r, frame[i].b};  // GRB on the wire.
    for (uint8_t byte : bytes) {
      for (uint8_t bit = 0x80; bit; bit >>= 1) {
        while (ESP.getCycleCount() - start < period) {}
        start = ESP.getCycleCount();
        GPIO.out_w1ts = mask;
        const uint32_t high = (byte & bit) ? t1h : t0h;
        while (ESP.getCycleCount() - start < high) {}
        GPIO.out_w1tc = mask;
      }
    }
  }
  portEXIT_CRITICAL(&mux);
  // The 50 us reset gap passes before the next frame (sent every 20 ms).
}

}  // namespace TurnHubSigil
