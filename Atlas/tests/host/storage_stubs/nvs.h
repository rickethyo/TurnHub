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
extern std::map<std::string, std::string> strings;
extern std::map<std::string, uint8_t> bytes;
extern int openError, readError, setError, commitError;
extern int writes, commits;
extern int reads, failReadAt;
extern const char *openedNamespace;
inline void reset() {
  blobs.clear();
  strings.clear();
  bytes.clear();
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
inline int nvs_get_str(nvs_handle_t, const char *key, char *out, size_t *size) {
  if (FakeNvs::readError != ESP_OK) return FakeNvs::readError;
  auto found = FakeNvs::strings.find(key);
  if (found == FakeNvs::strings.end()) return ESP_ERR_NVS_NOT_FOUND;
  if (out) {
    if (*size < found->second.size() + 1) return ESP_ERR_NVS_INVALID_HANDLE;
    memcpy(out, found->second.c_str(), found->second.size() + 1);
  }
  *size = found->second.size() + 1;
  return ESP_OK;
}
inline int nvs_erase_key(nvs_handle_t, const char *key) {
  if (FakeNvs::setError != ESP_OK) return FakeNvs::setError;
  const auto removed = FakeNvs::blobs.erase(key) + FakeNvs::strings.erase(key) + FakeNvs::bytes.erase(key);
  if (removed) ++FakeNvs::writes;
  return removed ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
constexpr int NVS_TYPE_U8 = 1;
using nvs_iterator_t = std::map<std::string, uint8_t>::const_iterator *;
struct nvs_entry_info_t { char key[16]; };
inline nvs_iterator_t nvs_entry_find(const char *, const char *, int) {
  return FakeNvs::bytes.empty() ? nullptr : new std::map<std::string, uint8_t>::const_iterator(FakeNvs::bytes.begin());
}
inline void nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t *info) {
  strncpy(info->key, (*it)->first.c_str(), sizeof(info->key));
}
inline nvs_iterator_t nvs_entry_next(nvs_iterator_t it) {
  if (++(*it) == FakeNvs::bytes.end()) { delete it; return nullptr; }
  return it;
}
inline void nvs_release_iterator(nvs_iterator_t it) { delete it; }
