#pragma once
#include <stdint.h>
#include <cstdio>
#include <string>

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
// Boot pauses (the OLED splash hold) cost no time on the host.
inline void delay(uint32_t) {}
