#pragma once

// Batched life changes from Left/Right (AdjustLife). A tap is one step
// (lifeUnitFor: 1 life, or 100 in a game counted in hundreds); holding
// repeats (LifePace); on the OLED in LIFE_ADJUST_FAST_STEP steps once held
// long enough, so +11 or -39 are quick, and always one step at a time on
// e-ink. The total goes to Atlas as one LifeAdjust once no key is held and
// nothing changed for LIFE_ADJUST_COMMIT_MS. Atlas decides whether it
// applies. Pure logic, host-tested.

#include <stdint.h>

#include "protocol.h"

namespace TurnHubSigil {

// How fast a held life key repeats. The protocol constants suit the OLED,
// which redraws every step; an e-ink panel shows only some of them, so it
// counts slower (playtest 2026-09-29, item 6). Both scale with the seated
// player's hold-timing preference (InputTiming long press, default 2 s): a
// player who needs longer holds also gets a slower ramp.
// A constructor rather than default member values: the ESP32 toolchain
// builds as C++11, where such a struct can't be brace-initialized.
struct LifePace {
  uint32_t repeatDelayMs;
  uint32_t repeatMs;
  uint32_t fastAfterMs;
  int32_t fastStep;  // Step once held past fastAfterMs.

  constexpr LifePace(uint32_t delayMs = TurnHubProtocol::LIFE_ADJUST_REPEAT_DELAY_MS,
      uint32_t everyMs = TurnHubProtocol::LIFE_ADJUST_REPEAT_MS,
      uint32_t fastMs = TurnHubProtocol::LIFE_ADJUST_FAST_AFTER_MS,
      int32_t step = TurnHubProtocol::LIFE_ADJUST_FAST_STEP)
      : repeatDelayMs(delayMs), repeatMs(everyMs), fastAfterMs(fastMs), fastStep(step) {}
};

// E-ink always counts in ones: the panel can't show every step, so the
// player counts the status-light blinks, and jumps of 5 can't be followed
// by eye (owner decision 2026-09-29).
constexpr LifePace EINK_LIFE_PACE(700, 300, 3000, 1);

// Life per step. A game that starts at 1000 life or more (Yu-Gi-Oh!'s 8000)
// counts in hundreds, as the portal and the Atlas screen do.
inline int32_t lifeUnitFor(int32_t startingLife) { return startingLife >= 1000 ? 100 : 1; }

inline LifePace lifePaceFor(bool eink, uint32_t longPressMs) {
  LifePace pace = eink ? EINK_LIFE_PACE : LifePace();
  if (longPressMs == 0) longPressMs = TurnHubProtocol::DEFAULT_LONG_PRESS_MS;
  const auto scale = [longPressMs](uint32_t ms) {
    return ms * longPressMs / TurnHubProtocol::DEFAULT_LONG_PRESS_MS;
  };
  pace.repeatDelayMs = scale(pace.repeatDelayMs);
  pace.repeatMs = scale(pace.repeatMs);
  pace.fastAfterMs = scale(pace.fastAfterMs);
  return pace;
}

class LifeAdjuster {
 public:
  void setPace(const LifePace &pace) { pace_ = pace; }
  // Life per step (lifeUnitFor). A new unit drops an unsent total.
  void setUnit(int32_t unit) {
    if (unit < 1) unit = 1;
    if (unit == unit_) return;
    unit_ = unit;
    cancel();
  }

  // A Left (-1) or Right (+1) key went down for `player` (the shown player).
  // A different player drops the old total first.
  void press(int8_t sign, uint8_t player, uint32_t nowMs) {
    if (held_ != 0 || sign == 0) return;
    if (player != player_) pending_ = 0;
    player_ = player;
    held_ = sign > 0 ? 1 : -1;
    heldSinceMs_ = lastRepeatMs_ = nowMs;
    add(held_, nowMs);
  }
  void release(uint32_t nowMs) {
    if (held_ == 0) return;
    held_ = 0;
    lastChangeMs_ = nowMs;
  }
  // Repeats a held key; true (with the total and player) when it is time to send.
  bool update(uint32_t nowMs, int32_t &delta, uint8_t &player) {
    if (held_ != 0) {
      const uint32_t heldMs = nowMs - heldSinceMs_;
      if (heldMs >= pace_.repeatDelayMs && nowMs - lastRepeatMs_ >= pace_.repeatMs) {
        lastRepeatMs_ = nowMs;
        add(heldMs >= pace_.fastAfterMs ? held_ * pace_.fastStep : held_, nowMs);
      }
      return false;
    }
    if (pending_ == 0 || nowMs - lastChangeMs_ < TurnHubProtocol::LIFE_ADJUST_COMMIT_MS) return false;
    delta = pending_;
    player = player_;
    pending_ = 0;
    return true;
  }
  // Drop the total (life no longer adjustable, or unpaired).
  void cancel() { pending_ = 0; held_ = 0; }
  int32_t pending() const { return pending_; }
  // The total in steps, for the status ring's laps.
  int32_t pendingSteps() const { return pending_ / unit_; }
  uint8_t player() const { return player_; }
  bool holding() const { return held_ != 0; }

 private:
  void add(int32_t steps, uint32_t nowMs) {
    pending_ += steps * unit_;
    if (pending_ > TurnHubProtocol::LIFE_ADJUST_MAX) pending_ = TurnHubProtocol::LIFE_ADJUST_MAX;
    if (pending_ < -TurnHubProtocol::LIFE_ADJUST_MAX) pending_ = -TurnHubProtocol::LIFE_ADJUST_MAX;
    lastChangeMs_ = nowMs;
  }

  LifePace pace_;
  int32_t unit_ = 1;
  int32_t pending_ = 0;
  uint8_t player_ = 0;
  int8_t held_ = 0;
  uint32_t heldSinceMs_ = 0;
  uint32_t lastRepeatMs_ = 0;
  uint32_t lastChangeMs_ = 0;
};

}  // namespace TurnHubSigil
