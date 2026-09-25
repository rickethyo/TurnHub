#pragma once

// Batched life changes from Left/Right (AdjustLife). A tap is +/-1; holding
// repeats every LIFE_ADJUST_REPEAT_MS after LIFE_ADJUST_REPEAT_DELAY_MS, in
// steps of LIFE_ADJUST_FAST_STEP once held LIFE_ADJUST_FAST_AFTER_MS, so +11
// or -39 are quick. The total goes to Atlas as one LifeAdjust once no key
// is held and nothing changed for LIFE_ADJUST_COMMIT_MS. Atlas decides
// whether it applies. Pure logic, host-tested.

#include <stdint.h>

#include "protocol.h"

namespace TurnHubSigil {

class LifeAdjuster {
 public:
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
      if (heldMs >= TurnHubProtocol::LIFE_ADJUST_REPEAT_DELAY_MS &&
          nowMs - lastRepeatMs_ >= TurnHubProtocol::LIFE_ADJUST_REPEAT_MS) {
        lastRepeatMs_ = nowMs;
        add(heldMs >= TurnHubProtocol::LIFE_ADJUST_FAST_AFTER_MS
            ? held_ * TurnHubProtocol::LIFE_ADJUST_FAST_STEP : held_, nowMs);
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
  uint8_t player() const { return player_; }
  bool holding() const { return held_ != 0; }

 private:
  void add(int32_t step, uint32_t nowMs) {
    pending_ += step;
    if (pending_ > TurnHubProtocol::LIFE_ADJUST_MAX) pending_ = TurnHubProtocol::LIFE_ADJUST_MAX;
    if (pending_ < -TurnHubProtocol::LIFE_ADJUST_MAX) pending_ = -TurnHubProtocol::LIFE_ADJUST_MAX;
    lastChangeMs_ = nowMs;
  }

  int32_t pending_ = 0;
  uint8_t player_ = 0;
  int8_t held_ = 0;
  uint32_t heldSinceMs_ = 0;
  uint32_t lastRepeatMs_ = 0;
  uint32_t lastChangeMs_ = 0;
};

}  // namespace TurnHubSigil
