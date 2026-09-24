#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>

#include "protocol.h"

namespace TurnHubSigil {

class SigilDisplay {
 public:
  SigilDisplay();

  void begin();
  void showUnpaired();
  void showReady(uint8_t sigilId);
  bool setSeatName(uint8_t slot, const char *name);
  void showGame(const TurnHubProtocol::GameDisplayPacket &snapshot);
  void showState(
      uint8_t sigilId,
      TurnHubProtocol::DisplayMode mode,
      uint8_t primaryPlayer,
      uint8_t secondaryPlayer,
      uint8_t turnNumber,
      uint8_t flags);

 private:
  void drawHeader(const char *title, uint8_t sigilId = 0xFF,
      bool host = false, uint8_t turnNumber = 0);
  void drawStatus(const char *line1, const char *line2 = nullptr);
  void drawCentered(const char *text, int16_t y, uint8_t maxSize = 1);
  void drawTwoLines(const char *text, int16_t y, int16_t width);
  void drawSeat(const char *label, const char *name, int16_t y,
      int16_t height, bool focused);
  void printClipped(const char *text, uint8_t maxChars);

  // Native portrait is 122 visible pixels by 250 (128 RAM columns).
  // Use 2 instead of 0 if the physical panel is mounted upside down.
  static constexpr uint8_t DISPLAY_ROTATION = 0;

  static constexpr int8_t EPD_CS = 17;
  static constexpr int8_t EPD_DC = 16;
  static constexpr int8_t EPD_RST = 22;
  static constexpr int8_t EPD_BUSY = 21;

  char seatNameA_[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};
  char seatNameB_[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};

  GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display_;
};

}  // namespace TurnHubSigil
