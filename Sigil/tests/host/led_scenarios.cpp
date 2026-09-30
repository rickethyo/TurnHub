#include "sigil_led.h"
#include "atlas_link.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

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
  // Seat B's turn on a shared Sigil: azure, a double pulse and only B's half
  // of the ring, so the two seats never differ by color alone.
  m.applyLedState(led(LedCue::YourTurn, 0, 0, 2, true), 0);
  f = m.render(0);
  assert(f.pixels[4].b == 255 && f.pixels[4].g == 110 && f.pixels[4].r == 0 && dark(f.pixels[1]));
  assert(dark(m.render(200).pixels[4]) && m.render(400).pixels[4].b == 255 && dark(m.render(900).pixels[4]));
  m.applyLedState(led(LedCue::YourTurn, 0, 0, 1, true), 0);  // Seat A: green, A's half.
  f = m.render(1300);
  assert(f.pixels[1].g > 240 && f.pixels[1].b == 0 && dark(f.pixels[4]));
  m.applyLedState(led(LedCue::YourTurn, 0, 0, 2, true, LedStyle::ReducedMotion), 0);
  assert(m.render(0).pixels[4].b == 255 && dark(m.render(2500).pixels[4]));  // Slow blink, not steady.

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

  // Atlas lost: Atlas's cue is replaced by one orange pixel sweeping 1..6
  // and back, center dark; the one-LED view double-blinks. Pairing still
  // outranks it; clearing it hands Atlas's cue back.
  {
    const Rgb orange{255, 50, 0};
    m.setAtlasLost(true, 2000);
    const uint8_t expected[] = {1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1};
    for (uint8_t step = 0; step < sizeof(expected); ++step) {
      const LedFrame lost = m.render(2000 + step * 200u + 50);
      assert(lit(lost) == 1 && lost.pixels[expected[step]] == orange);
      assert(dark(lost.pixels[LED_CENTER]));
    }
    assert(m.render(2000).single == orange && dark(m.render(2200).single) &&
        m.render(2300).single == orange && dark(m.render(2500).single));
    m.setAtlasLost(true, 9000);  // Still lost: the sweep keeps its start.
    assert(m.render(2050).pixels[1] == orange);
    m.setPairing(true, 3000);
    assert(m.render(3000).pixels[1] == (Rgb{255, 0, 0}));
    m.setPairing(false, 3100);
    m.applyLedState(led(LedCue::Waiting, 0, 0, 1, false, LedStyle::ReducedMotion), 3100);
    const LedFrame still = m.render(3100);
    assert(lit(still) == 2 && still.pixels[1] == orange && still.pixels[4] == orange &&
        still.pixels[1] == m.render(4700).pixels[1]);
    m.setAtlasLost(false, 5000);
    assert(!m.atlasLost() && m.render(5000).pixels[1].b > 0);
  }

  // Updating: cyan fills clockwise with the percent over dim blue, the
  // center blinks, and it outranks pairing and Atlas lost.
  {
    const Rgb cyan{0, 200, 200};
    m.setAtlasLost(true, 5000);
    m.setPairing(true, 5000);
    m.setUpdating(true, 0, 5000);
    LedFrame u = m.render(5000);
    for (uint8_t i = 1; i < LED_PIXELS; ++i) assert(u.pixels[i].b > 0 && u.pixels[i].r == 0 && u.pixels[i] != cyan);
    // Reduced motion (set above): the center holds steady.
    assert(u.pixels[LED_CENTER] == cyan && m.render(5600).pixels[LED_CENTER] == cyan);
    m.setUpdating(true, 50, 5700);
    u = m.render(5700);
    assert(u.pixels[1] == cyan && u.pixels[3] == cyan && u.pixels[4] != cyan);
    m.setUpdating(true, 1, 5800);
    assert(m.render(5800).pixels[1] == cyan && m.render(5800).pixels[2] != cyan);
    m.setUpdating(true, 200, 5900);  // Clamped to 100.
    u = m.render(5900);
    for (uint8_t i = 1; i < LED_PIXELS; ++i) assert(u.pixels[i] == cyan);
    assert(m.updating());
    m.setUpdating(false, 0, 6000);
    m.setPairing(false, 6000);
    m.setAtlasLost(false, 6000);
    assert(!m.updating());
  }

  // A pending pass counts down on the ring: six pixels, emptying to one.
  m.setPassPending(true, true, 5000);
  f = m.render(5000);
  assert(lit(f) == 6 && f.pixels[6] == (Rgb{0, 255, 0}) && f.pixels[LED_CENTER] == (Rgb{0, 255, 0}));
  m.setPassPending(true, true, 6600);  // Still pending: the start time holds.
  f = m.render(6600);
  assert(lit(f) == 3 && dark(f.pixels[4]) && f.pixels[3] == (Rgb{0, 255, 0}));
  assert(lit(m.render(9000)) == 1);
  m.setPassPending(false, false, 9100);
  // Everyone else sees the same countdown in amber.
  m.setPassPending(true, false, 9200);
  assert(lit(m.render(9200)) == 6 && m.render(9200).pixels[1] == (Rgb{255, 120, 0}));
  m.setPassPending(false, false, 9300);
  assert(m.render(9100).pixels[1].b > 0);

  // A held menu action fills the ring in white; the single LED brightens.
  m.applyLedState(led(LedCue::Waiting), 0);
  m.setHoldProgress(128);
  f = m.render(3000);
  assert(lit(f) == 4 && f.pixels[4] == (Rgb{120, 100, 70}) && dark(f.pixels[5]) && f.single.r == 60);
  m.setHoldProgress(0);
  assert(m.render(3000).pixels[1].b > 0 && m.render(3000).pixels[1].r == 0);

  // Accessibility styles, checked on the ring itself (ACCESSIBILITY.md). These
  // guarantees used to be tested on Atlas's own copy of the cadences, before
  // every Sigil drew its ring from LedState.
  {
    const auto ringChanges = [](int32_t state, uint32_t &minGapMs) {
      SigilLedModel s;
      s.applyLedState(state, 0);
      LedFrame last = s.render(0);
      uint32_t lastChange = 0, changes = 0;
      minGapMs = UINT32_MAX;
      for (uint32_t t = 10; t <= 12000; t += 10) {
        const LedFrame now = s.render(t);
        bool same = true;
        for (uint8_t i = 0; i < LED_PIXELS; ++i) same = same && now.pixels[i] == last.pixels[i];
        if (!same) {
          if (changes > 0 && t - lastChange < minGapMs) minGapMs = t - lastChange;
          lastChange = t; ++changes; last = now;
        }
      }
      return changes;
    };
    // Reduced motion: every Atlas cue and overlay is steady or changes at
    // most once a second (no breathing, flashing or counting).
    for (uint8_t cue = static_cast<uint8_t>(LedCue::Unassigned); cue <= static_cast<uint8_t>(LedCue::GameOver); ++cue) {
      for (int overlay = -1; overlay < static_cast<int>(LedOverlay::Count); ++overlay) {
        for (uint8_t seat = 1; seat <= 2; ++seat) {
          const uint8_t bits = overlay < 0 ? 0 : bit(static_cast<LedOverlay>(overlay));
          uint32_t gap = 0;
          ringChanges(led(static_cast<LedCue>(cue), bits, 3, seat, seat == 2, LedStyle::ReducedMotion), gap);
          if (gap < 1000) std::cout << "reduced motion too fast: cue " << unsigned(cue) << " overlay " << overlay
                                    << " seat " << unsigned(seat) << " gap " << gap << "\n";
          assert(gap >= 1000);
        }
      }
    }
    // Your turn and waiting differ by brightness, not only by color.
    SigilLedModel s;
    s.applyLedState(led(LedCue::YourTurn, 0, 0, 1, false, LedStyle::ReducedMotion), 0);
    const Rgb yours = s.render(1000).pixels[1];
    s.applyLedState(led(LedCue::Waiting, 0, 0, 1, false, LedStyle::ReducedMotion), 0);
    const Rgb waiting = s.render(1000).pixels[1];
    const auto peak = [](const Rgb &c) { return c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b); };
    assert(peak(yours) >= 2 * peak(waiting) && peak(waiting) > 0);
    // Cues that can meet in the same situation differ in timing, not only in
    // hue, for monochrome-safe and reduced motion alike.
    const auto onOff = [](int32_t state, uint8_t pixel) {
      SigilLedModel m2;
      m2.applyLedState(state, 0);
      std::string trace;
      for (uint32_t t = 0; t < 8000; t += 50) {
        const Rgb c = m2.render(t).pixels[pixel];
        trace += (c.r > 40 || c.g > 40 || c.b > 40) ? '1' : '0';
      }
      return trace;
    };
    for (LedStyle style : {LedStyle::MonochromeSafe, LedStyle::ReducedMotion}) {
      assert(onOff(led(LedCue::YourTurn, bit(LedOverlay::TimerExpired), 0, 1, false, style), LED_CENTER) !=
          onOff(led(LedCue::YourTurn, bit(LedOverlay::LongTurn), 0, 1, false, style), LED_CENTER));
      assert(onOff(led(LedCue::ConfirmationNeeded, 0, 0, 1, false, style), 1) !=
          onOff(led(LedCue::EliminationSelect, 0, 0, 1, false, style), 1));
    }
  }

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
  {  // Table clock: two Sigils booted at different times show looping cues in step.
    SigilLedModel a, b;
    const uint32_t aBoot = 1000, bBoot = 73321;  // Local millis() when Atlas read 50000.
    a.syncTableClock(50000, aBoot);
    b.syncTableClock(50000, bBoot + 40);  // Late radio delivery, then an on-time one.
    b.syncTableClock(52000, bBoot + 2000);
    a.applyLedState(led(LedCue::Unassigned), aBoot);
    b.applyLedState(led(LedCue::Unassigned), bBoot);
    a.applyLedState(led(LedCue::Paused), aBoot);
    b.applyLedState(led(LedCue::Paused), bBoot);
    for (uint32_t t = 0; t < 6000; t += 137) {
      const LedFrame fa = a.render(aBoot + t), fb = b.render(bBoot + t);
      for (uint8_t i = 0; i < LED_PIXELS; ++i) assert(fa.pixels[i] == fb.pixels[i]);
      assert(fa.single == fb.single);
    }
    // Atlas restarts (its clock jumps back): the old samples are dropped.
    b.syncTableClock(10, bBoot + 9000);
    assert(b.tableNow(bBoot + 9000) == 10);
  }
  // Atlas link: lost after LINK_TIMEOUT_MS of silence (counted from boot or
  // pairing), restored by the next packet, inert while unpaired.
  {
    AtlasLink link;
    assert(link.update(100000) == LinkChange::None && !link.lost());  // Unpaired.
    link.start(1000);
    assert(link.update(1000 + LINK_TIMEOUT_MS - 1) == LinkChange::None);
    assert(link.heard(3000) == LinkChange::None);
    assert(link.update(3000 + LINK_TIMEOUT_MS - 1) == LinkChange::None && !link.lost());
    assert(link.update(3000 + LINK_TIMEOUT_MS) == LinkChange::Lost && link.lost());
    assert(link.update(60000) == LinkChange::None && link.lost());  // Reported once.
    assert(link.heard(61000) == LinkChange::Restored && !link.lost());
    assert(link.heard(61500) == LinkChange::None);
    // millis() wrapping past zero doesn't fake a loss.
    link.heard(0xFFFFF000u);
    assert(link.update(0x00000100u) == LinkChange::None);
    link.stop();
    assert(link.update(0x7FFFFFFFu) == LinkChange::None && !link.lost());
    assert(link.heard(0x7FFFFFFFu) == LinkChange::None);
  }

  std::cout << "LED wire format, cue rendering, seat halves, overlays, local states, Atlas lost, link timeout and table clock passed\n";
}
