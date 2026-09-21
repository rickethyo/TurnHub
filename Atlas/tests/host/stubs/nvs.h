#pragma once
#include <Arduino.h>
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_ERR_NVS_NOT_FOUND=1, ESP_ERR_NVS_TYPE_MISMATCH=2,
    ESP_ERR_NVS_INVALID_HANDLE=3;
extern esp_err_t readError, eraseError, injectedCommitError;
inline const char *esp_err_to_name(int) { return "test error"; }
inline int nvs_get_str(uint32_t,const char*,char*,size_t*) { return readError; }
inline int nvs_get_blob(uint32_t,const char*,void*,size_t*) { return readError; }
inline int nvs_erase_key(uint32_t,const char*) { return eraseError; }
inline int nvs_commit(uint32_t) { return injectedCommitError; }
