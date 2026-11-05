#pragma once
#include <Arduino.h>
constexpr int WIFI_AP_STA=3;
constexpr int WIFI_STA=1;
struct WiFiStub {
  int lastMode = WIFI_STA;
  unsigned apStarts = 0;
  void mode(int value) { lastMode = value; }
  bool setSleep(bool) { return true; }
  bool softAP(const char*,const char*,int,bool,int) { ++apStarts; return true; }
  const char *softAPIP() { return "192.168.4.1"; }
  const char *macAddress() { return "test"; }
  int softAPgetStationNum() { return 0; }
  int channel() { return 6; }
};
extern WiFiStub WiFi;
