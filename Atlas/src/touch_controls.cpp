// Atlas touchscreen: the TFT's screen model and its touch buttons. The touch
// adapter only builds Intents (IntentOrigin::AtlasHardware); the handlers
// decide. The exceptions change no table state: Cancel takes a presence code
// off the screen (front_panel.cpp), the Menu, Info, QR, Tests and Table
// buttons switch screens, and a test is started on the harness. Drawing lives in
// atlas_art.cpp; the panel and touch reads in atlas_display.cpp.

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
#include "sigil_update_service.h"
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
// The Player screen: whose it is (by controller and slot, since lobby player
// numbers change with the turn order), and whether Concede is waiting for
// its confirmation. pressedSeat is the chip under the finger.
PlayerSeat shownSeat;
bool concedeArmed = false;
PlayerSeat pressedSeat;
// Skip for now on the setup Welcome screen: hidden until the next start-up,
// or until Setup under Menu. Presentation only; the stage stays Atlas's.
bool setupSkipped = false;

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

// The Menu belongs between games (the lobby and a finished game).
bool menuAvailable() {
  return hubState == HubState::Lobby || hubState == HubState::GameOver;
}

// The first Sigil waiting for its pairing code to be checked, or INVALID_ID.
// One at a time on this screen; the next shows once it is decided.
uint8_t waitingPairSlot() {
  if (hubState != HubState::Lobby) return INVALID_ID;
  for (uint8_t slot = 0; slot < MAX_PHYSICAL_SIGILS; ++slot) {
    if (sigilBus.pendingPairing(slot) != nullptr) return slot;
  }
  return INVALID_ID;
}

// The chips on the status screen, in the order they are drawn: the lobby's
// seats, or the match's players while one is in progress.
uint8_t chipSeats(PlayerSeat *out) {
  if (hubState == HubState::Lobby) return lobby.buildPlayers(out, MAX_SCREEN_PLAYERS);
  if (!matchInProgress()) return 0;
  const uint8_t count = game.playerCount() < MAX_SCREEN_PLAYERS ? game.playerCount() : MAX_SCREEN_PLAYERS;
  for (uint8_t i = 0; i < count; ++i) out[i] = *game.playerAt(i);
  return count;
}

bool sameSeat(const PlayerSeat &a, const PlayerSeat &b) {
  return a.controllerId == b.controllerId && a.slot == b.slot;
}

// The shown seat as it is now (its current player number), if it still
// plays: at the table in the lobby, or not out in a match.
bool currentShownSeat(PlayerSeat &seat) {
  if (hubState != HubState::Lobby && !matchInProgress()) return false;
  PlayerSeat seats[MAX_SCREEN_PLAYERS];
  const uint8_t count = chipSeats(seats);
  for (uint8_t i = 0; i < count; ++i) {
    if (!sameSeat(seats[i], shownSeat)) continue;
    if (hubState != HubState::Lobby && game.isEliminated(seats[i].playerNumber)) return false;
    seat = seats[i];
    return true;
  }
  return false;
}

// First-run setup takes the lobby's status screen until it is complete
// (Welcome can be skipped for now; "You're all set" waits for Done).
bool setupScreenDue() {
  if (hubState != HubState::Lobby || setupStage == TurnHub::SetupStage::Complete) return false;
  return !(setupStage == TurnHub::SetupStage::Welcome && setupSkipped);
}

// A code a phone asked for shows over whatever screen is open, then a
// pairing code waiting to be checked. The Table screen belongs to a match and
// closes when it ends; the Menu closes when a game starts.
ScreenKind activeScreen(uint32_t nowMs) {
  if (openScreen == ScreenKind::Table && !matchInProgress()) openScreen = ScreenKind::Status;
  // A player's screen closes when the lobby or match it belongs to ends, or
  // once that player is out.
  PlayerSeat shown;
  if (openScreen == ScreenKind::Player && !currentShownSeat(shown)) {
    openScreen = ScreenKind::Status;
    concedeArmed = false;
  }
  if ((openScreen == ScreenKind::Menu || openScreen == ScreenKind::Device) && !menuAvailable()) {
    openScreen = ScreenKind::Status;
  }
  if (pendingPresenceCode(nowMs) != nullptr) return ScreenKind::Code;
  if (waitingPairSlot() != INVALID_ID) return ScreenKind::PairCode;
  if (openScreen == ScreenKind::Status && setupScreenDue()) return ScreenKind::Setup;
  return openScreen;
}

