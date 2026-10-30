#pragma once
#include <stdint.h>

namespace TurnHub {
// Match-scoped state only. GameEngine owns every transition. Player numbers
// are 1-based; zero means none. selected is sealed before a draw is revealed.
struct DoomBag {
  uint16_t atRisk = 0, drawn = 0;
  uint8_t boundary = 0, warmup = 0, pendingTurn = 0, selected = 0, lastDraw = 0;
  uint32_t cycle = 0, lastCheck = 0;
  bool pending = false;
};
inline uint16_t doomBit(uint8_t player) {
  return player >= 1 && player <= 16 ? static_cast<uint16_t>(1u << (player - 1)) : 0;
}
inline uint8_t doomCount(uint16_t mask) {
  uint8_t count = 0;
  while (mask) { mask &= mask - 1; ++count; }
  return count;
}
inline uint8_t doomPrevious(uint8_t player, uint16_t living, uint8_t count) {
  if (!living || !count) return 0;
  for (uint8_t offset = 1; offset <= count; ++offset) {
    const uint8_t candidate = static_cast<uint8_t>((player + count - 1 - offset) % count + 1);
    if (living & doomBit(candidate)) return candidate;
  }
  return 0;
}
inline bool sameDoomBag(const DoomBag &a, const DoomBag &b) {
  return a.atRisk == b.atRisk && a.drawn == b.drawn && a.boundary == b.boundary && a.warmup == b.warmup &&
      a.pendingTurn == b.pendingTurn && a.selected == b.selected && a.lastDraw == b.lastDraw &&
      a.cycle == b.cycle && a.lastCheck == b.lastCheck && a.pending == b.pending;
}
inline bool validDoomBag(const DoomBag &bag, bool enabled, uint16_t living, uint8_t count, bool paused) {
  if (!enabled || !count) return sameDoomBag(bag, DoomBag{});
  const uint32_t all = (1u << count) - 1u;
  if (count < 5 || !bag.cycle || bag.warmup > 4 || bag.lastCheck >= bag.cycle ||
      bag.lastDraw > count || (bag.atRisk & ~living) || (bag.drawn & ~all) || (bag.drawn & living)) return false;
  if (bag.lastDraw && !(bag.drawn & doomBit(bag.lastDraw))) return false;
  if (doomCount(living) <= 2) return !bag.atRisk && !bag.boundary && !bag.warmup && !bag.pending && !bag.pendingTurn && !bag.selected;
  if (!(living & doomBit(bag.boundary))) return false;
  if (bag.pending && (!paused || bag.warmup || !bag.pendingTurn || bag.pendingTurn > count)) return false;
  if (!bag.pending && (bag.pendingTurn || bag.selected)) return false;
  return !bag.selected || (bag.pending && (bag.atRisk & doomBit(bag.selected)));
}
}  // namespace TurnHub
