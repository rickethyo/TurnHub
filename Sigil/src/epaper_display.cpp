#include "epaper_display.h"
#include "display_name.h"

#include <SPI.h>
#include <cstring>

namespace TurnHubSigil {
namespace {
constexpr int16_t MARGIN = 6;
constexpr int16_t CHAR_WIDTH = 6;
constexpr int16_t LEGEND_LINE = 11;
// Legend order and glyphs (built-in font: 0x09 ring, 0x18-0x1B arrows).
constexpr Key LEGEND_KEYS[] = {Key::Select, Key::Up, Key::Down, Key::Left, Key::Right};
char legendGlyph(Key key) {
  switch (key) {
    case Key::Up: return 0x18;
    case Key::Down: return 0x19;
    case Key::Right: return 0x1A;
    case Key::Left: return 0x1B;
    default: return 0x09;
  }
}
}

uint8_t EpaperDisplay::legendLines() const {
  if (!menu_.active) return 0;
  uint8_t lines = 0;
  for (Key key : LEGEND_KEYS) lines += menu_.compass[static_cast<uint8_t>(key)] != MENU_NONE;
  return lines;
}

int16_t EpaperDisplay::contentBottom() const {
  const uint8_t lines = legendLines();
  return display_.height() - (lines ? 3 + LEGEND_LINE * lines : 0);
}

void EpaperDisplay::drawLegend() {
  if (legendLines() == 0) return;
  int16_t y = contentBottom();
  display_.drawFastHLine(MARGIN, y, display_.width() - 2 * MARGIN, GxEPD_BLACK);
  y += 3;
  display_.setTextSize(1);
  for (Key key : LEGEND_KEYS) {
    const uint8_t action = menu_.compass[static_cast<uint8_t>(key)];
    if (action == MENU_NONE) continue;
    const auto a = static_cast<TurnHubProtocol::SigilAction>(action);
    char line[24];
    snprintf(line, sizeof(line), "%c %s%s", legendGlyph(key), sigilActionLabel(a),
        TurnHubProtocol::sigilActionHold(a) != TurnHubProtocol::ActionHold::None ? " (hold)" : "");
    display_.setCursor(MARGIN, y + 1);
    printClipped(line, (display_.width() - 2 * MARGIN) / CHAR_WIDTH);
    y += LEGEND_LINE;
  }
}

EpaperDisplay::EpaperDisplay()
    : display_(GxEPD2_213_B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)) {}

void EpaperDisplay::begin() {
  SPI.begin(18, 19, 23, EPD_CS);
  // This panel is write-only. Release MISO (GPIO 19) for the Pair button.
  // The installed ESP32 core maps MISO=-1 back to its default GPIO 19,
  // so explicitly detach it instead of relying on -1 to disable the input.
  spiDetachMISO(SPI.bus(), 19);

  display_.init(115200);
  gameFrameValid_ = false;
  partialRefreshCount_ = 0;
  display_.setRotation(DISPLAY_ROTATION);
  display_.setTextWrap(false);
  display_.setTextColor(GxEPD_BLACK);
  display_.setFullWindow();

  Serial.printf("SIGIL|DISPLAY|READY|%dx%d|ROTATION|%u\n",
      display_.width(), display_.height(), DISPLAY_ROTATION);
  Serial.printf("SIGIL|DISPLAY|POLICY|%s\n",
      ENABLE_GAME_PARTIAL_REFRESH && GxEPD2_213_B74::hasFastPartialUpdate
          ? "PARTIAL_TRIAL" : "FULL_ONLY");
}

bool EpaperDisplay::setSeatName(uint8_t slot, const char *name) {
  if (slot == 1) return updateDisplayName(seatNameA_, name);
  if (slot == 2) return updateDisplayName(seatNameB_, name);
  return false;
}

void EpaperDisplay::printClipped(const char *text, uint8_t maxChars) {
  if (text == nullptr || maxChars == 0) return;
  for (uint8_t i = 0; i < maxChars && text[i] != '\0'; ++i) {
    display_.print(text[i]);
  }
}

