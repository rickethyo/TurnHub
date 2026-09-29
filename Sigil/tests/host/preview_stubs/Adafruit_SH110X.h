#pragma once
// A stand-in for the SH1106 driver over an Adafruit GFX 1-bit canvas, for the
// screen preview. The host test suites use tests/host/stubs instead.
#include <Adafruit_GFX.h>
#include "Wire.h"

#define SH110X_BLACK 0
#define SH110X_WHITE 1

class Adafruit_SH1106G : public GFXcanvas1 {
 public:
  Adafruit_SH1106G(uint16_t w, uint16_t h, TwoWire *, int16_t, uint32_t, uint32_t) : GFXcanvas1(w, h) {}
  Adafruit_SH1106G(uint16_t w, uint16_t h, int16_t, int16_t, int16_t, int16_t, int16_t) : GFXcanvas1(w, h) {}
  bool begin(uint8_t, bool) { return true; }
  void clearDisplay() { fillScreen(SH110X_BLACK); }
  void display() { ++frames; }
  void setContrast(uint8_t) {}
  int frames = 0;
};
