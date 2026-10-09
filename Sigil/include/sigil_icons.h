#pragma once

// Glyphs both Sigil displays share (any Adafruit_GFX-style display), so the
// two look like one product. Drawn from primitives at any whole scale. Icons
// only add character: the words beside them always carry the meaning
// (ACCESSIBILITY.md).

#include "device_theme.h"
#include <math.h>
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

// One-bit theme glyphs share the same 13-pixel footprint. Words carry meaning.
template <typename Gfx>
inline void drawThemeIcon(Gfx &g, TurnHubTheme::Id theme, Icon kind,
    int16_t x, int16_t y, uint16_t color) {
  if (kind == Icon::None) return;
  if (theme == TurnHubTheme::Id::Daylight) {
    // Airy outline glyphs.
    if (kind == Icon::Turn || kind == Icon::TurnBack) {
      const bool back = kind == Icon::TurnBack;
      g.drawTriangle(x + (back ? 10 : 2), y, x + (back ? 10 : 2), y + 8,
          x + (back ? 2 : 10), y + 4, color);
    } else if (kind == Icon::Pause) {
      g.drawRect(x + 2, y, 3, 9, color); g.drawRect(x + 8, y, 3, 9, color);
    } else if (kind == Icon::Crown) {
      g.drawLine(x, y + 2, x + 2, y + 8, color);
      g.drawLine(x + 2, y + 8, x + 10, y + 8, color);
      g.drawLine(x + 10, y + 8, x + 12, y + 2, color);
      g.drawLine(x, y + 2, x + 4, y + 5, color);
      g.drawLine(x + 4, y + 5, x + 6, y, color);
      g.drawLine(x + 6, y, x + 8, y + 5, color);
      g.drawLine(x + 8, y + 5, x + 12, y + 2, color);
    } else drawIcon(g, kind, x, y, color);
  } else if (theme == TurnHubTheme::Id::Brass) {
    drawIcon(g, kind, x, y, color);
    g.drawFastHLine(x, y + 10, 13, color); // Engraved pedestal.
  } else if (theme == TurnHubTheme::Id::Contrast) {
    // Solid, boxed glyphs with a heavier silhouette.
    drawIcon(g, kind, x, y, color);
    g.drawRect(x - 2, y - 2, 17, 13, color);
  } else {
    // Graphite: compact solid geometry and directional chevrons.
    if (kind == Icon::Turn || kind == Icon::TurnBack) {
      const bool back = kind == Icon::TurnBack;
      for (int i = 0; i < 2; ++i) {
        int16_t a = x + (back ? 10 - 5*i : 2 + 5*i);
        int16_t b = a + (back ? -3 : 3);
        g.drawLine(a, y, b, y + 4, color);
        g.drawLine(b, y + 4, a, y + 8, color);
      }
    } else drawIcon(g, kind, x, y, color);
  }
}

// Graphite's angular life shield, with the same bottom-up fill as the heart.
template <typename Gfx>
inline void drawLifeShield(Gfx &g, int16_t x, int16_t y, int16_t w, int16_t h,
    uint8_t fill, uint16_t color, bool heavy = false) {
  if (w < 3 || h < 3) return;
  const int16_t filledFrom = h - (static_cast<int32_t>(h) * fill + 254) / 255;
  for (int16_t py = 0; py < h; ++py) {
    const int16_t inset = py < h/2 ? 0 : (py-h/2)*(w/2)/(h-h/2);
    for (int16_t px = inset; px < w-inset; ++px) {
      if (py == 0 || py >= filledFrom || px < inset + (heavy ? 2 : 1) ||
          px >= w-inset-(heavy ? 2 : 1)) g.drawPixel(x+px, y+py, color);
    }
  }
}

