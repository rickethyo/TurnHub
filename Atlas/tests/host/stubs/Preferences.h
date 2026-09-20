#pragma once
#include <Arduino.h>
#include <nvs.h>
// Mimics the framework's error reporting contract; NVS faults are injectable.
class Preferences {
 protected:
  uint32_t _handle=1;
  bool _started=false, _readOnly=false;
 public:
  bool begin(const char*,bool readOnly=false) { _started=true; _readOnly=readOnly; return true; }
  void end() { _started=false; }
  String getString(const char*,String fallback=String()) {
    if(readError!=ESP_OK) { log_e("read"); return fallback; }
    return "stored";
  }
  size_t getBytesLength(const char*) { if(readError!=ESP_OK) { log_e("blob"); return 0; } return 4; }
  bool remove(const char*) { log_e("erase"); return false; }
  size_t putString(const char*,const String &s) { return s.length(); }
};
