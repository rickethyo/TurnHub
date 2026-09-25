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
  bool validConfig() const;
  void line(const char *text, int16_t y, uint8_t maxSize = 1, bool inverse = false);
  void header(const char *title, uint8_t sigilId, uint8_t turn, bool host);
  void status(const char *first, const char *second = nullptr,
      const char *third = nullptr);

  const OledConfig config_;
  std::unique_ptr<Adafruit_SH1106G> display_;
  bool ready_ = false;
  char seatNameA_[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};
  char seatNameB_[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};
};

}  // namespace TurnHubSigil
