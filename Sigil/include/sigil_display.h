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
  void setSeatName(uint8_t slot, const char *name);
  void showState(
      uint8_t sigilId,
      TurnHubProtocol::DisplayMode mode,
      uint8_t primaryPlayer,
      uint8_t secondaryPlayer,
      uint8_t turnNumber,
      uint8_t flags);

 private:
  void drawHeader(const char *title);
  void drawStatus(const char *line1, const char *line2 = nullptr);
  void drawPlayerLabel(uint8_t primaryPlayer, uint8_t secondaryPlayer);
  void printClipped(const char *text, uint8_t maxChars);

  static constexpr int8_t EPD_CS = 17;
  static constexpr int8_t EPD_DC = 16;
  static constexpr int8_t EPD_RST = 22;
  static constexpr int8_t EPD_BUSY = 21;

  char seatNameA_[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};
  char seatNameB_[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};

  GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display_;
};

}  // namespace TurnHubSigil
