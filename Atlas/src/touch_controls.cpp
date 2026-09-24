// Atlas touchscreen: the TFT's screen model and its touch buttons. The touch
// adapter only builds Intents (IntentOrigin::AtlasHardware, like the master
// button); the handlers decide. Drawing lives in atlas_display.cpp.

#include "touch_controls.h"

#include <stdio.h>
#include <string.h>

#include "atlas_app.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

namespace {

constexpr uint32_t MASTER_HOLD_WARNING_MS = 1000;
constexpr int16_t MARGIN = 8;
constexpr int16_t ROW_HEIGHT = 60;  // Well above the 44 px minimum target.
constexpr int16_t PRIMARY_ROW_Y = 104;
constexpr int16_t SECONDARY_ROW_Y = PRIMARY_ROW_Y + ROW_HEIGHT + MARGIN;
constexpr int16_t FULL_WIDTH = ATLAS_SCREEN_WIDTH - 2 * MARGIN;

// Press state for the touch in progress.
bool touchDown = false;
bool pressInside = false;
bool holdFired = false;
TouchAction pressedAction = TouchAction::None;
uint32_t pressStartedAtMs = 0;
uint32_t lastContactAtMs = 0;

char noticeText[sizeof(AtlasScreen::notice)] = {};
uint32_t noticeAtMs = 0;

void addButton(AtlasScreen &screen, TouchAction action, const char *label,
    int16_t x, int16_t y, int16_t w, bool hold = false) {
  if (screen.buttonCount >= MAX_TOUCH_BUTTONS) return;
  TouchButton &button = screen.buttons[screen.buttonCount++];
  button.action = action;
  button.label = label;
  button.x = x;
  button.y = y;
  button.w = w;
  button.h = ROW_HEIGHT;
  button.hold = hold;
}

// The buttons for the current table state.
void layoutButtons(AtlasScreen &screen) {
  screen.buttonCount = 0;
  switch (hubState) {
    case HubState::Lobby:
      addButton(screen, TouchAction::Pair, "Pair a Sigil", MARGIN, PRIMARY_ROW_Y, FULL_WIDTH);
      break;
    case HubState::Running: {
      const int16_t passWidth = 200;
      addButton(screen, TouchAction::Pass, "Pass", MARGIN, PRIMARY_ROW_Y, passWidth);
      addButton(screen, TouchAction::Pause, "Pause", MARGIN * 2 + passWidth, PRIMARY_ROW_Y,
          FULL_WIDTH - passWidth - MARGIN);
      addButton(screen, TouchAction::EndMatch, "Hold to end match (draw)", MARGIN,
          SECONDARY_ROW_Y, FULL_WIDTH, true);
      break;
    }
    case HubState::Paused:
      addButton(screen, TouchAction::Resume, "Resume", MARGIN, PRIMARY_ROW_Y, FULL_WIDTH);
      addButton(screen, TouchAction::EndMatch, "Hold to end match (draw)", MARGIN,
          SECONDARY_ROW_Y, FULL_WIDTH, true);
      break;
    case HubState::Starting:
    case HubState::GameOver:
      break;
  }
}

// The current layout's button for an action, or nullptr if it is gone.
const TouchButton *currentButton(TouchAction action, AtlasScreen &layout) {
  layoutButtons(layout);
  for (uint8_t i = 0; i < layout.buttonCount; ++i) {
    if (layout.buttons[i].action == action) return &layout.buttons[i];
  }
  return nullptr;
}

TouchAction buttonAt(int16_t x, int16_t y) {
  AtlasScreen layout;
  layoutButtons(layout);
  for (uint8_t i = 0; i < layout.buttonCount; ++i) {
    if (layout.buttons[i].contains(x, y)) return layout.buttons[i].action;
  }
  return TouchAction::None;
}

const char *actionName(TouchAction action) {
  switch (action) {
    case TouchAction::Pair: return "PAIR";
    case TouchAction::Pass: return "PASS";
    case TouchAction::Pause: return "PAUSE";
    case TouchAction::Resume: return "RESUME";
    case TouchAction::EndMatch: return "END_MATCH";
    case TouchAction::None: break;
  }
  return "NONE";
}

void showNotice(uint32_t nowMs, const char *text) {
  strncpy(noticeText, text, sizeof(noticeText) - 1);
  noticeText[sizeof(noticeText) - 1] = '\0';
  noticeAtMs = nowMs;
}

// Adapter: turns one touch button into its Intent. Pass, Pause and Resume act
// for the active seat, like the master button's PASS.
void dispatchTouchAction(uint32_t nowMs, TouchAction action) {
  IntentResult result;
  switch (action) {
    case TouchAction::Pair:
    case TouchAction::EndMatch: {
      Intent intent;
      intent.type = action == TouchAction::Pair ? IntentType::PairRequest : IntentType::EndMatch;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      result = intents.dispatch(intent);
      break;
    }
    case TouchAction::Pass:
    case TouchAction::Pause:
    case TouchAction::Resume: {
      const PlayerSeat *active = game.activePlayer();
      if (active == nullptr) {
        result = IntentResult::reject(IntentStatus::InvalidState, "There is no active player");
        break;
      }
      const IntentType type = action == TouchAction::Pass ? IntentType::Pass
          : action == TouchAction::Pause ? IntentType::Pause : IntentType::Resume;
      result = dispatchSeatIntent(type, IntentOrigin::AtlasHardware, *active);
      break;
    }
    case TouchAction::None:
      return;
  }
  serialLog.print("ATLAS|TOUCH|");
  serialLog.print(actionName(action));
  serialLog.print("|");
  serialLog.println(result.accepted() ? "ACCEPTED" : result.message);
  showNotice(nowMs, result.message);
}

void formatTitle(AtlasScreen &screen, uint32_t nowMs) {
  const uint8_t players = game.hasPlayers() ? game.playerCount() : lobby.playerCount();
  switch (hubState) {
    case HubState::Lobby: {
      snprintf(screen.title, sizeof(screen.title), "Lobby");
      const uint32_t pairingMs = pairingRemainingMs(nowMs);
      if (pairingMs > 0) {
        snprintf(screen.detail, sizeof(screen.detail), "Pairing open: %lu s left",
            static_cast<unsigned long>((pairingMs + 999) / 1000));
      } else {
        snprintf(screen.detail, sizeof(screen.detail), "%u %s, %u %s",
            static_cast<unsigned>(players), players == 1 ? "player" : "players",
            static_cast<unsigned>(sigilBus.activeCount(nowMs)),
            sigilBus.activeCount(nowMs) == 1 ? "Sigil" : "Sigils");
      }
      break;
    }
    case HubState::Starting:
      snprintf(screen.title, sizeof(screen.title), "Starting");
      snprintf(screen.detail, sizeof(screen.detail), "The game begins shortly");
      break;
    case HubState::Running: {
      snprintf(screen.title, sizeof(screen.title), "Player %u's turn",
          static_cast<unsigned>(game.activePlayerNumber()));
      const uint32_t remainingMs = game.turnRemainingMs(nowMs);
      if (pendingPass.active) {
        snprintf(screen.detail, sizeof(screen.detail), "Pass pending: tap Pass to undo");
      } else if (game.turnTimerMs() > 0) {
        const uint32_t seconds = (remainingMs + 999) / 1000;
        snprintf(screen.detail, sizeof(screen.detail), "Turn time left %lu:%02lu",
            static_cast<unsigned long>(seconds / 60), static_cast<unsigned long>(seconds % 60));
      }
      break;
    }
    case HubState::Paused:
      snprintf(screen.title, sizeof(screen.title), "Paused");
      if (game.hasWinClaim()) {
        snprintf(screen.detail, sizeof(screen.detail), "A win claim is waiting");
      } else {
        snprintf(screen.detail, sizeof(screen.detail), "Player %u's turn",
            static_cast<unsigned>(game.activePlayerNumber()));
      }
      break;
    case HubState::GameOver:
      snprintf(screen.title, sizeof(screen.title), "Game over");
      if (game.endedInDraw()) {
        snprintf(screen.detail, sizeof(screen.detail), "The match ended in a draw");
      } else {
        snprintf(screen.detail, sizeof(screen.detail), "Player %u wins",
            static_cast<unsigned>(game.winnerPlayerNumber()));
      }
      break;
  }
}

}  // namespace

