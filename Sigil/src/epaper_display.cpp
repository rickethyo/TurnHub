#include "epaper_display.h"
#include "secure_link.h"
#include "brass_fonts.h"
#include "life_heart.h"
#include "display_name.h"
#include "commander_damage.h"

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
  uint8_t lines = updateNoticeText(life_.update) != nullptr ? 1 : 0;
  if (!menu_.active) return lines;
  for (Key key : LEGEND_KEYS) {
    if (lifeRequestShown() && (key == Key::Left || key == Key::Right)) continue;
    lines += menu_.compass[static_cast<uint8_t>(key)] != MENU_NONE;
  }
  if (lifeRequestShown()) lines += 2;
  else if (lifeKeysShown()) lines += 1;
  return lines;
}

bool EpaperDisplay::alreadyDrawn(DrawnInputs &out) const {
  memcpy(out.names[0], seatNameA_, sizeof(out.names[0]));
  memcpy(out.names[1], seatNameB_, sizeof(out.names[1]));
  out.menuActive = menu_.active;
  out.menuLife = menu_.life;
  memcpy(out.compass, menu_.compass, sizeof(out.compass));
  // Members one by one: a struct copy could carry padding bytes.
  out.request.target = life_.request.target;
  out.request.requester = life_.request.requester;
  out.request.tag = life_.request.tag;
  out.request.delta = life_.request.delta;
  out.startingLife = life_.startingLife;
  out.passPending = life_.passPending;
  out.update = life_.update;
  return drawnValid_ && !forceFull_ && memcmp(&out, &drawn_, sizeof(out)) == 0;
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
  ornamentRule(y);
  y += 3;
  display_.setTextSize(1);
  constexpr int16_t CAP = LEGEND_LINE - 1;
  const auto line = [&](Key key, const char *label) {
    drawKeycap(key, MARGIN, y);
    display_.setCursor(MARGIN + CAP + 4, y + 1);
    printClipped(label, (display_.width() - 2 * MARGIN - CAP - 4) / CHAR_WIDTH);
    y += LEGEND_LINE;
  };
  if (!menu_.active) {
    // No menu: only the update notice, if any, has a line.
  } else if (lifeRequestShown()) {
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
    if (!menu_.active) break;
    if (lifeRequestShown() && (key == Key::Left || key == Key::Right)) continue;
    const uint8_t action = menu_.compass[static_cast<uint8_t>(key)];
    if (action == MENU_NONE) continue;
    drawKeycap(key, MARGIN, y);
    char line[24];
    // "Factory reset (hold)" is too wide; the device menu screen says "hold" in words.
    const bool hold = menuActionNeedsHold(action) && action != MENU_LOCAL_FACTORY_RESET;
    snprintf(line, sizeof(line), "%s%s", sigilActionLabel(static_cast<TurnHubProtocol::SigilAction>(action)),
        hold ? " (hold)" : "");
    display_.setCursor(MARGIN + CAP + 4, y + 1);
    printClipped(line, (display_.width() - 2 * MARGIN - CAP - 4) / CHAR_WIDTH);
    y += LEGEND_LINE;
  }
  if (const char *update = updateNoticeText(life_.update)) {
    display_.setCursor(MARGIN, y + 1);
    printClipped(update, (display_.width() - 2 * MARGIN) / CHAR_WIDTH);
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
  drawnValid_ = false;
  gameFrameValid_ = false;
  partialRefreshCount_ = 0;
  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawFrame();
    char pages[16] = {};
    if (!confirm && page.pageCount > 1) {
      snprintf(pages, sizeof(pages), "Page %u of %u", static_cast<unsigned>(page.page + 1),
          static_cast<unsigned>(page.pageCount));
    }
    drawHeader(confirm ? "Join as" : "Who plays?", 0xFF, false, 0, pages[0] ? pages : nullptr);
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
        if (fontFits(page.items[i].name, &BrassFonts::EinkName, display_.width() - 2 * MARGIN)) {
          fontCentered(page.items[i].name, y + 28, &BrassFonts::EinkName);
        } else {
          drawCentered(page.items[i].name, y + 13, 2);
        }
      }
      if (notice) drawBanner(notice, ROW_TOP + 3 * ROW_HEIGHT + 2, true, Icon::None);
    }
    ornamentRule(legendY);
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

