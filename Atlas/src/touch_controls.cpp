// Atlas touchscreen: the TFT's screen model and its touch buttons. The touch
// adapter only builds Intents (IntentOrigin::AtlasHardware); the handlers
// decide. The exception is "Unlock admin", which opens the physical-presence
// window in front_panel.cpp. Drawing lives in atlas_display.cpp.

#include "touch_controls.h"

#include <stdio.h>
#include <string.h>

#include "atlas_app.h"
#include "harness_link.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

namespace {

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

// The test harness screen is showing (opened from the lobby's Tests button).
bool testsOpen = false;

void addButton(AtlasScreen &screen, TouchAction action, const char *label,
    int16_t x, int16_t y, int16_t w, uint32_t holdMs = 0) {
  if (screen.buttonCount >= MAX_TOUCH_BUTTONS) return;
  TouchButton &button = screen.buttons[screen.buttonCount++];
  button.action = action;
  button.label = label;
  button.x = x;
  button.y = y;
  button.w = w;
  button.h = ROW_HEIGHT;
  button.holdMs = holdMs;
}

// Between games: unlock admin (a hold), or lock it again early (a tap).
void addAdminButton(AtlasScreen &screen, int16_t y, uint32_t nowMs) {
  if (adminUnlockRemainingMs(nowMs) > 0) {
    addButton(screen, TouchAction::LockAdmin, "Admin unlocked: tap to lock", MARGIN, y, FULL_WIDTH);
  } else {
    addButton(screen, TouchAction::UnlockAdmin, "Hold to unlock admin", MARGIN, y, FULL_WIDTH,
        ADMIN_UNLOCK_HOLD_MS);
  }
}

bool harnessRunning(uint32_t nowMs) {
  TurnHubProtocol::HarnessReportFields report;
  return harnessReport(nowMs, report) && report.state == TurnHubProtocol::HarnessRunState::Running;
}

// The test harness screen: its premade tests in two rows of three, or Stop
// while one runs. It stays up through the game the harness plays until Back.
void layoutTests(AtlasScreen &screen, uint32_t nowMs) {
  if (harnessSigilId(nowMs) == INVALID_ID) {
    addButton(screen, TouchAction::CloseTests, "Back", MARGIN, SECONDARY_ROW_Y, FULL_WIDTH);
    return;
  }
  if (harnessRunning(nowMs)) {
    addButton(screen, TouchAction::StopTest, "Stop test", MARGIN, PRIMARY_ROW_Y, FULL_WIDTH);
    addButton(screen, TouchAction::CloseTests, "Back", MARGIN, SECONDARY_ROW_Y, FULL_WIDTH);
    return;
  }
  constexpr int16_t THIRD = (FULL_WIDTH - 2 * MARGIN) / 3;
  const int16_t x2 = MARGIN + THIRD + MARGIN;
  const int16_t x3 = x2 + THIRD + MARGIN;
  addButton(screen, TouchAction::RunRadioCheck, "Radio", MARGIN, PRIMARY_ROW_Y, THIRD);
  addButton(screen, TouchAction::RunQuickGame, "2p game", x2, PRIMARY_ROW_Y, THIRD);
  addButton(screen, TouchAction::RunFullGame, "4p game", x3, PRIMARY_ROW_Y, THIRD);
  addButton(screen, TouchAction::RunRematchGame, "Rematch", MARGIN, SECONDARY_ROW_Y, THIRD);
  addButton(screen, TouchAction::RunSoak, "Soak x5", x2, SECONDARY_ROW_Y, THIRD);
  addButton(screen, TouchAction::CloseTests, "Back", x3, SECONDARY_ROW_Y, THIRD);
}

// The buttons for the current table state.
void layoutButtons(AtlasScreen &screen, uint32_t nowMs) {
  screen.buttonCount = 0;
  if (testsOpen) {
    layoutTests(screen, nowMs);
    return;
  }
  switch (hubState) {
    case HubState::Lobby:
      if (harnessSigilId(nowMs) != INVALID_ID) {
        const int16_t pairWidth = 200;
        addButton(screen, TouchAction::Pair, "Pair a Sigil", MARGIN, PRIMARY_ROW_Y, pairWidth);
        addButton(screen, TouchAction::OpenTests, "Tests", MARGIN * 2 + pairWidth, PRIMARY_ROW_Y,
            FULL_WIDTH - pairWidth - MARGIN);
      } else {
        addButton(screen, TouchAction::Pair, "Pair a Sigil", MARGIN, PRIMARY_ROW_Y, FULL_WIDTH);
      }
      addAdminButton(screen, SECONDARY_ROW_Y, nowMs);
      break;
    case HubState::Running: {
      const int16_t passWidth = 200;
      addButton(screen, TouchAction::Pass, "Pass", MARGIN, PRIMARY_ROW_Y, passWidth);
      addButton(screen, TouchAction::Pause, "Pause", MARGIN * 2 + passWidth, PRIMARY_ROW_Y,
          FULL_WIDTH - passWidth - MARGIN);
      addButton(screen, TouchAction::EndMatch, "Hold to end match (draw)", MARGIN,
          SECONDARY_ROW_Y, FULL_WIDTH, END_MATCH_HOLD_MS);
      break;
    }
    case HubState::Paused:
      addButton(screen, TouchAction::Resume, "Resume", MARGIN, PRIMARY_ROW_Y, FULL_WIDTH);
      addButton(screen, TouchAction::EndMatch, "Hold to end match (draw)", MARGIN,
          SECONDARY_ROW_Y, FULL_WIDTH, END_MATCH_HOLD_MS);
      break;
    case HubState::GameOver:
      addAdminButton(screen, PRIMARY_ROW_Y, nowMs);
      break;
    case HubState::Starting:
      break;
  }
}

// The current layout's button for an action, or nullptr if it is gone.
const TouchButton *currentButton(TouchAction action, AtlasScreen &layout, uint32_t nowMs) {
  layoutButtons(layout, nowMs);
  for (uint8_t i = 0; i < layout.buttonCount; ++i) {
    if (layout.buttons[i].action == action) return &layout.buttons[i];
  }
  return nullptr;
}

TouchAction buttonAt(int16_t x, int16_t y, uint32_t nowMs) {
  AtlasScreen layout;
  layoutButtons(layout, nowMs);
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
    case TouchAction::UnlockAdmin: return "UNLOCK_ADMIN";
    case TouchAction::LockAdmin: return "LOCK_ADMIN";
    case TouchAction::OpenTests: return "OPEN_TESTS";
    case TouchAction::CloseTests: return "CLOSE_TESTS";
    case TouchAction::StopTest: return "STOP_TEST";
    case TouchAction::RunRadioCheck: return "RUN_RADIO_CHECK";
    case TouchAction::RunQuickGame: return "RUN_QUICK_GAME";
    case TouchAction::RunFullGame: return "RUN_FULL_GAME";
    case TouchAction::RunRematchGame: return "RUN_REMATCH_GAME";
    case TouchAction::RunSoak: return "RUN_SOAK";
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
// for the active seat. Unlock/Lock admin only open or close the presence
// window; they change no table state.
void dispatchTouchAction(uint32_t nowMs, TouchAction action) {
  IntentResult result;
  switch (action) {
    case TouchAction::UnlockAdmin:
      openAdminUnlock(nowMs);
      result = IntentResult::accept("Admin unlocked for 60 s");
      break;
    case TouchAction::LockAdmin:
      closeAdminUnlock();
      result = IntentResult::accept("Admin locked");
      break;
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
    // The test harness screen changes no table state. A test plays through
    // the harness's own Sigils and Atlas's normal handlers.
    case TouchAction::OpenTests:
      testsOpen = true;
      result = IntentResult::accept("Pick a test for the harness to run");
      break;
    case TouchAction::CloseTests:
      testsOpen = false;
      result = IntentResult::accept("Test screen closed");
      break;
    case TouchAction::StopTest:
      result = requestHarnessStop(nowMs) ? IntentResult::accept("Stopping the test")
          : IntentResult::reject(IntentStatus::InvalidState, "The test harness is not connected");
      break;
    case TouchAction::RunRadioCheck:
    case TouchAction::RunQuickGame:
    case TouchAction::RunFullGame:
    case TouchAction::RunRematchGame:
    case TouchAction::RunSoak: {
      const TurnHubProtocol::HarnessTest test = static_cast<TurnHubProtocol::HarnessTest>(
          static_cast<uint8_t>(action) - static_cast<uint8_t>(TouchAction::RunRadioCheck));
      result = requestHarnessTest(test, nowMs) ? IntentResult::accept("Test sent to the harness")
          : IntentResult::reject(IntentStatus::InvalidState, "The test harness is not connected");
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

// Test screen text: the test's name and its progress, all in words (a pass or
// failure never relies on color).
void formatTests(AtlasScreen &screen, uint32_t nowMs) {
  using TurnHubProtocol::HarnessRunState;
  snprintf(screen.title, sizeof(screen.title), "Test harness");
  if (harnessSigilId(nowMs) == INVALID_ID) {
    snprintf(screen.detail, sizeof(screen.detail), "Harness offline");
    return;
  }
  TurnHubProtocol::HarnessReportFields r;
  if (!harnessReport(nowMs, r) || r.state == HarnessRunState::Idle) {
    snprintf(screen.detail, sizeof(screen.detail), "Ready: pick a test");
    return;
  }
  snprintf(screen.title, sizeof(screen.title), "%s", TurnHubProtocol::harnessTestName(r.test));
  const char *step = TurnHubProtocol::harnessStepName(r.step);
  switch (r.state) {
    case HarnessRunState::Running:
      snprintf(screen.detail, sizeof(screen.detail), "%s, %u ok", step, static_cast<unsigned>(r.passed));
      break;
    case HarnessRunState::Passed:
      snprintf(screen.detail, sizeof(screen.detail), "PASSED: %u steps", static_cast<unsigned>(r.passed));
      break;
    case HarnessRunState::Failed:
      snprintf(screen.detail, sizeof(screen.detail), "FAILED at %s", step);
      break;
    default:
      snprintf(screen.detail, sizeof(screen.detail), "Stopped at %s", step);
      break;
  }
}

void formatTitle(AtlasScreen &screen, uint32_t nowMs) {
  if (testsOpen) {
    formatTests(screen, nowMs);
    return;
  }
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
  layoutButtons(screen, nowMs);
  if (noticeText[0] != '\0' && nowMs - noticeAtMs < TOUCH_NOTICE_MS) {
    snprintf(screen.notice, sizeof(screen.notice), "%s", noticeText);
  }
  // The admin unlock countdown, whenever no action message is showing.
  const uint32_t unlockMs = adminUnlockRemainingMs(nowMs);
  if (screen.notice[0] == '\0' && unlockMs > 0) {
    snprintf(screen.notice, sizeof(screen.notice), "Admin unlocked: %lu s left",
        static_cast<unsigned long>((unlockMs + 999) / 1000));
  }
  if (!touchDown || !pressInside) return;
  for (uint8_t i = 0; i < screen.buttonCount; ++i) {
    const TouchButton &button = screen.buttons[i];
    if (button.action != pressedAction) continue;
    screen.pressed = pressedAction;
    if (button.hold() && !holdFired) {
      const uint32_t heldMs = nowMs - pressStartedAtMs;
      const uint32_t leftMs = heldMs < button.holdMs ? button.holdMs - heldMs : 0;
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
      pressedAction = buttonAt(x, y, nowMs);
    }
    const TouchButton *button = currentButton(pressedAction, layout, nowMs);
    pressInside = button != nullptr && button->contains(x, y, TOUCH_SLOP_PX);
    if (button != nullptr && button->hold() && pressInside && !holdFired &&
        nowMs - pressStartedAtMs >= button->holdMs) {
      holdFired = true;
      dispatchTouchAction(nowMs, pressedAction);
    }
    return;
  }

  if (!touchDown || nowMs - lastContactAtMs < TOUCH_RELEASE_MS) return;
  touchDown = false;
  const TouchButton *button = currentButton(pressedAction, layout, nowMs);
  if (button != nullptr && pressInside && !holdFired) {
    if (button->hold()) {
      char hint[sizeof(noticeText)];
      snprintf(hint, sizeof(hint), "Keep holding for %lu s to %s",
          static_cast<unsigned long>(button->holdMs / 1000),
          button->action == TouchAction::EndMatch ? "end the match" : "unlock admin");
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
  testsOpen = false;
}

}  // namespace TurnHubAtlas
