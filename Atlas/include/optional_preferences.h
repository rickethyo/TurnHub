#pragma once

#include <Preferences.h>
#include <nvs.h>

namespace TurnHub {

// Optional values are absent on fresh profiles and after clearing a setting.
// Probe the requested type directly: isKey() cannot distinguish absence from
// other lookup failures. Only NOT_FOUND is quiet; Preferences still reports
// type mismatches, invalid handles, read errors, and write/commit failures.
class OptionalPreferences : public Preferences {
 public:
  String getString(const char *key, String fallback = String()) {
    size_t length = 0;
    if (_started && key != nullptr &&
        nvs_get_str(_handle, key, nullptr, &length) == ESP_ERR_NVS_NOT_FOUND) {
      return fallback;
    }
    return Preferences::getString(key, fallback);
  }

  size_t getBytesLength(const char *key) {
    size_t length = 0;
    if (_started && key != nullptr &&
        nvs_get_blob(_handle, key, nullptr, &length) == ESP_ERR_NVS_NOT_FOUND) {
      return 0;
    }
    return Preferences::getBytesLength(key);
  }

  bool remove(const char *key) {
    if (!_started || _readOnly || key == nullptr) {
      return Preferences::remove(key);
    }
    const esp_err_t error = nvs_erase_key(_handle, key);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
      return true;
    }
    if (error != ESP_OK) {
      log_e("nvs_erase_key fail: %s %s", key, esp_err_to_name(error));
      return false;
    }
    const esp_err_t commitError = nvs_commit(_handle);
    if (commitError != ESP_OK) {
      log_e("nvs_commit fail: %s", esp_err_to_name(commitError));
      return false;
    }
    return true;
  }
};

}  // namespace TurnHub