// --- Brass look (2026-09-29) ------------------------------------------------
//
// The portal's Brass theme in black and white: an engraved nameplate header
// with a cog-tooth edge, riveted banners, Cinzel for words, Oswald figures
// and a pressure-gauge dial for life, ornamental rules. Words still carry
// every meaning; the ornament is drawn in the same refresh, so it costs no
// extra flashes.

int16_t EpaperDisplay::fontWidth(const char *text, const GFXfont *font) {
  int16_t x1, y1;
  uint16_t w, h;
  display_.setFont(font);
  display_.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display_.setFont(nullptr);
  return static_cast<int16_t>(w);
}

bool EpaperDisplay::fontFits(const char *text, const GFXfont *font, int16_t width) {
  return fontWidth(text, font) <= width;
}

void EpaperDisplay::fontAt(const char *text, int16_t x, int16_t baseline, const GFXfont *font) {
  display_.setFont(font);
  display_.setTextSize(1);
  display_.setCursor(x, baseline);
  display_.print(text);
  display_.setFont(nullptr);
}

void EpaperDisplay::fontCentered(const char *text, int16_t baseline, const GFXfont *font) {
  int16_t x1, y1;
  uint16_t w, h;
  display_.setFont(font);
  display_.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display_.setTextSize(1);
  display_.setCursor((display_.width() - static_cast<int16_t>(w)) / 2 - x1, baseline);
  display_.print(text);
  display_.setFont(nullptr);
}

void EpaperDisplay::ornamentRule(int16_t y, const char *label) {
  const int16_t cx = display_.width() / 2;
  const int16_t half = label ? fontWidth(label, &BrassFonts::EinkSmall) / 2 + 9 : 5;
  display_.drawFastHLine(MARGIN, y, cx - half - MARGIN, GxEPD_BLACK);
  display_.drawFastHLine(cx + half, y, display_.width() - MARGIN - cx - half, GxEPD_BLACK);
  const auto diamond = [&](int16_t dx) {
    display_.fillTriangle(cx + dx - 2, y, cx + dx, y - 2, cx + dx + 2, y, GxEPD_BLACK);
    display_.fillTriangle(cx + dx - 2, y, cx + dx, y + 2, cx + dx + 2, y, GxEPD_BLACK);
  };
  if (label) {
    diamond(static_cast<int16_t>(-half + 3));
    diamond(static_cast<int16_t>(half - 3));
  } else {
    diamond(0);
  }
  if (label) fontCentered(label, y + 4, &BrassFonts::EinkSmall);
}

// A thin border with rounded corners: the page is one brass-framed plate.
void EpaperDisplay::drawFrame() {
  display_.drawRoundRect(0, 0, display_.width(), display_.height(), 4, GxEPD_BLACK);
}

