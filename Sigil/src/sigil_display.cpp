#include "sigil_display.h"

#include <SPI.h>

namespace TurnHubSigil {

SigilDisplay::SigilDisplay()
    : display_(GxEPD2_213_B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)) {}

void SigilDisplay::begin() {
  // Standard ESP32 VSPI pins. The display does not use MISO, but supplying
  // GPIO19 here keeps the hardware SPI bus configured conventionally.
  SPI.begin(18, 19, 23, EPD_CS);

  display_.init(115200);
  display_.setRotation(1);
  display_.setTextColor(GxEPD_BLACK);
  display_.setFullWindow();

  Serial.println("SIGIL|DISPLAY|READY|250x122");
}

void SigilDisplay::drawHeader(const char *title) {
  display_.setTextSize(2);
  display_.setCursor(12, 24);
  display_.print(title);
  display_.drawFastHLine(12, 34, display_.width() - 24, GxEPD_BLACK);
}

void SigilDisplay::drawStatus(const char *line1, const char *line2) {
  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawHeader("TurnHub");

    display_.setTextSize(2);
    display_.setCursor(12, 68);
    display_.print(line1);

    if (line2 != nullptr) {
      display_.setTextSize(1);
      display_.setCursor(12, 96);
      display_.print(line2);
    }
  } while (display_.nextPage());
}

void SigilDisplay::drawPlayerLabel(
    uint8_t primaryPlayer,
    uint8_t secondaryPlayer) {
  if (primaryPlayer == 0) {
    display_.print("Ready");
    return;
  }

  if (secondaryPlayer != 0) {
    display_.printf(
        "P%u + P%u",
        static_cast<unsigned>(primaryPlayer),
        static_cast<unsigned>(secondaryPlayer));
    return;
  }

  display_.printf("Player %u", static_cast<unsigned>(primaryPlayer));
}

void SigilDisplay::showUnpaired() {
  drawStatus("Unpaired", "Waiting for Atlas...");
}

void SigilDisplay::showReady(uint8_t sigilId) {
  char title[24];
  snprintf(title, sizeof(title), "Sigil %u", static_cast<unsigned>(sigilId + 1));
  drawStatus(title, "Ready for game");
}

void SigilDisplay::showState(
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
  switch (mode) {
    case TurnHubProtocol::DisplayMode::Lobby:
      header = "Lobby";
      break;
    case TurnHubProtocol::DisplayMode::Starting:
      header = "Starting";
      break;
    case TurnHubProtocol::DisplayMode::Running:
      header = "Game 1";
      break;
    case TurnHubProtocol::DisplayMode::Paused:
      header = "Paused";
      break;
    case TurnHubProtocol::DisplayMode::GameOver:
      header = "Game Over";
      break;
    case TurnHubProtocol::DisplayMode::Ready:
    default:
      showReady(sigilId);
      return;
  }

  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawHeader(header);

    display_.setTextSize(2);
    display_.setCursor(12, 68);

    if (mode == TurnHubProtocol::DisplayMode::Running && active) {
      display_.print("YOUR TURN");
    } else if (mode == TurnHubProtocol::DisplayMode::Starting && starter) {
      display_.print("GO FIRST");
    } else if (mode == TurnHubProtocol::DisplayMode::Paused && attention) {
      display_.print("ACTION NEEDED");
    } else if (mode == TurnHubProtocol::DisplayMode::GameOver && winner) {
      display_.print("WINNER!");
    } else {
      drawPlayerLabel(primaryPlayer, secondaryPlayer);
    }

    display_.setTextSize(1);
    display_.setCursor(12, 96);

    switch (mode) {
      case TurnHubProtocol::DisplayMode::Lobby:
        display_.printf("Sigil %u", static_cast<unsigned>(sigilId + 1));
        if (host) {
          display_.print("  HOST");
        }
        if (starter) {
          display_.print("  STARTER");
        }
        break;

      case TurnHubProtocol::DisplayMode::Starting:
        if (starter) {
          display_.printf(
              "Player %u starts",
              static_cast<unsigned>(primaryPlayer));
        } else {
          display_.print("Get ready...");
        }
        break;

      case TurnHubProtocol::DisplayMode::Running:
        if (active) {
          display_.printf(
              "Player %u",
              static_cast<unsigned>(primaryPlayer));
        } else {
          display_.print("Waiting");
        }
        if (turnNumber != 0) {
          display_.setCursor(170, 96);
          display_.printf("T:%u", static_cast<unsigned>(turnNumber));
        }
        break;

      case TurnHubProtocol::DisplayMode::Paused:
        if (attention) {
          display_.printf(
              "Player %u - check table",
              static_cast<unsigned>(primaryPlayer));
        } else {
          display_.print("Game paused");
        }
        if (turnNumber != 0) {
          display_.setCursor(170, 96);
          display_.printf("T:%u", static_cast<unsigned>(turnNumber));
        }
        break;

      case TurnHubProtocol::DisplayMode::GameOver:
        if (winner) {
          display_.printf(
              "Player %u wins",
              static_cast<unsigned>(primaryPlayer));
        } else {
          display_.print("Game complete");
        }
        break;

      case TurnHubProtocol::DisplayMode::Ready:
      default:
        break;
    }
  } while (display_.nextPage());
}

}  // namespace TurnHubSigil
