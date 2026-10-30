#pragma once

// Mana Tap, the Sigil's mini game for between turns or games (Home > Play
// Mana Tap). Orbs float up the field; tap the ones in your seat colour, which
// also carry your initial and a ring so colour is never the only sign
// (ACCESSIBILITY.md), and leave the rest. A wrong tap or a missed orb of
// yours costs one of three lives, and every ten points the orbs come faster.
// Device-local fun only: nothing reaches Atlas, and the game pauses itself
// when this Sigil's turn starts. Pure logic, host-tested; main.cpp feeds it
// time and taps, and the display draws a copy of it.

#include <stdint.h>

namespace TurnHubSigil {

constexpr uint8_t MANA_ORBS = 6;
constexpr uint8_t MANA_LIVES = 3;
// The field: below the header and score row, above the tab bar.
constexpr int16_t MANA_FIELD_TOP = 76, MANA_FIELD_BOTTOM = 272;
constexpr int16_t MANA_ORB_R = 18;
constexpr int16_t MANA_TAP_SLOP = 8;  // A tap this far outside an orb still counts.

enum class ManaPhase : uint8_t { Ready, Playing, Paused, Over };
enum class ManaEvent : uint8_t { None, Hit, Miss };

struct ManaOrb {
  bool live = false;
  bool target = false;  // In the player's colour.
  uint8_t seat = 0;     // The seat number whose colour it wears.
  int16_t x = 0;        // Centre.
  int32_t y8 = 0;       // Centre, in 1/256 px.
  int16_t y() const { return static_cast<int16_t>(y8 >> 8); }
};

class ManaTap {
 public:
  // seat: the player's seat number (its colour); best: their saved record.
  void open(uint8_t seat, uint16_t best, uint32_t seed) {
    *this = ManaTap();
    seat_ = seat;
    best_ = best;
    rng_ = seed ? seed : 0x2545F491u;
  }

  void start(uint32_t nowMs) {
    for (auto &o : orbs_) o = ManaOrb();
    score_ = 0;
    lives_ = MANA_LIVES;
    newBest_ = false;
    phase_ = ManaPhase::Playing;
    lastMs_ = nowMs;
    nextSpawnMs_ = nowMs + 300;
  }

  void pause() { if (phase_ == ManaPhase::Playing) phase_ = ManaPhase::Paused; }

  void resume(uint32_t nowMs) {
    if (phase_ != ManaPhase::Paused) return;
    phase_ = ManaPhase::Playing;
    // The time away does not count: orbs stay put and spawning waits again.
    nextSpawnMs_ = nowMs + (nextSpawnMs_ > lastMs_ ? nextSpawnMs_ - lastMs_ : 0);
    lastMs_ = nowMs;
  }

  // Moves the orbs to nowMs, spawns, and returns Miss if one of the player's
  // orbs escaped off the top.
  ManaEvent update(uint32_t nowMs) {
    if (phase_ != ManaPhase::Playing) return ManaEvent::None;
    uint32_t dt = nowMs - lastMs_;
    lastMs_ = nowMs;
    if (dt > 100) dt = 100;  // A stalled loop never teleports orbs.
    const int32_t step = static_cast<int32_t>(speed() * 256 * dt / 1000);
    ManaEvent event = ManaEvent::None;
    for (auto &o : orbs_) {
      if (!o.live) continue;
      o.y8 -= step;
      if (o.y() + MANA_ORB_R < MANA_FIELD_TOP) {
        o.live = false;
        if (o.target) { loseLife(); event = ManaEvent::Miss; }
      }
    }
    if (phase_ == ManaPhase::Playing && static_cast<int32_t>(nowMs - nextSpawnMs_) >= 0) {
      spawn();
      nextSpawnMs_ = nowMs + spawnGapMs();
    }
    return event;
  }

  // A tap at screen point x, y while playing.
  ManaEvent tap(int16_t x, int16_t y) {
    if (phase_ != ManaPhase::Playing) return ManaEvent::None;
    const int32_t reach = MANA_ORB_R + MANA_TAP_SLOP;
    for (auto &o : orbs_) {
      if (!o.live) continue;
      const int32_t dx = x - o.x, dy = y - o.y();
      if (dx * dx + dy * dy > reach * reach) continue;
      o.live = false;
      if (!o.target) { loseLife(); return ManaEvent::Miss; }
      ++score_;
      if (score_ > best_) { best_ = score_; newBest_ = true; }
      return ManaEvent::Hit;
    }
    return ManaEvent::None;
  }

  ManaPhase phase() const { return phase_; }
  uint16_t score() const { return score_; }
  uint16_t best() const { return best_; }
  bool newBest() const { return newBest_; }
  uint8_t lives() const { return lives_; }
  uint8_t seat() const { return seat_; }
  uint8_t level() const { return static_cast<uint8_t>(score_ / 10 < 20 ? score_ / 10 : 20); }
  const ManaOrb &orb(uint8_t i) const { return orbs_[i]; }
  // Rise speed in px/s and the gap between orbs, both quickening per level.
  uint16_t speed() const { return static_cast<uint16_t>(45 + level() * 9); }
  uint16_t spawnGapMs() const { return static_cast<uint16_t>(level() >= 12 ? 420 : 1000 - level() * 48); }

 private:
  uint32_t random() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
  }

  // A decoy seat whose hue sits well away from the player's (the screen and
  // ring use these same seat hues), so the colours never nearly match.
  uint8_t decoySeat() {
    static const int16_t hues[] = {38, 168, 12, 205, 95, 280, 330, 60};
    const int16_t mine = hues[(seat_ + 7) % 8];
    uint8_t options[8], count = 0;
    for (uint8_t n = 1; n <= 8; ++n) {
      int16_t d = hues[(n + 7) % 8] - mine;
      if (d < 0) d = -d;
      if (d > 180) d = 360 - d;
      if (d >= 50) options[count++] = n;
    }
    return count ? options[random() % count] : static_cast<uint8_t>(seat_ % 8 + 1);
  }

  void spawn() {
    for (auto &o : orbs_) {
      if (o.live) continue;
      o.live = true;
      o.target = random() % 100 < 55;
      o.seat = o.target ? seat_ : decoySeat();
      const int16_t span = 240 - 2 * (MANA_ORB_R + 6);
      o.x = static_cast<int16_t>(MANA_ORB_R + 6 + random() % span);
      o.y8 = static_cast<int32_t>(MANA_FIELD_BOTTOM + MANA_ORB_R) << 8;
      return;
    }
  }

  void loseLife() {
    if (lives_) --lives_;
    if (!lives_) phase_ = ManaPhase::Over;
  }

  ManaOrb orbs_[MANA_ORBS];
  ManaPhase phase_ = ManaPhase::Ready;
  uint16_t score_ = 0, best_ = 0;
  bool newBest_ = false;
  uint8_t lives_ = MANA_LIVES;
  uint8_t seat_ = 0;
  uint32_t rng_ = 0x2545F491u;
  uint32_t lastMs_ = 0, nextSpawnMs_ = 0;
};

}  // namespace TurnHubSigil
