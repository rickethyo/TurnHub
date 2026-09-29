#pragma once
// Arduino surface for the Sigil screen preview (render_sigil_screens.cpp):
// enough for Adafruit GFX's canvas and the real display classes to run on a
// PC. Not used by the host test suites.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cstdio>
#include <string>
#include <algorithm>
#include <cmath>
using std::sin;
using std::cos;
inline float radians(float degrees) { return degrees * 0.017453292f; }
#include "Print.h"

using std::min;
using std::max;
using boolean = bool;

struct SerialStub {
  std::string output;
  void println(const char *s) { output += std::string(s) + '\n'; }
  template <typename... Args> void printf(const char *format, Args... args) {
    char text[256];
    std::snprintf(text, sizeof(text), format, args...);
    output += text;
  }
};
extern SerialStub Serial;
extern uint32_t previewNowMs;
inline uint32_t millis() { return previewNowMs; }
inline void delay(uint32_t ms) { previewNowMs += ms; }
