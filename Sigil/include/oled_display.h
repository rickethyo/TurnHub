#pragma once

#include <Adafruit_SH110X.h>
#include <memory>

#include "oled_config.h"
#include "sigil_display.h"

namespace TurnHubSigil {

class OledDisplay final : public SigilDisplay {
 public:
  explicit OledDisplay(const OledConfig &config = OLED_CONFIG) : config_(config) {}
  void begin() override;
  void showBooting() override;
  void showUnpaired() override;
  void showReady(uint8_t sigilId) override;
  bool setSeatName(uint8_t slot, const char *name) override;
  void showGame(const TurnHubProtocol::GameDisplayPacket &snapshot) override;
  void showState(uint8_t sigilId, TurnHubProtocol::DisplayMode mode,
      uint8_t primaryPlayer, uint8_t secondaryPlayer, uint8_t turnNumber,
      uint8_t flags) override;

 private:
  enum class Align : uint8_t { Left, Center, Right };
  enum class Icon : uint8_t { None, Turn, TurnBack, Pause, Crown };

  bool validConfig() const;
  int16_t text(const char *value, int16_t y, uint8_t maxSize, Align align,
      bool inverse = false, int16_t left = 0, int16_t right = -1);
  void header(const char *title, const char *right, bool host = false);
  void banner(const char *message, int16_t y, bool highlight, Icon kind);
  void icon(Icon kind, int16_t x, int16_t y, uint16_t color);
  void heart(int16_t x, int16_t y);
  void lifeTotal(int32_t life, int16_t y, uint8_t maxSize);
  void splash(const char *caption);
  void status(const char *headerRight, const char *big, const char *first,
      const char *second = nullptr);
  // Draws the open menu list instead of the current screen; false if closed.
  bool drawMenuList();

  const OledConfig config_;
  std::unique_ptr<Adafruit_SH1106G> display_;
  bool ready_ = false;
  char seatNameA_[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};
  char seatNameB_[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};
};

}  // namespace TurnHubSigil
