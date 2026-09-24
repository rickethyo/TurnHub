#pragma once
#include <Arduino.h>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using esp_err_t=int;
using nvs_handle_t=uint32_t;
constexpr int ESP_OK=0, ESP_ERR_NVS_NOT_FOUND=1, ESP_ERR_NVS_TYPE_MISMATCH=2,
    ESP_ERR_NVS_INVALID_HANDLE=3;
constexpr int NVS_READWRITE = 1;
extern esp_err_t readError, eraseError, injectedCommitError;
// Fault injection for the raw blob open/write path used by NvsBlobStore
// (game recovery). Defaults to success so existing Preferences-based fault
// injection above is unaffected by default.
extern esp_err_t openError, setError;
// Backing store for nvs_get_blob/nvs_set_blob, shared across a test binary
// run so a simulated reboot (fresh GameEngine/Lobby, same call to
// beginGameRecovery) sees whatever a prior "boot" actually wrote -- exactly
// like real flash surviving a power cycle.
extern std::map<std::string, std::vector<uint8_t>> testBlobs;
// OptionalPreferences (see optional_preferences.h) probes nvs_get_blob/
// nvs_get_str directly and has always treated this stub as a pure
// error-injection lens: readError alone decides NOT_FOUND vs a real error vs
// "found, use the canned Preferences::getBytesLength/getString value". That
// contract predates NvsBlobStore's use of this same stub file and existing
// scenarios (see optionalStorage()) depend on it, so it stays the default.
// The game-recovery scenario opts into real map-backed storage for the
// duration of its own test and switches back off when done.
extern bool useRealNvsBlobs;
inline const char *esp_err_to_name(int) { return "test error"; }
inline int nvs_open(const char*, int, nvs_handle_t *handle) { *handle=1; return openError; }
inline void nvs_close(nvs_handle_t) {}
inline int nvs_get_str(uint32_t,const char*,char*,size_t*) { return readError; }
inline int nvs_get_blob(uint32_t,const char *key,void *out,size_t *size) {
  if (!useRealNvsBlobs) return readError;
  if (readError != ESP_OK) return readError;
  auto found = testBlobs.find(key);
  if (found == testBlobs.end()) return ESP_ERR_NVS_NOT_FOUND;
  if (out != nullptr) {
    if (*size < found->second.size()) return ESP_ERR_NVS_INVALID_HANDLE;
    memcpy(out, found->second.data(), found->second.size());
  }
  *size = found->second.size();
  return ESP_OK;
}
inline int nvs_set_blob(nvs_handle_t, const char *key, const void *data, size_t size) {
  if (setError != ESP_OK) return setError;
  const auto *b = static_cast<const uint8_t *>(data);
  testBlobs[key] = std::vector<uint8_t>(b, b + size);
  return ESP_OK;
}
inline int nvs_erase_key(uint32_t,const char*) { return eraseError; }
inline int nvs_commit(uint32_t) { return injectedCommitError; }
