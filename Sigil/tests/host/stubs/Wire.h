#pragma once
struct TwoWire {
  int calls = 0, sda = -1, scl = -1;
  bool succeeds = true;
  bool setPins(int data, int clock) {
    ++calls; sda = data; scl = clock; return succeeds;
  }
};
extern TwoWire Wire;
