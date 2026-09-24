#pragma once
#include <cstdint>
#include <cstdarg>
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
  String substring(size_t begin, size_t end) const { return String(substr(begin, end - begin)); }
  String substring(size_t begin) const { return String(substr(begin)); }
};
extern uint32_t testNow;
inline uint32_t millis() { return testNow; }
inline void delay(uint32_t ms) { testNow += ms; }
constexpr int HIGH=1, LOW=0, INPUT_PULLUP=2, OUTPUT=3;
constexpr float PI=3.14159265358979323846f;
extern int testDigitalRead;
inline int digitalRead(int) { return testDigitalRead; }
inline void digitalWrite(int,int) {}
inline void pinMode(int,int) {}
struct SerialStub {
  void begin(int) {}
  template<class T> void print(const T&) {}
  template<class T> void println(const T&) {}
  void println() {}
  size_t write(const uint8_t *, size_t size) { return size; }
};
constexpr int DEC=10, HEX=16;
// Minimal Arduino Print: subclasses implement write(); formatting matches the
// framework for the value types Atlas logs (text, chars, decimal/hex integers).
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t byte) = 0;
  virtual size_t write(const uint8_t *data, size_t size) {
    size_t n = 0; while (size--) n += write(*data++); return n;
  }
  size_t print(const char *text) { return text ? write(reinterpret_cast<const uint8_t *>(text), strlen(text)) : 0; }
  size_t print(const String &text) { return write(reinterpret_cast<const uint8_t *>(text.data()), text.size()); }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
  template<class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  size_t print(T value, int base = DEC) {
    char text[24];
    if (base == HEX) snprintf(text, sizeof(text), "%llX", static_cast<unsigned long long>(value));
    else if (std::is_signed<T>::value) snprintf(text, sizeof(text), "%lld", static_cast<long long>(value));
    else snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
    return print(text);
  }
  size_t println() { return print("\r\n"); }
  template<class T> size_t println(const T &value) { const size_t n = print(value); return n + println(); }
  template<class T> size_t println(T value, int base) { const size_t n = print(value, base); return n + println(); }
  size_t printf(const char *format, ...) __attribute__((format(printf, 2, 3))) {
    char text[256]; va_list args; va_start(args, format);
    const int length = vsnprintf(text, sizeof(text), format, args); va_end(args);
    return length > 0 ? print(text) : 0;
  }
};
extern SerialStub Serial;
extern int loggedErrors;
#define log_e(...) (++loggedErrors)
extern uint32_t testRandom;
inline uint32_t esp_random() { testRandom = testRandom * 1664525UL + 1013904223UL; return testRandom; }
struct EspStub { void restart() {} };
extern EspStub ESP;