void EpaperDisplay::drawTwoLines(const char *text, int16_t y, int16_t width) {
  // A name in Cinzel: one line, or two split at a space, or the small size;
  // the built-in font keeps all 12 protocol characters as a last resort.
  if (fontFits(text, &BrassFonts::EinkName, width)) {
    fontCentered(text, y + 22, &BrassFonts::EinkName);
    return;
  }
  const size_t length = strlen(text);
  for (size_t i = length; i > 0; --i) {
    if (text[i - 1] != ' ') continue;
    char first[20] = {};
    memcpy(first, text, i - 1 < sizeof(first) - 1 ? i - 1 : sizeof(first) - 1);
    const char *rest = text + i;
    if (fontFits(first, &BrassFonts::EinkName, width) && fontFits(rest, &BrassFonts::EinkName, width)) {
      fontCentered(first, y + 14, &BrassFonts::EinkName);
      fontCentered(rest, y + 33, &BrassFonts::EinkName);
      return;
    }
  }
  if (fontFits(text, &BrassFonts::EinkSmall, width)) {
    fontCentered(text, y + 21, &BrassFonts::EinkSmall);
    return;
  }
  const size_t capacity = width / (2 * CHAR_WIDTH);
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

// The engraved nameplate: a solid bar with a cog-tooth lower edge, a gear
// and a rivet, the title in Cinzel, then the Sigil and turn numbers (or a
// subtitle) in small caps, in white.
void EpaperDisplay::drawHeader(
    const char *title, uint8_t sigilId, bool host, uint8_t turnNumber, const char *subtitle) {
  const int16_t w = display_.width();
  display_.fillRect(0, 0, w, HEADER_BAR, GxEPD_BLACK);
  for (int16_t x = 1; x < w; x += 8) display_.fillRect(x, HEADER_BAR, 5, 2, GxEPD_BLACK);
  drawGear(display_, 13, 12, 8, 8, GxEPD_WHITE, 3, GxEPD_BLACK);
  drawRivet(display_, w - 7, 7, GxEPD_WHITE, GxEPD_BLACK);
  const bool second = sigilId != 0xFF || subtitle != nullptr;
  display_.setTextColor(GxEPD_WHITE);
  const int16_t room = w - 2 * 25;
  const GFXfont *font = fontFits(title, &BrassFonts::EinkTitle, room) ? &BrassFonts::EinkTitle
      : fontFits(title, &BrassFonts::EinkBanner, room) ? &BrassFonts::EinkBanner : &BrassFonts::EinkSmall;
  fontCentered(title, second ? 18 : 23, font);
  if (subtitle != nullptr) {
    fontCentered(subtitle, 32, &BrassFonts::EinkSmall);
  } else if (sigilId != 0xFF) {
    char sigil[12];
    snprintf(sigil, sizeof(sigil), "Sigil %u", static_cast<unsigned>(sigilId + 1));
    fontAt(sigil, MARGIN, 32, &BrassFonts::EinkSmall);
    if (host) drawIcon(display_, Icon::Crown, MARGIN + fontWidth(sigil, &BrassFonts::EinkSmall) + 4, 24, GxEPD_WHITE);
    if (turnNumber != 0) {
      char turn[12];
      snprintf(turn, sizeof(turn), "Round %u", static_cast<unsigned>(turnNumber));
      fontAt(turn, w - MARGIN - fontWidth(turn, &BrassFonts::EinkSmall) - 1, 32, &BrassFonts::EinkSmall);
    }
  }
  display_.setTextColor(GxEPD_BLACK);
}

// A message plate: filled (white text, an inner rule and rivets) when it
// concerns this Sigil now, a double frame otherwise. The words carry the
// meaning.
void EpaperDisplay::drawBanner(const char *message, int16_t y, bool highlight, Icon kind,
    uint8_t maxSize) {
  (void)kind;
  const int16_t width = display_.width() - 2 * MARGIN;
  const uint16_t ink = highlight ? GxEPD_WHITE : GxEPD_BLACK;
  if (highlight) {
    display_.fillRoundRect(MARGIN, y, width, BANNER_HEIGHT, 3, GxEPD_BLACK);
    display_.drawRoundRect(MARGIN + 2, y + 2, width - 4, BANNER_HEIGHT - 4, 2, GxEPD_WHITE);
    display_.fillCircle(MARGIN + 6, y + BANNER_HEIGHT / 2, 1, GxEPD_WHITE);
    display_.fillCircle(MARGIN + width - 7, y + BANNER_HEIGHT / 2, 1, GxEPD_WHITE);
  } else {
    display_.drawRoundRect(MARGIN, y, width, BANNER_HEIGHT, 3, GxEPD_BLACK);
    display_.drawRoundRect(MARGIN + 2, y + 2, width - 4, BANNER_HEIGHT - 4, 2, GxEPD_BLACK);
  }
  display_.setTextColor(ink);
  const int16_t room = width - 18;
  if (maxSize >= 2 && fontFits(message, &BrassFonts::EinkBanner, room)) {
    fontCentered(message, y + 16, &BrassFonts::EinkBanner);
  } else if (fontFits(message, &BrassFonts::EinkSmall, room)) {
    fontCentered(message, y + 15, &BrassFonts::EinkSmall);
  } else {
    drawCentered(message, y + (BANNER_HEIGHT - 8) / 2, 1);
  }
  display_.setTextColor(GxEPD_BLACK);
}

// The life total in Oswald figures beside the life dial, centered as one
// unit in the rows the built-in font's size maxSize would take (y to
// y + 8 * maxSize). The dial sweeps against the starting life (life_heart.h);
// it gives way when the number needs the room.
void EpaperDisplay::drawLife(int32_t life, int16_t y, uint8_t maxSize) {
  char number[16];
  snprintf(number, sizeof(number), "%ld", static_cast<long>(life));
  const int16_t width = display_.width() - 2 * MARGIN;
  const HeartLook look = lifeHeartLook(life, life_.startingLife);
  const GFXfont *font = maxSize >= 5 ? &BrassFonts::EinkLife : maxSize >= 3 ? &BrassFonts::EinkLifeSmall : nullptr;
  if (font != nullptr && !fontFits(number, font, width)) font = &BrassFonts::EinkLifeSmall;
  if (font == nullptr || !fontFits(number, font, width)) {
    // Too small or too long for the figures: the heart and built-in digits.
    const int16_t length = static_cast<int16_t>(strlen(number));
    uint8_t size = maxSize;
    const int16_t heartW = static_cast<int16_t>(ICON_WIDTH * look.sizePercent / 100);
    const int16_t heartH = static_cast<int16_t>(iconHeight(Icon::Heart) * look.sizePercent / 100);
    while (size > 1 && heartW + 4 + length * CHAR_WIDTH * size > width) --size;
    const int16_t x = MARGIN + (width - heartW - 4 - length * CHAR_WIDTH * size) / 2;
    drawLifeHeart(display_, x, y + (7 * size - heartH) / 2, heartW, heartH, look.fill, GxEPD_BLACK);
    display_.setTextSize(size);
    display_.setCursor(x + heartW + 4, y);
    display_.print(number);
    return;
  }
  int16_t x1, y1;
  uint16_t w, h;
  display_.setFont(font);
  display_.getTextBounds(number, 0, 0, &x1, &y1, &w, &h);
  display_.setFont(nullptr);
  const int16_t r = font == &BrassFonts::EinkLife ? 16 : 11;
  const bool dial = static_cast<int16_t>(w) + 2 * r + 10 <= width;
  const int16_t total = static_cast<int16_t>(w) + (dial ? 2 * r + 10 : 0);
  const int16_t left = MARGIN + (width - total) / 2;
  const int16_t middle = y + 7 * maxSize / 2;
  if (dial) drawLifeDial(display_, left + r + 2, middle, r, look.fill, GxEPD_BLACK, look.sizePercent - 100);
  fontAt(number, left + total - static_cast<int16_t>(w) - x1, middle + static_cast<int16_t>(h) / 2, font);
}

void EpaperDisplay::drawStatus(const char *line1, const char *line2, bool legend, const char *line3) {
  drawnValid_ = false;
  gameFrameValid_ = false;
  partialRefreshCount_ = 0;
  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawFrame();
    drawHeader("TurnHub");
    // The emblem: a large gear with the turn arrow in its hub, meshed with a
    // small one.
    const int16_t cx = display_.width() / 2;
    drawGear(display_, cx + 22, 92, 11, 9, GxEPD_BLACK, 4, GxEPD_WHITE, 12.0f);
    drawGear(display_, cx - 4, 72, 25, 12, GxEPD_BLACK, 11, GxEPD_WHITE);
    display_.fillTriangle(cx - 8, 66, cx - 8, 78, cx + 3, 72, GxEPD_BLACK);
    drawTwoLines(line1, 106, display_.width() - 2 * MARGIN);
    if (line2 != nullptr) {
      ornamentRule(150);
      if (fontFits(line2, &BrassFonts::EinkSmall, display_.width() - 2 * MARGIN)) {
        fontCentered(line2, 166, &BrassFonts::EinkSmall);
      } else {
        drawCentered(line2, 158);
      }
      if (line3 != nullptr) {
        if (fontFits(line3, &BrassFonts::EinkSmall, display_.width() - 2 * MARGIN)) {
          fontCentered(line3, 184, &BrassFonts::EinkSmall);
        } else {
          drawCentered(line3, 176);
        }
      }
    }
    if (legend) drawLegend();
  } while (display_.nextPage());
}

