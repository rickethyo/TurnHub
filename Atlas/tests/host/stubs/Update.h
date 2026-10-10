#pragma once
#include <Arduino.h>
constexpr int U_FLASH = 0;
struct UpdateStub {
  bool begin(size_t, int) { return true; }
  size_t write(uint8_t *, size_t n) { return n; }
  bool end() { return true; }
  void abort() {}
  void printError(SerialStub &) {}
};
static UpdateStub Update;
