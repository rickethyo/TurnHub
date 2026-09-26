#pragma once

// How the life heart looks against the game's starting life (StartingLife
// packet): below it the heart drains from the top, above it the heart grows,
// up to half again its size at double the starting life. Decoration only:
// the number beside it always carries the life total (ACCESSIBILITY.md).
// Pure logic, host-tested.

#include <stdint.h>

namespace TurnHubSigil {

struct HeartLook {
  uint8_t fill = 255;      // Filled share, 0 (outline only) to 255 (solid).
  uint8_t sizePercent = 100;  // 100 = the heart's normal size, up to 150.
};

inline HeartLook lifeHeartLook(int32_t life, int32_t startingLife) {
  HeartLook look;
  if (startingLife <= 0) return look;  // No game, or an older Atlas: plain heart.
  if (life <= 0) {
    look.fill = 0;
    return look;
  }
  if (life < startingLife) {
    // Keep a sliver while any life is left, so "empty" means zero.
    const int32_t fill = static_cast<int32_t>(static_cast<int64_t>(life) * 255 / startingLife);
    look.fill = static_cast<uint8_t>(fill < 16 ? 16 : fill);
    return look;
  }
  const int64_t over = static_cast<int64_t>(life) - startingLife;
  const int64_t grow = over >= startingLife ? 50 : over * 50 / startingLife;
  look.sizePercent = static_cast<uint8_t>(100 + grow);
  return look;
}

}  // namespace TurnHubSigil
