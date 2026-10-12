#pragma once
#define RTC_DATA_ATTR
constexpr int ESP_RST_POWERON = 1, ESP_RST_SW = 3, ESP_RST_DEEPSLEEP = 5;
extern int fixtureResetReason;
inline int esp_reset_reason() { return fixtureResetReason; }
