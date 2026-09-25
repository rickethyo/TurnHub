#pragma once

#include <stdint.h>

namespace TurnHubSigil {

enum class OledController { Unspecified, Sh1106 };
enum class OledBus { Unspecified, I2c, SoftwareSpi };
enum class OledPower { Unspecified, InternalChargePump };

// Photo evidence: Inland 1.3-inch OLED V2.0; matching KS0056 vendor example
// uses SH1106 128x64 SPI. Confirm the actual module/jumpers before enabling.
// TODO(hardware): fill this configuration only after identifying the module
// and reviewing the wiring. Supported options are NOT a selected hardware BOM.
// Unspecified values prevent all OLED bus/pin initialization.
struct OledConfig {
  OledController controller = OledController::Unspecified;
  OledBus bus = OledBus::Unspecified;
  uint16_t width = 0;   // TODO: native pixels (adapter supports 128x64).
  uint16_t height = 0;
  int8_t rotation = -1; // TODO: 0 or 2; portrait layout is not scaffolded.
  OledPower power = OledPower::Unspecified; // TODO: module power arrangement.
  int8_t reset = -2;    // TODO: GPIO, or explicitly -1 for no reset connection.
  int16_t i2cAddress = -1; // TODO: explicit 7-bit address, never auto-selected.
  uint32_t i2cClockHz = 0; // TODO: verified bus speed.
  int8_t sda = -1;        // TODO: I2C GPIO assignments.
  int8_t scl = -1;
  int8_t mosi = -1;       // TODO: 4-wire software SPI GPIO assignments.
  int8_t sclk = -1;
  int8_t dc = -1;
  int8_t cs = -1;
};

constexpr OledConfig OLED_CONFIG{};

}  // namespace TurnHubSigil
