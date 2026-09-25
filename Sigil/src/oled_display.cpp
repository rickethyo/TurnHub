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

void OledDisplay::line(const char *text, int16_t y, uint8_t maxSize, bool inverse) {
  if (!text) return;
  const size_t length = strlen(text);
  uint8_t size = maxSize;
  while (size > 1 && (length * 6 * size > static_cast<size_t>(display_->width()) ||
      y + 8 * size > display_->height())) --size;
  if (y < 0 || y + 8 * size > display_->height()) return;
  if (inverse) display_->fillRect(0, y, display_->width(), 8 * size, SH110X_WHITE);
  display_->setTextColor(inverse ? SH110X_BLACK : SH110X_WHITE);
  display_->setTextSize(size);
  display_->setCursor(0, y);
  const size_t capacity = display_->width() / (6 * size);
  for (size_t i = 0; i < length && i < capacity; ++i) display_->print(text[i]);
  display_->setTextColor(SH110X_WHITE);
}

void OledDisplay::header(const char *title, uint8_t sigilId, uint8_t turn, bool host) {
  char text[32];
  snprintf(text, sizeof(text), "%s S%u T%u%s", title,
      static_cast<unsigned>(sigilId + 1), static_cast<unsigned>(turn), host ? " H" : "");
  line(text, 0);
}

void OledDisplay::status(const char *first, const char *second, const char *third) {
  if (!ready_) return;
  display_->clearDisplay();
  line("TurnHub", 0);
  line(first, 8);
  line(second, 16);
  line(third, 24);
  display_->display();
}

void OledDisplay::showBooting() { status("Booting"); }
void OledDisplay::showUnpaired() { status("Unpaired", "Press Pair on both"); }
void OledDisplay::showReady(uint8_t sigilId) {
  char text[24];
  snprintf(text, sizeof(text), "Sigil %u", static_cast<unsigned>(sigilId + 1));
  status(text, "Ready for game");
}

void OledDisplay::showGame(const TurnHubProtocol::GameDisplayPacket &s) {
  if (!ready_) return;
  const uint8_t primary = TurnHubProtocol::displayPrimaryPlayer(s.state);
  const uint8_t secondary = TurnHubProtocol::displaySecondaryPlayer(s.state);
  const bool shared = secondary != 0;
  const bool active = TurnHubProtocol::hasDisplayFlag(s.state, TurnHubProtocol::DISPLAY_FLAG_ACTIVE);
  const char seat = primary < secondary ? 'A' : 'B';
  display_->clearDisplay();
  header(s.commander ? "Cmd" : "Game", s.sigilId,
      TurnHubProtocol::displayTurnNumber(s.state),
      TurnHubProtocol::hasDisplayFlag(s.state, TurnHubProtocol::DISPLAY_FLAG_HOST));
  line(active ? "YOUR TURN" : "WAITING FOR TURN", 8, 1, active);
  char text[32];
  if (shared) snprintf(text, sizeof(text), "%c: %s", seat, s.primary.name);
  else snprintf(text, sizeof(text), "%s", s.primary.name);
  line(text, 16);
  snprintf(text, sizeof(text), "LIFE %ld", static_cast<long>(s.primary.life));
  line(text, 24, 2);
  if (shared) {
    snprintf(text, sizeof(text), "%c: %s", seat == 'A' ? 'B' : 'A', s.secondary.name);
    line(text, 40);
    snprintf(text, sizeof(text), "LIFE %ld", static_cast<long>(s.secondary.life));
    line(text, 48);
  }
  // Minimal scaffold: details remain available on Atlas/companion clients.
  if (s.commander) line("CMD: see companion", 56);
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
  switch (mode) {
    case TurnHubProtocol::DisplayMode::Lobby:
      title = "Lobby";
      message = secondaryPlayer ? "SHARED SIGIL" : (starter ? "STARTER" : "IN LOBBY");
      break;
    case TurnHubProtocol::DisplayMode::Starting:
      title = "Start"; message = starter ? "GO FIRST" : "GET READY";
      indicateSeat = starter; break;
    case TurnHubProtocol::DisplayMode::Running:
      title = "Game"; message = active ? "YOUR TURN" : "WAITING";
      indicateSeat = active; break;
    case TurnHubProtocol::DisplayMode::Paused:
      title = "Pause"; message = attention ? "ACTION NEEDED" : "GAME PAUSED";
      indicateSeat = attention; break;
    case TurnHubProtocol::DisplayMode::GameOver:
      title = "Over"; message = winner ? "WINNER!" : "GAME COMPLETE";
      indicateSeat = winner; break;
    default: showReady(sigilId); return;
  }
  display_->clearDisplay();
  header(title, sigilId, turnNumber, flags & TurnHubProtocol::DISPLAY_FLAG_HOST);
  char text[32];
  if (secondaryPlayer && indicateSeat) {
    snprintf(text, sizeof(text), "%s: %c", message, primaryPlayer < secondaryPlayer ? 'A' : 'B');
    line(text, 8, 1, true);
  } else {
    line(message, 8, 1, indicateSeat);
  }
  if (secondaryPlayer) {
    snprintf(text, sizeof(text), "A: %s", seatNameA_[0] ? seatNameA_ : "Guest");
    line(text, 16);
    snprintf(text, sizeof(text), "B: %s", seatNameB_[0] ? seatNameB_ : "Guest");
    line(text, 24);
  } else {
    snprintf(text, sizeof(text), "Player %u", static_cast<unsigned>(primaryPlayer));
    line(seatNameA_[0] ? seatNameA_ : text, 16);
  }
  display_->display();
}

}  // namespace TurnHubSigil
