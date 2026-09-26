#include "epaper_display.h"
#include "life_heart.h"
#include "display_name.h"

#include <SPI.h>
#include <cstring>

namespace TurnHubSigil {
namespace {
constexpr int16_t MARGIN = 6;
constexpr int16_t CHAR_WIDTH = 6;
constexpr int16_t LEGEND_LINE = 11;
constexpr int16_t HEADER_BAR = 36;     // Solid title bar.
constexpr int16_t BANNER_HEIGHT = 22;
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

// Left/Right carry a life request's answer, or free life changes.
bool EpaperDisplay::lifeRequestShown() const { return life_.request.target != 0; }
bool EpaperDisplay::lifeKeysShown() const {
  return !lifeRequestShown() && menu_.life &&
      menu_.compass[static_cast<uint8_t>(Key::Left)] == MENU_NONE &&
      menu_.compass[static_cast<uint8_t>(Key::Right)] == MENU_NONE;
}

uint8_t EpaperDisplay::legendLines() const {
  if (!menu_.active) return 0;
  uint8_t lines = 0;
  for (Key key : LEGEND_KEYS) {
    if (lifeRequestShown() && (key == Key::Left || key == Key::Right)) continue;
    lines += menu_.compass[static_cast<uint8_t>(key)] != MENU_NONE;
  }
  if (lifeRequestShown()) lines += 2;
  else if (lifeKeysShown()) lines += 1;
  return lines;
}

int16_t EpaperDisplay::contentBottom() const {
  const uint8_t lines = legendLines();
  return display_.height() - (lines ? 3 + LEGEND_LINE * lines : 0);
}

// Each line starts with a keycap: a filled square with the direction's arrow,
// or a filled circle for the click (the likely action, listed first).
void EpaperDisplay::drawLegend() {
  if (legendLines() == 0) return;
  int16_t y = contentBottom();
  display_.drawFastHLine(MARGIN, y, display_.width() - 2 * MARGIN, GxEPD_BLACK);
  y += 3;
  display_.setTextSize(1);
  constexpr int16_t CAP = LEGEND_LINE - 1;
  const auto line = [&](Key key, const char *label) {
    drawKeycap(key, MARGIN, y);
    display_.setCursor(MARGIN + CAP + 4, y + 1);
    printClipped(label, (display_.width() - 2 * MARGIN - CAP - 4) / CHAR_WIDTH);
    y += LEGEND_LINE;
  };
  if (lifeRequestShown()) {
    line(Key::Right, "Approve life");
    line(Key::Left, "Deny life");
  } else if (lifeKeysShown()) {
    // Both arrows on one line: Left lowers, Right raises; hold for speed.
    drawKeycap(Key::Left, MARGIN, y);
    drawKeycap(Key::Right, MARGIN + CAP + 2, y);
    display_.setCursor(MARGIN + 2 * CAP + 6, y + 1);
    printClipped("Life -/+ (hold)", (display_.width() - 2 * MARGIN - 2 * CAP - 6) / CHAR_WIDTH);
    y += LEGEND_LINE;
  }
  for (Key key : LEGEND_KEYS) {
    if (lifeRequestShown() && (key == Key::Left || key == Key::Right)) continue;
    const uint8_t action = menu_.compass[static_cast<uint8_t>(key)];
    if (action == MENU_NONE) continue;
    const auto a = static_cast<TurnHubProtocol::SigilAction>(action);
    drawKeycap(key, MARGIN, y);
    char line[24];
    snprintf(line, sizeof(line), "%s%s", sigilActionLabel(a),
        TurnHubProtocol::sigilActionHold(a) != TurnHubProtocol::ActionHold::None ? " (hold)" : "");
    display_.setCursor(MARGIN + CAP + 4, y + 1);
    printClipped(line, (display_.width() - 2 * MARGIN - CAP - 4) / CHAR_WIDTH);
    y += LEGEND_LINE;
  }
}

void EpaperDisplay::configurePartial(bool enabled, uint8_t maxPartials, uint32_t idleCleanupMs) {
  partialEnabled_ = enabled;
  maxPartials_ = maxPartials;
  idleCleanupMs_ = idleCleanupMs;
  Serial.printf("SIGIL|DISPLAY|PARTIAL|%s|MAX|%u|IDLE_MS|%lu\n", enabled ? "ON" : "OFF",
      static_cast<unsigned>(maxPartials), static_cast<unsigned long>(idleCleanupMs));
}

// Bench tuning over serial: "epd on", "epd off", "epd max <n>" (partials
// before a full clean-up, 1-50), "epd idle <seconds>" (0 = no idle
// clean-up), "epd status". RAM only; a reboot restores the defaults.
bool EpaperDisplay::handleCommand(const char *line) {
  if (strncmp(line, "epd", 3) != 0) return false;
  const char *arg = line + 3;
  while (*arg == ' ') ++arg;
  bool enabled = partialEnabled_;
  uint8_t maxPartials = maxPartials_;
  uint32_t idleMs = idleCleanupMs_;
  if (!strcmp(arg, "on")) enabled = true;
  else if (!strcmp(arg, "off")) enabled = false;
  else if (!strncmp(arg, "max ", 4)) {
    const int n = atoi(arg + 4);
    if (n < 1 || n > 50) { Serial.println("SIGIL|DISPLAY|PARTIAL|MAX_RANGE|1-50"); return true; }
    maxPartials = static_cast<uint8_t>(n);
  } else if (!strncmp(arg, "idle ", 5)) {
    const int seconds = atoi(arg + 5);
    if (seconds < 0 || seconds > 600) { Serial.println("SIGIL|DISPLAY|PARTIAL|IDLE_RANGE|0-600"); return true; }
    idleMs = static_cast<uint32_t>(seconds) * 1000;
  } else if (strcmp(arg, "status") != 0) {
    Serial.println("SIGIL|DISPLAY|EPD|USAGE|on off max <n> idle <s> status");
    return true;
  }
  configurePartial(enabled, maxPartials, idleMs);
  return true;
}

// A clean-up is due once the game screen shows partial updates and nothing
// changed for idleCleanupMs.
uint32_t EpaperDisplay::idleWorkDueInMs(uint32_t nowMs) const {
  if (!gameFrameValid_ || partialRefreshCount_ == 0 || idleCleanupMs_ == 0) return UINT32_MAX;
  const uint32_t idle = nowMs - lastPartialAtMs_;
  return idle >= idleCleanupMs_ ? 0 : idleCleanupMs_ - idle;
}

void EpaperDisplay::idleWork(uint32_t nowMs) {
  if (idleWorkDueInMs(nowMs) != 0) return;
  Serial.println("SIGIL|DISPLAY|CLEANUP|IDLE");
  forceFull_ = true;
  showGame(lastGame_);
}

void EpaperDisplay::drawKeycap(Key key, int16_t x, int16_t y) {
  constexpr int16_t CAP = LEGEND_LINE - 1;
  if (key == Key::Select) {
    display_.fillCircle(x + CAP / 2, y + CAP / 2, CAP / 2, GxEPD_BLACK);
    return;
  }
  display_.fillRoundRect(x, y, CAP, CAP, 2, GxEPD_BLACK);
  display_.setTextColor(GxEPD_WHITE);
  display_.setCursor(x + 2, y + 1);
  display_.print(legendGlyph(key));
  display_.setTextColor(GxEPD_BLACK);
}

// Profile picker: a fixed key per name (Up, Right, Down), so one full
// refresh per page. Each name says in words what choosing it means.
void EpaperDisplay::showPicker(const TurnHubProtocol::ProfilePickerPacket &page, uint8_t) {
  using TurnHubProtocol::PickerMode;
  using TurnHubProtocol::PickerNotice;
  constexpr Key ROW_KEYS[] = {Key::Up, Key::Right, Key::Down};
  constexpr int16_t ROW_TOP = HEADER_BAR + 6;
  constexpr int16_t ROW_HEIGHT = 34;
  const bool confirm = page.mode == PickerMode::Confirm;
  const char *notice = nullptr;
  switch (page.notice) {
    case PickerNotice::NeedsPhone: notice = "Sign in on phone"; break;
    case PickerNotice::Unavailable: notice = "Not available"; break;
    case PickerNotice::TableFull: notice = "Table is full"; break;
    case PickerNotice::Failed: notice = "Try again"; break;
    default: break;
  }
  gameFrameValid_ = false;
  partialRefreshCount_ = 0;
  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    display_.fillRect(0, 0, display_.width(), HEADER_BAR, GxEPD_BLACK);
    display_.setTextColor(GxEPD_WHITE);
    drawCentered(confirm ? "Join as" : "Who plays?", 4, 2);
    if (!confirm && page.pageCount > 1) {
      char pages[16];
      snprintf(pages, sizeof(pages), "Page %u of %u", static_cast<unsigned>(page.page + 1),
          static_cast<unsigned>(page.pageCount));
      drawCentered(pages, 24);
    }
    display_.setTextColor(GxEPD_BLACK);
    int16_t legendY = display_.height() - 3 - 2 * LEGEND_LINE;
    if (confirm) {
      drawTwoLines(page.items[0].name, 70, display_.width() - 2 * MARGIN);
      drawBanner("Join?", 130, true, Icon::None, 2);
    } else {
      for (uint8_t i = 0; i < page.itemCount && i < TurnHubProtocol::PICKER_PAGE_ITEMS; ++i) {
        const int16_t y = ROW_TOP + i * ROW_HEIGHT;
        const uint8_t flags = page.items[i].flags;
        drawKeycap(ROW_KEYS[i], MARGIN, y);
        const char *tag = (flags & TurnHubProtocol::PICKER_ITEM_GUEST) ? "no profile" :
            (flags & TurnHubProtocol::PICKER_ITEM_LOCKED) ? "phone sign-in" :
            (flags & TurnHubProtocol::PICKER_ITEM_PLAYING) ? "at table: attach" : "";
        display_.setTextSize(1);
        display_.setCursor(MARGIN + LEGEND_LINE + 3, y + 1);
        printClipped(tag, (display_.width() - 2 * MARGIN - LEGEND_LINE - 3) / CHAR_WIDTH);
        drawCentered(page.items[i].name, y + 13, 2);
      }
      if (notice) drawBanner(notice, ROW_TOP + 3 * ROW_HEIGHT + 2, true, Icon::None);
    }
    display_.drawFastHLine(MARGIN, legendY, display_.width() - 2 * MARGIN, GxEPD_BLACK);
    legendY += 3;
    display_.setTextSize(1);
    const bool more = !confirm && page.pageCount > 1;
    if (confirm || more) {
      drawKeycap(Key::Select, MARGIN, legendY);
      display_.setCursor(MARGIN + LEGEND_LINE + 3, legendY + 1);
      display_.print(confirm ? "Yes, join" : "More names");
      legendY += LEGEND_LINE;
    }
    drawKeycap(Key::Left, MARGIN, legendY);
    display_.setCursor(MARGIN + LEGEND_LINE + 3, legendY + 1);
    display_.print(confirm || page.page > 0 ? "Back" : "Cancel");
  } while (display_.nextPage());
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
      partialEnabled_ && GxEPD2_213_B74::hasFastPartialUpdate
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

// Solid title bar, as on the OLED Sigil: the title, then the Sigil number
// (with a crown and HOST for the host) and the turn number, in white.
void EpaperDisplay::drawHeader(
    const char *title, uint8_t sigilId, bool host, uint8_t turnNumber) {
  display_.fillRect(0, 0, display_.width(), HEADER_BAR, GxEPD_BLACK);
  display_.setTextColor(GxEPD_WHITE);
  drawCentered(title, 4, 2);
  if (sigilId != 0xFF) {
    display_.setTextSize(1);
    display_.setCursor(MARGIN, 24);
    display_.printf("S%u", static_cast<unsigned>(sigilId + 1));
    if (host) {
      drawIcon(display_, Icon::Crown, MARGIN + 16, 23, GxEPD_WHITE);
      display_.setCursor(MARGIN + 32, 24);
      display_.print("HOST");
    }
    if (turnNumber != 0) {
      char turn[12];
      snprintf(turn, sizeof(turn), "TURN %u", static_cast<unsigned>(turnNumber));
      display_.setCursor(display_.width() - MARGIN - strlen(turn) * CHAR_WIDTH, 24);
      display_.print(turn);
    }
  }
  display_.setTextColor(GxEPD_BLACK);
}

// A message box: filled (white text, icons both sides) when it concerns this
// Sigil now, framed otherwise. The words carry the meaning.
void EpaperDisplay::drawBanner(const char *message, int16_t y, bool highlight, Icon kind,
    uint8_t maxSize) {
  const int16_t width = display_.width() - 2 * MARGIN;
  // The largest text size that fits the box; drawCentered shrinks the same way.
  const int16_t length = static_cast<int16_t>(strlen(message));
  uint8_t size = maxSize;
  while (size > 1 && length * CHAR_WIDTH * size > width) --size;
  const bool icons = highlight && kind != Icon::None &&
      length * CHAR_WIDTH * size + 2 * (ICON_WIDTH + 6) <= width;
  if (highlight) {
    display_.fillRoundRect(MARGIN, y, width, BANNER_HEIGHT, 5, GxEPD_BLACK);
    display_.setTextColor(GxEPD_WHITE);
  } else {
    display_.drawRoundRect(MARGIN, y, width, BANNER_HEIGHT, 5, GxEPD_BLACK);
  }
  if (icons) {
    const int16_t iconY = y + (BANNER_HEIGHT - iconHeight(kind)) / 2;
    drawIcon(display_, kind, MARGIN + 4, iconY, GxEPD_WHITE);
    drawIcon(display_, kind == Icon::Turn ? Icon::TurnBack : kind,
        MARGIN + width - 4 - ICON_WIDTH, iconY, GxEPD_WHITE);
  }
  drawCentered(message, y + (BANNER_HEIGHT - 8 * size) / 2, size);
  display_.setTextColor(GxEPD_BLACK);
}

// A heart and the largest life total that fits, centered as one unit. The
// heart drains or grows against the starting life (life_heart.h).
void EpaperDisplay::drawLife(int32_t life, int16_t y, uint8_t maxSize) {
  char number[16];
  snprintf(number, sizeof(number), "%ld", static_cast<long>(life));
  const int16_t width = display_.width() - 2 * MARGIN;
  const int16_t length = static_cast<int16_t>(strlen(number));
  uint8_t size = maxSize;
  const uint8_t heartScale = maxSize >= 5 ? 2 : 1;
  const HeartLook look = lifeHeartLook(life, life_.startingLife);
  const int16_t heartW = static_cast<int16_t>(ICON_WIDTH * heartScale * look.sizePercent / 100);
  const int16_t heartH = static_cast<int16_t>(iconHeight(Icon::Heart) * heartScale * look.sizePercent / 100);
  while (size > 1 && heartW + 4 + length * CHAR_WIDTH * size > width) --size;
  const int16_t total = heartW + 4 + length * CHAR_WIDTH * size;
  const int16_t x = MARGIN + (width - total) / 2;
  int16_t heartY = y + (7 * size - heartH) / 2;  // Centered on the digits' ink.
  if (heartY < y - 4) heartY = y - 4;
  drawLifeHeart(display_, x, heartY, heartW, heartH, look.fill, GxEPD_BLACK);
  display_.setTextSize(size);
  display_.setCursor(x + heartW + 4, y);
  display_.print(number);
}

void EpaperDisplay::drawStatus(const char *line1, const char *line2) {
  gameFrameValid_ = false;
  partialRefreshCount_ = 0;
  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawHeader("TurnHub");
    drawEmblem(display_, display_.width() / 2, 72, GxEPD_BLACK, 2);
    drawTwoLines(line1, 112, display_.width() - 2 * MARGIN);
    if (line2 != nullptr) drawCentered(line2, 156);
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

  const int16_t bottom = contentBottom();
  const int16_t secondaryY = min<int16_t>(s.commander ? 216 : 149, bottom - 36);
  const int16_t commanderLimit = shared ? secondaryY : bottom;
  const bool partial = partialEnabled_ && !forceFull_ &&
      GxEPD2_213_B74::hasFastPartialUpdate && gameFrameValid_ &&
      gameFrameSigilId_ == s.sigilId && gameFrameShared_ == shared &&
      gameFrameCommander_ == static_cast<bool>(s.commander) &&
      partialRefreshCount_ < maxPartials_;
  forceFull_ = false;
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
    // A pass in its grace period says so; the ring counts it down.
    if (life_.passPending) {
      drawBanner("PASSING", 39, true, Icon::Turn, 2);
    } else if (life_.passingPlayer) {
      // Another player's pass, still undoable: the whole table sees it.
      char passing[20];
      snprintf(passing, sizeof(passing), "P%u PASSING", static_cast<unsigned>(life_.passingPlayer));
      drawBanner(passing, 39, false, Icon::None, 2);
    } else {
      drawBanner(active ? "YOUR TURN" : "WAITING FOR TURN", 39, active,
          active ? Icon::Turn : Icon::None, 2);
    }
    if (life_.request.target != 0) {
      // Who asks and how much, in words; the legend says which key answers.
      char ask[24];
      snprintf(ask, sizeof(ask), "P%u: %+ld life?", static_cast<unsigned>(life_.request.requester),
          static_cast<long>(life_.request.delta));
      drawBanner(ask, 64, true, Icon::None, 2);
    } else {
      drawTwoLines(s.primary.name, 66, width);
    }
    drawLife(s.primary.life, 104, shared ? 4 : 6);
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
  if (partial) lastPartialAtMs_ = millis();
  lastGame_ = s;
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
  Icon kind = Icon::None;
  switch (mode) {
    case TurnHubProtocol::DisplayMode::Lobby:
      header = "Lobby";
      status = secondaryPlayer ? "SHARED SIGIL" : (starter ? "STARTER" : "IN LOBBY");
      break;
    case TurnHubProtocol::DisplayMode::Starting:
      header = "Starting";
      status = starter ? "GO FIRST" : "GET READY";
      indicateSeat = starter;
      kind = Icon::Turn;
      break;
    case TurnHubProtocol::DisplayMode::Running:
      header = "Game";
      status = active ? "YOUR TURN" : "WAITING";
      indicateSeat = active;
      kind = Icon::Turn;
      break;
    case TurnHubProtocol::DisplayMode::Paused:
      header = "Paused";
      status = attention ? "ACTION NEEDED" : "GAME PAUSED";
      indicateSeat = attention;
      kind = Icon::Pause;
      break;
    case TurnHubProtocol::DisplayMode::GameOver:
      header = "Game Over";
      status = winner ? "WINNER!" : "GAME COMPLETE";
      indicateSeat = winner;
      kind = Icon::Crown;
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
    drawBanner(status, bottom - 49, indicateSeat, kind, 2);
    if (shared && indicateSeat) drawCentered(focusA ? "SEAT A" : "SEAT B", bottom - 11);
    drawLegend();
  } while (display_.nextPage());
}

}  // namespace TurnHubSigil
