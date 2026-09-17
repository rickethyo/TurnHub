#include "sigil_display.h"

#include <SPI.h>

namespace TurnHubSigil {

SigilDisplay::SigilDisplay()
    : display_(GxEPD2_213_B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)) {}

void SigilDisplay::begin() {
  // Standard ESP32 VSPI pins. The display does not use MISO, but supplying
  // GPIO19 here keeps the hardware SPI bus configured conventionally.
  SPI.begin(18, 19, 23, EPD_CS);

  display_.init(115200);
  display_.setRotation(1);
  display_.setTextColor(GxEPD_BLACK);
  display_.setFullWindow();

  Serial.println("SIGIL|DISPLAY|READY|250x122");
}

void SigilDisplay::drawHeader() {
  display_.setTextSize(2);
  display_.setCursor(12, 24);
  display_.print("TurnHub");
  display_.drawFastHLine(12, 34, display_.width() - 24, GxEPD_BLACK);
}

void SigilDisplay::drawStatus(const char *line1, const char *line2) {
  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawHeader();

    display_.setTextSize(2);
    display_.setCursor(12, 68);
    display_.print(line1);

    if (line2 != nullptr) {
      display_.setTextSize(1);
      display_.setCursor(12, 96);
      display_.print(line2);
    }
  } while (display_.nextPage());
}

void SigilDisplay::showUnassigned() {
  drawStatus("Sigil", "Waiting for Atlas...");
}

void SigilDisplay::showAssigned(uint8_t sigilId) {
  char title[24];
  snprintf(title, sizeof(title), "Sigil %u", static_cast<unsigned>(sigilId + 1));
  drawStatus(title, "Connected to Atlas");
}

}  // namespace TurnHubSigil
