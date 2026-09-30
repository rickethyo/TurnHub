#pragma once
#include <Arduino.h>
#include <nvs.h>
#include <map>
#include <string>
// Mimics the framework's error reporting contract; NVS faults are injectable.
class Preferences {
 protected:
  uint32_t _handle=1;
  bool _started=false, _readOnly=false;
 public:
  // Like the framework, a failed nvs_open (including NOT_FOUND) is logged.
  bool begin(const char *name,bool readOnly=false) {
    if(_started) return false;
    if(nvs_open(name,readOnly?NVS_READONLY:NVS_READWRITE,&_handle)!=ESP_OK) { log_e("open"); return false; }
    _started=true; _readOnly=readOnly; return true;
  }
  void end() { _started=false; }
  // Strings written with putString read back (the Wi-Fi password); any
  // other present key reads as "stored".
  static std::map<std::string,String> &strings() { static std::map<std::string,String> saved; return saved; }
  String getString(const char *key,String fallback=String()) {
    if(readError!=ESP_OK) { log_e("read"); return fallback; }
    const auto found=strings().find(key);
    return found!=strings().end()?found->second:String("stored");
  }
  size_t getBytesLength(const char*) { if(readError!=ESP_OK) { log_e("blob"); return 0; } return 4; }
  bool remove(const char*) { log_e("erase"); return false; }
  size_t putString(const char *key,const String &s) { strings()[key]=s; return s.length(); }
};
