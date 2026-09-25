#include "oled_display.h"
#include "display_name.h"

#include <Arduino.h>
#include <Wire.h>
#include <cstring>
#include <new>

namespace TurnHubSigil {
namespace {
// ESP32 output pins excluding flash, UART0, and Sigil's existing controls/cues
// (main.cpp). These are validation exclusions, NOT proposed OLED assignments.
bool availableOutputPin(int pin) {
  if (pin < 0 || pin > 33 || (pin >= 6 && pin <= 11) || pin == 20 || pin == 24 ||
      (pin >= 28 && pin <= 31)) return false;
  switch (pin) {
    case 1: case 3:  // Serial diagnostics.
    case 13: case 14: case 27: // LEDs.
    case 19: case 25: case 26: case 32: // Buttons, including Pair.
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
    // (GPIO19 is the existing Pair button). No hardware SPI bus is started.
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
}

bool OledDisplay::setSeatName(uint8_t slot, const char *name) {
  if (slot == 1) return updateDisplayName(seatNameA_, name);
  if (slot == 2) return updateDisplayName(seatNameB_, name);
  return false;
}

namespace {
constexpr int16_t HEADER_HEIGHT = 11;
constexpr int16_t BANNER_HEIGHT = 11;
constexpr int16_t HEART_WIDTH = 13;
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

// Solid title bar: mode on the left, seat/turn on the right, crown for host.
void OledDisplay::header(const char *title, const char *right, bool host) {
  const int16_t w = display_->width();
  display_->fillRect(0, 0, w, HEADER_HEIGHT, SH110X_WHITE);
  const int16_t titleEnd = text(title, 2, 1, Align::Left, true, 3, w / 2 + 8);
  const int16_t rightStart = w - 3 - static_cast<int16_t>(strlen(right)) * 6;
  text(right, 2, 1, Align::Right, true, titleEnd + 2, w - 3);
  if (host && rightStart - 16 >= titleEnd + 2) {
    icon(Icon::Crown, rightStart - 16, 1, SH110X_BLACK);
  }
}

// Highlighted banners are filled and flanked by icons; plain ones get a frame.
// The words always carry the meaning; icons only add character.
void OledDisplay::banner(const char *message, int16_t y, bool highlight, Icon kind) {
  const int16_t w = display_->width();
  if (highlight) {
    display_->fillRoundRect(0, y, w, BANNER_HEIGHT, 3, SH110X_WHITE);
    icon(kind, 2, y + 1, SH110X_BLACK);
    icon(kind == Icon::Turn ? Icon::TurnBack : kind, w - 15, y + 1, SH110X_BLACK);
    text(message, y + 2, 1, Align::Center, true, 16, w - 16);
  } else {
    display_->drawRoundRect(0, y, w, BANNER_HEIGHT, 3, SH110X_WHITE);
    text(message, y + 2, 1, Align::Center, false, 3, w - 3);
  }
}

// 13x9 glyphs drawn from primitives.
void OledDisplay::icon(Icon kind, int16_t x, int16_t y, uint16_t color) {
  switch (kind) {
    case Icon::Turn:
      display_->fillTriangle(x + 3, y, x + 3, y + 8, x + 11, y + 4, color);
      break;
    case Icon::TurnBack:
      display_->fillTriangle(x + 10, y, x + 10, y + 8, x + 2, y + 4, color);
      break;
    case Icon::Pause:
      display_->fillRect(x + 3, y, 3, 9, color);
      display_->fillRect(x + 8, y, 3, 9, color);
      break;
    case Icon::Crown:
      display_->fillRect(x, y + 6, 13, 3, color);
      display_->fillTriangle(x, y + 6, x + 2, y, x + 4, y + 6, color);
      display_->fillTriangle(x + 4, y + 6, x + 6, y, x + 8, y + 6, color);
      display_->fillTriangle(x + 8, y + 6, x + 10, y, x + 12, y + 6, color);
      break;
    case Icon::None:
      break;
  }
}

void OledDisplay::heart(int16_t x, int16_t y) {
  display_->fillCircle(x + 3, y + 3, 3, SH110X_WHITE);
  display_->fillCircle(x + 9, y + 3, 3, SH110X_WHITE);
  display_->fillTriangle(x, y + 4, x + 12, y + 4, x + 6, y + 10, SH110X_WHITE);
}

// Heart plus the largest life number that fits, centered as one unit.
void OledDisplay::lifeTotal(int32_t life, int16_t y, uint8_t maxSize) {
  char number[16];
  snprintf(number, sizeof(number), "%ld", static_cast<long>(life));
  const int16_t w = display_->width();
  const int16_t length = static_cast<int16_t>(strlen(number));
  uint8_t size = maxSize;
  while (size > 1 && (HEART_WIDTH + 4 + length * 6 * size > w ||
      y + 8 * size > display_->height())) --size;
  const int16_t total = HEART_WIDTH + 4 + length * 6 * size;
  const int16_t x = total < w ? (w - total) / 2 : 0;
  heart(x, y + (8 * size - 11) / 2);
  text(number, y, size, Align::Left, false, x + HEART_WIDTH + 4, w);
}

// Boot splash: an hourglass (the turn timer) inside a double ring.
void OledDisplay::splash(const char *caption) {
  const int16_t cx = display_->width() / 2;
  display_->drawCircle(cx, 15, 14, SH110X_WHITE);
  display_->drawCircle(cx, 15, 12, SH110X_WHITE);
  display_->drawTriangle(cx - 6, 7, cx + 6, 7, cx, 15, SH110X_WHITE);
  display_->drawTriangle(cx - 6, 23, cx + 6, 23, cx, 15, SH110X_WHITE);
  display_->fillTriangle(cx - 4, 22, cx + 4, 22, cx, 18, SH110X_WHITE);
  text("TurnHub", 33, 2, Align::Center);
  text(caption, 54, 1, Align::Center);
}

void OledDisplay::status(const char *headerRight, const char *big,
    const char *first, const char *second) {
  if (!ready_) return;
  display_->clearDisplay();
  header("TurnHub", headerRight);
  text(big, 18, 2, Align::Center);
  text(first, 41, 1, Align::Center);
  text(second, 52, 1, Align::Center);
  display_->display();
}

void OledDisplay::showBooting() {
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

void OledDisplay::showGame(const TurnHubProtocol::GameDisplayPacket &s) {
  if (!ready_) return;
  const uint8_t primary = TurnHubProtocol::displayPrimaryPlayer(s.state);
  const uint8_t secondary = TurnHubProtocol::displaySecondaryPlayer(s.state);
  const bool shared = secondary != 0;
  const bool active = TurnHubProtocol::hasDisplayFlag(s.state, TurnHubProtocol::DISPLAY_FLAG_ACTIVE);
  const char seat = primary < secondary ? 'A' : 'B';
  const int16_t w = display_->width();
  char label[32];
  display_->clearDisplay();
  snprintf(label, sizeof(label), "S%u T%u", static_cast<unsigned>(s.sigilId + 1),
      static_cast<unsigned>(TurnHubProtocol::displayTurnNumber(s.state)));
  header(s.commander ? "COMMANDER" : "GAME", label,
      TurnHubProtocol::hasDisplayFlag(s.state, TurnHubProtocol::DISPLAY_FLAG_HOST));
  banner(active ? "YOUR TURN" : "WAITING FOR TURN", 13, active,
      active ? Icon::Turn : Icon::None);
  if (!shared) {
    text(s.primary.name, 27, 1, Align::Center);
    lifeTotal(s.primary.life, 38, 3);
  } else {
    snprintf(label, sizeof(label), "%c: %s", seat, s.primary.name);
    text(label, 27, 1, Align::Center);
    lifeTotal(s.primary.life, 36, 2);
    // The other seat on one quiet row; its life total is never truncated.
    char life[16];
    snprintf(life, sizeof(life), "\x03%ld", static_cast<long>(s.secondary.life));
    const int16_t lifeStart = w - static_cast<int16_t>(strlen(life)) * 6;
    display_->drawFastHLine(0, 54, w, SH110X_WHITE);
    snprintf(label, sizeof(label), "%c: %s", seat == 'A' ? 'B' : 'A', s.secondary.name);
    text(label, 56, 1, Align::Left, false, 0, lifeStart - 4);
    text(life, 56, 1, Align::Right, false, lifeStart);
  }
  display_->display();
}

void OledDisplay::showState(uint8_t sigilId, TurnHubProtocol::DisplayMode mode,
    uint8_t primaryPlayer, uint8_t secondaryPlayer, uint8_t turnNumber, uint8_t flags) {
  if (!ready_) return;
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
  snprintf(label, sizeof(label), "S%u T%u", static_cast<unsigned>(sigilId + 1),
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
    snprintf(label, sizeof(label), "A: %s", seatNameA_[0] ? seatNameA_ : "Guest");
    text(label, 31, 1, Align::Center);
    snprintf(label, sizeof(label), "B: %s", seatNameB_[0] ? seatNameB_ : "Guest");
    text(label, 43, 1, Align::Center);
  } else {
    snprintf(label, sizeof(label), "Player %u", static_cast<unsigned>(primaryPlayer));
    text(seatNameA_[0] ? seatNameA_ : label, 30, 2, Align::Center);
    // A lone winner gets a crown under their name.
    if (mode == TurnHubProtocol::DisplayMode::GameOver && winner) {
      icon(Icon::Crown, display_->width() / 2 - 6, 52, SH110X_WHITE);
    }
  }
  display_->display();
}

}  // namespace TurnHubSigil
