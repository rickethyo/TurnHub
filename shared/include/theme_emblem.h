#pragma once
#include "device_theme.h"
#include <initializer_list>

namespace TurnHubTheme {
// A recognizable header mark in a square centered on (cx,cy). Brass callers
// use their existing gear; these three marks work on one-bit and color GFX.
template <typename Gfx>
inline void drawHeaderMark(Gfx &g, Id theme, int16_t cx, int16_t cy,
    int16_t r, uint32_t ink) {
  if (theme == Id::Graphite) {
    g.drawLine(cx-r, cy, cx, cy-r, ink);
    g.drawLine(cx, cy-r, cx+r, cy, ink);
    g.drawLine(cx+r, cy, cx, cy+r, ink);
    g.drawLine(cx, cy+r, cx-r, cy, ink);
    g.fillRect(cx-1, cy-1, 3, 3, ink);
  } else if (theme == Id::Daylight) {
    const int16_t inner = r/2;
    g.drawCircle(cx, cy, inner, ink);
    g.drawLine(cx-r, cy, cx-inner-2, cy, ink);
    g.drawLine(cx+inner+2, cy, cx+r, cy, ink);
    g.drawLine(cx, cy-r, cx, cy-inner-2, ink);
    g.drawLine(cx, cy+inner+2, cx, cy+r, ink);
    if (r >= 6) {
      const int16_t d = r*3/4;
      for (int16_t sx : {-1, 1}) for (int16_t sy : {-1, 1})
        g.drawLine(cx+sx*(d-1), cy+sy*(d-1), cx+sx*d, cy+sy*d, ink);
    }
  } else {
    g.drawRect(cx-r, cy-r, 2*r+1, 2*r+1, ink);
    g.fillTriangle(cx-r+2, cy-r+2, cx+r-2, cy-r+2, cx, cy, ink);
    g.fillTriangle(cx-r+2, cy+r-2, cx+r-2, cy+r-2, cx, cy, ink);
  }
}
} // namespace TurnHubTheme
