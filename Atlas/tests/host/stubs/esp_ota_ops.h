#pragma once
#include <Arduino.h>
#include <vector>
#include "nvs.h"
struct esp_partition_t { uint32_t size; };
inline std::vector<uint8_t> &testOtaFlash() { static std::vector<uint8_t> bytes(0x1E0000, 0xFF); return bytes; }
inline const esp_partition_t *esp_ota_get_next_update_partition(void *) { static esp_partition_t p{0x1E0000}; return &p; }
inline int esp_partition_erase_range(const esp_partition_t *, size_t at, size_t n) {
 if(at+n>testOtaFlash().size()) return -1;
 std::fill(testOtaFlash().begin()+at,testOtaFlash().begin()+at+n,0xFF);return ESP_OK;
}
inline int esp_partition_write(const esp_partition_t *, size_t at, const void *data, size_t n) {
 if(at+n>testOtaFlash().size()) return -1;
 memcpy(testOtaFlash().data()+at,data,n);return ESP_OK;
}
inline int esp_partition_read(const esp_partition_t *, size_t at, void *data, size_t n) {
 if(at+n>testOtaFlash().size()) return -1;
 memcpy(data,testOtaFlash().data()+at,n);return ESP_OK;
}
