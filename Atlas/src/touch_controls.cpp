// Atlas touchscreen: the TFT's screen model and its touch buttons. The touch
// adapter only builds Intents (IntentOrigin::AtlasHardware); the handlers
// decide. The exceptions change no table state: Cancel takes a presence code
// off the screen (front_panel.cpp), the Info, QR, Tests and Table buttons
// switch screens, and a test is started on the harness. Drawing lives in
// atlas_display.cpp.

#include "avatars.h"
#include "touch_controls.h"

#include <stdio.h>
#include <string.h>

#include "atlas_app.h"
#include "controller_profiles.h"
#include "firmware_version.h"
#include "harness_link.h"
#include "profile_store.h"
#include "sd_card.h"
#include "serial_log.h"
#include "wifi_password_store.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

namespace {

constexpr int16_t MARGIN = 8;
constexpr int16_t FULL_WIDTH = ATLAS_SCREEN_WIDTH - 2 * MARGIN;
constexpr char PORTAL_ORIGIN[] = "http://192.168.4.1";
// Profile names are looked up at most this often (the store may read NVS).
constexpr uint32_t NAME_REFRESH_MS = 3000;

// Press state for the touch in progress.
bool touchDown = false;
bool pressInside = false;
bool holdFired = false;
TouchAction pressedAction = TouchAction::None;
uint32_t pressStartedAtMs = 0;
uint32_t lastContactAtMs = 0;

char noticeText[sizeof(AtlasScreen::notice)] = {};
uint32_t noticeAtMs = 0;

// Which screen is up, and the QR code chosen on the QR screen.
ScreenKind openScreen = ScreenKind::Status;
TouchAction qrChoice = TouchAction::QrPortal;

struct NameCache {
  char profileId[9] = {};
  char name[SCREEN_NAME_LENGTH + 1] = {};
  uint32_t fetchedAtMs = 0;
  bool valid = false;
};
NameCache names[MAX_SCREEN_PLAYERS];

// --- Layout -------------------------------------------------------------------

struct ButtonSpec {
  TouchAction action;
  const char *label;
  uint32_t holdMs;
  uint8_t weight;
};

// Lays a row of buttons across the screen, widths in proportion to weight.
void addRow(AtlasScreen &screen, int16_t y, const ButtonSpec *specs, uint8_t count) {
  uint16_t totalWeight = 0;
  for (uint8_t i = 0; i < count; ++i) totalWeight += specs[i].weight;
  const int16_t usable = FULL_WIDTH - (count - 1) * MARGIN;
  int16_t x = MARGIN;
  for (uint8_t i = 0; i < count && screen.buttonCount < MAX_TOUCH_BUTTONS; ++i) {
    const int16_t w = i + 1 == count ? MARGIN + FULL_WIDTH - x
        : static_cast<int16_t>(usable * specs[i].weight / totalWeight);
    TouchButton &button = screen.buttons[screen.buttonCount++];
    button.action = specs[i].action;
    button.label = specs[i].label;
    button.x = x;
    button.y = y;
    button.w = w;
    button.h = BUTTON_ROW_H;
    button.holdMs = specs[i].holdMs;
    button.selected = specs[i].action == qrChoice && openScreen == ScreenKind::Qr;
    x += w + MARGIN;
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
    const ButtonSpec back[] = {{TouchAction::CloseTests, "Back", 0, 1}};
    addRow(screen, BUTTON_ROW_Y, back, 1);
    return;
  }
  if (harnessRunning(nowMs)) {
    const ButtonSpec stop[] = {{TouchAction::StopTest, "Stop test", 0, 1}};
    const ButtonSpec back[] = {{TouchAction::CloseTests, "Back", 0, 1}};
    addRow(screen, BUTTON_UPPER_ROW_Y, stop, 1);
    addRow(screen, BUTTON_ROW_Y, back, 1);
    return;
  }
  const ButtonSpec upper[] = {{TouchAction::RunRadioCheck, "Radio", 0, 1},
      {TouchAction::RunQuickGame, "2p game", 0, 1}, {TouchAction::RunFullGame, "4p game", 0, 1}};
  const ButtonSpec lower[] = {{TouchAction::RunRematchGame, "Rematch", 0, 1},
      {TouchAction::RunSoak, "Soak x5", 0, 1}, {TouchAction::CloseTests, "Back", 0, 1}};
  addRow(screen, BUTTON_UPPER_ROW_Y, upper, 3);
  addRow(screen, BUTTON_ROW_Y, lower, 3);
}

bool matchInProgress() {
  return hubState == HubState::Running || hubState == HubState::Paused;
}

// A code a phone asked for shows over whatever screen is open. The Table
// screen belongs to a match and closes when it ends.
ScreenKind activeScreen(uint32_t nowMs) {
  if (openScreen == ScreenKind::Table && !matchInProgress()) openScreen = ScreenKind::Status;
  return pendingPresenceCode(nowMs) != nullptr ? ScreenKind::Code : openScreen;
}

// In-game controls kept off the main row: Master pass (a stuck turn, running
// games only) above, End match and Back below. Both act only when held.
void layoutTable(AtlasScreen &screen) {
  if (hubState == HubState::Running) {
    const ButtonSpec upper[] = {{TouchAction::MasterPass, "Master pass", MASTER_PASS_HOLD_MS, 1}};
    addRow(screen, BUTTON_UPPER_ROW_Y, upper, 1);
  }
  const ButtonSpec lower[] = {{TouchAction::EndMatch, "End match", END_MATCH_HOLD_MS, 3},
      {TouchAction::CloseScreen, "Back", 0, 2}};
  addRow(screen, BUTTON_ROW_Y, lower, 2);
}

// The buttons for the open screen and the table state.
void layoutButtons(AtlasScreen &screen, uint32_t nowMs) {
  screen.buttonCount = 0;
  switch (activeScreen(nowMs)) {
    case ScreenKind::Code: {
      const ButtonSpec row[] = {{TouchAction::CancelCode, "Cancel", 0, 1}};
      addRow(screen, BUTTON_ROW_Y, row, 1);
      return;
    }
    case ScreenKind::Tests:
      layoutTests(screen, nowMs);
      return;
    case ScreenKind::Table:
      layoutTable(screen);
      return;
    case ScreenKind::Info: {
      const ButtonSpec row[] = {{TouchAction::OpenQr, "QR codes", 0, 3}, {TouchAction::CloseScreen, "Back", 0, 2}};
      addRow(screen, BUTTON_ROW_Y, row, 2);
      return;
    }
    case ScreenKind::Qr: {
      const ButtonSpec row[] = {{TouchAction::QrWifi, "Wi-Fi", 0, 1}, {TouchAction::QrPortal, "Portal", 0, 1},
          {TouchAction::QrSignIn, "Sign in", 0, 1}, {TouchAction::CloseScreen, "Back", 0, 1}};
      addRow(screen, BUTTON_ROW_Y, row, 4);
      return;
    }
    case ScreenKind::Status:
      break;
  }

  switch (hubState) {
    case HubState::Lobby: {
      ButtonSpec row[6];
      uint8_t n = 0;
      if (lobby.playerCount() >= 2) row[n++] = {TouchAction::StartGame, "Start", 0, 3};
      if (lobby.playerCount() >= 1) row[n++] = {TouchAction::ClearLobby, "Clear", LOBBY_CLEAR_HOLD_MS, 2};
      row[n++] = {TouchAction::Pair, "Pair", 0, 3};
      row[n++] = {TouchAction::OpenQr, "QR", 0, 2};
      if (harnessSigilId(nowMs) != INVALID_ID) row[n++] = {TouchAction::OpenTests, "Tests", 0, 2};
      row[n++] = {TouchAction::OpenInfo, "Info", 0, 2};
      // A full row (players joined and a harness connected): equal widths keep
      // every button at least a finger wide.
      if (n == 6) for (uint8_t i = 0; i < n; ++i) row[i].weight = 1;
      addRow(screen, BUTTON_ROW_Y, row, n);
      break;
    }
    // Players pass from their own seats; the master pass and End match sit
    // on the Table screen, out of the way during play.
    case HubState::Running: {
      const ButtonSpec row[] = {{TouchAction::Pause, "Pause", 0, 3}, {TouchAction::OpenTable, "Table", 0, 2}};
      addRow(screen, BUTTON_ROW_Y, row, 2);
      break;
    }
    case HubState::Paused: {
      const ButtonSpec row[] = {{TouchAction::Resume, "Resume", 0, 3}, {TouchAction::OpenTable, "Table", 0, 2}};
      addRow(screen, BUTTON_ROW_Y, row, 2);
      break;
    }
    case HubState::GameOver: {
      const ButtonSpec row[] = {{TouchAction::Rematch, "Rematch", 0, 3}, {TouchAction::ResetTable, "Reset", 0, 2},
          {TouchAction::OpenQr, "QR", 0, 2}, {TouchAction::OpenInfo, "Info", 0, 2}};
      addRow(screen, BUTTON_ROW_Y, row, 4);
      break;
    }
    case HubState::Starting: {
      const ButtonSpec row[] = {{TouchAction::CancelStart, "Cancel start", 0, 1}};
      addRow(screen, BUTTON_ROW_Y, row, 1);
      break;
    }
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
    case TouchAction::StartGame: return "START_GAME";
    case TouchAction::CancelStart: return "CANCEL_START";
    case TouchAction::Rematch: return "REMATCH";
    case TouchAction::ResetTable: return "RESET_TABLE";
    case TouchAction::ClearLobby: return "CLEAR_LOBBY";
    case TouchAction::OpenTable: return "OPEN_TABLE";
    case TouchAction::MasterPass: return "MASTER_PASS";
    case TouchAction::Pause: return "PAUSE";
    case TouchAction::Resume: return "RESUME";
    case TouchAction::EndMatch: return "END_MATCH";
    case TouchAction::CancelCode: return "CANCEL_CODE";
    case TouchAction::OpenInfo: return "OPEN_INFO";
    case TouchAction::OpenQr: return "OPEN_QR";
    case TouchAction::CloseScreen: return "CLOSE_SCREEN";
    case TouchAction::QrWifi: return "QR_WIFI";
    case TouchAction::QrPortal: return "QR_PORTAL";
    case TouchAction::QrSignIn: return "QR_SIGN_IN";
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

// The Intent a table-wide button asks for.
IntentType tableIntentType(TouchAction action) {
  switch (action) {
    case TouchAction::Pair: return IntentType::PairRequest;
    case TouchAction::EndMatch: return IntentType::EndMatch;
    case TouchAction::MasterPass: return IntentType::MasterPass;
    case TouchAction::StartGame: return IntentType::StartGame;
    case TouchAction::CancelStart: return IntentType::CancelStart;
    case TouchAction::Rematch: return IntentType::Rematch;
    case TouchAction::ResetTable:
    case TouchAction::ClearLobby: return IntentType::ResetGame;
    default: return IntentType::None;
  }
}

// What a hold button does, for "Keep holding for 5 s to ...".
const char *holdPurpose(TouchAction action) {
  if (action == TouchAction::MasterPass) return "pass this turn";
  if (action == TouchAction::ClearLobby) return "clear the lobby";
  return "end the match";
}

// Screen changes: no Intent, no table state, no notice.
bool navigate(TouchAction action) {
  switch (action) {
    case TouchAction::OpenInfo: openScreen = ScreenKind::Info; return true;
    case TouchAction::OpenQr: openScreen = ScreenKind::Qr; return true;
    case TouchAction::OpenTests: openScreen = ScreenKind::Tests; return true;
    case TouchAction::OpenTable: openScreen = ScreenKind::Table; return true;
    case TouchAction::CloseScreen:
    case TouchAction::CloseTests: openScreen = ScreenKind::Status; return true;
    case TouchAction::QrWifi:
    case TouchAction::QrPortal:
    case TouchAction::QrSignIn: qrChoice = action; return true;
    default: return false;
  }
}

// Adapter: turns one touch button into its Intent. Pause and Resume act for
// the active seat; the rest act for the table (origin only, no seat).
// Canceling a presence code changes no table state.
void dispatchTouchAction(uint32_t nowMs, TouchAction action) {
  if (navigate(action)) {
    serialLog.print("ATLAS|TOUCH|");
    serialLog.println(actionName(action));
    return;
  }
  IntentResult result;
  switch (action) {
    // Someone at the table did not ask for this code: take it off the screen.
    case TouchAction::CancelCode:
      cancelPresenceCode();
      result = IntentResult::accept("Code canceled");
      break;
    case TouchAction::Pair:
    case TouchAction::EndMatch:
    case TouchAction::MasterPass:
    case TouchAction::StartGame:
    case TouchAction::CancelStart:
    case TouchAction::Rematch:
    case TouchAction::ResetTable:
    case TouchAction::ClearLobby: {
      Intent intent;
      intent.type = tableIntentType(action);
      intent.actor.origin = IntentOrigin::AtlasHardware;
      result = intents.dispatch(intent);
      // After a master pass or End match, show the table what happened.
      if (result.accepted() && (action == TouchAction::MasterPass || action == TouchAction::EndMatch)) {
        openScreen = ScreenKind::Status;
      }
      break;
    }
    case TouchAction::Pause:
    case TouchAction::Resume: {
      const PlayerSeat *active = game.activePlayer();
      if (active == nullptr) {
        result = IntentResult::reject(IntentStatus::InvalidState, "There is no active player");
        break;
      }
      const IntentType type = action == TouchAction::Pause ? IntentType::Pause : IntentType::Resume;
      result = dispatchSeatIntent(type, IntentOrigin::AtlasHardware, *active);
      break;
    }
    // A test plays through the harness's own Sigils and Atlas's normal handlers.
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
    default:
      return;
  }
  serialLog.print("ATLAS|TOUCH|");
  serialLog.print(actionName(action));
  serialLog.print("|");
  serialLog.println(result.accepted() ? "ACCEPTED" : result.message);
  showNotice(nowMs, result.message);
}

// --- Content ------------------------------------------------------------------

void formatClock(char *out, size_t size, uint32_t ms) {
  const uint32_t seconds = ms / 1000;
  if (seconds >= 3600) {
    snprintf(out, size, "%lu:%02lu:%02lu", static_cast<unsigned long>(seconds / 3600),
        static_cast<unsigned long>(seconds / 60 % 60), static_cast<unsigned long>(seconds % 60));
  } else {
    snprintf(out, size, "%lu:%02lu", static_cast<unsigned long>(seconds / 60),
        static_cast<unsigned long>(seconds % 60));
  }
}

// A player's display name: the profile name, or "Player N" for a guest.
void playerName(const PlayerSeat &seat, const String &profileId, uint32_t nowMs, char *out) {
  NameCache &cache = names[(seat.playerNumber - 1) % MAX_SCREEN_PLAYERS];
  if (!cache.valid || strcmp(cache.profileId, profileId.c_str()) != 0 ||
      nowMs - cache.fetchedAtMs >= NAME_REFRESH_MS) {
    const String name = profileId.length() ? TurnHubProfiles::nameForProfile(profileId) : String();
    if (name.length()) {
      size_t n = 0;
      for (; n < name.length() && n < SCREEN_NAME_LENGTH; ++n) {
        cache.name[n] = name[n] >= 32 && name[n] <= 126 ? name[n] : '?';
      }
      cache.name[n] = '\0';
    } else {
      snprintf(cache.name, sizeof(cache.name), "Player %u", static_cast<unsigned>(seat.playerNumber));
    }
    snprintf(cache.profileId, sizeof(cache.profileId), "%s", profileId.c_str());
    cache.fetchedAtMs = nowMs;
    cache.valid = true;
  }
  memcpy(out, cache.name, sizeof(cache.name));
}

// Player chips: the game's seats once a game exists, otherwise the lobby's.
void addPlayers(AtlasScreen &screen, uint32_t nowMs) {
  PlayerSeat seats[MAX_SCREEN_PLAYERS];
  uint8_t count = 0;
  const bool inGame = game.hasPlayers() && hubState != HubState::Lobby;
  if (inGame) {
    for (uint8_t i = 0; i < game.playerCount() && count < MAX_SCREEN_PLAYERS; ++i) {
      const PlayerSeat *seat = game.playerAt(i);
      if (seat != nullptr) seats[count++] = *seat;
    }
  } else {
    count = lobby.buildPlayers(seats, MAX_SCREEN_PLAYERS);
  }
  PlayerSeat starter;
  const bool hasStarter = !inGame && lobby.selectedStarter(starter);
  const uint8_t waitingOn = game.hasWinClaim() ? game.nextWinConfirmationPlayerNumber() : 0;
  screen.showLife = inGame;
  for (uint8_t i = 0; i < count; ++i) {
    const PlayerSeat &seat = seats[i];
    ScreenPlayer &p = screen.players[screen.playerCount++];
    p.number = seat.playerNumber;
    const String profile = inGame ? String(seat.profileId)
        : TurnHubControllers::existingProfileForSeat(seat.controllerId, seat.slot);
    playerName(seat, profile, nowMs, p.name);
    // Only preset avatars reach Atlas's screen; custom ones stay signed-in only.
    const uint8_t avatar = TurnHubProfiles::avatarForProfile(
        profile.length() ? profile : TurnHubControllers::profileForSeat(seat.controllerId, seat.slot));
    if (TurnHubAvatars::validPresetAvatar(avatar)) p.avatar = avatar;
    if (inGame) {
      p.life = game.lifeTotal(seat.playerNumber);
      if (hubState != HubState::GameOver && seat.playerNumber == game.activePlayerNumber()) p.flags |= CHIP_ACTIVE;
      if (game.isEliminated(seat.playerNumber)) p.flags |= CHIP_OUT;
      if (hubState == HubState::GameOver && seat.playerNumber == game.winnerPlayerNumber()) p.flags |= CHIP_WINNER;
      if (seat.playerNumber == waitingOn) p.flags |= CHIP_WAITING;
    } else {
      if (hasStarter && seat.sameSeat(starter)) p.flags |= CHIP_STARTER;
    }
  }
}

const char *nameOfPlayer(const AtlasScreen &screen, uint8_t number) {
  for (uint8_t i = 0; i < screen.playerCount; ++i) {
    if (screen.players[i].number == number) return screen.players[i].name;
  }
  return "";
}

void formatTurnClock(AtlasScreen &screen, uint32_t nowMs) {
  if (hubState != HubState::Running && hubState != HubState::Paused) return;
  if (game.turnTimerMs() > 0) {
    const uint32_t left = game.turnRemainingMs(nowMs);
    screen.timerPermille = static_cast<int16_t>(static_cast<uint64_t>(left) * 1000 / game.turnTimerMs());
    const TurnHub::TurnTimerPhase phase = game.turnTimerPhase(nowMs);
    screen.timerWarning = phase == TurnHub::TurnTimerPhase::Warning ||
        phase == TurnHub::TurnTimerPhase::Expired;
    formatClock(screen.clock, sizeof(screen.clock), left);
  } else {
    formatClock(screen.clock, sizeof(screen.clock), game.currentTurnElapsedMs(nowMs));
  }
}

void formatStatus(AtlasScreen &screen, uint32_t nowMs) {
  addPlayers(screen, nowMs);
  formatTurnClock(screen, nowMs);
  const uint8_t players = game.hasPlayers() ? game.playerCount() : lobby.playerCount();
  switch (hubState) {
    case HubState::Lobby: {
      snprintf(screen.badge, sizeof(screen.badge), "LOBBY");
      snprintf(screen.title, sizeof(screen.title), "Lobby");
      const uint32_t pairingMs = pairingRemainingMs(nowMs);
      if (pairingMs > 0) {
        snprintf(screen.detail, sizeof(screen.detail), "Pairing open: %lu s left",
            static_cast<unsigned long>((pairingMs + 999) / 1000));
      } else {
        snprintf(screen.detail, sizeof(screen.detail), "%u %s, %u %s",
            static_cast<unsigned>(players), players == 1 ? "player" : "players",
            static_cast<unsigned>(screen.sigilsOnline), screen.sigilsOnline == 1 ? "Sigil" : "Sigils");
      }
      if (screen.playerCount == 0) {
        // An empty table: how to join, with the portal's QR code.
        snprintf(screen.lines[0], sizeof(screen.lines[0]), "Join from a Sigil menu,");
        snprintf(screen.lines[1], sizeof(screen.lines[1]), "or scan to open the");
        snprintf(screen.lines[2], sizeof(screen.lines[2]), "table portal.");
        screen.lineCount = 3;
        snprintf(screen.qr, sizeof(screen.qr), "%s/portal", PORTAL_ORIGIN);
      }
      break;
    }
    case HubState::Starting:
      snprintf(screen.badge, sizeof(screen.badge), "STARTING");
      snprintf(screen.title, sizeof(screen.title), "Starting");
      snprintf(screen.detail, sizeof(screen.detail), "The game begins shortly");
      break;
    case HubState::Running: {
      snprintf(screen.badge, sizeof(screen.badge), "PLAYING");
      const char *name = nameOfPlayer(screen, game.activePlayerNumber());
      snprintf(screen.title, sizeof(screen.title), "%s's turn", name);
      if (pendingPass.active) {
        // Count down the grace period so the table sees the pass is still
        // cancelable, and for how long (turntest, 2026-09-26).
        const uint32_t elapsed = millis() - pendingPass.requestedAtMs;
        const uint32_t leftMs = elapsed < PASS_GRACE_MS ? PASS_GRACE_MS - elapsed : 0;
        snprintf(screen.detail, sizeof(screen.detail), "Passing in %lus: that seat can cancel",
            static_cast<unsigned long>((leftMs + 999) / 1000));
      } else if (game.turnTimerMs() > 0) {
        snprintf(screen.detail, sizeof(screen.detail), "Turn time left %s", screen.clock);
      } else {
        snprintf(screen.detail, sizeof(screen.detail), "Turn time %s", screen.clock);
      }
      break;
    }
    case HubState::Paused:
      snprintf(screen.badge, sizeof(screen.badge), "PAUSED");
      snprintf(screen.title, sizeof(screen.title), "Paused");
      if (game.hasWinClaim()) {
        snprintf(screen.detail, sizeof(screen.detail), "Win claim: waiting on %s",
            nameOfPlayer(screen, game.nextWinConfirmationPlayerNumber()));
      } else {
        snprintf(screen.detail, sizeof(screen.detail), "%s's turn",
            nameOfPlayer(screen, game.activePlayerNumber()));
      }
      break;
    case HubState::GameOver:
      snprintf(screen.badge, sizeof(screen.badge), "GAME OVER");
      snprintf(screen.title, sizeof(screen.title), "Game over");
      if (game.endedInDraw()) {
        snprintf(screen.detail, sizeof(screen.detail), "The match ended in a draw");
      } else {
        snprintf(screen.detail, sizeof(screen.detail), "%s wins",
            nameOfPlayer(screen, game.winnerPlayerNumber()));
      }
      break;
  }
}

// Test screen text: the test's name and its progress, all in words (a pass or
// failure never relies on color).
void formatTests(AtlasScreen &screen, uint32_t nowMs) {
  using TurnHubProtocol::HarnessRunState;
  snprintf(screen.badge, sizeof(screen.badge), "TESTS");
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

// A presence code a signed-in phone asked for: the digits, and a QR code
// that opens the portal with the code filled in on the phone that scans it.
// Only that phone's profile can use it; Cancel takes it off the screen.
void formatCode(AtlasScreen &screen, uint32_t nowMs) {
  const PresenceRequest *request = pendingPresenceCode(nowMs);
  if (request == nullptr) return;
  snprintf(screen.badge, sizeof(screen.badge), "%s", request->setup ? "SETUP" : "VERIFY");
  snprintf(screen.title, sizeof(screen.title), "%s", request->setup ? "Set up this Atlas" : "Admin code");
  const String name = TurnHubProfiles::nameForProfile(String(request->profileId));
  const uint32_t leftS = (PRESENCE_CODE_MS - (nowMs - request->shownAtMs) + 999) / 1000;
  // Profile names are clipped to the screen's name width; the fallback is not.
  char who[SCREEN_NAME_LENGTH + 1];
  snprintf(who, sizeof(who), "%s", name.c_str());
  snprintf(screen.detail, sizeof(screen.detail), "For %s (%lu s)",
      name.length() ? who : "a signed-in phone", static_cast<unsigned long>(leftS));
  snprintf(screen.code, sizeof(screen.code), "%03lu %03lu",
      static_cast<unsigned long>(request->code / 1000), static_cast<unsigned long>(request->code % 1000));
  snprintf(screen.qr, sizeof(screen.qr), "%s/portal#code=%06lu", PORTAL_ORIGIN,
      static_cast<unsigned long>(request->code));
  snprintf(screen.qrCaption, sizeof(screen.qrCaption), "Enter it on that phone");
}

// The Table screen: whose turn a master pass would skip, in words.
void formatTable(AtlasScreen &screen, uint32_t nowMs) {
  snprintf(screen.badge, sizeof(screen.badge), "TABLE");
  snprintf(screen.title, sizeof(screen.title), "Table controls");
  if (hubState != HubState::Running) {
    snprintf(screen.detail, sizeof(screen.detail), "Resume to use Master pass");
    return;
  }
  if (game.hasWinClaim() || eliminationTargetPlayer != 0) {
    snprintf(screen.detail, sizeof(screen.detail), "Finish the table decision first");
    return;
  }
  const PlayerSeat *active = game.activePlayer();
  if (active == nullptr) return;
  char name[SCREEN_NAME_LENGTH + 1];
  playerName(*active, String(active->profileId), nowMs, name);
  snprintf(screen.detail, sizeof(screen.detail), "Stuck turn? Master pass skips %s", name);
}

void formatInfo(AtlasScreen &screen, uint32_t nowMs) {
  snprintf(screen.badge, sizeof(screen.badge), "INFO");
  snprintf(screen.title, sizeof(screen.title), "Table info");
  snprintf(screen.detail, sizeof(screen.detail), "TurnHub Atlas v%s", TurnHubFirmware::VERSION);
  char uptime[12];
  formatClock(uptime, sizeof(uptime), nowMs);
  snprintf(screen.lines[0], sizeof(screen.lines[0]), "Wi-Fi: %s", AtlasConfig::WIFI_SSID);
  snprintf(screen.lines[1], sizeof(screen.lines[1]), "Portal: 192.168.4.1");
  snprintf(screen.lines[2], sizeof(screen.lines[2]), "Sigils online: %u", static_cast<unsigned>(screen.sigilsOnline));
  snprintf(screen.lines[3], sizeof(screen.lines[3]), "SD card: %s", screen.sdMissing ? "NOT INSERTED" : "ready");
  snprintf(screen.lines[4], sizeof(screen.lines[4]), "Up %s", uptime);
  screen.lineCount = 5;
}

// The Wi-Fi code carries the password. The shipped default is public, so it
// shows freely; an admin-set password needs the admin unlock window.
void formatQr(AtlasScreen &screen, uint32_t nowMs) {
  snprintf(screen.badge, sizeof(screen.badge), "QR CODES");
  switch (qrChoice) {
    case TouchAction::QrWifi: {
      snprintf(screen.title, sizeof(screen.title), "Join the Wi-Fi");
      // The screen rebuilds every 50 ms; read NVS at most every 2 s.
      static String stored;
      static uint32_t readAtMs = 0;
      static bool read = false;
      if (!read || nowMs - readAtMs >= 2000) {
        stored = TurnHub::readStoredWifiPassword();
        readAtMs = nowMs;
        read = true;
      }
      const bool custom = TurnHub::validWifiPassword(stored) && stored != AtlasConfig::WIFI_DEFAULT_PASSWORD;
      if (custom && !anyPresenceActive(nowMs)) {
        snprintf(screen.detail, sizeof(screen.detail), "Network: %s", AtlasConfig::WIFI_SSID);
        snprintf(screen.lines[0], sizeof(screen.lines[0]), "This network has a private");
        snprintf(screen.lines[1], sizeof(screen.lines[1]), "password. An Admin must verify");
        snprintf(screen.lines[2], sizeof(screen.lines[2]), "at the table to show its code.");
        screen.lineCount = 3;
        return;
      }
      const String password = custom ? stored : String(AtlasConfig::WIFI_DEFAULT_PASSWORD);
      snprintf(screen.detail, sizeof(screen.detail), "Scan to join %s", AtlasConfig::WIFI_SSID);
      snprintf(screen.qr, sizeof(screen.qr), "WIFI:T:WPA;S:%s;P:%s;;", AtlasConfig::WIFI_SSID, password.c_str());
      snprintf(screen.qrCaption, sizeof(screen.qrCaption), "%s", AtlasConfig::WIFI_SSID);
      return;
    }
    case TouchAction::QrSignIn:
      snprintf(screen.title, sizeof(screen.title), "Sign in");
      snprintf(screen.detail, sizeof(screen.detail), "Scan to sign in to your profile");
      snprintf(screen.qr, sizeof(screen.qr), "%s/login", PORTAL_ORIGIN);
      snprintf(screen.qrCaption, sizeof(screen.qrCaption), "192.168.4.1/login");
      return;
    default:
      snprintf(screen.title, sizeof(screen.title), "Table portal");
      snprintf(screen.detail, sizeof(screen.detail), "Scan on the Atlas Wi-Fi");
      snprintf(screen.qr, sizeof(screen.qr), "%s/portal", PORTAL_ORIGIN);
      snprintf(screen.qrCaption, sizeof(screen.qrCaption), "192.168.4.1/portal");
      return;
  }
}

bool sameText(const char *a, const char *b) { return strcmp(a, b) == 0; }

}  // namespace

bool samePlayer(const ScreenPlayer &a, const ScreenPlayer &b) {
  return a.number == b.number && a.life == b.life && a.flags == b.flags && a.avatar == b.avatar &&
      sameText(a.name, b.name);
}

bool sameHeader(const AtlasScreen &a, const AtlasScreen &b) {
  return sameText(a.badge, b.badge) && a.sdMissing == b.sdMissing && a.sigilsOnline == b.sigilsOnline;
}

bool sameHero(const AtlasScreen &a, const AtlasScreen &b) {
  return a.kind == b.kind && sameText(a.title, b.title) && sameText(a.detail, b.detail) &&
      sameText(a.notice, b.notice);
}

bool sameTimer(const AtlasScreen &a, const AtlasScreen &b) {
  return a.timerPermille == b.timerPermille && a.timerWarning == b.timerWarning && sameText(a.clock, b.clock);
}

bool sameBody(const AtlasScreen &a, const AtlasScreen &b) {
  if (a.kind != b.kind || a.playerCount != b.playerCount || a.showLife != b.showLife ||
      a.lineCount != b.lineCount || !sameText(a.qr, b.qr) || !sameText(a.qrCaption, b.qrCaption) ||
      !sameText(a.code, b.code)) {
    return false;
  }
  for (uint8_t i = 0; i < a.lineCount; ++i) if (!sameText(a.lines[i], b.lines[i])) return false;
  for (uint8_t i = 0; i < a.playerCount; ++i) if (!samePlayer(a.players[i], b.players[i])) return false;
  return true;
}

bool sameButtons(const AtlasScreen &a, const AtlasScreen &b) {
  if (a.buttonCount != b.buttonCount || a.pressed != b.pressed ||
      a.holdSecondsLeft != b.holdSecondsLeft || a.holdPermille != b.holdPermille) {
    return false;
  }
  for (uint8_t i = 0; i < a.buttonCount; ++i) {
    const TouchButton &x = a.buttons[i];
    const TouchButton &y = b.buttons[i];
    if (x.action != y.action || x.x != y.x || x.y != y.y || x.w != y.w || x.h != y.h ||
        x.selected != y.selected || strcmp(x.label, y.label) != 0) {
      return false;
    }
  }
  return true;
}

bool sameScreen(const AtlasScreen &a, const AtlasScreen &b) {
  return sameHeader(a, b) && sameHero(a, b) && sameTimer(a, b) && sameBody(a, b) && sameButtons(a, b);
}

void buildAtlasScreen(uint32_t nowMs, AtlasScreen &screen) {
  screen = AtlasScreen();
  screen.kind = activeScreen(nowMs);
  screen.sdMissing = !sdCardReady();
  screen.sigilsOnline = sigilBus.activeCount(nowMs);
  switch (screen.kind) {
    case ScreenKind::Status: formatStatus(screen, nowMs); break;
    case ScreenKind::Tests: formatTests(screen, nowMs); break;
    case ScreenKind::Info: formatInfo(screen, nowMs); break;
    case ScreenKind::Qr: formatQr(screen, nowMs); break;
    case ScreenKind::Code: formatCode(screen, nowMs); break;
    case ScreenKind::Table: formatTable(screen, nowMs); break;
  }
  layoutButtons(screen, nowMs);
  if (noticeText[0] != '\0' && nowMs - noticeAtMs < TOUCH_NOTICE_MS) {
    snprintf(screen.notice, sizeof(screen.notice), "%s", noticeText);
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
      // Whole tenths, so the fill bar redraws about ten times a second.
      screen.holdPermille = static_cast<uint16_t>(
          (heldMs >= button.holdMs ? 1000 : heldMs * 1000 / button.holdMs) / 100 * 100);
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
          static_cast<unsigned long>(button->holdMs / 1000), holdPurpose(pressedAction));
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
  openScreen = ScreenKind::Status;
  qrChoice = TouchAction::QrPortal;
  for (auto &cache : names) cache = NameCache();
}

}  // namespace TurnHubAtlas
