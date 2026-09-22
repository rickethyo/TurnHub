#pragma once
#include <Arduino.h>
#include <nvs.h>

class Preferences {
 protected:
  nvs_handle_t _handle = 1;
  bool _started = false, _readOnly = false;
 public:
  bool begin(const char *, bool readOnly = false) {
    _readOnly = readOnly;
    return _started = FakeNvs::openError == ESP_OK;
  }
  void end() { _started = false; }
  bool isKey(const char *key) {
    return _started && (FakeNvs::bytes.count(key) || FakeNvs::strings.count(key) || FakeNvs::blobs.count(key));
  }
  String getString(const char *key, String fallback = String()) {
    auto found = FakeNvs::strings.find(key);
    return found == FakeNvs::strings.end() || FakeNvs::readError != ESP_OK ? fallback : String(found->second);
  }
  size_t putString(const char *key, const String &value) {
    if (!_started || _readOnly || FakeNvs::setError != ESP_OK) return 0;
    FakeNvs::strings[key] = value;
    ++FakeNvs::writes;
    return nvs_commit(_handle) == ESP_OK ? value.length() : 0;
  }
  uint8_t getUChar(const char *key, uint8_t fallback = 0) {
    auto found = FakeNvs::bytes.find(key);
    return found == FakeNvs::bytes.end() ? fallback : found->second;
  }
  size_t putUChar(const char *key, uint8_t value) {
    if (!_started || _readOnly || FakeNvs::setError != ESP_OK) return 0;
    FakeNvs::bytes[key] = value;
    ++FakeNvs::writes;
    return nvs_commit(_handle) == ESP_OK ? 1 : 0;
  }
  size_t getBytesLength(const char *key) {
    size_t size = 0;
    return nvs_get_blob(_handle, key, nullptr, &size) == ESP_OK ? size : 0;
  }
  bool remove(const char *key) { return nvs_erase_key(_handle, key) == ESP_OK && nvs_commit(_handle) == ESP_OK; }
};
