#pragma once
#include <stdint.h>
#include "sigil_menu.h"
#include "device_navigation.h"

namespace TurnHubSigil {
constexpr int16_t SIGIL_SCREEN_W = 240, SIGIL_SCREEN_H = 320;
constexpr int16_t NAV_Y = 272, NAV_H = 48;
constexpr uint8_t TOUCH_NONE = 0xFF, TOUCH_NAV = 0x80, TOUCH_PREV = 0x90, TOUCH_NEXT = 0x91;
constexpr uint8_t TOUCH_CONTROLS_BACK=0x92, TOUCH_CONTROLS_PREV=0x93, TOUCH_CONTROLS_NEXT=0x94;
// Mana Tap (mana_tap.h): Home's Play button, the game's field (main.cpp
// reads where it was hit), its Start/Pause/Resume button and Quit.
constexpr uint8_t TOUCH_MANA_OPEN=0x95, TOUCH_MANA_FIELD=0x96, TOUCH_MANA_ACTION=0x97, TOUCH_MANA_QUIT=0x98;
constexpr uint8_t TOUCH_CONTROL=0xA0; // Two targets per shared control: minus/off, plus/on.
struct PadRect {
  int16_t x, y, w, h;
  bool contains(int16_t px, int16_t py, int16_t slop = 0) const {
    return w > 0 && h > 0 && px >= x - slop && px < x + w + slop &&
        py >= y - slop && py < y + h + slop;
  }
};
struct TouchRegion { PadRect rect; uint8_t target; uint32_t meaning; };
struct TouchMap {
  uint8_t menuRevision = 0;
  uint8_t actor = 0;
  uint16_t lifeRequest = 0;
  TouchRegion regions[14] = {};
  uint8_t count = 0;
  void add(PadRect rect, uint8_t target, uint32_t meaning) {
    if (count < 14 && rect.w > 0 && rect.h > 0) regions[count++] = {rect, target, meaning};
  }
  const TouchRegion *at(int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < count; ++i) if (regions[i].rect.contains(x, y)) return &regions[i];
    return nullptr;
  }
};
// One contact owns one semantic action until physical release. Dragging out,
// blank space or a changed action consumes it without choosing another.
class TouchContact {
 public:
  uint8_t sample(const TouchMap &map, bool down, int16_t x, int16_t y) {
    if (!down) { held_ = canceled_ = false; target_ = TOUCH_NONE; return TOUCH_NONE; }
    if (!held_) {
      held_ = true;
      const auto *hit = map.at(x, y);
      if (!hit) { canceled_ = true; return TOUCH_NONE; }
      target_ = hit->target; meaning_ = hit->meaning;
    }
    if (canceled_) return TOUCH_NONE;
    for (uint8_t i = 0; i < map.count; ++i) {
      const auto &r = map.regions[i];
      if (r.target == target_ && r.meaning == meaning_ && r.rect.contains(x, y, 8)) return target_;
    }
    canceled_ = true;
    return TOUCH_NONE;
  }
 private:
  bool held_ = false, canceled_ = false;
  uint8_t target_ = TOUCH_NONE;
  uint32_t meaning_ = 0;
};
}  // namespace TurnHubSigil
