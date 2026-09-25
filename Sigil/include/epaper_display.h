#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>

#include "sigil_display.h"
#include "sigil_icons.h"

namespace TurnHubSigil {

class EpaperDisplay final : public SigilDisplay {
 public:
  EpaperDisplay();

  void begin() override;
  void showBooting() override;
  void showUnpaired() override;
  void showReady(uint8_t sigilId) override;
  bool setSeatName(uint8_t slot, const char *name) override;
  void showGame(const TurnHubProtocol::GameDisplayPacket &snapshot) override;
  void showState(
      uint8_t sigilId,
      TurnHubProtocol::DisplayMode mode,
      uint8_t primaryPlayer,
      uint8_t secondaryPlayer,
      uint8_t turnNumber,
      uint8_t flags) override;

 private:
  void drawHeader(const char *title, uint8_t sigilId = 0xFF,
      bool host = false, uint8_t turnNumber = 0);
  void drawStatus(const char *line1, const char *line2 = nullptr);
  void drawBanner(const char *message, int16_t y, bool highlight, Icon kind, uint8_t maxSize = 1);
  void drawLife(int32_t life, int16_t y, uint8_t maxSize);
  void drawCentered(const char *text, int16_t y, uint8_t maxSize = 1);
  void drawTwoLines(const char *text, int16_t y, int16_t width);
  void drawSeat(const char *label, const char *name, int16_t y,
      int16_t height, bool focused);
  void printClipped(const char *text, uint8_t maxChars);
  // Compass legend: one line per key with an action, at the bottom.
  uint8_t legendLines() const;
  int16_t contentBottom() const;
  void drawLegend();

  // Native portrait is 122 visible pixels by 250 (128 RAM columns).
  // Use 2 instead of 0 if the physical panel is mounted upside down.
  static constexpr uint8_t DISPLAY_ROTATION = 0;
  // The bench panel loses contrast during partial updates. Use full refreshes
  // until the exact panel/driver match and differential waveform are verified.
  static constexpr bool ENABLE_GAME_PARTIAL_REFRESH = false;
  static constexpr uint8_t MAX_PARTIAL_REFRESHES = 10;

  static constexpr int8_t EPD_CS = 17;
  static constexpr int8_t EPD_DC = 16;
  static constexpr int8_t EPD_RST = 22;
  static constexpr int8_t EPD_BUSY = 21;

  char seatNameA_[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};
  char seatNameB_[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};

  bool gameFrameValid_ = false;
  bool gameFrameShared_ = false;
  bool gameFrameCommander_ = false;
  uint8_t gameFrameSigilId_ = 0xFF;
  uint8_t partialRefreshCount_ = 0;

  GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display_;
};

}  // namespace TurnHubSigil
