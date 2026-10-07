#pragma once

#include <stdint.h>

namespace TurnHubFirmware {

// Keep MAJOR/MINOR/PATCH and VERSION in step. The numbers go into the
// firmware descriptor that OTA packages are checked against (FIRMWARE_UPDATES.md).
constexpr uint8_t MAJOR = 0;
constexpr uint8_t MINOR = 6;
constexpr uint8_t PATCH = 16;
constexpr const char *VERSION = "0.6.16-dev";
constexpr const char *BUILD_DATE = __DATE__;
constexpr const char *BUILD_TIME = __TIME__;

}  // namespace TurnHubFirmware
