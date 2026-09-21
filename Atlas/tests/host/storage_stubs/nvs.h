#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using esp_err_t = int;
using nvs_handle_t = uint32_t;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1,
    ESP_ERR_NVS_TYPE_MISMATCH = 2, ESP_ERR_NVS_INVALID_HANDLE = 3;
constexpr int NVS_READWRITE = 1;
namespace FakeNvs {
extern std::map<std::string, std::vector<uint8_t>> blobs;
extern int openError, readError, setError, commitError;
extern int writes, commits;
extern int reads, failReadAt;
extern const char *openedNamespace;
inline void reset() {
  blobs.clear();
  openError = readError = setError = commitError = ESP_OK;
  writes = commits = 0;
  reads = failReadAt = 0;
  openedNamespace = nullptr;
}
}
inline const char *esp_err_to_name(int) { return "injected error"; }
inline int nvs_open(const char *name, int, nvs_handle_t *handle) {
  FakeNvs::openedNamespace = name;
  *handle = 1;
  return FakeNvs::openError;
}
inline void nvs_close(nvs_handle_t) {}
inline int nvs_get_blob(nvs_handle_t, const char *key, void *out, size_t *size) {
  ++FakeNvs::reads;
  if (FakeNvs::failReadAt == FakeNvs::reads) return ESP_ERR_NVS_INVALID_HANDLE;
  if (FakeNvs::readError != ESP_OK) return FakeNvs::readError;
  auto found = FakeNvs::blobs.find(key);
  if (found == FakeNvs::blobs.end()) return ESP_ERR_NVS_NOT_FOUND;
  if (out != nullptr) {
    if (*size < found->second.size()) return ESP_ERR_NVS_INVALID_HANDLE;
    memcpy(out, found->second.data(), found->second.size());
  }
  *size = found->second.size();
  return ESP_OK;
}
inline int nvs_set_blob(nvs_handle_t, const char *key, const void *data, size_t size) {
  ++FakeNvs::writes;
  if (FakeNvs::setError != ESP_OK) return FakeNvs::setError;
  const auto *bytes = static_cast<const uint8_t *>(data);
  FakeNvs::blobs[key] = std::vector<uint8_t>(bytes, bytes + size);
  return ESP_OK;
}
inline int nvs_commit(nvs_handle_t) {
  ++FakeNvs::commits;
  return FakeNvs::commitError;
}