void EpaperDisplay::drawCentered(const char *text, int16_t y, uint8_t maxSize) {
  const int16_t width = display_.width() - 2 * MARGIN;
  const size_t length = strlen(text);
  uint8_t size = maxSize;
  while (size > 1 && length * CHAR_WIDTH * size > static_cast<size_t>(width)) --size;
  const size_t capacity = width / (CHAR_WIDTH * size);
  const size_t count = length < capacity ? length : capacity;
  display_.setTextSize(size);
  // The built-in font uses top-left cursors and 6x8 character cells.
  display_.setCursor((display_.width() - count * CHAR_WIDTH * size) / 2, y);
  printClipped(text, count);
}

void EpaperDisplay::drawTwoLines(const char *text, int16_t y, int16_t width) {
  // Keep all 12 protocol name characters at size 2, including unbroken names.
  const size_t capacity = width / (2 * CHAR_WIDTH);
  const size_t length = strlen(text);
  if (length <= capacity) {
    drawCentered(text, y + 9, 2);
    return;
  }
  size_t split = capacity;
  for (size_t i = capacity; i > 0; --i) {
    if (text[i] == ' ' && length - i - 1 <= capacity) {
      split = i;
      break;
    }
  }
  char first[20] = {};
  memcpy(first, text, split);
  drawCentered(first, y, 2);
  const char *rest = text + split;
  while (*rest == ' ') ++rest;
  drawCentered(rest, y + 18, 2);
}

void EpaperDisplay::drawHeader(
    const char *title, uint8_t sigilId, bool host, uint8_t turnNumber) {
  drawCentered(title, 4, 2);
  if (sigilId != 0xFF) {
    display_.setTextSize(1);
    display_.setCursor(MARGIN, 25);
    display_.printf("S%u%s", static_cast<unsigned>(sigilId + 1), host ? " HOST" : "");
    if (turnNumber != 0) {
      char turn[12];
      snprintf(turn, sizeof(turn), "TURN %u", static_cast<unsigned>(turnNumber));
      display_.setCursor(display_.width() - MARGIN - strlen(turn) * CHAR_WIDTH, 25);
      display_.print(turn);
    }
  }
  display_.drawFastHLine(MARGIN, 36, display_.width() - 2 * MARGIN, GxEPD_BLACK);
}

void EpaperDisplay::drawStatus(const char *line1, const char *line2) {
  gameFrameValid_ = false;
  partialRefreshCount_ = 0;
  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawHeader("TurnHub");
    drawTwoLines(line1, 82, display_.width() - 2 * MARGIN);
    if (line2 != nullptr) drawCentered(line2, 146);
    drawLegend();
  } while (display_.nextPage());
}

void EpaperDisplay::drawSeat(
    const char *label, const char *name, int16_t y, int16_t height, bool focused) {
  const int16_t width = display_.width() - 2 * MARGIN;
  const bool compact = height < 62;  // No room for a two-line size-2 name.
  if (focused) {
    display_.fillRect(MARGIN, y + (compact ? 1 : 4), width, compact ? 12 : 16, GxEPD_BLACK);
    display_.setTextColor(GxEPD_WHITE);
  }
  drawCentered(label, y + (compact ? 3 : 8));
  display_.setTextColor(GxEPD_BLACK);
  if (compact) {
    drawCentered(name, y + 17, 2);
  } else {
    drawTwoLines(name, y + 26, width);
  }
  display_.drawFastHLine(MARGIN, y + height - 1, width, GxEPD_BLACK);
}

void EpaperDisplay::showBooting() {
  drawStatus("Booting");
}

void EpaperDisplay::showUnpaired() {
  drawStatus("Unpaired", "Press Pair on both");
}

void EpaperDisplay::showReady(uint8_t sigilId) {
  char title[24];
  snprintf(title, sizeof(title), "Sigil %u", static_cast<unsigned>(sigilId + 1));
  drawStatus(title, "Ready for game");
}

