#include "nvs_blob_store.h"
#include <Arduino.h>
#include <string.h>

namespace TurnHubStorage {
namespace {
bool validKey(const char *key) {
  return key != nullptr && key[0] != '\0' && strlen(key) <= 15;
}

Status readStatus(esp_err_t error) {
  if (error == ESP_OK) return Status::Ok;
  if (error == ESP_ERR_NVS_NOT_FOUND) return Status::NotFound;
  log_e("TurnHub storage read failed: %s", esp_err_to_name(error));
  return error == ESP_ERR_NVS_TYPE_MISMATCH ? Status::Corrupt : Status::IoError;
}
}  // namespace

NvsBlobStore::~NvsBlobStore() {
  if (ready_) nvs_close(handle_);
}

Status NvsBlobStore::begin(const char *nameSpace) {
  if (ready_) return Status::Ok;
  if (!validKey(nameSpace)) return Status::InvalidArgument;
  const esp_err_t error = nvs_open(nameSpace, NVS_READWRITE, &handle_);
  ready_ = error == ESP_OK;
  if (!ready_) log_e("TurnHub storage open failed: %s", esp_err_to_name(error));
  return ready_ ? Status::Ok : Status::Unavailable;
}

Status NvsBlobStore::read(const char *key, void *data, size_t capacity,
                          size_t &size) {
  size = 0;
  if (!ready_) return Status::Unavailable;
  if (!validKey(key) || (data == nullptr && capacity != 0)) {
    return Status::InvalidArgument;
  }
  Status status = readStatus(nvs_get_blob(handle_, key, nullptr, &size));
  if (status != Status::Ok || data == nullptr) return status;
  if (size > capacity) return Status::Corrupt;
  size_t actual = capacity;
  status = readStatus(nvs_get_blob(handle_, key, data, &actual));
  if (status == Status::Ok && actual != size) return Status::Corrupt;
  return status;
}

Status NvsBlobStore::write(const char *key, const void *data, size_t size) {
  if (!ready_) return Status::Unavailable;
  if (!validKey(key) || data == nullptr || size == 0) return Status::InvalidArgument;
  esp_err_t error = nvs_set_blob(handle_, key, data, size);
  if (error == ESP_OK) error = nvs_commit(handle_);
  if (error != ESP_OK) {
    log_e("TurnHub storage write failed: %s", esp_err_to_name(error));
    return Status::IoError;
  }
  return Status::Ok;
}
}  // namespace TurnHubStorage