// The life heart in a w x h box at (x, y), drawn pixel by pixel so it can
// take any size (life_heart.h). fill 255 is the solid heart; less leaves the
// top as an outline, draining like a vial.
template <typename Gfx>
inline void drawLifeHeart(Gfx &g, int16_t x, int16_t y, int16_t w, int16_t h,
    uint8_t fill, uint16_t color) {
  if (w < 3 || h < 3) return;
  // Classic heart curve, (X^2 + Y^2 - 1)^3 <= X^2 Y^3, fitted to the box.
  const auto inside = [w, h](int16_t px, int16_t py) {
    if (px < 0 || py < 0 || px >= w || py >= h) return false;
    const float fx = ((px + 0.5f) / w * 2.0f - 1.0f) * 1.16f;
    const float fy = 1.22f - (py + 0.5f) / h * 2.26f;
    const float a = fx * fx + fy * fy - 1.0f;
    return a * a * a - fx * fx * fy * fy * fy <= 0.0f;
  };
  const int16_t filledFrom = static_cast<int16_t>(h - (static_cast<int32_t>(h) * fill + 254) / 255);
  for (int16_t py = 0; py < h; ++py) {
    for (int16_t px = 0; px < w; ++px) {
      if (!inside(px, py)) continue;
      const bool edge = !inside(px - 1, py) || !inside(px + 1, py) ||
          !inside(px, py - 1) || !inside(px, py + 1);
      if (edge || py >= filledFrom) g.drawPixel(x + px, y + py, color);
    }
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

// --- Brass look (2026-09-29) ---------------------------------------------------
// The portal's Brass theme in one bit: gears, rivets and a pressure-gauge life
// dial, all decoration beside words and numbers that carry the meaning.

// A gear: a root disc with trapezoid teeth and an axle hole (hole radius 0
// for none), centered on (cx, cy) with outer radius r.
template <typename Gfx>
inline void drawGear(Gfx &g, int16_t cx, int16_t cy, int16_t r, uint8_t teeth, uint16_t color,
    int16_t hole, uint16_t holeColor, float angleDeg = 0.0f) {
  const float root = r * 0.74f;
  g.fillCircle(cx, cy, static_cast<int16_t>(root + 0.5f), color);
  const float step = 6.2831853f / teeth;
  for (uint8_t i = 0; i < teeth; ++i) {
    const float a = angleDeg * 0.017453292f + i * step;
    const float base = step * 0.30f, tip = step * 0.19f;
    const auto px = [&](float ang, float rad) { return static_cast<int16_t>(lroundf(cx + cosf(ang) * rad)); };
    const auto py = [&](float ang, float rad) { return static_cast<int16_t>(lroundf(cy + sinf(ang) * rad)); };
    const int16_t x1 = px(a - base, root - 1), y1 = py(a - base, root - 1);
    const int16_t x2 = px(a - tip, r), y2 = py(a - tip, r);
    const int16_t x3 = px(a + tip, r), y3 = py(a + tip, r);
    const int16_t x4 = px(a + base, root - 1), y4 = py(a + base, root - 1);
    g.fillTriangle(x1, y1, x2, y2, x3, y3, color);
    g.fillTriangle(x1, y1, x3, y3, x4, y4, color);
  }
  if (hole > 0) g.fillCircle(cx, cy, hole, holeColor);
}

// A rivet: a dot with a highlight pixel in the background color.
template <typename Gfx>
inline void drawRivet(Gfx &g, int16_t x, int16_t y, uint16_t color, uint16_t highlight) {
  g.fillCircle(x, y, 2, color);
  g.drawPixel(x - 1, y - 1, highlight);
}

// The life dial, the Sigil's version of the portal's gauge: a double ring,
// ticks over 270 degrees, a thick arc and a needle at the share of the
// starting life left (fill 0-255, as lifeHeartLook reports it). Above the
// starting life an outer arc grows instead (over: 0-50, lifeHeartLook's
// sizePercent - 100), so the dial needs r + 2 of room. Decoration: the number
// beside it carries the life total.
template <typename Gfx>
inline void drawLifeDial(Gfx &g, int16_t cx, int16_t cy, int16_t r, uint8_t fill, uint16_t color,
    uint8_t over = 0) {
  constexpr float START = 2.3561945f, SPAN = 4.712389f;  // 135 degrees, 270 degrees.
  g.drawCircle(cx, cy, r, color);
  if (r >= 10) g.drawCircle(cx, cy, r - 2, color);
  const int16_t inner = r >= 10 ? r - 2 : r;
  const uint8_t ticks = r >= 14 ? 8 : 4;
  for (uint8_t i = 0; i <= ticks; ++i) {
    const float a = START + SPAN * i / ticks;
    const float r0 = inner - (i % 2 == 0 ? 4 : 2);
    g.drawLine(static_cast<int16_t>(lroundf(cx + cosf(a) * inner)), static_cast<int16_t>(lroundf(cy + sinf(a) * inner)),
        static_cast<int16_t>(lroundf(cx + cosf(a) * r0)), static_cast<int16_t>(lroundf(cy + sinf(a) * r0)), color);
  }
  const float share = fill / 255.0f;
  const float arcR = inner - (r >= 14 ? 7 : 4);
  const float end = START + SPAN * share;
  for (float a = START; a <= end; a += 0.05f) {
    const int16_t x = static_cast<int16_t>(lroundf(cx + cosf(a) * arcR));
    const int16_t y = static_cast<int16_t>(lroundf(cy + sinf(a) * arcR));
    g.fillCircle(x, y, r >= 14 ? 1 : 0, color);
  }
  g.drawLine(cx, cy, static_cast<int16_t>(lroundf(cx + cosf(end) * (inner - 2))),
      static_cast<int16_t>(lroundf(cy + sinf(end) * (inner - 2))), color);
  g.fillCircle(cx, cy, r >= 14 ? 2 : 1, color);
  if (over > 0) {
    const float overEnd = START + SPAN * (over > 50 ? 50 : over) / 50.0f;
    for (float a = START; a <= overEnd; a += 0.06f) {
      g.drawPixel(static_cast<int16_t>(lroundf(cx + cosf(a) * (r + 2))),
          static_cast<int16_t>(lroundf(cy + sinf(a) * (r + 2))), color);
    }
  }
}

}  // namespace TurnHubSigil
