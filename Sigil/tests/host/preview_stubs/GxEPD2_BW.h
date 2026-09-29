#pragma once
// A stand-in for GxEPD2's black-and-white driver: the real paged drawing API
// over an Adafruit GFX 1-bit canvas, so the preview can save what the panel
// would show. Counts full and partial refreshes.
#include <Adafruit_GFX.h>

#define GxEPD_BLACK 0x0000
#define GxEPD_WHITE 0xFFFF

struct GxEPD2_213_B74 {
  static const uint16_t WIDTH = 128;
  static const uint16_t WIDTH_VISIBLE = 122;
  static const uint16_t HEIGHT = 250;
  static const bool hasFastPartialUpdate = true;
  GxEPD2_213_B74(int, int, int, int) {}
};

template <typename Driver, uint16_t PAGE_HEIGHT>
class GxEPD2_BW : public GFXcanvas1 {
 public:
  explicit GxEPD2_BW(Driver) : GFXcanvas1(Driver::WIDTH_VISIBLE, Driver::HEIGHT) {}
  void init(uint32_t) {}
  void setFullWindow() { partial_ = false; }
  void setPartialWindow(int16_t, int16_t, int16_t, int16_t) { partial_ = true; }
  void firstPage() {}
  bool nextPage() { ++(partial_ ? partialRefreshes : fullRefreshes); return false; }
  void powerOff() {}
  void hibernate() {}
  int fullRefreshes = 0;
  int partialRefreshes = 0;

 private:
  bool partial_ = false;
};