void EpaperDisplay::drawSeat(
    const char *label, const char *name, int16_t y, int16_t height, bool focused) {
  const int16_t width = display_.width() - 2 * MARGIN;
  const bool compact = height < 62;  // No room for a two-line size-2 name.
  // The seat's label on a small plate: filled for the seat this concerns
  // now, framed otherwise, so focus is a shape as well as a fill.
  const int16_t barY = y + (compact ? 1 : 4), barH = compact ? 12 : 16;
  if (focused) {
    display_.fillRoundRect(MARGIN, barY, width, barH, 3, GxEPD_BLACK);
    display_.setTextColor(GxEPD_WHITE);
  } else {
    display_.drawFastHLine(MARGIN + 8, barY + barH / 2, 12, GxEPD_BLACK);
    display_.drawFastHLine(MARGIN + width - 20, barY + barH / 2, 12, GxEPD_BLACK);
  }
  if (fontFits(label, &BrassFonts::EinkSmall, width - 44)) {
    fontCentered(label, barY + barH / 2 + 4, &BrassFonts::EinkSmall);
  } else {
    drawCentered(label, y + (compact ? 3 : 8));
  }
  display_.setTextColor(GxEPD_BLACK);
  if (compact) {
    if (fontFits(name, &BrassFonts::EinkName, width)) fontCentered(name, y + 32, &BrassFonts::EinkName);
    else if (fontFits(name, &BrassFonts::EinkSmall, width)) fontCentered(name, y + 30, &BrassFonts::EinkSmall);
    else drawCentered(name, y + 17, 2);
  } else {
    drawTwoLines(name, y + 26, width);
  }
  display_.drawFastHLine(MARGIN, y + height - 1, width, GxEPD_BLACK);
}

