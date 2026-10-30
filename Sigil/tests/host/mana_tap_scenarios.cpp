// Mana Tap (mana_tap.h): hits score, wrong taps and escaped orbs cost lives,
// the pace quickens, and pausing freezes the field.
#include <assert.h>
#include <stdio.h>

#include "mana_tap.h"

using namespace TurnHub;
constexpr ManaField F = MANA_SIGIL_FIELD;

namespace {
// The first live orb of the wanted kind, or -1.
int find(const ManaTap &g, bool target) {
  for (uint8_t i = 0; i < MANA_ORBS; ++i)
    if (g.orb(i).live && g.orb(i).target == target) return i;
  return -1;
}
// Runs the clock until an orb of the wanted kind is in the field.
int await(ManaTap &g, uint32_t &now, bool target) {
  for (int n = 0; n < 2000; ++n) {
    const int i = find(g, target);
    if (i >= 0 && g.orb(i).y() < F.bottom - F.radius) return i;
    now += 20;
    g.update(now);
  }
  return -1;
}
}  // namespace

int main() {
  ManaTap g;
  g.open(F, 3, 7, 12345);
  assert(g.phase() == ManaPhase::Ready && g.best() == 7 && g.lives() == MANA_LIVES);
  assert(g.tap(100, 100) == ManaEvent::None);  // Nothing to tap before Start.
  uint32_t now = 1000;
  g.start(now);
  assert(g.phase() == ManaPhase::Playing && g.score() == 0);

  // Every orb is the player's seat or a seat whose hue is far from it.
  int i = await(g, now, true);
  assert(i >= 0 && await(g, now, false) >= 0);
  for (uint8_t n = 0; n < MANA_ORBS; ++n) {
    const ManaOrb &o = g.orb(n);
    if (!o.live) continue;
    if (o.target) assert(o.seat == 3);
    else assert(o.seat != 3 && o.seat != 1 && o.seat != 8 && o.seat >= 1 && o.seat <= 8);
    assert(o.x >= F.radius && o.x <= F.width - F.radius);
  }

  // Tapping one of yours scores; blank space does nothing.
  assert(g.tap(0, 0) == ManaEvent::None);
  const uint8_t lives = g.lives();
  i = await(g, now, true);
  assert(g.tap(g.orb(i).x + F.radius, g.orb(i).y()) == ManaEvent::Hit);
  assert(g.score() == 1 && g.lives() == lives && !g.orb(i).live);

  // A decoy costs a life.
  i = await(g, now, false);
  assert(i >= 0);
  assert(g.tap(g.orb(i).x, g.orb(i).y()) == ManaEvent::Miss);
  assert(g.lives() == lives - 1);

  // Paused, nothing moves and taps are ignored; resuming carries on.
  i = await(g, now, true);
  const int16_t y = g.orb(i).y();
  g.pause();
  assert(g.phase() == ManaPhase::Paused);
  now += 60000;
  assert(g.update(now) == ManaEvent::None && g.orb(i).y() == y);
  assert(g.tap(g.orb(i).x, y) == ManaEvent::None);
  g.resume(now);
  now += 20;
  g.update(now);
  assert(g.phase() == ManaPhase::Playing && g.orb(i).y() < y && g.orb(i).y() > y - 10);

  // Letting your own orbs escape ends the run; a beaten record is flagged.
  ManaEvent last = ManaEvent::None;
  for (int n = 0; n < 20000 && g.phase() == ManaPhase::Playing; ++n) {
    now += 20;
    const ManaEvent e = g.update(now);
    if (e != ManaEvent::None) last = e;
  }
  assert(g.phase() == ManaPhase::Over && g.lives() == 0 && last == ManaEvent::Miss);
  assert(g.best() == 7 && !g.newBest());

  // Scoring past the record raises it, and each ten points quickens the pace.
  g.open(F, 1, 2, 99);
  g.start(now);
  const uint16_t slow = g.speed(), gap = g.spawnGapMs();
  while (g.score() < 10) {
    i = await(g, now, true);
    assert(i >= 0);
    assert(g.tap(g.orb(i).x, g.orb(i).y()) == ManaEvent::Hit);
  }
  assert(g.best() == 10 && g.newBest() && g.level() == 1);
  assert(g.speed() > slow && g.spawnGapMs() < gap);
  // Start again keeps the record but clears the run.
  g.start(now);
  assert(g.score() == 0 && g.best() == 10 && !g.newBest() && g.lives() == MANA_LIVES);

  puts("PASS mana tap: hits, wrong taps, escapes, pause, pace and records");
  return 0;
}
