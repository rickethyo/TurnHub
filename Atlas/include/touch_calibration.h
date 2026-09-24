#pragma once

// Atlas touchscreen calibration: maps raw 12-bit XPT2046 readings to screen
// pixels. Pure math, so host tests cover it. atlas_display.cpp runs the
// on-screen 4-point calibration and keeps the result in NVS; resistive panels
// differ from unit to unit, so the config.h values are only a fallback.

#include <stdint.h>
#include <stdlib.h>

#include "config.h"

namespace TurnHubAtlas {

struct TouchCalibration {
  // Swapped: the raw Y channel runs along the screen's x axis.
  uint8_t swapXY = 0;
  // Raw readings at the first and last pixel of each screen axis. A falling
  // range means that axis is inverted.
  int16_t xAtFirst = 0;
  int16_t xAtLast = 0;
  int16_t yAtFirst = 0;
  int16_t yAtLast = 0;
};

// Calibration targets, inset from the corners: top-left, top-right,
// bottom-right, bottom-left.
constexpr int16_t TOUCH_CAL_INSET = 24;
constexpr uint8_t TOUCH_CAL_POINTS = 4;
// A usable calibration spans at least this many raw counts along each axis
// between the targets (a real press spans roughly 2500-3000).
constexpr int16_t TOUCH_CAL_MIN_SPAN = 1000;

inline void touchCalibrationTarget(uint8_t index, int16_t width, int16_t height,
    int16_t &x, int16_t &y) {
  x = index == 1 || index == 2 ? width - 1 - TOUCH_CAL_INSET : TOUCH_CAL_INSET;
  y = index >= 2 ? height - 1 - TOUCH_CAL_INSET : TOUCH_CAL_INSET;
}

inline TouchCalibration defaultTouchCalibration() {
  using namespace AtlasConfig;
  TouchCalibration cal;
  cal.swapXY = TOUCH_SWAP_XY ? 1 : 0;
  cal.xAtFirst = TOUCH_INVERT_X ? TOUCH_RAW_X_MAX : TOUCH_RAW_X_MIN;
  cal.xAtLast = TOUCH_INVERT_X ? TOUCH_RAW_X_MIN : TOUCH_RAW_X_MAX;
  cal.yAtFirst = TOUCH_INVERT_Y ? TOUCH_RAW_Y_MAX : TOUCH_RAW_Y_MIN;
  cal.yAtLast = TOUCH_INVERT_Y ? TOUCH_RAW_Y_MIN : TOUCH_RAW_Y_MAX;
  return cal;
}

inline bool validTouchCalibration(const TouchCalibration &cal) {
  return cal.swapXY <= 1 && abs(cal.xAtLast - cal.xAtFirst) >= TOUCH_CAL_MIN_SPAN &&
      abs(cal.yAtLast - cal.yAtFirst) >= TOUCH_CAL_MIN_SPAN;
}

// Solves a calibration from the raw readings taken at the four targets.
// Returns false when the presses do not describe a usable panel (too little
// travel, or the axes do not separate), so the caller can ask again.
inline bool solveTouchCalibration(const uint16_t rawX[TOUCH_CAL_POINTS],
    const uint16_t rawY[TOUCH_CAL_POINTS], int16_t width, int16_t height,
    TouchCalibration &out) {
  // How far each raw channel moves across the screen's x axis (left targets
  // 0 and 3, right targets 1 and 2) and down its y axis (top 0 and 1,
  // bottom 2 and 3), doubled to stay in integers.
  const int32_t xAcrossX = (rawX[1] + rawX[2]) - (rawX[0] + rawX[3]);
  const int32_t yAcrossX = (rawY[1] + rawY[2]) - (rawY[0] + rawY[3]);
  const int32_t xDownY = (rawX[2] + rawX[3]) - (rawX[0] + rawX[1]);
  const int32_t yDownY = (rawY[2] + rawY[3]) - (rawY[0] + rawY[1]);

  const bool swap = abs(yAcrossX) > abs(xAcrossX);
  const int32_t alongX = swap ? yAcrossX : xAcrossX;
  const int32_t crossX = swap ? xAcrossX : yAcrossX;
  const int32_t alongY = swap ? xDownY : yDownY;
  const int32_t crossY = swap ? yDownY : xDownY;
  // Each axis must move its own channel clearly more than the other one.
  if (abs(alongX) < 2 * TOUCH_CAL_MIN_SPAN || abs(alongY) < 2 * TOUCH_CAL_MIN_SPAN ||
      abs(crossX) * 2 > abs(alongX) || abs(crossY) * 2 > abs(alongY)) {
    return false;
  }

  const uint16_t *a = swap ? rawY : rawX;  // Channel along screen x.
  const uint16_t *b = swap ? rawX : rawY;  // Channel along screen y.
  const int32_t left = (a[0] + a[3]) / 2, right = (a[1] + a[2]) / 2;
  const int32_t top = (b[0] + b[1]) / 2, bottom = (b[2] + b[3]) / 2;
  const int32_t xPixels = width - 1 - 2 * TOUCH_CAL_INSET;
  const int32_t yPixels = height - 1 - 2 * TOUCH_CAL_INSET;
  // Extrapolate from the inset targets out to the screen edges.
  const int32_t xEdge = (right - left) * TOUCH_CAL_INSET / xPixels;
  const int32_t yEdge = (bottom - top) * TOUCH_CAL_INSET / yPixels;

  TouchCalibration cal;
  cal.swapXY = swap ? 1 : 0;
  cal.xAtFirst = static_cast<int16_t>(left - xEdge);
  cal.xAtLast = static_cast<int16_t>(right + xEdge);
  cal.yAtFirst = static_cast<int16_t>(top - yEdge);
  cal.yAtLast = static_cast<int16_t>(bottom + yEdge);
  if (!validTouchCalibration(cal)) return false;
  out = cal;
  return true;
}

inline int16_t mapTouchAxis(int32_t raw, int32_t atFirst, int32_t atLast, int16_t size) {
  int32_t value = (raw - atFirst) * (size - 1) / (atLast - atFirst);
  if (value < 0) value = 0;
  if (value > size - 1) value = size - 1;
  return static_cast<int16_t>(value);
}

inline void mapTouch(const TouchCalibration &cal, uint16_t rawX, uint16_t rawY,
    int16_t width, int16_t height, int16_t &x, int16_t &y) {
  const uint16_t alongX = cal.swapXY ? rawY : rawX;
  const uint16_t alongY = cal.swapXY ? rawX : rawY;
  x = mapTouchAxis(alongX, cal.xAtFirst, cal.xAtLast, width);
  y = mapTouchAxis(alongY, cal.yAtFirst, cal.yAtLast, height);
}

}  // namespace TurnHubAtlas
