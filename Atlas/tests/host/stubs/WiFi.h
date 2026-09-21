#pragma once
#include <Arduino.h>
constexpr int WIFI_AP_STA=0;
struct WiFiStub {
  void mode(int) {}
  bool softAP(const char*,const char*,int,bool,int) { return true; }
  const char *softAPIP() { return "192.168.4.1"; }
  const char *macAddress() { return "test"; }
  int softAPgetStationNum() { return 0; }
};
extern WiFiStub WiFi;
