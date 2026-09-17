#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>

namespace TurnHubSigil {

// Inland / Keyestudio KS0461-compatible 2.13" 250x122 monochrome e-paper.
// The commonly reported panel is compatible with the Waveshare 2.13 V3
// family (SSD1680). We use GxEPD2's 2.13" SSD1680 driver.
class SigilDisplay {
 public:
  SigilDisplay();

  void begin();
  void showUnassigned();
  void showAssigned(uint8_t sigilId);

 private:
  void drawHeader();
  void drawStatus(const char *line1, const char *line2 = nullptr);

  static constexpr int8_t EPD_CS = 17;
  static constexpr int8_t EPD_DC = 16;
  static constexpr int8_t EPD_RST = 22;
  static constexpr int8_t EPD_BUSY = 21;

  GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display_;
};

}  // namespace TurnHubSigil
