#pragma once
#include <stdint.h>

namespace TurnHubAtlas {
// Source rectangle for a 3:2 center crop, used for phone-normalized and direct Scryfall JPEGs.
struct PlaneArtFit { int x, y, w, h; };
inline PlaneArtFit planeArtFit(int width, int height) {
  if (width <= 0 || height <= 0 || width > 2048 || height > 2048) return {0, 0, 0, 0};
  int w = width, h = height;
  if (width * 2 > height * 3) w = height * 3 / 2;
  else h = width * 2 / 3;
  if (!w || !h) return {0, 0, 0, 0};
  return {(width - w) / 2, (height - h) / 2, w, h};
}
}  // namespace TurnHubAtlas
