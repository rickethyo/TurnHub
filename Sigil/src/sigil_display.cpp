#include "sigil_display.h"

#include <SPI.h>
#include <cstring>

namespace TurnHubSigil {

SigilDisplay::SigilDisplay()
    : display_(GxEPD2_213_B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)) {}

void SigilDisplay::begin() {
  SPI.begin(18, 19, 23, EPD_CS);
  // This panel is write-only. Release MISO (GPIO 19) for the Pair button.
  // The installed ESP32 core maps MISO=-1 back to its default GPIO 19,
  // so explicitly detach it instead of relying on -1 to disable the input.
  spiDetachMISO(SPI.bus(), 19);

  display_.init(115200);
  display_.setRotation(1);
  display_.setTextColor(GxEPD_BLACK);
  display_.setFullWindow();

  Serial.println("SIGIL|DISPLAY|READY|250x122");
}

void SigilDisplay::setSeatName(uint8_t slot, const char *name) {
  char *target = slot == 1 ? seatNameA_ : slot == 2 ? seatNameB_ : nullptr;
  if (target == nullptr) {
    return;
  }

  memset(target, 0, TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1);
  if (name != nullptr) {
    strncpy(target, name, TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH);
    target[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH] = '\0';
  }
}

void SigilDisplay::printClipped(const char *text, uint8_t maxChars) {
  if (text == nullptr || maxChars == 0) {
    return;
  }
  for (uint8_t i = 0; i < maxChars && text[i] != '\0'; ++i) {
    display_.print(text[i]);
  }
}

void SigilDisplay::drawHeader(const char *title) {
  display_.setTextSize(2);
  display_.setCursor(7, 18);
  display_.print(title);
  display_.drawFastHLine(6, 23, display_.width() - 12, GxEPD_BLACK);
}

void SigilDisplay::drawStatus(const char *line1, const char *line2) {
  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    drawHeader("TurnHub");

    display_.setTextSize(2);
    display_.setCursor(10, 64);
    display_.print(line1);

    if (line2 != nullptr) {
      display_.setTextSize(1);
      display_.setCursor(10, 94);
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
    printClipped(seatNameA_[0] ? seatNameA_ : "Guest A", 8);
    display_.print(" + ");
    printClipped(seatNameB_[0] ? seatNameB_ : "Guest B", 8);
    return;
  }

  if (seatNameA_[0] != '\0') {
    printClipped(seatNameA_, TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH);
    return;
  }

  display_.printf("Player %u", static_cast<unsigned>(primaryPlayer));
}

void SigilDisplay::showUnpaired() {
  drawStatus("Unpaired", "Press Pair on both");
}

void SigilDisplay::showReady(uint8_t sigilId) {
  char title[24];
  snprintf(title, sizeof(title), "Sigil %u", static_cast<unsigned>(sigilId + 1));
  drawStatus(title, "Ready for game");
}

