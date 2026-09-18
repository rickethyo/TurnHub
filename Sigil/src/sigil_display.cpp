#include "sigil_display.h"

#include <SPI.h>

namespace TurnHubSigil {

SigilDisplay::SigilDisplay()
    : display_(GxEPD2_213_B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)) {}

void SigilDisplay::begin() {
  SPI.begin(18, 19, 23, EPD_CS);

  display_.init(115200);
  display_.setRotation(1);
  display_.setTextColor(GxEPD_BLACK);
  display_.setFullWindow();

  Serial.println("SIGIL|DISPLAY|READY|250x122");
}

void SigilDisplay::drawHeader(const char *title) {
  display_.setTextSize(2);
  display_.setCursor(10, 22);
  display_.print(title);
  display_.drawFastHLine(10, 30, display_.width() - 20, GxEPD_BLACK);
}

void SigilDisplay::drawStatus(const char *line1, const char *line2) {
  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawHeader("TurnHub");

    display_.setTextSize(2);
    display_.setCursor(10, 66);
    display_.print(line1);

    if (line2 != nullptr) {
      display_.setTextSize(1);
      display_.setCursor(10, 96);
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
    case TurnHubProtocol::DisplayMode::Lobby: header = "Lobby"; break;
    case TurnHubProtocol::DisplayMode::Starting: header = "Starting"; break;
    case TurnHubProtocol::DisplayMode::Running: header = "Game"; break;
    case TurnHubProtocol::DisplayMode::Paused: header = "Paused"; break;
    case TurnHubProtocol::DisplayMode::GameOver: header = "Game Over"; break;
    case TurnHubProtocol::DisplayMode::Ready:
    default:
      showReady(sigilId);
      return;
  }

  const bool shared = secondaryPlayer != 0;
  const bool focused = active || starter || winner || attention;

  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawHeader(header);

    display_.setTextSize(1);
    display_.setCursor(196, 20);
    display_.printf("S%u", static_cast<unsigned>(sigilId + 1));
    if (host) {
      display_.setCursor(220, 20);
      display_.print("H");
    }

    if (shared) {
      // Player numbering is assigned A then B for a shared physical Sigil.
      // Atlas may move the focused player into primaryPlayer, so recover the
      // fixed physical A/B labels from the lower/higher player number.
      const uint8_t playerA = primaryPlayer < secondaryPlayer
          ? primaryPlayer
          : secondaryPlayer;
      const uint8_t playerB = primaryPlayer < secondaryPlayer
          ? secondaryPlayer
          : primaryPlayer;
      const bool focusA = focused && primaryPlayer == playerA;
      const bool focusB = focused && primaryPlayer == playerB;

      constexpr int16_t boxY = 38;
      constexpr int16_t boxH = 54;
      constexpr int16_t leftX = 8;
      constexpr int16_t rightX = 127;
      constexpr int16_t boxW = 115;

      display_.drawRect(leftX, boxY, boxW, boxH, GxEPD_BLACK);
      display_.drawRect(rightX, boxY, boxW, boxH, GxEPD_BLACK);
      if (focusA) {
        display_.drawRect(leftX + 2, boxY + 2, boxW - 4, boxH - 4, GxEPD_BLACK);
      }
      if (focusB) {
        display_.drawRect(rightX + 2, boxY + 2, boxW - 4, boxH - 4, GxEPD_BLACK);
      }

      display_.setTextSize(1);
      display_.setCursor(leftX + 8, boxY + 14);
      display_.print(focusA ? "> SEAT A" : "  SEAT A");
      display_.setCursor(rightX + 8, boxY + 14);
      display_.print(focusB ? "> SEAT B" : "  SEAT B");

      display_.setTextSize(3);
      display_.setCursor(leftX + 26, boxY + 44);
      display_.printf("P%u", static_cast<unsigned>(playerA));
      display_.setCursor(rightX + 26, boxY + 44);
      display_.printf("P%u", static_cast<unsigned>(playerB));

      display_.setTextSize(1);
      display_.setCursor(10, 111);
      if (mode == TurnHubProtocol::DisplayMode::Running && active) {
        display_.printf("YOUR TURN - %c", focusA ? 'A' : 'B');
      } else if (mode == TurnHubProtocol::DisplayMode::Starting && starter) {
        display_.printf("GO FIRST - %c", focusA ? 'A' : 'B');
      } else if (mode == TurnHubProtocol::DisplayMode::Paused && attention) {
        display_.printf("ACTION NEEDED - %c", focusA ? 'A' : 'B');
      } else if (mode == TurnHubProtocol::DisplayMode::GameOver && winner) {
        display_.printf("WINNER - %c", focusA ? 'A' : 'B');
      } else if (mode == TurnHubProtocol::DisplayMode::Paused) {
        display_.print("GAME PAUSED");
      } else if (mode == TurnHubProtocol::DisplayMode::Lobby) {
        display_.print(host ? "SHARED SIGIL - HOST" : "SHARED SIGIL");
      } else {
        display_.print("WAITING");
      }

      if (turnNumber != 0) {
        display_.setCursor(205, 111);
        display_.printf("T%u", static_cast<unsigned>(turnNumber));
      }
    } else {
      display_.setTextSize(2);
      display_.setCursor(10, 67);

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
      display_.setCursor(10, 98);
      switch (mode) {
        case TurnHubProtocol::DisplayMode::Lobby:
          display_.printf("Player %u", static_cast<unsigned>(primaryPlayer));
          if (host) display_.print("  HOST");
          if (starter) display_.print("  STARTER");
          break;
        case TurnHubProtocol::DisplayMode::Starting:
          display_.print(starter ? "You start" : "Get ready...");
          break;
        case TurnHubProtocol::DisplayMode::Running:
          display_.printf(active ? "Player %u - active" : "Player %u - waiting",
              static_cast<unsigned>(primaryPlayer));
          break;
        case TurnHubProtocol::DisplayMode::Paused:
          display_.printf(attention ? "Player %u - check table" : "Game paused",
              static_cast<unsigned>(primaryPlayer));
          break;
        case TurnHubProtocol::DisplayMode::GameOver:
          display_.printf(winner ? "Player %u wins" : "Game complete",
              static_cast<unsigned>(primaryPlayer));
          break;
        case TurnHubProtocol::DisplayMode::Ready:
        default:
          break;
      }

      if (turnNumber != 0) {
        display_.setCursor(205, 98);
        display_.printf("T%u", static_cast<unsigned>(turnNumber));
      }
    }
  } while (display_.nextPage());
}

}  // namespace TurnHubSigil