void EpaperDisplay::showBooting() {
  drawStatus("Booting");
}

void EpaperDisplay::showUnpaired() {
  // The BOOT button may be inside the case: the joystick hold always works
  // (main.cpp's updateJoystickPair). Unpaired has no menu, so no legend.
  drawStatus("Unpaired", "Hold joystick to", false, "enter pairing mode");
}

// The open device menu replaces the ready and lobby screens; its legend holds
// Factory reset and Back. False if it is closed.
bool EpaperDisplay::drawDeviceMenu() {
  if (!menu_.active || !menu_.deviceMenu) return false;
  drawStatus("Device menu", "Unpair/reset: hold");
  return true;
}

void EpaperDisplay::showReady(uint8_t sigilId) {
  if (drawDeviceMenu()) return;
  char title[24];
  snprintf(title, sizeof(title), "Sigil %u", static_cast<unsigned>(sigilId + 1));
  drawStatus(title, "Ready for game");
}

void EpaperDisplay::showAtlasLost(uint8_t sigilId) {
  (void)sigilId;
  // Only the device menu is offered (SigilMenu::setOffline): Menu on Up in
  // the legend, and the menu itself while open.
  if (drawDeviceMenu()) return;
  drawStatus("Atlas lost", "Searching for Atlas");
}

void EpaperDisplay::showPairingCode(uint16_t code) {
  char digits[5];
  TurnHubSecureLink::formatPairingCode(code, digits);
  char line[16];
  snprintf(line, sizeof(line), "Code %s", digits);
  // No legend: the Sigil waits for the owner to confirm on Atlas.
  drawStatus(line, "Confirm on Atlas", false);
}

