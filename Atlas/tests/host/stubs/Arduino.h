#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <cctype>
#include <type_traits>
#define PROGMEM
#define F(value) value
using __FlashStringHelper = char;
class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  String(const std::string &value) : std::string(value) {}
  template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  String(T value) : std::string(std::to_string(value)) {}
  long toInt() const { return strtol(c_str(), nullptr, 10); }
  bool equalsIgnoreCase(const String &other) const {
    if (size() != other.size()) return false;
    for (size_t i=0;i<size();++i) if (tolower((*this)[i]) != tolower(other[i])) return false;
    return true;
  }
  void toUpperCase() { for (auto &c : *this) c = static_cast<char>(toupper(c)); }
  void replace(const char *from, const char *to) {
    size_t pos=0; while ((pos=find(from,pos))!=npos) { std::string::replace(pos,strlen(from),to); pos+=strlen(to); }
  }
  void trim() {
    const auto first=find_first_not_of(" \t\r\n"), last=find_last_not_of(" \t\r\n");
    *this = first==npos ? std::string() : substr(first,last-first+1);
  }
  void remove(size_t index) { erase(index); }
};
extern uint32_t testNow;
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
extern SerialStub Serial;
extern int loggedErrors;
#define log_e(...) (++loggedErrors)
extern uint32_t testRandom;
inline uint32_t esp_random() { testRandom = testRandom * 1664525UL + 1013904223UL; return testRandom; }
struct EspStub { void restart() {} };
extern EspStub ESP;
