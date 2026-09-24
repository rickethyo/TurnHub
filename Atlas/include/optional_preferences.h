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
  // A namespace never written (e.g. no owner Wi-Fi password yet) is absent,
  // not broken. Preferences::begin logs every read-only open of one, and the
  // admin portal polls /api/network, so a read-only NOT_FOUND returns false
  // quietly. Other open failures still go through Preferences and log.
  bool begin(const char *name, bool readOnly = false) {
    if (readOnly && !_started && name != nullptr) {
      nvs_handle_t handle = 0;
      const esp_err_t error = nvs_open(name, NVS_READONLY, &handle);
      if (error == ESP_ERR_NVS_NOT_FOUND) {
        return false;
      }
      if (error == ESP_OK) {
        _handle = handle;
        _readOnly = true;
        _started = true;
        return true;
      }
    }
    return Preferences::begin(name, readOnly);
  }

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
