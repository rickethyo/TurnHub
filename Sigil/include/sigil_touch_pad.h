#pragma once

// The touch Sigil's on-screen keys (the E32R28T stand-in for the S3 Sigil):
// the five compass keys drawn as buttons along the bottom of the 240 x 320
// portrait screen. A press on one is that key held, so the menu, picker,
// life and commander requests use sigil_menu.h. Pure geometry,
// shared by the renderer (sigil_display_lvgl.cpp) and the touch input
// (main.cpp), so what is drawn is what is pressed.

#include <stdint.h>

#include "sigil_menu.h"

namespace TurnHubSigil {

constexpr int16_t SIGIL_SCREEN_W = 240;
constexpr int16_t SIGIL_SCREEN_H = 320;
// Content above, keys below.
constexpr int16_t PAD_TOP = 176;
constexpr int16_t PAD_ROW_H = 44;
constexpr int16_t PAD_GAP = 4;

struct PadRect { int16_t x, y, w, h; };

// Up and Down span the width (big targets on a resistive panel); Left,
// Select and Right share the middle row.
inline PadRect padRect(Key key) {
  constexpr int16_t full = SIGIL_SCREEN_W - 2 * PAD_GAP;
  constexpr int16_t third = (full - 2 * PAD_GAP) / 3;
  constexpr int16_t row0 = PAD_TOP + PAD_GAP;
  constexpr int16_t row1 = row0 + PAD_ROW_H + PAD_GAP;
  constexpr int16_t row2 = row1 + PAD_ROW_H + PAD_GAP;
  switch (key) {
    case Key::Up: return {PAD_GAP, row0, full, PAD_ROW_H};
    case Key::Left: return {PAD_GAP, row1, third, PAD_ROW_H};
    case Key::Select: return {static_cast<int16_t>(PAD_GAP * 2 + third), row1, third, PAD_ROW_H};
    case Key::Right:
      return {static_cast<int16_t>(PAD_GAP * 3 + 2 * third), row1,
          static_cast<int16_t>(full - 2 * third - 2 * PAD_GAP), PAD_ROW_H};
    case Key::Down: return {PAD_GAP, row2, full, PAD_ROW_H};
    default: return {0, 0, 0, 0};
  }
}

// The key under a screen point, or Key::Count for none. The gaps between
// buttons count for the nearer one, so a press never falls through.
inline Key padKeyAt(int16_t x, int16_t y) {
  if (y < PAD_TOP || y >= SIGIL_SCREEN_H || x < 0 || x >= SIGIL_SCREEN_W) return Key::Count;
  const PadRect up = padRect(Key::Up), mid = padRect(Key::Left);
  if (y < mid.y - PAD_GAP / 2) return y >= up.y - PAD_GAP ? Key::Up : Key::Count;
  if (y >= mid.y + mid.h + PAD_GAP / 2) return Key::Down;
  const PadRect select = padRect(Key::Select), right = padRect(Key::Right);
  if (x < select.x - PAD_GAP / 2) return Key::Left;
  if (x < right.x - PAD_GAP / 2) return Key::Select;
  return Key::Right;
}

}  // namespace TurnHubSigil
