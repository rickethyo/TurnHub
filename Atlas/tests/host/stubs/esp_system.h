#pragma once
#include <Arduino.h>
inline void esp_fill_random(void *data, size_t n) { auto *b=static_cast<uint8_t *>(data); while(n--) *b++=static_cast<uint8_t>(esp_random()); }
