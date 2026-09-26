#include "sigil_led.h"
#include <cassert>
#include <iostream>

using namespace TurnHubSigil;
using namespace TurnHubProtocol;

static int32_t led(LedCue cue, uint8_t overlays = 0, uint8_t player = 0, uint8_t seat = 1,
    bool shared = false, LedStyle style = LedStyle::Default, uint32_t ageMs = 0) {
  LedStateFields f;
  f.cue = cue; f.overlays = overlays; f.playerNumber = player; f.seatSlot = seat;
  f.sharedSeat = shared; f.style = style; f.anchorAgeMs = ageMs;
  return encodeLedState(f);
}
static uint8_t bit(LedOverlay o) { return static_cast<uint8_t>(1u << static_cast<uint8_t>(o)); }
static bool dark(const Rgb &c) { return c.r == 0 && c.g == 0 && c.b == 0; }
static unsigned lit(const LedFrame &f) {
  unsigned n = 0;
  for (uint8_t i = 1; i < LED_PIXELS; ++i) n += !dark(f.pixels[i]);
  return n;
}

int main() {
  // Wire format round trip, clamping and the change key.
  {
    const int32_t v = led(LedCue::ConfirmationNeeded, bit(LedOverlay::Host) | bit(LedOverlay::TimerExpired),
        12, 2, true, LedStyle::MonochromeSafe, 1600);
    const LedStateFields f = decodeLedState(v);
    assert(f.cue == LedCue::ConfirmationNeeded && f.playerNumber == 12 && f.seatSlot == 2);
    assert(f.sharedSeat && f.style == LedStyle::MonochromeSafe && f.anchorAgeMs == 1600);
    assert(f.overlays == (bit(LedOverlay::Host) | bit(LedOverlay::TimerExpired)));
    assert(ledStateKey(v) == ledStateKey(led(LedCue::ConfirmationNeeded,
        bit(LedOverlay::Host) | bit(LedOverlay::TimerExpired), 12, 2, true, LedStyle::MonochromeSafe, 99999)));
    assert(decodeLedState(led(LedCue::Waiting, 0, 0, 1, false, LedStyle::Default, 10000000)).anchorAgeMs ==
        LED_ANCHOR_MAX_UNITS * LED_ANCHOR_UNIT_MS);
    assert(decodeLedState(0x0F).cue == LedCue::Off);  // Unknown cue reads as Off.
  }

  SigilLedModel m;
  // Nothing received yet: dark.
  assert(lit(m.render(1000)) == 0 && dark(m.render(1000).single));

  // Waiting: whole ring and single LED a steady dim blue.
  m.applyLedState(led(LedCue::Waiting), 1000);
  LedFrame f = m.render(1000);
  assert(lit(f) == 6 && f.pixels[1].b > 0 && f.pixels[1].r == 0 && f.pixels[1] == m.render(5000).pixels[1]);
  assert(f.single == f.pixels[1] && f.pixels[LED_CENTER] == f.pixels[1]);

  // Your turn breathes green; reduced motion holds it steady.
  m.applyLedState(led(LedCue::YourTurn), 0);
  assert(m.render(0).pixels[1].g < 10 && m.render(1300).pixels[1].g > 240);
  m.applyLedState(led(LedCue::YourTurn, 0, 0, 1, false, LedStyle::ReducedMotion), 0);
  assert(m.render(0).pixels[1].g == 255 && m.render(1300).pixels[1].g == 255);

  // Joined: player number as that many ring pixels; the single LED flashes it.
  m.applyLedState(led(LedCue::Joined, 0, 4), 0);
  f = m.render(0);
  assert(lit(f) == 4 && dark(f.pixels[5]) && dark(f.pixels[LED_CENTER]));
  unsigned flashes = 0; bool was = false;
  for (uint32_t t = 0; t < 4 * 360 + 3000; t += 10) {
    const bool on = !dark(m.render(t).single);
    flashes += on && !was; was = on;
  }
  assert(flashes == 4);
  m.applyLedState(led(LedCue::Joined, 0, 8), 0);
  assert(lit(m.render(0)) == 6 && !dark(m.render(0).pixels[LED_CENTER]));

  // Shared seat: only the focused half of the ring, one (A) or two (B) pulses.
  m.applyLedState(led(LedCue::ConfirmationNeeded, 0, 0, 2, true), 0);
  f = m.render(0);
  assert(dark(f.pixels[1]) && dark(f.pixels[3]) && !dark(f.pixels[4]) && !dark(f.pixels[6]));
  assert(dark(m.render(200).pixels[4]) && !dark(m.render(400).pixels[4]));  // Second pulse.
  m.applyLedState(led(LedCue::ConfirmationNeeded, 0, 0, 1, true), 0);
  assert(!dark(m.render(0).pixels[1]) && dark(m.render(0).pixels[4]) && dark(m.render(400).pixels[1]));
  m.applyLedState(led(LedCue::EliminationSelect), 0);  // One seat: steady full ring.
  assert(lit(m.render(900)) == 6);

  // Overlays take the center by priority; Host never hides the cue on one LED.
  m.applyLedState(led(LedCue::YourTurn, bit(LedOverlay::Host) | bit(LedOverlay::TimerExpired)), 0);
  f = m.render(1300);
  assert(f.pixels[LED_CENTER] == (Rgb{255, 0, 0}) && f.single == f.pixels[LED_CENTER] && f.pixels[1].g > 240);
  m.applyLedState(led(LedCue::YourTurn, bit(LedOverlay::Host)), 0);
  f = m.render(1300);
  assert(f.pixels[LED_CENTER].b == 255 && f.single == f.pixels[1]);

  // Anchored cues keep their phase from Atlas's anchor, not packet arrival.
  m.applyLedState(led(LedCue::TurnStarted, 0, 0, 1, false, LedStyle::Default, 208), 5000);
  assert(dark(m.render(5000).pixels[1]) && !dark(m.render(5192).pixels[1]));

  // A winner's ring turns gold at game over; others stay dark.
  m.applyLedState(led(LedCue::GameOver, bit(LedOverlay::Winner)), 0);
  assert(m.render(0).pixels[3] == (Rgb{255, 170, 0}));
  m.applyLedState(led(LedCue::GameOver), 0);
  assert(lit(m.render(0)) == 0);

  // Unassigned: one pixel circles the ring.
  m.applyLedState(led(LedCue::Unassigned), 0);
  assert(lit(m.render(0)) == 1 && !dark(m.render(0).pixels[1]) && !dark(m.render(250).pixels[2]));

  // Sigil-local states win over Atlas's cue, then hand it back.
  m.applyLedState(led(LedCue::Waiting), 0);
  m.setPairing(true, 100);
  assert(m.render(100).pixels[1] == (Rgb{255, 0, 0}) && dark(m.render(350).pixels[1]));
  m.setPairing(false, 400);
  assert(m.render(400).pixels[1].b > 0);
  m.flashPassAck(1000);
  assert(m.render(1100).single == (Rgb{0, 255, 0}) && m.render(1250).single.b > 0);

  // A pending pass counts down on the ring: six pixels, emptying to one.
  m.setPassPending(true, 5000);
  f = m.render(5000);
  assert(lit(f) == 6 && f.pixels[6] == (Rgb{0, 255, 0}) && f.pixels[LED_CENTER] == (Rgb{0, 255, 0}));
  m.setPassPending(true, 6600);  // Still pending: the start time holds.
  f = m.render(6600);
  assert(lit(f) == 3 && dark(f.pixels[4]) && f.pixels[3] == (Rgb{0, 255, 0}));
  assert(lit(m.render(9000)) == 1);
  m.setPassPending(false, 9100);
  assert(m.render(9100).pixels[1].b > 0);

  // A held menu action fills the ring in white; the single LED brightens.
  m.applyLedState(led(LedCue::Waiting), 0);
  m.setHoldProgress(128);
  f = m.render(3000);
  assert(lit(f) == 4 && f.pixels[4] == (Rgb{120, 100, 70}) && dark(f.pixels[5]) && f.single.r == 60);
  m.setHoldProgress(0);
  assert(m.render(3000).pixels[1].b > 0 && m.render(3000).pixels[1].r == 0);

  // Legacy channels from an older Atlas; clear() goes dark.
  m.applyLegacyRed(true); m.applyLegacyBlue(128);
  assert(!m.semantic() && m.render(2000).single == (Rgb{255, 0, 128}));
  m.clear();
  assert(lit(m.render(0)) == 0);

  // Seat colors: only the calm cues (Joined, Waiting) take them; action cues
  // keep their standard colors; a shared Sigil splits the ring by seat.
  {
    SigilLedModel c;
    c.applySeatColor(encodeSeatColor(1, true, 0xFF8800));
    c.applyLedState(led(LedCue::Waiting), 0);
    LedFrame w = c.render(0);
    assert(w.pixels[1].r > w.pixels[1].b && w.pixels[1].g > 0 && w.pixels[6] == w.pixels[1]);
    c.applyLedState(led(LedCue::Joined, 0, 2), 0);
    LedFrame j = c.render(0);
    assert(j.pixels[1] == Rgb(255, 136, 0) && j.pixels[2] == j.pixels[1] && dark(j.pixels[3]));
    c.applyLedState(led(LedCue::YourTurn), 0);
    assert(c.render(0).pixels[1].r == 0);  // Your turn stays green.
    c.applySeatColor(encodeSeatColor(2, true, 0x0000FF));
    c.applyLedState(led(LedCue::Waiting, 0, 0, 1, true), 0);
    LedFrame sh = c.render(0);
    assert(sh.pixels[1].r > 0 && sh.pixels[4].b > 0 && sh.pixels[4].r == 0);
    c.applySeatColor(encodeSeatColor(1, false, 0));
    c.applyLedState(led(LedCue::Waiting), 0);
    assert(c.render(0).pixels[1].r == 0 && c.render(0).pixels[1].b > 0);  // Back to blue.
    c.applySeatColor(encodeSeatColor(1, true, 0xFF0000));
    c.clear();
    c.applyLedState(led(LedCue::Waiting), 0);
    assert(c.render(0).pixels[1].r == 0);  // Unpaired: colors forgotten.
  }

  {  // Life laps: +8 is lap two (cyan, 2 lit) over a dim green ring.
    SigilLedModel life;
    life.setLifePending(8);
    const LedFrame g = life.render(0);
    assert(g.pixels[1] == g.pixels[2] && g.pixels[1].b > 0 && g.pixels[1].g > 0 && g.pixels[1].r == 0);
    assert(g.pixels[3].g > 0 && g.pixels[3].b == 0 && g.pixels[3].g < 255);
    life.setLifePending(-6);
    const LedFrame r = life.render(0);
    assert(r.pixels[1].r == 255 && r.pixels[6].r == 255 && dark(r.pixels[0]));
  }
  std::cout << "LED wire format, cue rendering, seat halves, overlays and local states passed\n";
}
