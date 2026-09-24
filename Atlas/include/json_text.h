#pragma once

// Minimal helpers for the hand-built JSON documents Atlas serves.

#include <Arduino.h>

namespace TurnHub {

// Escapes text for use inside a JSON string literal. Quotes, backslashes and
// common whitespace are escaped; other control characters are dropped.
inline String jsonEscape(const char *text) {
  String out;
  if (text == nullptr) return out;
  for (const char *p = text; *p; ++p) {
    const char c = *p;
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<uint8_t>(c) >= 0x20) out += c;
        break;
    }
  }
  return out;
}

inline String jsonEscape(const String &text) {
  return jsonEscape(text.c_str());
}

inline const char *jsonBool(bool value) {
  return value ? "true" : "false";
}

}  // namespace TurnHub
