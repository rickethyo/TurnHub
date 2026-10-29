#pragma once

// A small JSON value for turnhub-core: parse, build and print. Portable C++17
// with no other dependency, so the same code runs on Atlas (ESP32-S3), in the
// Android app (NDK) and in host tests. Objects keep insertion order.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace turnhub {

class Json {
 public:
  enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };

  Json() = default;
  Json(std::nullptr_t) {}
  Json(bool b) : type_(Type::Bool), bool_(b) {}
  Json(int v) : type_(Type::Number), number_(v) {}
  Json(int64_t v) : type_(Type::Number), number_(static_cast<double>(v)) {}
  Json(double v) : type_(Type::Number), number_(v) {}
  Json(const char *s) : type_(Type::String), string_(s) {}
  Json(std::string s) : type_(Type::String), string_(std::move(s)) {}

  static Json array() { Json j; j.type_ = Type::Array; return j; }
  static Json object() { Json j; j.type_ = Type::Object; return j; }

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isBool() const { return type_ == Type::Bool; }
  bool isNumber() const { return type_ == Type::Number; }
  bool isString() const { return type_ == Type::String; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }

  bool asBool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
  int64_t asInt(int64_t fallback = 0) const { return type_ == Type::Number ? static_cast<int64_t>(number_) : fallback; }
  double asDouble(double fallback = 0) const { return type_ == Type::Number ? number_ : fallback; }
  const std::string &asString() const { static const std::string empty; return type_ == Type::String ? string_ : empty; }

  // Arrays.
  size_t size() const { return type_ == Type::Array ? items_.size() : type_ == Type::Object ? members_.size() : 0; }
  const Json &operator[](size_t i) const { static const Json none; return type_ == Type::Array && i < items_.size() ? items_[i] : none; }
  Json &push(Json v) { if (type_ != Type::Array) { *this = array(); } items_.push_back(std::move(v)); return items_.back(); }

  // Objects.
  bool has(const std::string &key) const;
  const Json &get(const std::string &key) const;
  Json &set(const std::string &key, Json v);
  const std::vector<std::pair<std::string, Json>> &members() const { return members_; }

  bool operator==(const Json &o) const;
  bool operator!=(const Json &o) const { return !(*this == o); }

  std::string dump() const;
  // Returns Null and sets ok=false on malformed text.
  static Json parse(const std::string &text, bool *ok = nullptr);

 private:
  void dumpTo(std::string &out) const;
  Type type_ = Type::Null;
  bool bool_ = false;
  double number_ = 0;
  std::string string_;
  std::vector<Json> items_;
  std::vector<std::pair<std::string, Json>> members_;
};

}  // namespace turnhub
