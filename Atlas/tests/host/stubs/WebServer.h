#pragma once
#include <Arduino.h>
constexpr int HTTP_GET=0;
class WebServer {
 public:
  explicit WebServer(int) {}
  void sendHeader(const char*,const char*) {}
  void send(int,const char*,const char*) {}
  template<class F> void on(const char*,int,F) {}
  template<class F> void onNotFound(F) {}
  void begin() {}
  void handleClient() {}
};