bool sameButtons(const AtlasScreen &a, const AtlasScreen &b) {
  if (a.buttonCount != b.buttonCount || a.pressed != b.pressed ||
      a.holdSecondsLeft != b.holdSecondsLeft) {
    return false;
  }
  for (uint8_t i = 0; i < a.buttonCount; ++i) {
    const TouchButton &x = a.buttons[i];
    const TouchButton &y = b.buttons[i];
    if (x.action != y.action || x.x != y.x || x.y != y.y || x.w != y.w || x.h != y.h ||
        strcmp(x.label, y.label) != 0) {
      return false;
    }
  }
  return true;
}

bool sameScreen(const AtlasScreen &a, const AtlasScreen &b) {
  return strcmp(a.title, b.title) == 0 && strcmp(a.detail, b.detail) == 0 &&
      strcmp(a.notice, b.notice) == 0 && sameButtons(a, b);
}

void buildAtlasScreen(uint32_t nowMs, AtlasScreen &screen) {
  screen = AtlasScreen();
  formatTitle(screen, nowMs);
  layoutButtons(screen);
  if (noticeText[0] != '\0' && nowMs - noticeAtMs < TOUCH_NOTICE_MS) {
    snprintf(screen.notice, sizeof(screen.notice), "%s", noticeText);
  }
  // A master (BOOT) hold counting toward ending the match; shown from
  // MASTER_HOLD_WARNING_MS in so an ordinary PASS press does not flash it.
  const uint32_t masterLeftMs = masterEndMatchRemainingMs(nowMs);
  if (masterLeftMs > 0 && masterLeftMs <= MASTER_END_MATCH_HOLD_MS - MASTER_HOLD_WARNING_MS) {
    snprintf(screen.notice, sizeof(screen.notice), "Keep holding BOOT to end match: %lu s",
        static_cast<unsigned long>((masterLeftMs + 999) / 1000));
  }
  if (!touchDown || !pressInside) return;
  for (uint8_t i = 0; i < screen.buttonCount; ++i) {
    const TouchButton &button = screen.buttons[i];
    if (button.action != pressedAction) continue;
    screen.pressed = pressedAction;
    if (button.hold && !holdFired) {
      const uint32_t heldMs = nowMs - pressStartedAtMs;
      const uint32_t leftMs = heldMs < MASTER_END_MATCH_HOLD_MS ? MASTER_END_MATCH_HOLD_MS - heldMs : 0;
      screen.holdSecondsLeft = static_cast<uint8_t>((leftMs + 999) / 1000);
    }
  }
}

