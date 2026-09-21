#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
class String : public std::string {
 public:
  using std::string::string;
  String() = default;
};
inline uint32_t testNow = 1000;
inline uint32_t millis() { return testNow; }
inline void delay(uint32_t ms) { testNow += ms; }
constexpr int HIGH=1, LOW=0, INPUT_PULLUP=2, OUTPUT=3;
inline int digitalRead(int) { return HIGH; }
inline void digitalWrite(int,int) {}
inline void pinMode(int,int) {}
struct SerialStub {
  void begin(int) {}
  template<class T> void print(const T&) {}
  template<class T> void println(const T&) {}
  void println() {}
};
inline SerialStub Serial;
inline int loggedErrors=0;
#define log_e(...) (++loggedErrors)
inline uint32_t esp_random() { return 12345; }