void EpaperDisplay::showGame(const TurnHubProtocol::GameDisplayPacket &s) {
  const uint8_t primary = TurnHubProtocol::displayPrimaryPlayer(s.state);
  const uint8_t secondary = TurnHubProtocol::displaySecondaryPlayer(s.state);
  const bool shared = secondary != 0;
  const bool active = TurnHubProtocol::hasDisplayFlag(s.state, TurnHubProtocol::DISPLAY_FLAG_ACTIVE);
  const char primarySeat = primary < secondary ? 'A' : 'B';
  const int16_t width = display_.width() - 2 * MARGIN;
  char life[16];
  snprintf(life, sizeof(life), "%ld", static_cast<long>(s.primary.life));

  const int16_t bottom = contentBottom();
  const int16_t secondaryY = min<int16_t>(s.commander ? 216 : 149, bottom - 36);
  const int16_t commanderLimit = shared ? secondaryY : bottom;
  const bool partial = ENABLE_GAME_PARTIAL_REFRESH &&
      GxEPD2_213_B74::hasFastPartialUpdate && gameFrameValid_ &&
      gameFrameSigilId_ == s.sigilId && gameFrameShared_ == shared &&
      gameFrameCommander_ == static_cast<bool>(s.commander) &&
      partialRefreshCount_ < MAX_PARTIAL_REFRESHES;
  const uint32_t refreshStartMs = millis();
  if (partial) {
    // Redraw one complete snapshot using the differential waveform. GxEPD2
    // aligns the 122 visible columns to RAM bytes and syncs both image buffers.
    display_.setPartialWindow(0, 0, display_.width(), display_.height());
  } else {
    display_.setFullWindow();
  }
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawHeader(s.commander ? "Commander" : "Game", s.sigilId,
        TurnHubProtocol::hasDisplayFlag(s.state, TurnHubProtocol::DISPLAY_FLAG_HOST),
        TurnHubProtocol::displayTurnNumber(s.state));
    // Atlas places the active local player first; keep the turn cue with them.
    if (active) {
      display_.fillRect(MARGIN, 40, width, 20, GxEPD_BLACK);
      display_.setTextColor(GxEPD_WHITE);
      drawCentered("YOUR TURN", 42, 2);
      display_.setTextColor(GxEPD_BLACK);
    } else {
      drawCentered("WAITING FOR TURN", 46);
    }
    drawTwoLines(s.primary.name, 66, width);
    drawCentered(life, 104, shared ? 4 : 6);
    char label[20] = "LIFE";
    if (shared) snprintf(label, sizeof(label), "LIFE / SEAT %c", primarySeat);
    drawCentered(label, shared ? 138 : 156);

    if (s.commander) {
      // Received damage belongs to the primary player, above the other seat.
      const int16_t commanderY = shared ? 152 : 188;
      char heading[20] = "CMD TAKEN";
      if (s.omittedSources) snprintf(heading, sizeof(heading), "CMD TAKEN +%u", s.omittedSources);
      if (commanderY + 10 <= commanderLimit) drawCentered(heading, commanderY);
      if (!s.sourceCount && commanderY + 42 <= commanderLimit) {
        drawCentered("No commander", commanderY + 20);
        drawCentered("damage received", commanderY + 32);
      }
      for (uint8_t i = 0; i < s.sourceCount; ++i) {
        if (commanderY + 28 + 16 * i > commanderLimit) break;  // Legend below.
        const auto &entry = s.sources[i];
        display_.setTextSize(1);
        display_.setCursor(MARGIN, commanderY + 12 + 16 * i);
        printClipped(entry.name, TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH);
        char damage[24];
        // Keep slot identity when only commander 2 has damage.
        if (!entry.damage[0] && entry.damage[1]) {
          snprintf(damage, sizeof(damage), "%ld (C2)", static_cast<long>(entry.damage[1]));
        } else if (entry.damage[1]) {
          snprintf(damage, sizeof(damage), "%ld/%ld",
              static_cast<long>(entry.damage[0]), static_cast<long>(entry.damage[1]));
        } else {
          snprintf(damage, sizeof(damage), "%ld", static_cast<long>(entry.damage[0]));
        }
        display_.setCursor(display_.width() - MARGIN - strlen(damage) * CHAR_WIDTH, commanderY + 20 + 16 * i);
        display_.print(damage);
      }
    }

    if (shared) {
      display_.drawFastHLine(MARGIN, secondaryY, width, GxEPD_BLACK);
      char otherName[20];
      snprintf(otherName, sizeof(otherName), "%c: %s", primarySeat == 'A' ? 'B' : 'A', s.secondary.name);
      drawCentered(otherName, secondaryY + 5);
      char otherLife[24];
      snprintf(otherLife, sizeof(otherLife), "%ld LIFE", static_cast<long>(s.secondary.life));
      drawCentered(otherLife, secondaryY + 17, 2);
    }
    drawLegend();
  } while (display_.nextPage());
  // Unlike the full path, GxEPD2's partial path leaves panel power enabled.
  // Power off after both RAM images are synchronized; do not reset/hibernate.
  display_.powerOff();
  partialRefreshCount_ = partial ? partialRefreshCount_ + 1 : 0;
  gameFrameValid_ = true;
  gameFrameSigilId_ = s.sigilId;
  gameFrameShared_ = shared;
  gameFrameCommander_ = s.commander != 0;
  Serial.printf("SIGIL|DISPLAY|REFRESH|%s|MS|%lu|PARTIALS|%u\n",
      partial ? "PARTIAL" : "FULL",
      static_cast<unsigned long>(millis() - refreshStartMs),
      static_cast<unsigned>(partialRefreshCount_));
}

