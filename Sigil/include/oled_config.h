#pragma once

#include <stdint.h>

namespace TurnHubSigil {

enum class OledController { Unspecified, Sh1106 };
enum class OledBus { Unspecified, I2c, SoftwareSpi };
enum class OledPower { Unspecified, InternalChargePump };

// Field defaults are unset so a partially filled config is rejected before any
// bus/pin initialization. OLED_CONFIG below is the selected hardware profile.
struct OledConfig {
  OledController controller = OledController::Unspecified;
  OledBus bus = OledBus::Unspecified;
  uint16_t width = 0;   // Native pixels (adapter supports 128x64).
  uint16_t height = 0;
  int8_t rotation = -1; // 0 or 2; portrait layout is not scaffolded.
  OledPower power = OledPower::Unspecified;
  int8_t reset = -2;    // GPIO, or explicitly -1 for no reset connection.
  int16_t i2cAddress = -1; // Explicit 7-bit address, never auto-selected.
  uint32_t i2cClockHz = 0;
  int8_t sda = -1;        // I2C GPIO assignments.
  int8_t scl = -1;
  int8_t mosi = -1;       // 4-wire software SPI GPIO assignments.
  int8_t sclk = -1;
  int8_t dc = -1;
  int8_t cs = -1;
};

// Inland 1.3-inch OLED V2.0 on the Sigil carrier's EPD header, 4-wire SPI.
// Owner-verified wiring (2026-09-24) against the Sigil schematic:
//   CLK J19/GPIO18, MOSI J12/GPIO23, RES J13/GPIO22, DC J22/GPIO16,
//   CS J21/GPIO17, VCC 3.3 V, GND common. EPD_BUSY/GPIO21 is unused.
// Controller SH1106 128x64 is inferred from the vendor (KS0056) example and
// still needs confirmation on the panel itself.
inline OledConfig makeOledConfig() {
  OledConfig c{};
  c.controller = OledController::Sh1106;
  c.bus = OledBus::SoftwareSpi;
  c.width = 128;
  c.height = 64;
  c.rotation = 2;  // Panel is mounted upside down on the carrier.
  c.power = OledPower::InternalChargePump;
  c.reset = 22;
  c.mosi = 23;
  c.sclk = 18;
  c.dc = 16;
  c.cs = 17;
  return c;
}

const OledConfig OLED_CONFIG = makeOledConfig();

}  // namespace TurnHubSigil
