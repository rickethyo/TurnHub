#pragma once

// Glyphs both Sigil displays share (any Adafruit_GFX-style display), so the
// two look like one product. Drawn from primitives at any whole scale. Icons
// only add character: the words beside them always carry the meaning
// (ACCESSIBILITY.md).

#include <stdint.h>

namespace TurnHubSigil {

enum class Icon : uint8_t { None, Turn, TurnBack, Pause, Crown, Heart };

// Width and height of an icon at scale 1 (the heart is 13x11, the rest 13x9).
constexpr int16_t ICON_WIDTH = 13;
inline int16_t iconHeight(Icon kind) { return kind == Icon::Heart ? 11 : 9; }

template <typename Gfx>
inline void drawIcon(Gfx &g, Icon kind, int16_t x, int16_t y, uint16_t color, uint8_t s = 1) {
  auto tri = [&](int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    g.fillTriangle(x + x0 * s, y + y0 * s, x + x1 * s, y + y1 * s, x + x2 * s, y + y2 * s, color);
  };
  switch (kind) {
    case Icon::Turn:
      tri(3, 0, 3, 8, 11, 4);
      break;
    case Icon::TurnBack:
      tri(10, 0, 10, 8, 2, 4);
      break;
    case Icon::Pause:
      g.fillRect(x + 3 * s, y, 3 * s, 9 * s, color);
      g.fillRect(x + 8 * s, y, 3 * s, 9 * s, color);
      break;
    case Icon::Crown:
      g.fillRect(x, y + 6 * s, 13 * s, 3 * s, color);
      tri(0, 6, 2, 0, 4, 6);
      tri(4, 6, 6, 0, 8, 6);
      tri(8, 6, 10, 0, 12, 6);
      break;
    case Icon::Heart:
      g.fillCircle(x + 3 * s, y + 3 * s, 3 * s, color);
      g.fillCircle(x + 9 * s, y + 3 * s, 3 * s, color);
      tri(0, 4, 12, 4, 6, 10);
      break;
    case Icon::None:
      break;
  }
}

// The TurnHub emblem: an hourglass (the turn timer) inside a double ring,
// centered on (cx, cy); radius 14 at scale 1.
template <typename Gfx>
inline void drawEmblem(Gfx &g, int16_t cx, int16_t cy, uint16_t color, uint8_t s = 1) {
  g.drawCircle(cx, cy, 14 * s, color);
  g.drawCircle(cx, cy, 12 * s, color);
  if (s > 1) g.drawCircle(cx, cy, 13 * s, color);
  g.drawTriangle(cx - 6 * s, cy - 8 * s, cx + 6 * s, cy - 8 * s, cx, cy, color);
  g.drawTriangle(cx - 6 * s, cy + 8 * s, cx + 6 * s, cy + 8 * s, cx, cy, color);
  g.fillTriangle(cx - 4 * s, cy + 7 * s, cx + 4 * s, cy + 7 * s, cx, cy + 3 * s, color);
}

}  // namespace TurnHubSigil