// Between games: Pair (lobby only) and QR codes above; Tests (while a
// harness is connected), Info, Device and Back below.
void layoutMenu(AtlasScreen &screen, uint32_t nowMs) {
  ButtonSpec upper[3];
  uint8_t n = 0;
  if (hubState == HubState::Lobby && setupStage != TurnHub::SetupStage::Complete) {
    upper[n++] = {TouchAction::OpenSetup, "Setup", 0, 1};
  }
  if (hubState == HubState::Lobby) upper[n++] = {TouchAction::Pair, "Pair a Sigil", 0, 1};
  upper[n++] = {TouchAction::OpenQr, "QR codes", 0, 1};
  addRow(screen, BUTTON_UPPER_ROW_Y, upper, n);
  ButtonSpec lower[4];
  n = 0;
  if (harnessSigilId(nowMs) != INVALID_ID) lower[n++] = {TouchAction::OpenTests, "Tests", 0, 1};
  lower[n++] = {TouchAction::OpenInfo, "Info", 0, 1};
  lower[n++] = {TouchAction::OpenDevice, "Device", 0, 1};
  lower[n++] = {TouchAction::CloseScreen, "Back", 0, 1};
  addRow(screen, BUTTON_ROW_Y, lower, n);
}

// Menu > Device: Unpair Sigils (lobby only, as the handler requires) and
// Factory reset above, Sleep and Back below. Unpair and Factory reset act
// only when held for the BOOT button's times, which they stand in for; Sleep
// is a tap (nothing saved is lost, and a touch wakes Atlas).
void layoutDevice(AtlasScreen &screen) {
  ButtonSpec upper[2];
  uint8_t n = 0;
  if (hubState == HubState::Lobby) upper[n++] = {TouchAction::UnpairSigils, "Unpair Sigils", DEVICE_UNPAIR_HOLD_MS, 1};
  upper[n++] = {TouchAction::FactoryResetAtlas, "Factory reset", DEVICE_RESET_HOLD_MS, 1};
  addRow(screen, BUTTON_UPPER_ROW_Y, upper, n);
  const ButtonSpec lower[] = {{TouchAction::SleepAtlas, "Sleep", 0, 1}, {TouchAction::CloseScreen, "Back", 0, 1}};
  addRow(screen, BUTTON_ROW_Y, lower, 2);
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

// One player's screen (playtest 2026-09-29, item 9): life steps above,
// Concede and Back below. Concede never acts on one touch or on a hold
// alone: it asks again with Concede / Cancel.
void layoutPlayer(AtlasScreen &screen) {
  // Lobby: turn order (item 11). Earlier and Later, then Back.
  if (hubState == HubState::Lobby) {
    const ButtonSpec upper[] = {{TouchAction::MoveEarlier, "Earlier", 0, 1},
        {TouchAction::MoveLater, "Later", 0, 1},
        {TouchAction::RemoveSeat, "Remove", LOBBY_REMOVE_HOLD_MS, 1}};
    addRow(screen, BUTTON_UPPER_ROW_Y, upper, 3);
    PlayerSeat seat;
    if (currentShownSeat(seat) && lobby.hasSecondary(seat.controllerId)) {
      const ButtonSpec lower[] = {{TouchAction::SeatBLeft, "B left", 0, 1},
          {TouchAction::SeatBRight, "B right", 0, 1}, {TouchAction::CloseScreen, "Back", 0, 1}};
      addRow(screen, BUTTON_ROW_Y, lower, 3);
    } else {
      const ButtonSpec lower[] = {{TouchAction::CloseScreen, "Back", 0, 1}};
      addRow(screen, BUTTON_ROW_Y, lower, 1);
    }
    return;
  }
  if (concedeArmed) {
    const ButtonSpec lower[] = {{TouchAction::ConfirmConcede, "Concede", 0, 3},
        {TouchAction::CancelConcede, "Cancel", 0, 2}};
    addRow(screen, BUTTON_ROW_Y, lower, 2);
    return;
  }
  // Yu-Gi-Oh! life moves in hundreds, as on the portal's life cards.
  const bool hundreds = game.settings().profile == TurnHub::GameProfile::Yugioh;
  const ButtonSpec upper[] = {{TouchAction::LifeMinus5, hundreds ? "-1000" : "-5", 0, 1},
      {TouchAction::LifeMinus1, hundreds ? "-100" : "-1", 0, 1},
      {TouchAction::LifePlus1, hundreds ? "+100" : "+1", 0, 1},
      {TouchAction::LifePlus5, hundreds ? "+1000" : "+5", 0, 1}};
  addRow(screen, BUTTON_UPPER_ROW_Y, upper, 4);
  const ButtonSpec lower[] = {{TouchAction::Concede, "Concede", 0, 1}, {TouchAction::CloseScreen, "Back", 0, 1}};
  addRow(screen, BUTTON_ROW_Y, lower, 2);
}

// The chip of `seat` on the status screen, as a touch target (chips are
// drawn as chips, not buttons). Eliminated players' chips open nothing.
bool chipButton(const PlayerSeat &seat, uint32_t nowMs, TouchButton &out) {
  if (seat.controllerId == INVALID_ID || activeScreen(nowMs) != ScreenKind::Status) return false;
  PlayerSeat seats[MAX_SCREEN_PLAYERS];
  const uint8_t count = chipSeats(seats);
  for (uint8_t i = 0; i < count; ++i) {
    if (!sameSeat(seats[i], seat)) continue;
    if (hubState != HubState::Lobby && game.isEliminated(seats[i].playerNumber)) return false;
    out = TouchButton();
    out.action = TouchAction::OpenPlayer;
    screenChipCell(i, count, out.x, out.y, out.w, out.h);
    return true;
  }
  return false;
}

// The seat whose chip is at (x, y), if any.
bool chipAt(int16_t x, int16_t y, uint32_t nowMs, PlayerSeat &found) {
  PlayerSeat seats[MAX_SCREEN_PLAYERS];
  const uint8_t count = chipSeats(seats);
  for (uint8_t i = 0; i < count; ++i) {
    TouchButton chip;
    if (chipButton(seats[i], nowMs, chip) && chip.contains(x, y)) {
      found = seats[i];
      return true;
    }
  }
  return false;
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
    case ScreenKind::PairCode: {
      const ButtonSpec row[] = {{TouchAction::PairConfirm, "Codes match", 0, 3},
          {TouchAction::PairReject, "Reject", 0, 2}};
      addRow(screen, BUTTON_ROW_Y, row, 2);
      return;
    }
    case ScreenKind::Tests:
      layoutTests(screen, nowMs);
      return;
    case ScreenKind::Table:
      layoutTable(screen);
      return;
    case ScreenKind::Menu:
      layoutMenu(screen, nowMs);
      return;
    case ScreenKind::Device:
      layoutDevice(screen);
      return;
    case ScreenKind::Player:
      layoutPlayer(screen);
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
    case ScreenKind::Setup: {
      if (setupStage == TurnHub::SetupStage::Finished) {
        const ButtonSpec row[] = {{TouchAction::SetupPair, "Pair a Sigil", 0, 3},
            {TouchAction::SetupDone, "Done", 0, 2}};
        addRow(screen, BUTTON_ROW_Y, row, 2);
      } else {
        // Pairing belongs to setup too (the app's Sigils step), so every
        // device can take one update prompt; opening it stays a touch here.
        const ButtonSpec row[] = {{TouchAction::Pair, "Pair a Sigil", 0, 3},
            {TouchAction::SkipSetup, "Skip", 0, 2}, {TouchAction::OpenMenu, "Menu", 0, 2}};
        addRow(screen, BUTTON_ROW_Y, row, 3);
      }
      return;
    }
    case ScreenKind::Status:
      break;
  }

  switch (hubState) {
    // Start and Clear up front; Pair, QR codes, Tests and Info wait under Menu.
    case HubState::Lobby: {
      ButtonSpec row[3];
      uint8_t n = 0;
      if (lobby.playerCount() >= 2) row[n++] = {TouchAction::StartGame, "Start", 0, 3};
      if (lobby.playerCount() >= 1) row[n++] = {TouchAction::ClearLobby, "Clear", LOBBY_CLEAR_HOLD_MS, 2};
      row[n++] = {TouchAction::OpenMenu, "Menu", 0, 2};
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
          {TouchAction::OpenMenu, "Menu", 0, 2}};
      addRow(screen, BUTTON_ROW_Y, row, 3);
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
  if (action == TouchAction::OpenPlayer) {
    static TouchButton chip;
    return chipButton(pressedSeat, nowMs, chip) ? &chip : nullptr;
  }
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
  pressedSeat = PlayerSeat();
  return chipAt(x, y, nowMs, pressedSeat) ? TouchAction::OpenPlayer : TouchAction::None;
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
    case TouchAction::PairConfirm: return "PAIR_CONFIRM";
    case TouchAction::PairReject: return "PAIR_REJECT";
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
    case TouchAction::OpenMenu: return "OPEN_MENU";
    case TouchAction::OpenPlayer: return "OPEN_PLAYER";
    case TouchAction::LifeMinus5: return "LIFE_MINUS_5";
    case TouchAction::LifeMinus1: return "LIFE_MINUS_1";
    case TouchAction::LifePlus1: return "LIFE_PLUS_1";
    case TouchAction::LifePlus5: return "LIFE_PLUS_5";
    case TouchAction::Concede: return "CONCEDE_ASK";
    case TouchAction::ConfirmConcede: return "CONCEDE";
    case TouchAction::CancelConcede: return "CONCEDE_CANCEL";
    case TouchAction::MoveEarlier: return "MOVE_EARLIER";
    case TouchAction::MoveLater: return "MOVE_LATER";
    case TouchAction::SeatBLeft: return "SEAT_B_LEFT";
    case TouchAction::SeatBRight: return "SEAT_B_RIGHT";
    case TouchAction::RemoveSeat: return "REMOVE_SEAT";
    case TouchAction::SkipSetup: return "SKIP_SETUP";
    case TouchAction::OpenSetup: return "OPEN_SETUP";
    case TouchAction::SetupPair: return "SETUP_PAIR";
    case TouchAction::SetupDone: return "SETUP_DONE";
    case TouchAction::OpenDevice: return "OPEN_DEVICE";
    case TouchAction::UnpairSigils: return "UNPAIR_SIGILS";
    case TouchAction::FactoryResetAtlas: return "FACTORY_RESET_ATLAS";
    case TouchAction::SleepAtlas: return "SLEEP_ATLAS";
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
  if (action == TouchAction::UnpairSigils) return "unpair every Sigil";
  if (action == TouchAction::FactoryResetAtlas) return "erase Atlas";
  return "end the match";
}

// Screen changes: no Intent, no table state, no notice. Back leaves the
// Menu's own screens (Info, QR codes, Tests) for the Menu while it is
// available, and everything else for the status screen.
bool navigate(TouchAction action) {
  switch (action) {
    case TouchAction::OpenMenu: openScreen = ScreenKind::Menu; return true;
    case TouchAction::OpenInfo: openScreen = ScreenKind::Info; return true;
    case TouchAction::OpenQr: openScreen = ScreenKind::Qr; return true;
    case TouchAction::OpenTests: openScreen = ScreenKind::Tests; return true;
    case TouchAction::OpenTable: openScreen = ScreenKind::Table; return true;
    case TouchAction::OpenDevice: openScreen = ScreenKind::Device; return true;
    case TouchAction::OpenPlayer:
      openScreen = ScreenKind::Player;
      shownSeat = pressedSeat;
      concedeArmed = false;
      return true;
    // Concede asks again; only ConfirmConcede sends the Intent.
    case TouchAction::Concede: concedeArmed = true; return true;
    case TouchAction::CancelConcede: concedeArmed = false; return true;
    case TouchAction::CloseScreen:
    case TouchAction::CloseTests: {
      concedeArmed = false;
      const bool fromMenuScreen = openScreen == ScreenKind::Info || openScreen == ScreenKind::Qr ||
          openScreen == ScreenKind::Tests || openScreen == ScreenKind::Device;
      openScreen = fromMenuScreen && menuAvailable() ? ScreenKind::Menu : ScreenKind::Status;
      return true;
    }
    case TouchAction::QrWifi:
    case TouchAction::QrPortal:
    case TouchAction::QrSignIn: qrChoice = action; return true;
    case TouchAction::SkipSetup:
      setupSkipped = true;
      openScreen = ScreenKind::Status;
      return true;
    case TouchAction::OpenSetup:
      setupSkipped = false;
      openScreen = ScreenKind::Status;
      return true;
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
    // The owner compared the Sigil's code with this screen's.
    case TouchAction::PairConfirm:
    case TouchAction::PairReject: {
      Intent intent;
      intent.type = IntentType::PairConfirm;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      const uint8_t slot = waitingPairSlot();
      intent.payload.value = (slot == INVALID_ID ? MAX_PHYSICAL_SIGILS : slot) |
          (action == TouchAction::PairConfirm ? TurnHub::PAIR_CONFIRM_ACCEPT : 0);
      result = intents.dispatch(intent);
      break;
    }
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
      // After a master pass, End match or Pair (from the Menu), show the
      // table what happened: the turn, the result or the pairing countdown.
      if (result.accepted() && (action == TouchAction::MasterPass || action == TouchAction::EndMatch ||
          action == TouchAction::Pair)) {
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
    // The Player screen acts for the player whose chip was tapped, as their
    // own Sigil would: ChangeLife for their own life, and Concede. The
    // handlers decide; nothing here changes the game.
    case TouchAction::LifeMinus5:
    case TouchAction::LifeMinus1:
    case TouchAction::LifePlus1:
    case TouchAction::LifePlus5:
    case TouchAction::ConfirmConcede: {
      PlayerSeat seat;
      if (!currentShownSeat(seat) || hubState == HubState::Lobby) {
        result = IntentResult::reject(IntentStatus::InvalidActor, "That player is not in this game");
        break;
      }
      Intent intent;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      intent.actor.controllerId = seat.controllerId;
      intent.actor.slot = seat.slot;
      intent.actor.playerNumber = seat.playerNumber;
      if (action == TouchAction::ConfirmConcede) {
        intent.type = IntentType::Concede;
      } else {
        intent.type = IntentType::ChangeLife;
        intent.payload.targetPlayer = seat.playerNumber;
        // The small and big steps (LifeMinus1 / LifeMinus5 name the usual ones).
        const int32_t step = game.settings().profile == TurnHub::GameProfile::Yugioh ? 100 : 1;
        const int32_t bigStep = step == 100 ? 1000 : 5;
        intent.payload.value = action == TouchAction::LifeMinus5 ? -bigStep
            : action == TouchAction::LifeMinus1 ? -step : action == TouchAction::LifePlus1 ? step : bigStep;
      }
      result = intents.dispatch(intent);
      if (action == TouchAction::ConfirmConcede) {
        concedeArmed = false;
        if (result.accepted()) openScreen = ScreenKind::Status;
      }
      break;
    }
    // Turn order in the lobby: the table device asks, the handler decides.
    case TouchAction::MoveEarlier:
    case TouchAction::MoveLater:
    case TouchAction::SeatBLeft:
    case TouchAction::SeatBRight: {
      PlayerSeat seat;
      if (!currentShownSeat(seat)) {
        result = IntentResult::reject(IntentStatus::InvalidActor, "That player is not at the table");
        break;
      }
      Intent intent;
      const bool sides = action == TouchAction::SeatBLeft || action == TouchAction::SeatBRight;
      intent.type = sides ? IntentType::SetSeatSide : IntentType::MoveSeat;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      intent.payload.targetPlayer = seat.playerNumber;
      intent.payload.value = sides ? (action == TouchAction::SeatBLeft ? 1 : 0) :
          (action == TouchAction::MoveEarlier ? -1 : 1);
      result = intents.dispatch(intent);
      break;
    }
    // Held Remove on a lobby Player screen: that seat leaves the lobby.
    case TouchAction::RemoveSeat: {
      PlayerSeat seat;
      if (!currentShownSeat(seat)) {
        result = IntentResult::reject(IntentStatus::InvalidActor, "That player is not at the table");
        break;
      }
      Intent intent;
      intent.type = IntentType::RemoveSeat;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      intent.actor.controllerId = seat.controllerId;
      intent.actor.slot = seat.slot;
      result = intents.dispatch(intent);
      if (result.accepted()) openScreen = ScreenKind::Status;
      break;
    }
    // "You're all set": leave setup, and for Pair a Sigil open pairing too.
    case TouchAction::SetupDone:
    case TouchAction::SetupPair: {
      Intent intent;
      intent.type = IntentType::AdvanceSetup;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      intent.payload.value = static_cast<int32_t>(TurnHub::SetupStage::Complete);
      result = intents.dispatch(intent);
      if (result.accepted() && action == TouchAction::SetupPair) {
        Intent pair;
        pair.type = IntentType::PairRequest;
        pair.actor.origin = IntentOrigin::AtlasHardware;
        result = intents.dispatch(pair);
      }
      break;
    }
    // Menu > Device, held: the BOOT button's Unpair and Factory reset, as the
    // same AtlasHardware Intents (front_panel.cpp's updateBootButton). The
    // handlers decide: Unpair needs the lobby and no seated Sigil.
    case TouchAction::UnpairSigils:
    case TouchAction::FactoryResetAtlas: {
      Intent intent;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      if (action == TouchAction::UnpairSigils) {
        intent.type = IntentType::ForgetPairing;
        intent.payload.value = TurnHub::FORGET_ALL_SIGILS;
      } else {
        intent.type = IntentType::FactoryReset;
        intent.payload.value = TurnHub::FACTORY_RESET_ATLAS;
      }
      result = intents.dispatch(intent);
      break;
    }
    case TouchAction::SleepAtlas: {
      Intent intent;
      intent.type = IntentType::Sleep;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      result = intents.dispatch(intent);
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
    // Phone-joined players included (they showed as "Player N" in the lobby).
    const String profile = profileIdForTableSeat(seat, inGame);
    playerName(seat, profile, nowMs, p.name);
    // Only preset avatars reach Atlas's screen; custom ones stay signed-in only.
    const uint8_t avatar = TurnHubProfiles::avatarForProfile(
        profile.length() ? profile : TurnHubControllers::profileForSeat(seat.controllerId, seat.slot));
    if (TurnHubAvatars::validPresetAvatar(avatar)) p.avatar = avatar;
    if (inGame) {
      p.life = game.lifeTotal(seat.playerNumber);
      uint32_t turnMs = 0;
      if (const TurnHub::PlayerStats *stats = game.statsForPlayer(seat.playerNumber)) turnMs = stats->totalTurnMs;
      // Two-Headed Giant: both teammates hold the turn.
      const bool hasTurn = hubState != HubState::GameOver && game.hasTurn(seat.playerNumber) &&
          !game.isEliminated(seat.playerNumber);
      if (hasTurn) {
        turnMs += game.currentTurnElapsedMs(nowMs);
      }
      formatClock(p.turnTime, sizeof(p.turnTime), turnMs);
      if (hasTurn) p.flags |= CHIP_ACTIVE;
      if (game.isEliminated(seat.playerNumber)) p.flags |= CHIP_OUT;
      if (hubState == HubState::GameOver && game.isWinner(seat.playerNumber)) p.flags |= CHIP_WINNER;
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

// "Ana's" or, in Two-Headed Giant, "Team 1's": whose turn or win it is.
void formatSide(char *out, size_t size, const AtlasScreen &screen, uint8_t number, const char *suffix) {
  const uint8_t team = game.teamOf(number);
  if (team) snprintf(out, size, "Team %u%s", static_cast<unsigned>(team), suffix);
  else snprintf(out, size, "%s%s", nameOfPlayer(screen, number), suffix);
}

// Two-Headed Giant: the starting team skips the draw of its first turn.
bool startingTeamFirstTurn() {
  if (!game.twoHeadedGiant() || !game.hasTurn(game.starterPlayerNumber())) return false;
  const TurnHub::PlayerStats *stats = game.statsForPlayer(game.starterPlayerNumber());
  return stats != nullptr && stats->turnsCompleted == 0;
}

// The round: each living player's completed turns, where the starter's turn
// opens a new round. The highest count among the living is the rounds
// finished by the players furthest along; if the active player is one of
// them, their turn opens the next round.
void formatTurnClock(AtlasScreen &screen, uint32_t nowMs) {
  const bool over = hubState == HubState::GameOver && game.hasPlayers();
  if (hubState != HubState::Running && hubState != HubState::Paused && !over) return;
  // A finished game keeps its final round and length in the header.
  screen.round = game.currentRound();
  formatClock(screen.gameClock, sizeof(screen.gameClock), game.gameElapsedMs(nowMs));
  if (over) return;
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
      } else if (nextGameSettings.twoHeadedGiant) {
        snprintf(screen.detail, sizeof(screen.detail), "%u %s, Two-Headed Giant",
            static_cast<unsigned>(players), players == 1 ? "player" : "players");
      } else {
        snprintf(screen.detail, sizeof(screen.detail), "%u %s, %u %s",
            static_cast<unsigned>(players), players == 1 ? "player" : "players",
            static_cast<unsigned>(screen.sigilsOnline), screen.sigilsOnline == 1 ? "Sigil" : "Sigils");
      }
      if (screen.playerCount == 0) {
        // An empty table: how to join. The portal's QR code is under Menu.
        snprintf(screen.lines[0], sizeof(screen.lines[0]), "Join from a Sigil's menu,");
        snprintf(screen.lines[1], sizeof(screen.lines[1]), "or on the table portal.");
        snprintf(screen.lines[2], sizeof(screen.lines[2]), "Menu: pair, QR codes, info.");
        screen.lineCount = 3;
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
      formatSide(screen.title, sizeof(screen.title), screen, game.activePlayerNumber(), "'s turn");
      if (pendingPass.active) {
        // Count down the grace period so the table sees the pass is still
        // cancelable, and for how long (turntest, 2026-09-26).
        const uint32_t elapsed = millis() - pendingPass.requestedAtMs;
        const uint32_t leftMs = elapsed < PASS_GRACE_MS ? PASS_GRACE_MS - elapsed : 0;
        snprintf(screen.detail, sizeof(screen.detail), "Passing in %lus: that seat can cancel",
            static_cast<unsigned long>((leftMs + 999) / 1000));
      } else if (startingTeamFirstTurn()) {
        snprintf(screen.detail, sizeof(screen.detail), "Starting team: skip your first draw");
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
      } else if (eliminationTargetPlayer != 0) {
        snprintf(screen.detail, sizeof(screen.detail), "Eliminate %s? Their Sigil decides",
            nameOfPlayer(screen, eliminationTargetPlayer));
      } else {
        formatSide(screen.detail, sizeof(screen.detail), screen, game.activePlayerNumber(), "'s turn");
      }
      break;
    case HubState::GameOver:
      snprintf(screen.badge, sizeof(screen.badge), "GAME OVER");
      snprintf(screen.title, sizeof(screen.title), "Game over");
      if (game.endedInDraw()) {
        snprintf(screen.detail, sizeof(screen.detail), "The match ended in a draw");
      } else {
        formatSide(screen.detail, sizeof(screen.detail), screen, game.winnerPlayerNumber(), " wins");
      }
      break;
  }
  // Sigil updates run between games; their progress and outcome, in words,
  // take the detail line so a failure is never silent at the table.
  if (hubState == HubState::Lobby || hubState == HubState::GameOver) {
    char update[sizeof(screen.detail)];
    if (sigilUpdateNotice(update, sizeof(update), nowMs)) {
      snprintf(screen.detail, sizeof(screen.detail), "%s", update);
    }
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

// The pairing code for the first waiting Sigil, to compare with its screen.
// The words carry the whole instruction; the code is not a secret (it only
// proves both ends agreed the same key).
void formatPairCode(AtlasScreen &screen, uint32_t nowMs) {
  const uint8_t slot = waitingPairSlot();
  const TurnHubSecureLink::PendingPairing *pending =
      slot == INVALID_ID ? nullptr : sigilBus.pendingPairing(slot);
  if (pending == nullptr) return;
  snprintf(screen.badge, sizeof(screen.badge), "PAIR");
  snprintf(screen.title, sizeof(screen.title), "Pair Sigil %u", static_cast<unsigned>(slot + 1));
  const uint32_t elapsed = nowMs - pending->startedMs;
  const uint32_t leftS = elapsed >= TurnHubSecureLink::PAIR_CONFIRM_TIMEOUT_MS ? 0 :
      (TurnHubSecureLink::PAIR_CONFIRM_TIMEOUT_MS - elapsed + 999) / 1000;
  snprintf(screen.detail, sizeof(screen.detail), "Check the Sigil (%lu s)",
      static_cast<unsigned long>(leftS));
  TurnHubSecureLink::formatPairingCode(pending->code, screen.code);
  // Two short lines fit beside the QR column, under the code.
  snprintf(screen.lines[0], sizeof(screen.lines[0]), "Same code on the Sigil?");
  const uint8_t waiting = sigilBus.pendingPairingCount();
  if (waiting > 1) {
    snprintf(screen.lines[1], sizeof(screen.lines[0]), "%u more waiting after",
        static_cast<unsigned>(waiting - 1));
  } else {
    snprintf(screen.lines[1], sizeof(screen.lines[0]), "If not, Reject.");
  }
  screen.lineCount = 2;
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

// "Update available", in words, for the on-board LED's blue blink.
void formatUpdateLine(char *out, size_t size, TurnHub::UpdateKind kind) {
  snprintf(out, size, "%s: use the app", TurnHub::updateKindText(kind));
}

void formatMenu(AtlasScreen &screen) {
  snprintf(screen.badge, sizeof(screen.badge), "MENU");
  snprintf(screen.title, sizeof(screen.title), "Table menu");
  const TurnHub::UpdateKind update = firmwareUpdateKind();
  if (update != TurnHub::UpdateKind::None) {
    formatUpdateLine(screen.detail, sizeof(screen.detail), update);
    return;
  }
  snprintf(screen.detail, sizeof(screen.detail), "%s",
      hubState == HubState::Lobby ? "Pair Sigils, share codes, table info" : "Share codes, table info");
}

void formatDevice(AtlasScreen &screen) {
  snprintf(screen.badge, sizeof(screen.badge), "DEVICE");
  snprintf(screen.title, sizeof(screen.title), "Atlas device");
  snprintf(screen.detail, sizeof(screen.detail), "%s",
      hubState == HubState::Lobby ? "Hold: Unpair 3 s, reset 10 s. Sleep: tap" : "Hold 10 s to reset. Sleep: tap");
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
  const TurnHub::UpdateKind update = firmwareUpdateKind();
  if (update != TurnHub::UpdateKind::None) {
    formatUpdateLine(screen.lines[4], sizeof(screen.lines[4]), update);
  } else {
    snprintf(screen.lines[4], sizeof(screen.lines[4]), "Up %s", uptime);
  }
  screen.lineCount = 5;
}

// The owner-set Wi-Fi password, or "" for the shipped default. The screen
// rebuilds every 50 ms; NVS is read at most every 2 s.
const String &customWifiPassword(uint32_t nowMs) {
  static String stored;
  static uint32_t readAtMs = 0;
  static bool read = false;
  if (!read || nowMs - readAtMs >= 2000) {
    stored = TurnHub::readStoredWifiPassword();
    if (!TurnHub::validWifiPassword(stored) || stored == AtlasConfig::WIFI_DEFAULT_PASSWORD) stored = String();
    readAtMs = nowMs;
    read = true;
  }
  return stored;
}

// First-run setup, all in words (no QR code needed; owner, 2026-09-30):
// Welcome says how to start from a phone, the app first; after the phone
// finishes, "You're all set" says what changed and what comes next.
void formatSetup(AtlasScreen &screen, uint32_t nowMs) {
  snprintf(screen.badge, sizeof(screen.badge), "SETUP");
  if (setupStage == TurnHub::SetupStage::Finished) {
    snprintf(screen.title, sizeof(screen.title), "You're all set");
    snprintf(screen.detail, sizeof(screen.detail), "This table is ready to play");
    snprintf(screen.lines[0], sizeof(screen.lines[0]), "Its Wi-Fi now has your password.");
    snprintf(screen.lines[1], sizeof(screen.lines[1]), "The app reconnects by itself; other");
    snprintf(screen.lines[2], sizeof(screen.lines[2]), "phones rejoin %s.", AtlasConfig::WIFI_SSID);
    snprintf(screen.lines[3], sizeof(screen.lines[3]), "Next: pair your Sigils, or tap Done.");
    screen.lineCount = 4;
    return;
  }
  snprintf(screen.title, sizeof(screen.title), "Welcome to TurnHub");
  const uint32_t pairingMs = pairingRemainingMs(nowMs);
  if (pairingMs > 0) {
    snprintf(screen.detail, sizeof(screen.detail), "Pairing open: %lu s left",
        static_cast<unsigned long>((pairingMs + 999) / 1000));
  } else {
    snprintf(screen.detail, sizeof(screen.detail), "Set up this table from a phone");
  }
  snprintf(screen.lines[0], sizeof(screen.lines[0]), "1. Open the TurnHub app, tap Connect.");
  snprintf(screen.lines[1], sizeof(screen.lines[1]), "   It joins this table's Wi-Fi itself.");
  snprintf(screen.lines[2], sizeof(screen.lines[2]), "2. No app? Join Wi-Fi %s,", AtlasConfig::WIFI_SSID);
  if (customWifiPassword(nowMs).length()) {
    snprintf(screen.lines[3], sizeof(screen.lines[3]), "   with the table's own password,");
  } else {
    snprintf(screen.lines[3], sizeof(screen.lines[3]), "   password %s,", AtlasConfig::WIFI_DEFAULT_PASSWORD);
  }
  snprintf(screen.lines[4], sizeof(screen.lines[4]), "   then open 192.168.4.1");
  screen.lineCount = 5;
}

// The Wi-Fi code carries the password. The shipped default is public, so it
// shows freely; an admin-set password needs someone verified at the table.
void formatQr(AtlasScreen &screen, uint32_t nowMs) {
  snprintf(screen.badge, sizeof(screen.badge), "QR CODES");
  switch (qrChoice) {
    case TouchAction::QrWifi: {
      snprintf(screen.title, sizeof(screen.title), "Join the Wi-Fi");
      const String &stored = customWifiPassword(nowMs);
      const bool custom = stored.length() > 0;
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

// One player's screen: their name, and their life in words (or the
// concession question).
void formatPlayer(AtlasScreen &screen, uint32_t nowMs) {
  PlayerSeat seat;
  if (!currentShownSeat(seat)) return;
  const bool inLobby = hubState == HubState::Lobby;
  snprintf(screen.badge, sizeof(screen.badge), inLobby ? "ORDER" : "PLAYER");
  char name[SCREEN_NAME_LENGTH + 1];
  playerName(seat, profileIdForTableSeat(seat, !inLobby), nowMs, name);
  snprintf(screen.title, sizeof(screen.title), "%s", name);
  if (inLobby && nextGameSettings.twoHeadedGiant) {
    // Two-Headed Giant teams are neighbours in turn order (1+2, 3+4, ...).
    snprintf(screen.detail, sizeof(screen.detail), "Turn order: %u of %u, Team %u",
        static_cast<unsigned>(seat.playerNumber), static_cast<unsigned>(lobby.playerCount()),
        static_cast<unsigned>((seat.playerNumber + 1) / 2));
  } else if (inLobby) {
    // Show each player's position and the shared Sigil's physical seating.
    snprintf(screen.detail, sizeof(screen.detail), "Turn order: %u of %u%s",
        static_cast<unsigned>(seat.playerNumber), static_cast<unsigned>(lobby.playerCount()),
        lobby.hasSecondary(seat.controllerId) ?
            (lobby.secondaryFirst(seat.controllerId) ? "; B left, before A" : "; B right, after A") : "");
  } else if (concedeArmed) {
    snprintf(screen.detail, sizeof(screen.detail), "Concede for %s? Their game ends.", name);
  } else {
    snprintf(screen.detail, sizeof(screen.detail), "%s %ld%s",
        game.twoHeadedGiant() ? "Team life" : "Life",
        static_cast<long>(game.lifeTotal(seat.playerNumber)),
        game.hasTurn(seat.playerNumber) ? ", their turn" : "");
  }
}

}  // namespace

void buildAtlasScreen(uint32_t nowMs, AtlasScreen &screen) {
  screen = AtlasScreen();
  screen.kind = activeScreen(nowMs);
  screen.sdMissing = !sdCardReady();
  screen.update = firmwareUpdateKind();
  screen.sigilsOnline = sigilBus.activeCount(nowMs);
  switch (screen.kind) {
    case ScreenKind::Status: formatStatus(screen, nowMs); break;
    case ScreenKind::Tests: formatTests(screen, nowMs); break;
    case ScreenKind::Info: formatInfo(screen, nowMs); break;
    case ScreenKind::Qr: formatQr(screen, nowMs); break;
    case ScreenKind::Code: formatCode(screen, nowMs); break;
    case ScreenKind::PairCode: formatPairCode(screen, nowMs); break;
    case ScreenKind::Table: formatTable(screen, nowMs); break;
    case ScreenKind::Menu: formatMenu(screen); break;
    case ScreenKind::Device: formatDevice(screen); break;
    case ScreenKind::Player: formatPlayer(screen, nowMs); break;
    case ScreenKind::Setup: formatSetup(screen, nowMs); break;
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
  shownSeat = pressedSeat = PlayerSeat();
  concedeArmed = false;
  setupSkipped = false;
  for (auto &cache : names) cache = NameCache();
}

}  // namespace TurnHubAtlas
