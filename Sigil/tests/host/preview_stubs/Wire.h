#pragma once
struct TwoWire {
  bool setPins(int, int) { return true; }
};
extern TwoWire Wire;