void SigilDisplay::showGame(const TurnHubProtocol::GameDisplayPacket &s) {
  const bool shared = TurnHubProtocol::displaySecondaryPlayer(s.state) != 0;
  const bool active = TurnHubProtocol::hasDisplayFlag(s.state, TurnHubProtocol::DISPLAY_FLAG_ACTIVE);
  char life[16];
  snprintf(life, sizeof(life), "%ld", static_cast<long>(s.primary.life));
  const int width = shared ? 156 : 238;
  uint8_t size = 4;
  while (size > 1 && strlen(life) * 6 * size > static_cast<unsigned>(width)) --size;
  display_.setFullWindow();
  display_.firstPage();
  do {
    display_.fillScreen(GxEPD_WHITE);
    // Built-in font cursors are top-left, not baselines.
    display_.setTextSize(2);
    display_.setCursor(6, 3);
    display_.print(s.commander ? "Commander" : "Game");
    display_.setTextSize(1);
    display_.setCursor(194, 9);
    display_.printf("S%u%s", s.sigilId + 1,
        TurnHubProtocol::hasDisplayFlag(s.state, TurnHubProtocol::DISPLAY_FLAG_HOST) ? " HOST" : "");
    display_.drawFastHLine(6, 23, 238, GxEPD_BLACK);
    display_.setTextSize(2);
    display_.setCursor(6, 27);
    printClipped(s.primary.name, 12);
    display_.setTextSize(size);
    display_.setCursor(6 + (width - strlen(life) * 6 * size) / 2, 44);
    display_.print(life);
    display_.setTextSize(1);
    display_.setCursor(6 + (width - 24) / 2, 76);
    display_.print("LIFE");
    if (shared) {
      display_.setCursor(170, 45);
      printClipped(s.secondary.name, 12);
      display_.setCursor(170, 57);
      display_.printf("%ld", static_cast<long>(s.secondary.life));
      display_.setCursor(170, 69);
      display_.print("LIFE");
    }
    if (s.commander && !s.sourceCount) {
      display_.setCursor(6, 88);
      display_.print("CMD none received");
    }
    for (uint8_t i = 0; i < s.sourceCount; ++i) {
      const auto &entry = s.sources[i];
      display_.setCursor(6, 84 + 8 * i);
      display_.print("CMD ");
      printClipped(entry.name, 9);
      // Keep slot identity when only commander 2 has damage.
      if (!entry.damage[0] && entry.damage[1]) display_.printf(" %ld (C2)", static_cast<long>(entry.damage[1]));
      else if (entry.damage[1]) display_.printf(" %ld/%ld", static_cast<long>(entry.damage[0]), static_cast<long>(entry.damage[1]));
      else display_.printf(" %ld", static_cast<long>(entry.damage[0]));
      if (i == 2 && s.omittedSources) display_.printf(" +%u", s.omittedSources);
    }
    display_.drawFastHLine(6, 110, 238, GxEPD_BLACK);
    display_.setCursor(6, 113);
    display_.print(active ? "YOUR TURN" : "OTHER PLAYER'S TURN");
    display_.setCursor(194, 113);
    display_.printf("TURN %u", TurnHubProtocol::displayTurnNumber(s.state));
  } while (display_.nextPage());
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
    display_.setCursor(199, 17);
    display_.printf("S%u", static_cast<unsigned>(sigilId + 1));
    if (host) {
      display_.setCursor(224, 17);
      display_.print("HOST");
    }

    if (shared) {
      const uint8_t playerA = primaryPlayer < secondaryPlayer
          ? primaryPlayer
          : secondaryPlayer;
      const uint8_t playerB = primaryPlayer < secondaryPlayer
          ? secondaryPlayer
          : primaryPlayer;
      const bool focusA = focused && primaryPlayer == playerA;
      const bool focusB = focused && primaryPlayer == playerB;
      const bool namedA = seatNameA_[0] != '\0';
      const bool namedB = seatNameB_[0] != '\0';

      constexpr int16_t contentX = 6;
      constexpr int16_t contentY = 29;
      constexpr int16_t contentH = 68;
      constexpr int16_t gap = 4;
      constexpr int16_t footerY = 103;

      int16_t leftW = 117;
      int16_t rightW = 117;
      if (focusA) {
        leftW = 164;
        rightW = 70;
      } else if (focusB) {
        leftW = 70;
        rightW = 164;
      }

      const int16_t leftX = contentX;
      const int16_t rightX = contentX + leftW + gap;

      display_.drawRect(leftX, contentY, leftW, contentH, GxEPD_BLACK);
      display_.drawRect(rightX, contentY, rightW, contentH, GxEPD_BLACK);

      if (focusA) {
        display_.drawRect(leftX + 2, contentY + 2, leftW - 4, contentH - 4, GxEPD_BLACK);
      }
      if (focusB) {
        display_.drawRect(rightX + 2, contentY + 2, rightW - 4, contentH - 4, GxEPD_BLACK);
      }

      display_.setTextSize(1);
      display_.setCursor(leftX + 7, contentY + 12);
      display_.print("SEAT A");
      display_.setCursor(rightX + 7, contentY + 12);
      display_.print("SEAT B");

      // Names identify players; seat labels already identify the two controls.
      auto drawName = [&](int16_t x, int16_t width, const char *name, bool named) {
        const uint8_t size = width >= 110 ? 2 : 1;
        display_.setTextSize(size);
        display_.setCursor(x + 7, contentY + 32);
        printClipped(named ? name : "Guest", (width - 14) / (6 * size));
      };
      drawName(leftX, leftW, seatNameA_, namedA);
      drawName(rightX, rightW, seatNameB_, namedB);

      display_.drawFastHLine(6, 100, display_.width() - 12, GxEPD_BLACK);
      display_.setTextSize(1);
      display_.setCursor(7, footerY + 10);

      if (mode == TurnHubProtocol::DisplayMode::Running && active) {
        display_.printf("YOUR TURN  SEAT %c", focusA ? 'A' : 'B');
      } else if (mode == TurnHubProtocol::DisplayMode::Starting && starter) {
        display_.printf("GO FIRST  SEAT %c", focusA ? 'A' : 'B');
      } else if (mode == TurnHubProtocol::DisplayMode::Paused && attention) {
        display_.printf("ACTION NEEDED  SEAT %c", focusA ? 'A' : 'B');
      } else if (mode == TurnHubProtocol::DisplayMode::GameOver && winner) {
        display_.printf("WINNER  SEAT %c", focusA ? 'A' : 'B');
      } else if (mode == TurnHubProtocol::DisplayMode::Paused) {
        display_.print("GAME PAUSED");
      } else if (mode == TurnHubProtocol::DisplayMode::Lobby) {
        display_.print(host ? "SHARED SIGIL  HOST" : "SHARED SIGIL");
      } else {
        display_.print("WAITING");
      }

      if (turnNumber != 0) {
        display_.setCursor(210, footerY + 10);
        display_.printf("T%u", static_cast<unsigned>(turnNumber));
      }
    } else {
      display_.setTextSize(2);
      display_.setCursor(10, 64);

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
      display_.setCursor(10, 94);
      if (seatNameA_[0] != '\0') {
        printClipped(seatNameA_, 12);
        if (mode == TurnHubProtocol::DisplayMode::Lobby && host) display_.print(" HOST");
        if (mode == TurnHubProtocol::DisplayMode::Lobby && starter) display_.print(" START");
      } else {
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
      }

      if (turnNumber != 0) {
        display_.setCursor(205, 94);
        display_.printf("T%u", static_cast<unsigned>(turnNumber));
      }
    }
  } while (display_.nextPage());
}

}  // namespace TurnHubSigil
