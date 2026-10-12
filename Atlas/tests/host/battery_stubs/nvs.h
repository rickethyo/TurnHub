#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
using esp_err_t = int;
using nvs_handle_t = uint32_t;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, ESP_ERR_NVS_TYPE_MISMATCH = 2;
constexpr int NVS_READWRITE = 1;
extern std::map<std::string,std::vector<uint8_t>> fixtureFlash;
extern std::vector<uint8_t> fixturePending;
extern unsigned fixtureCommits;
extern bool fixtureFailCommit;
inline const char *esp_err_to_name(int) { return "injected battery storage error"; }
inline int nvs_open(const char *name, int, nvs_handle_t *handle) {
  if (strcmp(name,"battery")) return ESP_ERR_NVS_NOT_FOUND;
  *handle = 1; return ESP_OK;
}
inline void nvs_close(nvs_handle_t) {}
inline int nvs_get_blob(nvs_handle_t,const char *key,void *out,size_t *size) {
  const auto found = fixtureFlash.find(key);
  if (found == fixtureFlash.end()) return ESP_ERR_NVS_NOT_FOUND;
  if (out) {
    if (*size < found->second.size()) return ESP_ERR_NVS_TYPE_MISMATCH;
    memcpy(out,found->second.data(),found->second.size());
  }
  *size = found->second.size(); return ESP_OK;
}
inline int nvs_set_blob(nvs_handle_t,const char *,const void *data,size_t size) {
  const auto bytes = static_cast<const uint8_t *>(data);
  fixturePending.assign(bytes,bytes+size); return ESP_OK;
}
inline int nvs_commit(nvs_handle_t) {
  ++fixtureCommits;
  if (fixtureFailCommit) return ESP_ERR_NVS_TYPE_MISMATCH;
  fixtureFlash["gauge"] = fixturePending; return ESP_OK;
}
inline int nvs_erase_key(nvs_handle_t,const char *key) {
  return fixtureFlash.erase(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
