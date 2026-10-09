#pragma once

#include <stdint.h>

namespace TurnHubFirmware {

// Keep MAJOR/MINOR/PATCH and VERSION in step. The numbers go into the
// firmware descriptor that OTA packages are checked against (FIRMWARE_UPDATES.md).
constexpr uint8_t MAJOR = 0;
constexpr uint8_t MINOR = 7;
constexpr uint8_t PATCH = 5;
constexpr const char *VERSION = "0.7.5-dev";
constexpr const char *BUILD_DATE = __DATE__;
constexpr const char *BUILD_TIME = __TIME__;

}  // namespace TurnHubFirmware
