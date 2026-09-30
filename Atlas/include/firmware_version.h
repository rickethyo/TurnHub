#pragma once

#include <stdint.h>

namespace TurnHubFirmware {

// Keep MAJOR/MINOR/PATCH and VERSION in step. The numbers go into the
// firmware descriptor that OTA packages are checked against (SIGIL_OTA.md).
constexpr uint8_t MAJOR = 0;
constexpr uint8_t MINOR = 6;
constexpr uint8_t PATCH = 1;
constexpr const char *VERSION = "0.6.1-dev";
constexpr const char *BUILD_DATE = __DATE__;
constexpr const char *BUILD_TIME = __TIME__;

}  // namespace TurnHubFirmware
