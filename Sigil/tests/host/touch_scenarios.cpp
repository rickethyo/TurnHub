// Touch Sigil (E32R28T stand-in): calibration math and the on-screen keys.
#include <assert.h>
#include <stdio.h>

#include "sigil_touch_pad.h"
#include "touch_calibration.h"

using namespace TurnHubSigil;

int main() {
  // Raw readings at the four inset crosses on the first board (2026-10-28):
  // raw Y rises along screen x, raw X falls down screen y.
  const uint16_t rawX[TOUCH_CAL_POINTS] = {3500, 3500, 380, 380};
  const uint16_t rawY[TOUCH_CAL_POINTS] = {720, 3470, 3470, 720};
  TouchCalibration cal;
  assert(solveTouchCalibration(rawX, rawY, SIGIL_SCREEN_W, SIGIL_SCREEN_H, cal));
  assert(cal.swapXY == 1 && cal.xAtFirst < cal.xAtLast && cal.yAtFirst > cal.yAtLast);
  int16_t x = 0, y = 0;
  mapTouch(cal, 1940, 2095, SIGIL_SCREEN_W, SIGIL_SCREEN_H, x, y);  // The centre.
  assert(x > 110 && x < 130 && y > 150 && y < 170);
  // Presses that do not separate the axes are refused.
  const uint16_t flat[TOUCH_CAL_POINTS] = {2000, 2010, 2020, 2000};
  assert(!solveTouchCalibration(flat, flat, SIGIL_SCREEN_W, SIGIL_SCREEN_H, cal));

  // Every key's own rectangle maps back to it; the content area is no key.
  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    const PadRect r = padRect(static_cast<Key>(i));
    assert(padKeyAt(r.x + r.w / 2, r.y + r.h / 2) == static_cast<Key>(i));
    assert(padKeyAt(r.x + 1, r.y + 1) == static_cast<Key>(i));
  }
  assert(padKeyAt(120, 100) == Key::Count);
  assert(padKeyAt(120, SIGIL_SCREEN_H - 1) == Key::Down);
  puts("PASS touch: calibration solve and mapping, on-screen key hit areas");
  return 0;
}