void EpaperDisplay::showState(
    uint8_t sigilId,
    TurnHubProtocol::DisplayMode mode,
    uint8_t primaryPlayer,
    uint8_t secondaryPlayer,
    uint8_t turnNumber,
    uint8_t flags) {
  const bool active = (flags & TurnHubProtocol::DISPLAY_FLAG_ACTIVE) != 0;
  const bool host = (flags & TurnHubProtocol::DISPLAY_FLAG_HOST) != 0;
  const bool starter = (flags & TurnHubProtocol::DISPLAY_FLAG_STARTER) != 0;
  const bool winner = (flags & TurnHubProtocol::DISPLAY_FLAG_WINNER) != 0;
  const bool attention = (flags & TurnHubProtocol::DISPLAY_FLAG_ATTENTION) != 0;

  const char *header = "TurnHub";
  const char *status = "WAITING";
  bool indicateSeat = false;
  switch (mode) {
    case TurnHubProtocol::DisplayMode::Lobby:
      header = "Lobby";
      status = secondaryPlayer ? "SHARED SIGIL" : (starter ? "STARTER" : "IN LOBBY");
      break;
    case TurnHubProtocol::DisplayMode::Starting:
      header = "Starting";
      status = starter ? "GO FIRST" : "GET READY";
      indicateSeat = starter;
      break;
    case TurnHubProtocol::DisplayMode::Running:
      header = "Game";
      status = active ? "YOUR TURN" : "WAITING";
      indicateSeat = active;
      break;
    case TurnHubProtocol::DisplayMode::Paused:
      header = "Paused";
      status = attention ? "ACTION NEEDED" : "GAME PAUSED";
      indicateSeat = attention;
      break;
    case TurnHubProtocol::DisplayMode::GameOver:
      header = "Game Over";
      status = winner ? "WINNER!" : "GAME COMPLETE";
      indicateSeat = winner;
      break;
    case TurnHubProtocol::DisplayMode::Ready:
    default:
      showReady(sigilId);
      return;
  }

  const bool shared = secondaryPlayer != 0;
  const bool focused = active || starter || winner || attention;
  const bool focusA = focused && primaryPlayer < secondaryPlayer;
  const bool focusB = focused && primaryPlayer > secondaryPlayer;
  // Without a legend these are the original 196 / 201 / 239 positions.
  const int16_t bottom = contentBottom();
  const int16_t divider = bottom - 54;
  gameFrameValid_ = false;
  partialRefreshCount_ = 0;
  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawHeader(header, sigilId, host, turnNumber);
    if (shared) {
      // Keep physical A/B ordering stable; Atlas may put the focused seat first.
      const int16_t seatHeight = min<int16_t>(69, (divider - 44 - 7) / 2);
      drawSeat("SEAT A", seatNameA_[0] ? seatNameA_ : "Guest", 44, seatHeight, focusA);
      drawSeat("SEAT B", seatNameB_[0] ? seatNameB_ : "Guest", 44 + seatHeight + 7, seatHeight, focusB);
    } else {
      char player[20] = "Ready";
      if (primaryPlayer) snprintf(player, sizeof(player), "Player %u", static_cast<unsigned>(primaryPlayer));
      drawSeat(player, seatNameA_[0] ? seatNameA_ : player, 55, min<int16_t>(98, divider - 59), focused);
    }
    display_.drawFastHLine(MARGIN, divider, display_.width() - 2 * MARGIN, GxEPD_BLACK);
    drawTwoLines(status, bottom - 49, display_.width() - 2 * MARGIN);
    if (shared && indicateSeat) drawCentered(focusA ? "SEAT A" : "SEAT B", bottom - 11);
    drawLegend();
  } while (display_.nextPage());
}

}  // namespace TurnHubSigil
