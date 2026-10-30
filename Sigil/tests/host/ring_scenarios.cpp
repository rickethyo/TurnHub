// LED ring patterns (led_ring.h): what each Sigil state shows, and the
// current cap that keeps the ring inside the USB budget.
#include <assert.h>
#include <stdio.h>

#include "led_ring.h"

using namespace TurnHubSigil;
using namespace TurnHubProtocol;

namespace {
uint32_t sum(const RingPixel *f, uint8_t n) {
  uint32_t s = 0;
  for (uint8_t i = 0; i < n; ++i) s += f[i].r + f[i].g + f[i].b;
  return s;
}
uint8_t lit(const RingPixel *f, uint8_t n) {
  uint8_t c = 0;
  for (uint8_t i = 0; i < n; ++i) c += (f[i].r | f[i].g | f[i].b) ? 1 : 0;
  return c;
}
}  // namespace

int main() {
  constexpr uint8_t N = 12;
  RingPixel f[RING_MAX_LEDS];
  const uint32_t budget = static_cast<uint32_t>(RING_BUDGET_MA) * 255 / RING_MA_PER_CHANNEL;
  RingInput in;

  // Unpaired, pairing and Atlas lost each spin one bright pixel and a tail.
  renderRing(in, 0, f, N);
  assert(lit(f, N) == 2);
  in.pairing = true;
  renderRing(in, 0, f, N);
  assert(lit(f, N) == 2 && f[0].b > f[0].r);
  in.pairing = false; in.paired = true; in.atlasLost = true;
  renderRing(in, 0, f, N);
  assert(lit(f, N) == 2 && f[0].r > f[0].b);
  in.atlasLost = false;

  // This Sigil's turn: every LED in the seat colour, under the current cap.
  in.state = encodeDisplayState(DisplayMode::Running, 3, 0, 1, DISPLAY_FLAG_ACTIVE);
  renderRing(in, 0, f, N);
  assert(lit(f, N) == N && sum(f, N) <= budget);
  const RingPixel seat = f[0];
  // A 24-LED ring at the same colour is scaled harder to stay in budget.
  renderRing(in, 0, f, RING_MAX_LEDS);
  assert(lit(f, RING_MAX_LEDS) == RING_MAX_LEDS && sum(f, RING_MAX_LEDS) <= budget);
  // Another player's turn: dark.
  in.state = encodeDisplayState(DisplayMode::Running, 3, 0, 1);
  renderRing(in, 0, f, N);
  assert(lit(f, N) == 0);
  // Attention pulses amber over anything else.
  in.state = encodeDisplayState(DisplayMode::Running, 3, 0, 1, DISPLAY_FLAG_ACTIVE | DISPLAY_FLAG_ATTENTION);
  renderRing(in, 400, f, N);
  assert(lit(f, N) == N && f[0].r > f[0].g && f[0].b == 0);
  // Lobby breathes dimly in the seat hue; paused lights every other LED.
  in.state = encodeDisplayState(DisplayMode::Lobby, 3, 0, 0);
  renderRing(in, 1200, f, N);
  assert(lit(f, N) == N && sum(f, N) < sum(&seat, 1) * N);
  in.state = encodeDisplayState(DisplayMode::Paused, 3, 0, 2);
  renderRing(in, 0, f, N);
  assert(lit(f, N) == N / 2);
  // The winner gets a chase; a loser's ring is dark.
  in.state = encodeDisplayState(DisplayMode::GameOver, 3, 0, 9, DISPLAY_FLAG_WINNER);
  renderRing(in, 0, f, N);
  assert(lit(f, N) == N && sum(f, N) <= budget);
  in.state = encodeDisplayState(DisplayMode::GameOver, 3, 0, 9);
  renderRing(in, 0, f, N);
  assert(lit(f, N) == 0);
  // A 7-LED jewel: the spinner runs round LEDs 1-6 and leaves the centre dark;
  // the turn fill lights the centre too.
  in = RingInput{};
  renderRing(in, 0, f, 7, true);
  assert(lit(f, 7) == 2 && !(f[0].r | f[0].g | f[0].b));
  in.paired = true;
  in.state = encodeDisplayState(DisplayMode::Running, 3, 0, 1, DISPLAY_FLAG_ACTIVE);
  renderRing(in, 0, f, 7, true);
  assert(lit(f, 7) == 7 && f[0].r == f[1].r && sum(f, 7) <= budget);
  // No ring fitted (count 0) is a no-op.
  renderRing(in, 0, f, 0);

  puts("PASS ring: spinners, turn colour, attention, lobby, pause, winner, current cap, jewel centre");
  return 0;
}
