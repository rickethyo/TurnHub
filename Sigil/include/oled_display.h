#pragma once

#include <Adafruit_SH110X.h>
#include <memory>

#include "oled_config.h"
#include "sigil_display.h"
#include "sigil_icons.h"

namespace TurnHubSigil {

class OledDisplay final : public SigilDisplay {
 public:
  explicit OledDisplay(const OledConfig &config = OLED_CONFIG) : config_(config) {}
  void begin() override;
  void showBooting() override;
  void showUnpaired() override;
  void showReady(uint8_t sigilId) override;
  void showAtlasLost(uint8_t sigilId) override;
  void showPairingCode(uint16_t code) override;
  void showUpdate(const char *status, int8_t percent) override;
  void showSleeping() override;
  bool setSeatName(uint8_t slot, const char *name) override;
  void showGame(const TurnHubProtocol::GameDisplayPacket &snapshot) override;
  void showState(uint8_t sigilId, TurnHubProtocol::DisplayMode mode,
      uint8_t primaryPlayer, uint8_t secondaryPlayer, uint8_t turnNumber,
      uint8_t flags) override;
  void showPicker(const TurnHubProtocol::ProfilePickerPacket &page, uint8_t cursor) override;
  // The one-line key legend steps to its next key every LEGEND_STEP_MS.
  uint32_t idleWorkDueInMs(uint32_t nowMs) const override;
  void idleWork(uint32_t nowMs) override;
  static constexpr uint32_t LEGEND_STEP_MS = 2500;
#ifdef TURNHUB_SCREEN_PREVIEW
  // The host screen preview reads the canvas back (tests/host/render_sigil_screens.cpp).
  Adafruit_GFX &previewGfx() { return *display_; }
#endif

 private:
  enum class Align : uint8_t { Left, Center, Right };

  bool validConfig() const;
  int16_t text(const char *value, int16_t y, uint8_t maxSize, Align align,
      bool inverse = false, int16_t left = 0, int16_t right = -1);
  // Brass text (brass_fonts.h) inside the rows [top, bottom) and columns
  // [left, right), baseline as low as the band allows. Returns the x just past
  // the text, or -1 (nothing drawn) if it does not fit, so the caller can
  // fall back to the built-in font.
  int16_t fontText(const char *value, const GFXfont *font, int16_t top, int16_t bottom,
      Align align, bool inverse = false, int16_t left = 0, int16_t right = -1);
  // A rule with a center diamond, the Brass divider.
  void rule(int16_t y, int16_t left, int16_t right);
  // A status screen's big line (Cinzel, else built-in size 2) over a rule.
  void bigLine(const char *big);
  void header(const char *title, const char *right, bool host = false);
  void banner(const char *message, int16_t y, bool highlight, Icon kind);
  void icon(Icon kind, int16_t x, int16_t y, uint16_t color);
  void lifeTotal(int32_t life, int16_t y, uint8_t maxSize);
  void splash(const char *caption);
  void status(const char *headerRight, const char *big, const char *first,
      const char *second = nullptr);
  // Draws the open menu list instead of the current screen; false if closed.
  bool drawDeviceMenu();
  // The key legend has room for one line here, so it shows one key at a
  // time: the click first, then the others in turn (idleWork). Each entry is
  // the key's glyph and its words. Returns the number of entries.
  uint8_t legendEntries(char entries[][24]) const;
  // Draws the current entry on the bottom row and remembers the row so
  // idleWork can step it; drawn = false notes this screen has no legend.
  void legend(bool drawn);
  void drawLegendRow();

  const OledConfig config_;
  std::unique_ptr<Adafruit_SH1106G> display_;
  bool ready_ = false;
  bool legendShown_ = false;
  uint8_t legendIndex_ = 0;
  MenuView legendMenu_;              // The menu the index belongs to.
  mutable bool legendTimed_ = false;  // legendSinceMs_ is set.
  mutable uint32_t legendSinceMs_ = 0;
  char seatNameA_[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};
  char seatNameB_[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1] = {};
};

}  // namespace TurnHubSigil