// E-paper keeps this image with no power, so the sleeping Sigil says how to
// wake it. No legend: no menu works until it wakes. The panel hibernates.
void EpaperDisplay::showSleeping() {
  drawStatus("Asleep", "Click joystick", false, "to wake");
  display_.hibernate();
}

void EpaperDisplay::showUpdate(const char *status, int8_t percent) {
  char line[24];
  if (percent >= 0) {
    snprintf(line, sizeof(line), "%s %d%%", status, static_cast<int>(percent));
  } else {
    snprintf(line, sizeof(line), "%s", status);
  }
  // No legend: the Sigil takes no input while it updates.
  drawStatus("Updating", line, false);
}

void EpaperDisplay::showGame(const TurnHubProtocol::GameDisplayPacket &s) {
  const uint8_t primary = TurnHubProtocol::displayPrimaryPlayer(s.state);
  const uint8_t secondary = TurnHubProtocol::displaySecondaryPlayer(s.state);
  const bool shared = secondary != 0;
  const bool active = TurnHubProtocol::hasDisplayFlag(s.state, TurnHubProtocol::DISPLAY_FLAG_ACTIVE);
  const char primarySeat = primary < secondary ? 'A' : 'B';
  const int16_t width = display_.width() - 2 * MARGIN;

  const bool cmdShown = commanderDamageShown(s);
  constexpr int16_t CMD_HEADING = 12;  // Rule and "CMD TAKEN".
  constexpr int16_t CMD_ROW = 10;      // One source: name left, damage right.
  const int16_t cmdHeight = cmdShown ? CMD_HEADING + CMD_ROW * s.sourceCount : 0;
  const int16_t bottom = contentBottom();
  const int16_t secondaryY = cmdShown ? bottom - 36 : min<int16_t>(149, bottom - 36);
  const int16_t commanderLimit = (shared ? secondaryY : bottom) - 2;
  const bool partial = partialEnabled_ && !forceFull_ &&
      GxEPD2_213_B74::hasFastPartialUpdate && gameFrameValid_ &&
      gameFrameSigilId_ == s.sigilId && gameFrameShared_ == shared &&
      gameFrameCommander_ == cmdShown &&
      partialRefreshCount_ < maxPartials_;
  DrawnInputs inputs;
  memset(&inputs, 0, sizeof(inputs));
  inputs.kind = 1;
  memcpy(&inputs.game, &s, sizeof(s));
  if (alreadyDrawn(inputs)) {
    Serial.println("SIGIL|DISPLAY|REFRESH|SKIPPED_SAME");
    return;
  }
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
    drawFrame();
    drawHeader(s.commander ? "Commander" : "Game", s.sigilId,
        TurnHubProtocol::hasDisplayFlag(s.state, TurnHubProtocol::DISPLAY_FLAG_HOST),
        TurnHubProtocol::displayTurnNumber(s.state));
    // Atlas places the active local player first; keep the turn cue with them.
    // A pass in its grace period says so; the ring counts it down.
    // Another player's pending pass is left to the status ring and the OLED
    // and Atlas screens: on e-ink it cost every Sigil two full refreshes per
    // pass (test feedback, 2026-09-28). This Sigil's own pass still shows.
    if (life_.passPending) {
      drawBanner("PASSING", 39, true, Icon::Turn, 2);
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
    // The life total shrinks (not below size 3, or 2 when shared) so the
    // commander block fits above the legend or the other seat.
    uint8_t lifeSize = shared ? 4 : 6;
    const uint8_t minLifeSize = shared ? 2 : 3;
    const int16_t labelGap = shared ? 2 : 4;
    while (cmdShown && lifeSize > minLifeSize &&
        104 + 8 * lifeSize + labelGap + 10 > commanderLimit - cmdHeight) --lifeSize;
    const int16_t labelY = 104 + 8 * lifeSize + labelGap;
    drawLife(s.primary.life, 104, lifeSize);
    char label[20] = "Life";
    if (shared) snprintf(label, sizeof(label), "Seat %c life", primarySeat);
    ornamentRule(labelY + 3, label);

    if (cmdShown) {
      // Damage this player received, one compact row per source, anchored to
      // the bottom of the free area so the legend never covers it. Rows that
      // still do not fit join the "+N" count with the ones Atlas omitted.
      int16_t y = max<int16_t>(labelY + 10, commanderLimit - cmdHeight);
      uint8_t fit = s.sourceCount;
      while (fit && y + CMD_HEADING + CMD_ROW * fit > commanderLimit) --fit;
      const unsigned hidden = s.omittedSources + (s.sourceCount - fit);
      display_.drawFastHLine(MARGIN, y, width, GxEPD_BLACK);
      fontAt("Cmd taken", MARGIN, y + 10, &BrassFonts::EinkSmall);
      display_.setTextSize(1);
      if (hidden) {
        char more[8];
        snprintf(more, sizeof(more), "+%u", hidden);
        display_.setCursor(display_.width() - MARGIN - strlen(more) * CHAR_WIDTH, y + 3);
        display_.print(more);
      }
      y += CMD_HEADING;
      for (uint8_t i = 0; i < fit; ++i, y += CMD_ROW) {
        const auto &entry = s.sources[i];
        char damage[24];
        formatCommanderDamage(entry, damage, sizeof(damage));
        const int16_t damageWidth = static_cast<int16_t>(strlen(damage)) * CHAR_WIDTH;
        display_.setCursor(MARGIN, y);
        printClipped(entry.name, (width - damageWidth - CHAR_WIDTH) / CHAR_WIDTH);
        display_.setCursor(display_.width() - MARGIN - damageWidth, y);
        display_.print(damage);
      }
    }

    if (shared) {
      // The other seat on its own riveted plate.
      display_.drawRoundRect(MARGIN, secondaryY, width, 34, 3, GxEPD_BLACK);
      drawRivet(display_, MARGIN + 4, secondaryY + 4, GxEPD_BLACK, GxEPD_WHITE);
      drawRivet(display_, MARGIN + width - 5, secondaryY + 4, GxEPD_BLACK, GxEPD_WHITE);
      char otherName[20];
      snprintf(otherName, sizeof(otherName), "%c: %s", primarySeat == 'A' ? 'B' : 'A', s.secondary.name);
      if (fontFits(otherName, &BrassFonts::EinkSmall, width - 16)) fontCentered(otherName, secondaryY + 12, &BrassFonts::EinkSmall);
      else drawCentered(otherName, secondaryY + 5);
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
  gameFrameCommander_ = cmdShown;
  memcpy(&drawn_, &inputs, sizeof(inputs));
  drawnValid_ = true;
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
  if (drawDeviceMenu()) return;
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
  DrawnInputs inputs;
  memset(&inputs, 0, sizeof(inputs));
  inputs.kind = 2;
  const uint8_t stateArgs[6] = {sigilId, static_cast<uint8_t>(mode), primaryPlayer,
      secondaryPlayer, turnNumber, flags};
  memcpy(inputs.state, stateArgs, sizeof(stateArgs));
  if (alreadyDrawn(inputs)) {
    Serial.println("SIGIL|DISPLAY|REFRESH|SKIPPED_SAME");
    return;
  }
  gameFrameValid_ = false;
  partialRefreshCount_ = 0;
  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawFrame();
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
    ornamentRule(divider);
    drawBanner(status, bottom - 49, indicateSeat, kind, 2);
    if (shared && indicateSeat) fontCentered(focusA ? "Seat A" : "Seat B", bottom - 4, &BrassFonts::EinkSmall);
    drawLegend();
  } while (display_.nextPage());
  memcpy(&drawn_, &inputs, sizeof(inputs));
  drawnValid_ = true;
}

}  // namespace TurnHubSigil
