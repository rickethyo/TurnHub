#include "oled_display.h"
#include "secure_link.h"
#include "brass_fonts.h"
#include "picker_list.h"
#include "avatars.h"
#include "display_name.h"
#include "life_heart.h"
#include "commander_damage.h"

#include <Arduino.h>
#include <Wire.h>
#include <cstring>
#include <new>

namespace TurnHubSigil {
namespace {
// How long the boot logo stays up before the first status screen.
constexpr uint32_t OLED_SPLASH_MS = 1000;
// ESP32 output pins excluding flash, UART0, and Sigil's existing controls/cues
// (main.cpp). These are validation exclusions, NOT proposed OLED assignments.
bool availableOutputPin(int pin) {
  if (pin < 0 || pin > 33 || (pin >= 6 && pin <= 11) || pin == 20 || pin == 24 ||
      (pin >= 28 && pin <= 31)) return false;
  switch (pin) {
    case 1: case 3:  // Serial diagnostics.
    case 0: // Pair (the DevKit BOOT button).
    case 4:  // Display-type strap.
    case 13: // Spare front light (J6).
    case 19: // Wokwi's Pair button.
    case 21: // BUSY on the shared display header.
    case 26: // Status ring.
    case 32: // Thumbstick click (VRX/VRY are input-only GPIO34/35).
    case 33: // Buzzer.
      return false;
    default: return true;
  }
}
}

bool OledDisplay::validConfig() const {
  const auto &c = config_;
  if (c.controller != OledController::Sh1106 || c.width != 128 ||
      c.height != 64 || (c.rotation != 0 && c.rotation != 2) ||
      c.power != OledPower::InternalChargePump ||
      (c.reset != -1 && !availableOutputPin(c.reset))) return false;

  int pins[5];
  uint8_t count = 0;
  if (c.bus == OledBus::I2c) {
    if (c.i2cAddress < 0x08 || c.i2cAddress > 0x77 ||
        c.i2cClockHz == 0 || c.i2cClockHz > 400000) return false;
    pins[count++] = c.sda;
    pins[count++] = c.scl;
  } else if (c.bus == OledBus::SoftwareSpi) {
    pins[count++] = c.mosi;
    pins[count++] = c.sclk;
    pins[count++] = c.dc;
    pins[count++] = c.cs;
  } else {
    return false;
  }
  if (c.reset != -1) pins[count++] = c.reset;
  for (uint8_t i = 0; i < count; ++i) {
    if (!availableOutputPin(pins[i])) return false;
    for (uint8_t j = 0; j < i; ++j) if (pins[i] == pins[j]) return false;
  }
  return true;
}

void OledDisplay::begin() {
  if (ready_) return;
  if (!validConfig()) {
    Serial.println("SIGIL|DISPLAY|OLED|UNCONFIGURED_OR_INVALID|CHECK_OLED_CONFIG");
    return;
  }
  const auto &c = config_;
  if (c.bus == OledBus::I2c) {
    if (!Wire.setPins(c.sda, c.scl)) {
      Serial.println("SIGIL|DISPLAY|OLED|I2C_INIT_FAILED");
      return;
    }
    // The driver starts Wire using these explicitly selected pins.
    display_.reset(new (std::nothrow) Adafruit_SH1106G(c.width, c.height,
        &Wire, c.reset, c.i2cClockHz, c.i2cClockHz));
  } else {
    // Software SPI uses only the explicit write-only pins, never default MISO
    // (GPIO19, Wokwi's Pair button). No hardware SPI bus is started.
    display_.reset(new (std::nothrow) Adafruit_SH1106G(c.width, c.height,
        c.mosi, c.sclk, c.dc, c.reset, c.cs));
  }
  // Address is unused for SPI. I2C must supply its own explicit address.
  if (!display_ || !display_->begin(
      c.bus == OledBus::I2c ? c.i2cAddress : 0, c.reset >= 0)) {
    display_.reset();
    Serial.println("SIGIL|DISPLAY|OLED|INIT_FAILED");
    return;
  }
  display_->setRotation(c.rotation);
  display_->setTextWrap(false);
  display_->setTextColor(SH110X_WHITE);
  display_->clearDisplay();
  ready_ = true;
  Serial.printf("SIGIL|DISPLAY|OLED|READY|%dx%d|ROTATION|%u\n",
      display_->width(), display_->height(), static_cast<unsigned>(c.rotation));
  // Hold the logo long enough to be seen before the first status screen
  // replaces it (owner request, 2026-09-25). Runs once, in setup().
  showBooting();
  delay(OLED_SPLASH_MS);
}

bool OledDisplay::setSeatName(uint8_t slot, const char *name) {
  if (slot == 1) return updateDisplayName(seatNameA_, name);
  if (slot == 2) return updateDisplayName(seatNameB_, name);
  return false;
}

namespace {
constexpr int16_t HEADER_HEIGHT = 11;
constexpr int16_t BANNER_HEIGHT = 11;
}

// Draws one text run inside [left, right), shrinking to fit and clipping only
// if even size 1 is too wide. Returns the x just past the drawn text.
int16_t OledDisplay::text(const char *value, int16_t y, uint8_t maxSize,
    Align align, bool inverse, int16_t left, int16_t right) {
  if (!value || !value[0]) return left;
  if (right < 0) right = display_->width();
  const int16_t span = right - left;
  const size_t length = strlen(value);
  uint8_t size = maxSize;
  while (size > 1 && (static_cast<int32_t>(length) * 6 * size > span ||
      y + 8 * size > display_->height())) --size;
  if (span < 6 || y < 0 || y + 8 * size > display_->height()) return left;
  const size_t capacity = span / (6 * size);
  const size_t shown = length < capacity ? length : capacity;
  const int16_t width = static_cast<int16_t>(shown * 6 * size);
  int16_t x = left;
  if (align == Align::Center) x = left + (span - width) / 2;
  else if (align == Align::Right) x = right - width;
  display_->setTextColor(inverse ? SH110X_BLACK : SH110X_WHITE);
  display_->setTextSize(size);
  display_->setCursor(x, y);
  for (size_t i = 0; i < shown; ++i) display_->print(value[i]);
  display_->setTextColor(SH110X_WHITE);
  return x + width;
}

// --- Brass look (2026-09-29, extended 2026-09-30) ----------------------------
//
// The e-ink Sigil's Brass family scaled to 128x64: a nameplate header with a
// gear and a rivet, Cinzel for titles, names and big lines, Oswald figures
// for life, ornamental rules. Each Brass run is measured first and falls
// back to the built-in font when it does not fit its band, so every word and
// digit still shows. Small body text (key help, commander rows, list rows,
// the other seat's row) stays in the built-in font.

int16_t OledDisplay::fontText(const char *value, const GFXfont *font, int16_t top, int16_t bottom,
    Align align, bool inverse, int16_t left, int16_t right) {
  if (!value || !value[0]) return left;
  if (right < 0) right = display_->width();
  int16_t x1, y1;
  uint16_t w, h;
  display_->setFont(font);
  display_->getTextBounds(value, 0, 0, &x1, &y1, &w, &h);
  // Ink rows relative to the baseline: y1 .. y1 + h - 1. Put the ink's
  // bottom on the band's last row, and give up if its top would leave it.
  const int16_t baseline = bottom - (y1 + static_cast<int16_t>(h));
  if (w == 0 || static_cast<int16_t>(w) > right - left || baseline + y1 < top ||
      bottom > display_->height()) {
    display_->setFont(nullptr);
    return -1;
  }
  int16_t x = left;
  if (align == Align::Center) x = left + (right - left - static_cast<int16_t>(w)) / 2;
  else if (align == Align::Right) x = right - static_cast<int16_t>(w);
  display_->setTextColor(inverse ? SH110X_BLACK : SH110X_WHITE);
  display_->setTextSize(1);
  display_->setCursor(x - x1, baseline);
  for (const char *c = value; *c; ++c) display_->print(*c);
  display_->setFont(nullptr);
  display_->setTextColor(SH110X_WHITE);
  return x + static_cast<int16_t>(w);
}

void OledDisplay::rule(int16_t y, int16_t left, int16_t right) {
  const int16_t cx = (left + right) / 2;
  display_->drawFastHLine(left, y, cx - 4 - left, SH110X_WHITE);
  display_->drawFastHLine(cx + 5, y, right - cx - 5, SH110X_WHITE);
  display_->fillTriangle(cx - 2, y, cx, y - 2, cx + 2, y, SH110X_WHITE);
  display_->fillTriangle(cx - 2, y, cx, y + 2, cx + 2, y, SH110X_WHITE);
}

// The nameplate: a solid bar with a gear at the left, the title in Cinzel
// caps (built-in if it would crowd the right-hand label), then seat/turn on
// the right and a crown for the host.
void OledDisplay::header(const char *title, const char *right, bool host) {
  const int16_t w = display_->width();
  display_->fillRect(0, 0, w, HEADER_HEIGHT, SH110X_WHITE);
  drawGear(*display_, 6, 5, 4, 6, SH110X_BLACK, 1, SH110X_WHITE);
  const int16_t rightStart = w - 3 - static_cast<int16_t>(strlen(right)) * 6;
  const int16_t titleRight = rightStart - (host ? 18 : 4);
  // Caps sit on row 8; a descender (the J of JOIN AS) may use the bar's
  // last rows.
  int16_t titleEnd = fontText(title, &BrassFonts::OledHeader, 1, 9, Align::Left, true, 13, titleRight);
  if (titleEnd < 0) titleEnd = fontText(title, &BrassFonts::OledHeader, 0, HEADER_HEIGHT, Align::Left, true, 13, titleRight);
  if (titleEnd < 0) titleEnd = text(title, 2, 1, Align::Left, true, 13, w / 2 + 8);
  text(right, 2, 1, Align::Right, true, titleEnd + 2, w - 3);
  if (host && rightStart - 16 >= titleEnd + 2) {
    icon(Icon::Crown, rightStart - 16, 1, SH110X_BLACK);
  }
}

// Banners are brass tickets with notched ends (the Brass look, 2026-09-29):
// highlighted ones filled and flanked by icons, plain ones outlined. The
// words always carry the meaning; icons only add character.
void OledDisplay::banner(const char *message, int16_t y, bool highlight, Icon kind) {
  const int16_t w = display_->width();
  constexpr int16_t NOTCH = 3, MID = BANNER_HEIGHT / 2;
  if (highlight) {
    display_->fillRect(NOTCH, y, w - 2 * NOTCH, BANNER_HEIGHT, SH110X_WHITE);
    display_->fillTriangle(0, y + MID, NOTCH, y, NOTCH, y + BANNER_HEIGHT - 1, SH110X_WHITE);
    display_->fillTriangle(w - 1, y + MID, w - 1 - NOTCH, y, w - 1 - NOTCH, y + BANNER_HEIGHT - 1, SH110X_WHITE);
    icon(kind, 2, y + 1, SH110X_BLACK);
    icon(kind == Icon::Turn ? Icon::TurnBack : kind, w - 15, y + 1, SH110X_BLACK);
    if (fontText(message, &BrassFonts::OledHeader, y + 2, y + 10, Align::Center, true, 16, w - 16) < 0) {
      text(message, y + 2, 1, Align::Center, true, 16, w - 16);
    }
  } else {
    display_->drawFastHLine(NOTCH, y, w - 2 * NOTCH, SH110X_WHITE);
    display_->drawFastHLine(NOTCH, y + BANNER_HEIGHT - 1, w - 2 * NOTCH, SH110X_WHITE);
    display_->drawLine(0, y + MID, NOTCH, y, SH110X_WHITE);
    display_->drawLine(0, y + MID, NOTCH, y + BANNER_HEIGHT - 1, SH110X_WHITE);
    display_->drawLine(w - 1, y + MID, w - 1 - NOTCH, y, SH110X_WHITE);
    display_->drawLine(w - 1, y + MID, w - 1 - NOTCH, y + BANNER_HEIGHT - 1, SH110X_WHITE);
    // Outlined tickets keep the built-in font: Cinzel crowds the thin frame.
    text(message, y + 2, 1, Align::Center, false, 4, w - 4);
  }
}

// Shared glyphs (sigil_icons.h), so the OLED and e-ink Sigils match.
void OledDisplay::icon(Icon kind, int16_t x, int16_t y, uint16_t color) {
  drawIcon(*display_, kind, x, y, color);
}

// The life dial plus the life total in Oswald figures, centered as one unit
// in the rows [y, y + 8 * maxSize) (the built-in font's size maxSize). The
// largest Oswald size that fits is used; the built-in font only if none does,
// so the number is never cut. The dial sweeps against the starting life, and
// an outer arc grows above it (life_heart.h, sigil_icons.h).
void OledDisplay::lifeTotal(int32_t life, int16_t y, uint8_t maxSize) {
  char number[16];
  snprintf(number, sizeof(number), "%ld", static_cast<long>(life));
  const HeartLook look = lifeHeartLook(life, life_.startingLife);
  const int16_t w = display_->width();
  const int16_t bottom = y + 8 * maxSize;
  struct Figures { const GFXfont *font; int16_t dialR; };
  const Figures large[] = {{&BrassFonts::OledLife, 9}, {&BrassFonts::OledLifeMid, 7},
      {&BrassFonts::OledLifeSmall, 6}};
  const Figures small[] = {{&BrassFonts::OledLifeSmall, 6}};
  const Figures *options = maxSize >= 3 ? large : small;
  const uint8_t count = maxSize >= 3 ? 3 : 1;
  for (uint8_t i = 0; i < count; ++i) {
    int16_t x1, y1;
    uint16_t tw, th;
    display_->setFont(options[i].font);
    display_->getTextBounds(number, 0, 0, &x1, &y1, &tw, &th);
    display_->setFont(nullptr);
    const int16_t r = options[i].dialR;
    const int16_t dialW = 2 * (r + 2) + 1;
    const int16_t total = dialW + 4 + static_cast<int16_t>(tw);
    if (total > w || static_cast<int16_t>(th) > bottom - y) continue;
    const int16_t x = (w - total) / 2;
    // The figures sit centered in the band (the spare row goes above them,
    // away from the name line); the dial centers on their ink.
    const int16_t inkTop = y + (bottom - y - static_cast<int16_t>(th) + 1) / 2;
    int16_t cy = inkTop + static_cast<int16_t>(th) / 2;
    if (cy - r - 2 < y) cy = y + r + 2;
    if (cy + r + 2 >= display_->height()) cy = display_->height() - r - 3;
    drawLifeDial(*display_, x + r + 2, cy, r, look.fill, SH110X_WHITE, look.sizePercent - 100);
    fontText(number, options[i].font, y, inkTop + static_cast<int16_t>(th), Align::Left, false,
        x + dialW + 4, w);
    return;
  }
  // Built-in fallback: the largest size that fits beside a smaller dial.
  const int16_t length = static_cast<int16_t>(strlen(number));
  uint8_t size = maxSize;
  const auto dialR = [](uint8_t s) -> int16_t { return s >= 3 ? 8 : s == 2 ? 5 : 3; };
  const auto dialW = [&](uint8_t s) -> int16_t { return 2 * (dialR(s) + 2) + 1; };
  while (size > 1 && (dialW(size) + 3 + length * 6 * size > w ||
      y + 8 * size > display_->height())) --size;
  const int16_t total = dialW(size) + 3 + length * 6 * size;
  const int16_t x = total < w ? (w - total) / 2 : 0;
  const int16_t r = dialR(size);
  int16_t cy = y + (7 * size) / 2;
  if (cy - r - 2 < y) cy = y + r + 2;
  if (cy + r + 2 >= display_->height()) cy = display_->height() - r - 3;
  drawLifeDial(*display_, x + r + 2, cy, r, look.fill, SH110X_WHITE, look.sizePercent - 100);
  text(number, y, size, Align::Left, false, x + dialW(size) + 3, w);
}

// Boot splash: the Brass emblem, a gear with the turn arrow in its hub
// meshed with a smaller one.
void OledDisplay::splash(const char *caption) {
  const int16_t cx = display_->width() / 2;
  drawGear(*display_, cx + 15, 21, 7, 8, SH110X_WHITE, 2, SH110X_BLACK, 10.0f);
  drawGear(*display_, cx - 3, 14, 13, 10, SH110X_WHITE, 6, SH110X_BLACK);
  display_->fillTriangle(cx - 6, 10, cx - 6, 18, cx + 1, 14, SH110X_WHITE);
  if (fontText("TurnHub", &BrassFonts::OledName, 31, 43, Align::Center) < 0) {
    text("TurnHub", 30, 2, Align::Center);
  }
  rule(48, 20, display_->width() - 20);
  text(caption, 54, 1, Align::Center);
}

namespace {
// Built-in font glyphs: 0x09 ring for the click, 0x18-0x1B arrows (as the e-ink legend).
char keyGlyph(Key key) {
  switch (key) {
    case Key::Up: return 0x18;
    case Key::Down: return 0x19;
    case Key::Right: return 0x1A;
    case Key::Left: return 0x1B;
    default: return 0x09;
  }
}
constexpr Key LEGEND_KEYS[] = {Key::Select, Key::Up, Key::Down, Key::Left, Key::Right};
constexpr int16_t LEGEND_Y = 56;  // The bottom text row.
}  // namespace

// The device menu as a compass: each row is a key and what it does. The row
// being held says so; the status light shows the progress.
bool OledDisplay::drawDeviceMenu() {
  if (!menu_.active || !menu_.deviceMenu) return false;
  constexpr int16_t ROW_HEIGHT = 12;
  legendShown_ = false;
  display_->clearDisplay();
  header("MENU", "DEVICE");
  const int16_t w = display_->width();
  int16_t y = HEADER_HEIGHT + 3;
  for (Key key : LEGEND_KEYS) {
    const uint8_t action = menu_.compass[static_cast<uint8_t>(key)];
    if (action == MENU_NONE) continue;
    const char *label = sigilActionLabel(static_cast<TurnHubProtocol::SigilAction>(action));
    const bool held = menu_.holdAction == action;
    char line[28];
    if (held) snprintf(line, sizeof(line), "HOLD: %s", label);
    else snprintf(line, sizeof(line), "%c %s", keyGlyph(key), label);
    if (held) display_->fillRect(0, y - 1, w, ROW_HEIGHT - 1, SH110X_WHITE);
    text(line, y + 1, 1, Align::Left, held, 3, w - 3);
    y += ROW_HEIGHT;
  }
  // "(hold)" does not fit beside Factory reset (21 characters a row), so the
  // hint says it in words.
  text("Hold 5 s to erase", LEGEND_Y, 1, Align::Center);
  display_->display();
  return true;
}

uint8_t OledDisplay::legendEntries(char entries[][24]) const {
  uint8_t count = 0;
  if (!menu_.active) return 0;
  for (Key key : LEGEND_KEYS) {
    const uint8_t action = menu_.compass[static_cast<uint8_t>(key)];
    if (action == MENU_NONE) continue;
    snprintf(entries[count++], 24, "%c %s%s", keyGlyph(key),
        sigilActionLabel(static_cast<TurnHubProtocol::SigilAction>(action)),
        menuActionNeedsHold(action) ? " (hold)" : "");
  }
  // Left/Right with nothing else on them change life.
  if (menu_.life && menu_.compass[static_cast<uint8_t>(Key::Left)] == MENU_NONE &&
      menu_.compass[static_cast<uint8_t>(Key::Right)] == MENU_NONE) {
    snprintf(entries[count++], 24, "\x1b\x1a Life -/+ (hold)");
  }
  return count;
}

void OledDisplay::drawLegendRow() {
  char entries[KEY_COUNT + 1][24];
  const uint8_t count = legendEntries(entries);
  if (count == 0) return;
  display_->fillRect(0, LEGEND_Y, display_->width(), 8, SH110X_BLACK);
  text(entries[legendIndex_ % count], LEGEND_Y, 1, Align::Center);
}

void OledDisplay::legend(bool drawn) {
  // A different menu starts again from its click.
  const bool same = legendMenu_.active == menu_.active && legendMenu_.life == menu_.life &&
      memcmp(legendMenu_.compass, menu_.compass, sizeof(menu_.compass)) == 0;
  if (!same) {
    legendMenu_ = menu_;
    legendIndex_ = 0;
    legendTimed_ = false;
  }
  char entries[KEY_COUNT + 1][24];
  legendShown_ = drawn && legendEntries(entries) > 0;
  if (legendShown_) drawLegendRow();
}

uint32_t OledDisplay::idleWorkDueInMs(uint32_t nowMs) const {
  char entries[KEY_COUNT + 1][24];
  if (!ready_ || !legendShown_ || legendEntries(entries) < 2) return UINT32_MAX;
  if (!legendTimed_) {
    legendTimed_ = true;
    legendSinceMs_ = nowMs;
  }
  const uint32_t elapsed = nowMs - legendSinceMs_;
  return elapsed >= LEGEND_STEP_MS ? 0 : LEGEND_STEP_MS - elapsed;
}

void OledDisplay::idleWork(uint32_t nowMs) {
  if (!ready_ || !legendShown_) return;
  ++legendIndex_;
  legendSinceMs_ = nowMs;
  legendTimed_ = true;
  drawLegendRow();
  display_->display();
}

// Profile picker as a list (picker_list.h): the page's names, More names,
// Back; or "Join as" a name with Yes / Back. Each row says in words what it
// is, so nothing depends on the highlight alone.
void OledDisplay::showPicker(const TurnHubProtocol::ProfilePickerPacket &page, uint8_t cursor) {
  legendShown_ = false;
  if (!ready_) return;
  using TurnHubProtocol::PickerNotice;
  constexpr int16_t ROW_HEIGHT = 12;
  const bool confirm = page.mode == TurnHubProtocol::PickerMode::Confirm;
  const PickerRows rows = pickerRows(page);
  if (cursor >= rows.count) cursor = 0;
  display_->clearDisplay();
  char position[12] = "";
  if (!confirm && page.pageCount > 1) {
    snprintf(position, sizeof(position), "%u/%u", static_cast<unsigned>(page.page + 1),
        static_cast<unsigned>(page.pageCount));
  }
  header(confirm ? "JOIN AS" : "WHO PLAYS?", position);
  const int16_t w = display_->width();
  int16_t y = HEADER_HEIGHT + 2;
  const char *notice = page.notice == PickerNotice::NeedsPhone ? "Sign in on phone" :
      page.notice == PickerNotice::Unavailable ? "Not available" :
      page.notice == PickerNotice::TableFull ? "Table is full" :
      page.notice == PickerNotice::Failed ? "Try again" : nullptr;
  if (confirm) {
    if (fontText(page.items[0].name, &BrassFonts::OledName, y + 1, y + 16, Align::Center) < 0 &&
        fontText(page.items[0].name, &BrassFonts::OledSmall, y + 3, y + 14, Align::Center) < 0) {
      text(page.items[0].name, y, 2, Align::Center);
    }
    y += 18;
  } else if (notice) {
    banner(notice, y, true, Icon::None);
    y += BANNER_HEIGHT + 1;
  }
  // Scroll so the cursor row stays visible.
  const uint8_t fit = static_cast<uint8_t>((display_->height() - y) / ROW_HEIGHT);
  const uint8_t first = cursor >= fit ? cursor - (fit - 1) : 0;
  for (uint8_t i = first; i < rows.count && i < first + fit; ++i, y += ROW_HEIGHT) {
    const bool selected = i == cursor;
    if (selected) display_->fillRect(0, y - 1, w, ROW_HEIGHT - 1, SH110X_WHITE);
    char line[32];
    const PickerRow row = rows.rows[i];
    if (row == PickerRow::More) snprintf(line, sizeof(line), "More names");
    else if (row == PickerRow::Yes) snprintf(line, sizeof(line), "Yes, join");
    else if (row == PickerRow::Back) snprintf(line, sizeof(line), confirm || page.page > 0 ? "Back" : "Cancel");
    else {
      const auto &item = page.items[static_cast<uint8_t>(row)];
      const char *tag = (item.flags & TurnHubProtocol::PICKER_ITEM_LOCKED) ? " (phone)" :
          (item.flags & TurnHubProtocol::PICKER_ITEM_PLAYING) ? " (attach)" : "";
      snprintf(line, sizeof(line), "%s%s", item.name, tag);
    }
    text(line, y + 1, 1, Align::Left, selected, 3, w - 3);
  }
  display_->display();
}

void OledDisplay::status(const char *headerRight, const char *big,
    const char *first, const char *second) {
  if (!ready_) return;
  if (drawDeviceMenu()) return;
  display_->clearDisplay();
  header("TurnHub", headerRight);
  bigLine(big);
  text(first, 41, 1, Align::Center);
  text(second, 52, 1, Align::Center);
  legend(second == nullptr);  // The bottom row is free without a second line.
  display_->display();
}

// The status screens' big line: Cinzel over an ornamental rule.
void OledDisplay::bigLine(const char *big) {
  if (fontText(big, &BrassFonts::OledName, 15, 31, Align::Center) < 0) text(big, 16, 2, Align::Center);
  rule(36, 16, display_->width() - 16);
}

void OledDisplay::showBooting() {
  legendShown_ = false;
  if (!ready_) return;
  display_->clearDisplay();
  splash("Booting");
  display_->display();
}

void OledDisplay::showUnpaired() {
  status("PAIR", "UNPAIRED", "Press Pair on both", "Sigil and Atlas");
}

void OledDisplay::showReady(uint8_t sigilId) {
  char big[24];
  snprintf(big, sizeof(big), "SIGIL %u", static_cast<unsigned>(sigilId + 1));
  status("READY", big, "Ready for game");
}

void OledDisplay::showAtlasLost(uint8_t sigilId) {
  legendShown_ = false;
  if (!ready_) return;
  // Drawn directly, not through status(): an open menu list must not cover
  // it, since none of its actions can reach Atlas now.
  char label[12];
  snprintf(label, sizeof(label), "SIGIL %u", static_cast<unsigned>(sigilId + 1));
  display_->clearDisplay();
  header("TurnHub", label);
  bigLine("NO ATLAS");
  text("Atlas not responding", 41, 1, Align::Center);
  text("Searching...", 52, 1, Align::Center);
  display_->display();
}

void OledDisplay::showPairingCode(uint16_t code) {
  legendShown_ = false;
  if (!ready_) return;
  char digits[5];
  TurnHubSecureLink::formatPairingCode(code, digits);
  // Drawn directly: an open menu list must not cover the code.
  display_->clearDisplay();
  header("TurnHub", "PAIR");
  if (fontText(digits, &BrassFonts::OledLifeMid, 13, 38, Align::Center) < 0) {
    text(digits, 15, 3, Align::Center);
  }
  text("Same code on Atlas?", 41, 1, Align::Center);
  text("Confirm it there", 52, 1, Align::Center);
  display_->display();
}

void OledDisplay::showUpdate(const char *status, int8_t percent) {
  legendShown_ = false;
  if (!ready_) return;
  // Drawn directly: an open menu list must not cover it.
  display_->clearDisplay();
  header("TurnHub", "UPDATE");
  char big[8];
  if (percent >= 0) {
    snprintf(big, sizeof(big), "%d%%", static_cast<int>(percent));
  } else {
    snprintf(big, sizeof(big), "...");
  }
  bigLine(big);
  text(status, 41, 1, Align::Center);
  text("Keep it powered", 52, 1, Align::Center);
  display_->display();
}

void OledDisplay::showGame(const TurnHubProtocol::GameDisplayPacket &s) {
  if (!ready_ || drawDeviceMenu()) return;
  const uint8_t primary = TurnHubProtocol::displayPrimaryPlayer(s.state);
  const uint8_t secondary = TurnHubProtocol::displaySecondaryPlayer(s.state);
  const bool shared = secondary != 0;
  const bool active = TurnHubProtocol::hasDisplayFlag(s.state, TurnHubProtocol::DISPLAY_FLAG_ACTIVE);
  const char seat = primary < secondary ? 'A' : 'B';
  const int16_t w = display_->width();
  char label[32];
  display_->clearDisplay();
  snprintf(label, sizeof(label), "S%u R%u", static_cast<unsigned>(s.sigilId + 1),
      static_cast<unsigned>(TurnHubProtocol::displayTurnNumber(s.state)));
  header(s.commander ? "COMMANDER" : "GAME", label,
      TurnHubProtocol::hasDisplayFlag(s.state, TurnHubProtocol::DISPLAY_FLAG_HOST));
  const bool asking = life_.request.target != 0;
  if (asking) {
    snprintf(label, sizeof(label), "P%u: %+ld LIFE?", static_cast<unsigned>(life_.request.requester),
        static_cast<long>(life_.request.delta));
    banner(label, 12, true, Icon::None);
  } else if (life_.passPending) {
    // The pass waits out Atlas's grace period; the ring counts it down.
    banner("PASSING...", 12, true, Icon::Turn);
  } else if (life_.passingPlayer) {
    // Another player's pass, still undoable: the whole table sees it.
    snprintf(label, sizeof(label), "P%u PASSING...", static_cast<unsigned>(life_.passingPlayer));
    banner(label, 12, false, Icon::None);
  } else {
    banner(active ? "YOUR TURN" : "WAITING FOR TURN", 12, active,
        active ? Icon::Turn : Icon::None);
  }
  // A change still being gathered shows the total it will make.
  const bool pending = life_.pending != 0 && life_.pendingPlayer == primary;
  const int32_t shownLife = pending ? s.primary.life + life_.pending : s.primary.life;
  char line[32];
  if (asking) snprintf(line, sizeof(line), "\x1b deny   approve \x1a");
  else if (pending) snprintf(line, sizeof(line), "%+ld, sending...", static_cast<long>(life_.pending));
  {
    // Each shared-seat player uses the same full-screen layout.
    // Atlas puts the currently controlled player in primary.
    // Commander damage, once taken, gets the bottom two rows; the life total
    // drops to size 2 to make room and the key help gives way.
    const bool cmdShown = commanderDamageShown(s);
    const int16_t lifeY = cmdShown ? 32 : 36;
    snprintf(label, sizeof(label), "%c: %s", seat, s.primary.name);
    if (!shared) snprintf(label, sizeof(label), "%s", s.primary.name);
    if (asking || pending ||
        fontText(label, &BrassFonts::OledSmall, 23, 31, Align::Center) < 0) {
      text(asking || pending ? line : label, 24, 1, Align::Center);
    }
    lifeTotal(shownLife, 32, cmdShown ? 2 : 3);
    // Left of the life total, when the number leaves room (up to 3 digits).
    char digits[12];
    snprintf(digits, sizeof(digits), "%ld", static_cast<long>(shownLife));
    const uint8_t mine = primary < secondary || !secondary ? life_.avatar[0] : life_.avatar[1];
    if (mine && strlen(digits) <= 3) {
      display_->fillRect(0, lifeY, 18, 16, SH110X_BLACK);
      TurnHubAvatars::drawAvatar(*display_, mine, 0, lifeY, SH110X_WHITE);
    }
    if (cmdShown) {
      // "Cmd <source>" left, damage right. With more than two sources the
      // second row counts the rest; Atlas and the web portal list them all.
      // A pending pass keeps its undo hint on the bottom row.
      const uint8_t total = s.sourceCount + s.omittedSources;
      const uint8_t rows = life_.passPending ? 1 : 2;
      for (uint8_t i = 0; i < rows && i < s.sourceCount; ++i) {
        const int16_t y = 48 + 8 * i;
        if (i == rows - 1 && total > rows) {
          snprintf(label, sizeof(label), "+%u more cmd sources", static_cast<unsigned>(total - i));
          text(label, y, 1, Align::Left, false, 0, w);
          break;
        }
        char damage[24];
        formatCommanderDamage(s.sources[i], damage, sizeof(damage));
        const int16_t damageStart = w - static_cast<int16_t>(strlen(damage)) * 6;
        snprintf(label, sizeof(label), "Cmd %s", s.sources[i].name);
        text(label, y, 1, Align::Left, false, 0, damageStart - 6);
        text(damage, y, 1, Align::Right, false, damageStart);
      }
      if (life_.passPending) text("Click again to undo", LEGEND_Y, 1, Align::Center);
      legend(false);
    } else if (life_.passPending) {
      // Undo pass is on the click (sigil_menu.cpp): say so plainly.
      text("Click again to undo", LEGEND_Y, 1, Align::Center);
      legend(false);
    } else {
      // One quiet line of key help (turntest, 2026-09-26): the compass legend,
      // a key at a time. The ask line above already names its keys.
      legend(!asking && !pending);
    }
  }
  display_->display();
}

void OledDisplay::showState(uint8_t sigilId, TurnHubProtocol::DisplayMode mode,
    uint8_t primaryPlayer, uint8_t secondaryPlayer, uint8_t turnNumber, uint8_t flags) {
  if (!ready_ || drawDeviceMenu()) return;
  const bool active = flags & TurnHubProtocol::DISPLAY_FLAG_ACTIVE;
  const bool starter = flags & TurnHubProtocol::DISPLAY_FLAG_STARTER;
  const bool winner = flags & TurnHubProtocol::DISPLAY_FLAG_WINNER;
  const bool attention = flags & TurnHubProtocol::DISPLAY_FLAG_ATTENTION;
  const char *title;
  const char *message;
  bool indicateSeat = false;
  Icon kind = Icon::None;
  switch (mode) {
    case TurnHubProtocol::DisplayMode::Lobby:
      title = "LOBBY";
      message = secondaryPlayer ? "SHARED SIGIL" : (starter ? "STARTER" : "IN LOBBY");
      break;
    case TurnHubProtocol::DisplayMode::Starting:
      title = "START"; message = starter ? "GO FIRST" : "GET READY";
      indicateSeat = starter; kind = Icon::Turn; break;
    case TurnHubProtocol::DisplayMode::Running:
      title = "GAME"; message = active ? "YOUR TURN" : "WAITING";
      indicateSeat = active; kind = Icon::Turn; break;
    case TurnHubProtocol::DisplayMode::Paused:
      title = "PAUSED"; message = attention ? "ACTION NEEDED" : "GAME PAUSED";
      indicateSeat = attention; kind = Icon::Pause; break;
    case TurnHubProtocol::DisplayMode::GameOver:
      title = "GAME OVER"; message = winner ? "WINNER!" : "GAME COMPLETE";
      indicateSeat = winner; kind = Icon::Crown; break;
    default: showReady(sigilId); return;
  }
  display_->clearDisplay();
  char label[32];
  snprintf(label, sizeof(label), "S%u R%u", static_cast<unsigned>(sigilId + 1),
      static_cast<unsigned>(turnNumber));
  header(title, label, flags & TurnHubProtocol::DISPLAY_FLAG_HOST);
  if (secondaryPlayer && indicateSeat) {
    snprintf(label, sizeof(label), "%s: %c", message,
        primaryPlayer < secondaryPlayer ? 'A' : 'B');
    banner(label, 13, true, kind);
  } else {
    banner(message, 13, indicateSeat, indicateSeat ? kind : Icon::None);
  }
  if (secondaryPlayer) {
    const bool seatA = primaryPlayer < secondaryPlayer;
    snprintf(label, sizeof(label), "%c: %s", seatA ? 'A' : 'B',
        seatA ? (seatNameA_[0] ? seatNameA_ : "Guest") : (seatNameB_[0] ? seatNameB_ : "Guest"));
    if (fontText(label, &BrassFonts::OledName, 28, 44, Align::Center) < 0 &&
        fontText(label, &BrassFonts::OledSmall, 31, 42, Align::Center) < 0) {
      text(label, 30, 1, Align::Center);
    }
  } else {
    snprintf(label, sizeof(label), "Player %u", static_cast<unsigned>(primaryPlayer));
    const char *name = seatNameA_[0] ? seatNameA_ : label;
    if (fontText(name, &BrassFonts::OledName, 28, 44, Align::Center) < 0 &&
        fontText(name, &BrassFonts::OledSmall, 31, 42, Align::Center) < 0) {
      text(name, 30, 2, Align::Center);
    }
  }
  // The legend takes the bottom row; with none, a lone winner gets a crown
  // under their name (the banner already says WINNER! in words).
  char entries[KEY_COUNT + 1][24];
  const bool legendFits = legendEntries(entries) > 0;
  legend(legendFits);
  if (!legendFits && !secondaryPlayer && mode == TurnHubProtocol::DisplayMode::GameOver && winner) {
    icon(Icon::Crown, display_->width() / 2 - 6, 52, SH110X_WHITE);
  }
  display_->display();
}

}  // namespace TurnHubSigil
