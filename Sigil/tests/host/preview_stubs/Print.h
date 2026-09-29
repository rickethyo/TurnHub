#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <cstdio>
#include <string>

class __FlashStringHelper;
class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  unsigned int length() const { return static_cast<unsigned int>(size()); }
};

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *buffer, size_t size) {
    size_t n = 0;
    while (size--) n += write(*buffer++);
    return n;
  }
  size_t write(const char *text) { return text ? write(reinterpret_cast<const uint8_t *>(text), strlen(text)) : 0; }
  size_t print(const char *text) { return write(text); }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
  size_t print(const String &s) { return write(s.c_str()); }
  size_t print(int value) { return printf("%d", value); }
  size_t print(unsigned value) { return printf("%u", value); }
  size_t print(long value) { return printf("%ld", value); }
  size_t print(unsigned long value) { return printf("%lu", value); }
  size_t println(const char *text = "") { return print(text) + print('\n'); }
  template <typename... Args> size_t printf(const char *format, Args... args) {
    char text[256];
    std::snprintf(text, sizeof(text), format, args...);
    return write(text);
  }
};
