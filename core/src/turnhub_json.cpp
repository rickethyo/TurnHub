#include "turnhub_json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace turnhub {

bool Json::has(const std::string &key) const {
  if (type_ != Type::Object) return false;
  for (const auto &m : members_) if (m.first == key) return true;
  return false;
}

const Json &Json::get(const std::string &key) const {
  static const Json none;
  if (type_ != Type::Object) return none;
  for (const auto &m : members_) if (m.first == key) return m.second;
  return none;
}

Json &Json::set(const std::string &key, Json v) {
  if (type_ != Type::Object) *this = object();
  for (auto &m : members_) if (m.first == key) { m.second = std::move(v); return m.second; }
  members_.emplace_back(key, std::move(v));
  return members_.back().second;
}

bool Json::operator==(const Json &o) const {
  if (type_ != o.type_) return false;
  switch (type_) {
    case Type::Null: return true;
    case Type::Bool: return bool_ == o.bool_;
    case Type::Number: return number_ == o.number_;
    case Type::String: return string_ == o.string_;
    case Type::Array: return items_ == o.items_;
    case Type::Object:
      if (members_.size() != o.members_.size()) return false;
      for (const auto &m : members_) if (!o.has(m.first) || o.get(m.first) != m.second) return false;
      return true;
  }
  return false;
}

namespace {

void escape(const std::string &s, std::string &out) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
        else out += static_cast<char>(c);
    }
  }
  out += '"';
}

struct Parser {
  const std::string &s;
  size_t i = 0;
  bool ok = true;

  void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
  bool eat(char c) { ws(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
  bool word(const char *w) { size_t n = 0; while (w[n]) ++n; if (s.compare(i, n, w) == 0) { i += n; return true; } return false; }

  static void utf8(uint32_t cp, std::string &out) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | cp >> 6); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += static_cast<char>(0xE0 | cp >> 12); out += static_cast<char>(0x80 | (cp >> 6 & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { out += static_cast<char>(0xF0 | cp >> 18); out += static_cast<char>(0x80 | (cp >> 12 & 0x3F)); out += static_cast<char>(0x80 | (cp >> 6 & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
  }

  uint32_t hex4() {
    if (i + 4 > s.size()) { ok = false; return 0; }
    uint32_t v = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = s[i++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= c - '0';
      else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
      else { ok = false; return 0; }
    }
    return v;
  }

  std::string str() {
    std::string out;
    if (!eat('"')) { ok = false; return out; }
    while (i < s.size() && s[i] != '"') {
      char c = s[i++];
      if (c != '\\') { out += c; continue; }
      if (i >= s.size()) { ok = false; return out; }
      c = s[i++];
      switch (c) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          uint32_t cp = hex4();
          if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
            i += 2;
            const uint32_t lo = hex4();
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          utf8(cp, out);
          break;
        }
        default: ok = false; return out;
      }
    }
    if (i >= s.size()) { ok = false; return out; }
    ++i;
    return out;
  }

  Json value(int depth) {
    if (depth > 64) { ok = false; return Json(); }
    ws();
    if (i >= s.size()) { ok = false; return Json(); }
    const char c = s[i];
    if (c == '{') {
      ++i;
      Json o = Json::object();
      if (eat('}')) return o;
      do {
        ws();
        std::string key = str();
        if (!ok || !eat(':')) { ok = false; return Json(); }
        o.set(key, value(depth + 1));
        if (!ok) return Json();
      } while (eat(','));
      if (!eat('}')) ok = false;
      return o;
    }
    if (c == '[') {
      ++i;
      Json a = Json::array();
      if (eat(']')) return a;
      do {
        a.push(value(depth + 1));
        if (!ok) return Json();
      } while (eat(','));
      if (!eat(']')) ok = false;
      return a;
    }
    if (c == '"') return Json(str());
    if (word("true")) return Json(true);
    if (word("false")) return Json(false);
    if (word("null")) return Json();
    const char *start = s.c_str() + i;
    char *end = nullptr;
    const double d = strtod(start, &end);
    if (end == start) { ok = false; return Json(); }
    i += static_cast<size_t>(end - start);
    return Json(d);
  }
};

}  // namespace

void Json::dumpTo(std::string &out) const {
  switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: {
      char buf[32];
      if (std::floor(number_) == number_ && std::fabs(number_) < 9.0e15) snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(number_));
      else snprintf(buf, sizeof(buf), "%.17g", number_);
      out += buf;
      break;
    }
    case Type::String: escape(string_, out); break;
    case Type::Array:
      out += '[';
      for (size_t k = 0; k < items_.size(); ++k) { if (k) out += ','; items_[k].dumpTo(out); }
      out += ']';
      break;
    case Type::Object:
      out += '{';
      for (size_t k = 0; k < members_.size(); ++k) {
        if (k) out += ',';
        escape(members_[k].first, out);
        out += ':';
        members_[k].second.dumpTo(out);
      }
      out += '}';
      break;
  }
}

std::string Json::dump() const {
  std::string out;
  dumpTo(out);
  return out;
}

Json Json::parse(const std::string &text, bool *ok) {
  Parser p{text};
  Json v = p.value(0);
  p.ws();
  if (p.i != text.size()) p.ok = false;
  if (ok) *ok = p.ok;
  return p.ok ? v : Json();
}

}  // namespace turnhub
