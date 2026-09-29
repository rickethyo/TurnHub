#pragma once
#include <stdint.h>
struct SPIClass {
  void begin(int, int, int, int) {}
  void *bus() { return nullptr; }
};
extern SPIClass SPI;
inline void spiDetachMISO(void *, int) {}