void updateTouchControls(uint32_t nowMs, bool touched, int16_t x, int16_t y) {
  AtlasScreen layout;
  if (touched) {
    lastContactAtMs = nowMs;
    if (!touchDown) {
      touchDown = true;
      holdFired = false;
      pressStartedAtMs = nowMs;
      pressedAction = buttonAt(x, y);
    }
    const TouchButton *button = currentButton(pressedAction, layout);
    pressInside = button != nullptr && button->contains(x, y);
    if (button != nullptr && button->hold && pressInside && !holdFired &&
        nowMs - pressStartedAtMs >= MASTER_END_MATCH_HOLD_MS) {
      holdFired = true;
      dispatchTouchAction(nowMs, pressedAction);
    }
    return;
  }

  if (!touchDown || nowMs - lastContactAtMs < TOUCH_RELEASE_MS) return;
  touchDown = false;
  const TouchButton *button = currentButton(pressedAction, layout);
  if (button != nullptr && pressInside && !holdFired) {
    if (button->hold) {
      char hint[sizeof(noticeText)];
      snprintf(hint, sizeof(hint), "Keep holding for %lu s to end the match",
          static_cast<unsigned long>(MASTER_END_MATCH_HOLD_MS / 1000));
      showNotice(nowMs, hint);
    } else {
      dispatchTouchAction(nowMs, pressedAction);
    }
  }
  pressedAction = TouchAction::None;
  pressInside = false;
}

bool touchCalibrationAllowed() {
  return hubState == HubState::Lobby;
}

void resetTouchControls() {
  touchDown = false;
  pressInside = false;
  holdFired = false;
  pressedAction = TouchAction::None;
  noticeText[0] = '\0';
}

}  // namespace TurnHubAtlas
