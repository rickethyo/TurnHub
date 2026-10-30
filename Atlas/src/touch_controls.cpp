// Atlas touchscreen: the TFT's screen model and its touch buttons. The touch
// adapter only builds Intents (IntentOrigin::AtlasHardware); the handlers
// decide. The exceptions change no table state: Cancel takes a presence code
// off the screen (front_panel.cpp), the tabs and list rows switch screens, and
// the confirm popup asks before a hazardous action. Nothing acts on a hold.
// The board draws the screen model (atlas_board.h).

#include "avatars.h"
#include "touch_controls.h"
#include "player_controls_model.h"

#include <stdio.h>
#include <string.h>

#include "account_access.h"
#include "atlas_app.h"
#include "atlas_battery.h"
#include "atlas_display.h"
#include "controller_profiles.h"
#include "firmware_version.h"
#include "home_wifi.h"
#include "optional_preferences.h"
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

// Press state for the touch in progress (pressedArg: which of the keys or
// rows sharing an action).
bool touchDown = false;
bool pressInside = false;
TouchAction pressedAction = TouchAction::None;
uint8_t pressedArg = 0;
uint32_t lastContactAtMs = 0;

char noticeText[sizeof(AtlasScreen::notice)] = {};
uint32_t noticeAtMs = 0;

// Which screen is up, and the QR code chosen on the QR screen.
ScreenKind openScreen = ScreenKind::Home;
uint8_t overviewPage = 0;
TouchAction qrChoice = TouchAction::QrPortal;
// The Player screen: whose it is (by controller and slot, since lobby player
// numbers change with the turn order). pressedSeat is the chip under the finger.
PlayerSeat shownSeat;
// The confirm popup: the action it asks about, and the screen it covers. It
// closes by itself when that screen does (a match ending, a code showing).
TouchAction confirmAction = TouchAction::None;
ScreenKind confirmOver = ScreenKind::Status;
bool playerControlsOpen = false;
uint8_t playerControlsPage = 0;
PlayerSeat pressedSeat;
// Skip for now on the setup Welcome screen: hidden until the next start-up,
// or until Setup under Menu. Presentation only; the stage stays Atlas's.
bool setupSkipped = false;

// Add player: the page of saved profiles shown (not at either table), the
// name being typed for a new one, and the PIN being typed for one that
// needs it. Presentation only, like the open screen.
constexpr uint8_t SEAT_PAGE_ROWS = 4;
constexpr uint8_t TYPED_PIN_LENGTH = 8;
struct SeatRow {
  char profileId[TurnHubProfiles::PROFILE_ID_LENGTH + 1] = {};
  char label[SCREEN_NAME_LENGTH + 7] = {};  // "Name (PIN)"
  bool needsPin = false;
};
SeatRow seatRows[SEAT_PAGE_ROWS];
uint8_t seatRowCount = 0;
uint8_t seatPage = 0;
uint8_t seatPages = 0;
char typedName[SCREEN_NAME_LENGTH + 1] = {};
char typedPin[TYPED_PIN_LENGTH + 1] = {};
uint8_t pinRow = 0;
// The name keyboard, six keys a row: letters, then hyphen, apostrophe and
// space (NameKey's arg is the index).
constexpr char NAME_KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-' ";
constexpr uint8_t NAME_KEY_COUNT = sizeof(NAME_KEYS) - 1;
const char *const NAME_KEY_LABELS[NAME_KEY_COUNT] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L",
    "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "-", "'", "Space"};
const char *const DIGIT_LABELS[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};

struct NameCache {
  char profileId[9] = {};
  char name[SCREEN_NAME_LENGTH + 1] = {};
  uint32_t fetchedAtMs = 0;
  bool valid = false;
};
NameCache names[MAX_SCREEN_PLAYERS];

// Mana Tap (mana_tap.h), opened from Home. Presentation only, like the open
// screen: it changes no table state. It pauses when its screen is not up and
// when a turn passes in this table's match (then the Game tab shows); Home's
// Play Mana Tap carries on a paused run. Atlas keeps one record for the
// table in NVS ("manatap"/"best"), written only when beaten.
TurnHub::ManaTap manaTap;
bool manaTurnPaused = false;
uint8_t manaTurnPlayer = 0;  // The active player when last checked (0: none).
int32_t manaStoredBest = -1;  // -1 until read.

uint16_t manaBest() {
  if (manaStoredBest < 0) {
    manaStoredBest = 0;
    TurnHub::OptionalPreferences prefs;
    if (prefs.begin("manatap", true)) {
      manaStoredBest = prefs.getString("best", "0").toInt();
      prefs.end();
    }
    if (manaStoredBest < 0 || manaStoredBest > 65535) manaStoredBest = 0;
  }
  return static_cast<uint16_t>(manaStoredBest);
}

void keepManaBest() {
  if (manaTap.best() <= manaBest()) return;
  TurnHub::OptionalPreferences prefs;
  if (!prefs.begin("manatap", false)) return;
  if (prefs.putString("best", String(manaTap.best())) > 0) manaStoredBest = manaTap.best();
  prefs.end();
}

void openManaTap(uint32_t nowMs) {
  openScreen = ScreenKind::ManaTap;
  if (manaTap.phase() == TurnHub::ManaPhase::Paused) return;  // Carry on.
  manaTap.open(TurnHub::MANA_ATLAS_FIELD, 1, manaBest(), nowMs * 2654435761u + 1);
  manaTurnPaused = false;
}

// --- Layout -------------------------------------------------------------------

struct ButtonSpec {
  TouchAction action;
  const char *label;
  uint8_t confirm;  // ASK: confirm in the popup before acting.
  uint8_t weight;
};
constexpr uint8_t ASK = 1;

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
    button.confirm = specs[i].confirm != 0;
    button.selected = specs[i].action == qrChoice && openScreen == ScreenKind::Qr;
    x += w + MARGIN;
  }
}

// Stacks buttons full width down the body, from its top (portrait menus).
void addColumn(AtlasScreen &screen, const ButtonSpec *specs, uint8_t count) {
  for (uint8_t i = 0; i < count; ++i) addRow(screen, SCREEN_BODY_Y + i * (BUTTON_ROW_H + MARGIN), &specs[i], 1);
}

// One grouped list (Settings and its pages): full-width rows packed from the
// body's top, as tall as LIST_ROW_H or as fit above `bottom`. The display
// draws touching rows as one card.
void addList(AtlasScreen &screen, const ButtonSpec *specs, uint8_t count, int16_t bottom = BUTTON_ROW_Y - MARGIN) {
  if (count == 0) return;
  int16_t h = (bottom - SCREEN_BODY_Y) / count;
  if (h > LIST_ROW_H) h = LIST_ROW_H;
  for (uint8_t i = 0; i < count; ++i) {
    addRow(screen, SCREEN_BODY_Y + i * h, &specs[i], 1);
    screen.buttons[screen.buttonCount - 1].h = h;
  }
}
// The list's bottom on pages with no Back row: just above the tab bar.
constexpr int16_t LIST_BOTTOM_TABS = ATLAS_SCREEN_HEIGHT - 48 - MARGIN;

// One key or list row at an exact spot, told apart by arg.
void addKey(AtlasScreen &screen, TouchAction action, const char *label, int16_t x, int16_t y, int16_t w, int16_t h,
    uint8_t arg) {
  if (screen.buttonCount >= MAX_TOUCH_BUTTONS) return;
  TouchButton &button = screen.buttons[screen.buttonCount++];
  button.action = action;
  button.label = label;
  button.x = x;
  button.y = y;
  button.w = w;
  button.h = h;
  button.arg = arg;
}

bool matchInProgress() {
  return table().hubState == HubState::Running || table().hubState == HubState::Paused;
}

// Settings navigation remains available in every game state.
bool menuAvailable() {
  return true;  // Navigation/settings remain available during play.
}

// The first Sigil waiting for its pairing code to be checked, or INVALID_ID.
// One at a time on this screen; the next shows once it is decided.
uint8_t waitingPairSlot() {
  if (table().hubState != HubState::Lobby) return INVALID_ID;
  for (uint8_t slot = 0; slot < MAX_PHYSICAL_SIGILS; ++slot) {
    if (sigilBus.pendingPairing(slot) != nullptr) return slot;
  }
  return INVALID_ID;
}

// The chips on the status screen, in the order they are drawn: the lobby's
// seats, or the match's players while one is in progress.
uint8_t chipSeats(PlayerSeat *out) {
  if (table().hubState == HubState::Lobby) return table().lobby.buildPlayers(out, MAX_SCREEN_PLAYERS);
  if (!matchInProgress()) return 0;
  const uint8_t count = table().game.playerCount() < MAX_SCREEN_PLAYERS ? table().game.playerCount() : MAX_SCREEN_PLAYERS;
  for (uint8_t i = 0; i < count; ++i) out[i] = *table().game.playerAt(i);
  return count;
}

bool sameSeat(const PlayerSeat &a, const PlayerSeat &b) {
  return a.controllerId == b.controllerId && a.slot == b.slot;
}

// The shown seat as it is now (its current player number), if it still
// plays: at the table in the lobby, or not out in a match.
bool currentShownSeat(PlayerSeat &seat) {
  if (table().hubState != HubState::Lobby && !matchInProgress()) return false;
  PlayerSeat seats[MAX_SCREEN_PLAYERS];
  const uint8_t count = chipSeats(seats);
  for (uint8_t i = 0; i < count; ++i) {
    if (!sameSeat(seats[i], shownSeat)) continue;
    if (table().hubState != HubState::Lobby && table().game.isEliminated(seats[i].playerNumber)) return false;
    seat = seats[i];
    return true;
  }
  return false;
}

// First-run setup takes the lobby's status screen until it is complete
// (Welcome can be skipped for now; "You're all set" waits for Done).
bool setupScreenDue() {
  if (table().hubState != HubState::Lobby || setupStage == TurnHub::SetupStage::Complete) return false;
  return !(setupStage == TurnHub::SetupStage::Welcome && setupSkipped);
}

// The screen under any confirm popup. A code a phone asked for shows over
// whatever screen is open, then a pairing code waiting to be checked. The
// Table screen belongs to a match and closes when it ends; Settings remains
// available during a game.
ScreenKind baseScreen(uint32_t nowMs) {
  if (openScreen == ScreenKind::Table && !matchInProgress()) openScreen = ScreenKind::Status;
  // A player's screen closes when the lobby or match it belongs to ends, or
  // once that player is out.
  PlayerSeat shown;
  if (openScreen == ScreenKind::Player && !currentShownSeat(shown)) openScreen = ScreenKind::Status;
  if ((openScreen == ScreenKind::Menu || openScreen == ScreenKind::Device || openScreen == ScreenKind::Themes) && !menuAvailable()) {
    openScreen = ScreenKind::Status;
  }
  const bool lobbyOnly = openScreen == ScreenKind::Seat || openScreen == ScreenKind::Name ||
      openScreen == ScreenKind::Pin || openScreen == ScreenKind::NewGame;
  if (lobbyOnly && table().hubState != HubState::Lobby) openScreen = ScreenKind::Status;
  if (pendingPresenceCode(nowMs) != nullptr) return ScreenKind::Code;
  if (waitingPairSlot() != INVALID_ID) return ScreenKind::PairCode;
  if (openScreen == ScreenKind::Status && setupScreenDue()) return ScreenKind::Setup;
  return openScreen;
}

// The confirm popup sits over the screen that asked, while that screen is up.
ScreenKind activeScreen(uint32_t nowMs) {
  const ScreenKind base = baseScreen(nowMs);
  if (confirmAction == TouchAction::None) return base;
  if (base == confirmOver) return ScreenKind::Confirm;
  confirmAction = TouchAction::None;
  return base;
}

// The Settings tab: one list. Setup (until it is done) and Pair a Sigil in
// the lobby, then QR codes, Theme, Home Wi-Fi and Device.
void layoutMenu(AtlasScreen &screen) {
  static char theme[32];
  snprintf(theme, sizeof(theme), "Theme: %s", TurnHubTheme::palette(atlasDisplayTheme()).label);
  const bool homeOn = homeWifiEnabled();
  ButtonSpec rows[6];
  uint8_t n = 0;
  if (table().hubState == HubState::Lobby && setupStage != TurnHub::SetupStage::Complete) {
    rows[n++] = {TouchAction::OpenSetup, "Setup", 0, 1};
  }
  if (table().hubState == HubState::Lobby) rows[n++] = {TouchAction::Pair, "Pair a Sigil", 0, 1};
  rows[n++] = {TouchAction::OpenQr, "QR codes", 0, 1};
  rows[n++] = {TouchAction::OpenThemes, theme, 0, 1};
  rows[n++] = {TouchAction::HomeWifi, homeOn ? "Home Wi-Fi: on" : "Home Wi-Fi: off", 0, 1};
  rows[n++] = {TouchAction::OpenDevice, "Device", 0, 1};
  addList(screen, rows, n, LIST_BOTTOM_TABS);
  for (uint8_t i = 0; i < screen.buttonCount; ++i) {
    if (screen.buttons[i].action == TouchAction::HomeWifi) screen.buttons[i].selected = homeOn;
  }
}

// Settings > Device: USB drive, Sleep (between games; a touch wakes Atlas),
// Unpair Sigils (lobby only, as the handler requires) and Factory reset (not
// during a match). Unpair and Factory reset ask first in the popup; they
// stand in for the BOOT button's held gestures.
void layoutDevice(AtlasScreen &screen) {
  const bool usbWanted = sdCardUsbShareWanted();
  ButtonSpec rows[4];
  uint8_t n = 0;
  rows[n++] = {TouchAction::UsbDrive, usbWanted ? "USB drive: on" : "USB drive: off", 0, 1};
  if (!matchInProgress()) rows[n++] = {TouchAction::SleepAtlas, "Sleep", 0, 1};
  if (table().hubState == HubState::Lobby) rows[n++] = {TouchAction::UnpairSigils, "Unpair Sigils", ASK, 1};
  if (!matchInProgress()) rows[n++] = {TouchAction::FactoryResetAtlas, "Factory reset", ASK, 1};
  addList(screen, rows, n);
  for (uint8_t i = 0; i < screen.buttonCount; ++i) {
    if (screen.buttons[i].action == TouchAction::UsbDrive) screen.buttons[i].selected = usbWanted;
  }
  const ButtonSpec back[] = {{TouchAction::CloseScreen, "Back", 0, 1}};
  addRow(screen, BUTTON_ROW_Y, back, 1);
}

// Settings > Theme: the four looks and screen inversion, then Back.
void layoutThemes(AtlasScreen &screen) {
  const ButtonSpec rows[] = {{TouchAction::ThemeGraphite, "Graphite", 0, 1}, {TouchAction::ThemeDaylight, "Daylight", 0, 1},
      {TouchAction::ThemeBrass, "Brass", 0, 1}, {TouchAction::ThemeContrast, "High contrast", 0, 1},
      {TouchAction::InvertDisplay, atlasDisplayInverted() ? "Invert colours: on" : "Invert colours: off", 0, 1}};
  addList(screen, rows, 5);
  for (uint8_t i = 0; i < screen.buttonCount; ++i) {
    auto &button = screen.buttons[i];
    if (button.action == TouchAction::InvertDisplay) button.selected = atlasDisplayInverted();
    if (button.action >= TouchAction::ThemeGraphite && button.action <= TouchAction::ThemeContrast)
      button.selected = static_cast<uint8_t>(button.action) - static_cast<uint8_t>(TouchAction::ThemeGraphite) == static_cast<uint8_t>(atlasDisplayTheme());
  }
  const ButtonSpec back[] = {{TouchAction::CloseScreen, "Back", 0, 1}};
  addRow(screen, BUTTON_ROW_Y, back, 1);
}

// In-game controls kept off the main row: Master pass (a stuck turn, running
// games only) and End match, each confirmed in the popup; then Back.
void layoutTable(AtlasScreen &screen) {
  ButtonSpec rows[2];
  uint8_t n = 0;
  if (table().hubState == HubState::Running) rows[n++] = {TouchAction::MasterPass, "Master pass", ASK, 1};
  rows[n++] = {TouchAction::EndMatch, "End match", ASK, 1};
  addList(screen, rows, n);
  const ButtonSpec back[] = {{TouchAction::CloseScreen, "Back", 0, 1}};
  addRow(screen, BUTTON_ROW_Y, back, 1);
}

// The game the next match plays, as the app's game tiles name it.
const char *gameName(const TurnHub::GameSettings &settings) {
  switch (settings.profile) {
    case TurnHub::GameProfile::Magic: return "Standard";
    case TurnHub::GameProfile::Commander:
    case TurnHub::GameProfile::StarCommander:
    case TurnHub::GameProfile::DoomCommander: return "Commander";
    case TurnHub::GameProfile::Yugioh: return "Yu-Gi-Oh!";
    default: return "Other";
  }
}

// Magic and Commander modes, as the app's Mode choice names them.
const char *modeName(const TurnHub::GameSettings &settings) {
  if (settings.profile == TurnHub::GameProfile::StarCommander) return "Star Commander";
  if (settings.profile == TurnHub::GameProfile::DoomCommander) return "Bag of Doom";
  return settings.twoHeadedGiant ? "Two-Headed Giant" : "Free-for-all";
}

// New game (lobby): the app's game setup as rows that each step their
// setting (ConfigureGame from the table), then Start game once two sit.
void layoutNewGame(AtlasScreen &screen) {
  const TurnHub::GameSettings &next = table().nextGameSettings;
  static char game[32], mode[32], life[32], timer[32], house[32];
  snprintf(game, sizeof(game), "Game: %s", gameName(next));
  snprintf(mode, sizeof(mode), "Mode: %s", modeName(next));
  TurnHub::OfficialRules locked{};
  const bool official = TurnHub::lockedRules(next, locked);
  snprintf(life, sizeof(life), "Starting life: %ld%s", static_cast<long>(next.startingLife), official ? " (rules)" : "");
  if (next.turnTimerMs == TurnHub::TURN_TIMER_OFF) snprintf(timer, sizeof(timer), "Turn timer: off");
  else snprintf(timer, sizeof(timer), "Turn timer: %lu min", static_cast<unsigned long>(next.turnTimerMs / 60000));
  snprintf(house, sizeof(house), "House rules: %s", next.houseRules ? "on" : "off");
  ButtonSpec rows[5];
  uint8_t n = 0;
  rows[n++] = {TouchAction::GameType, game, 0, 1};
  if (TurnHub::magicRules(next.profile)) rows[n++] = {TouchAction::GameMode, mode, 0, 1};
  rows[n++] = {TouchAction::GameLife, life, 0, 1};
  rows[n++] = {TouchAction::GameTimer, timer, 0, 1};
  TurnHub::OfficialRules rules{};
  if (TurnHub::officialRules(next.profile, next.twoHeadedGiant, rules)) rows[n++] = {TouchAction::GameHouseRules, house, 0, 1};
  addList(screen, rows, n);
  ButtonSpec lower[2];
  uint8_t m = 0;
  if (table().lobby.playerCount() >= 2) lower[m++] = {TouchAction::StartGame, "Start game", 0, 3};
  lower[m++] = {TouchAction::CloseScreen, "Back", 0, 2};
  addRow(screen, BUTTON_ROW_Y, lower, m);
}

// One player's screen (playtest 2026-09-29, item 9): life steps above,
// More, Concede (confirmed in the popup) and Back below.
void layoutPlayer(AtlasScreen &screen) {
  // Lobby: turn order (item 11). Earlier and Later, then Back.
  if (table().hubState == HubState::Lobby) {
    const ButtonSpec upper[] = {{TouchAction::MoveEarlier, "Earlier", 0, 1},
        {TouchAction::MoveLater, "Later", 0, 1},
        {TouchAction::RemoveSeat, "Remove", ASK, 1}};
    addColumn(screen, upper, 3);
    PlayerSeat seat;
    if (currentShownSeat(seat) && table().lobby.hasSecondary(seat.controllerId)) {
      const ButtonSpec lower[] = {{TouchAction::SeatBLeft, "B left", 0, 1},
          {TouchAction::SeatBRight, "B right", 0, 1}, {TouchAction::CloseScreen, "Back", 0, 1}};
      addRow(screen, BUTTON_ROW_Y, lower, 3);
    } else {
      const ButtonSpec lower[] = {{TouchAction::CloseScreen, "Back", 0, 1}};
      addRow(screen, BUTTON_ROW_Y, lower, 1);
    }
    return;
  }
  if (playerControlsOpen) {
    PlayerSeat seat; if (!currentShownSeat(seat)) return;
    screen.showPlayerControls=true; screen.controls=playerControlsFor(seat.playerNumber);
    uint8_t ids[TurnHubUi::PLAYER_CONTROL_COUNT];
    const uint8_t count=TurnHubUi::playerControlRows(screen.controls,ids), pages=(count+2)/3;
    if (playerControlsPage>=pages) playerControlsPage=pages ? pages-1 : 0;
    screen.controlsPage=playerControlsPage;
    for (uint8_t j=0;j<3 && playerControlsPage*3+j<count;++j) {
      const uint8_t id=ids[playerControlsPage*3+j]; const bool toggle=TurnHubUi::playerControlToggle(id);
      const int16_t y=SCREEN_BODY_Y+j*78+24;
      if (!toggle || id!=8) addKey(screen,TouchAction::ControlMinus,toggle ? "Off" : "-",16,y,52,44,id);
      addKey(screen,TouchAction::ControlPlus,toggle ? "On" : "+",252,y,52,44,id);
    }
    ButtonSpec row[3]; uint8_t n=0;
    if (playerControlsPage) row[n++]={TouchAction::PlayerControlsPrev,"Prev",0,1};
    row[n++]={TouchAction::PlayerControlsBack,"Done",0,1};
    if (playerControlsPage+1<pages) row[n++]={TouchAction::PlayerControlsNext,"Next",0,1};
    addRow(screen,BUTTON_ROW_Y,row,n); return;
  }
  // Yu-Gi-Oh! life moves in hundreds, as on the portal's life cards.
  const bool hundreds = table().game.settings().profile == TurnHub::GameProfile::Yugioh;
  const ButtonSpec upper[] = {{TouchAction::LifeMinus5, hundreds ? "-1000" : "-5", 0, 1},
      {TouchAction::LifeMinus1, hundreds ? "-100" : "-1", 0, 1},
      {TouchAction::LifePlus1, hundreds ? "+100" : "+1", 0, 1},
      {TouchAction::LifePlus5, hundreds ? "+1000" : "+5", 0, 1}};
  addRow(screen, BUTTON_UPPER_ROW_Y, upper, 4);
  const ButtonSpec lower[] = {{TouchAction::OpenPlayerControls,"More",0,1},
      {TouchAction::Concede, "Concede", ASK, 1}, {TouchAction::CloseScreen, "Back", 0, 1}};
  addRow(screen, BUTTON_ROW_Y, lower, 3);
}

// The confirm popup: the action in its own words, and Cancel.
const char *confirmLabel(TouchAction action) {
  switch (action) {
    case TouchAction::EndMatch: return "End match";
    case TouchAction::MasterPass: return "Pass turn";
    case TouchAction::ClearLobby: return "Clear lobby";
    case TouchAction::ResetTable: return "Reset";
    case TouchAction::RemoveSeat: return "Remove";
    case TouchAction::Concede: return "Concede";
    case TouchAction::UnpairSigils: return "Unpair all";
    case TouchAction::FactoryResetAtlas: return "Erase Atlas";
    default: return "Confirm";
  }
}

void layoutConfirm(AtlasScreen &screen) {
  const ButtonSpec row[] = {{TouchAction::ConfirmNo, "Cancel", 0, 1},
      {TouchAction::ConfirmYes, confirmLabel(confirmAction), 0, 1}};
  addRow(screen, CONFIRM_BUTTON_Y, row, 2);
  // Inside the popup's card, which is inset from the screen's edges.
  constexpr int16_t INSET = 2 * MARGIN;
  const int16_t w = (ATLAS_SCREEN_WIDTH - 2 * INSET - 3 * MARGIN) / 2;
  for (uint8_t i = 0; i < screen.buttonCount; ++i) {
    screen.buttons[i].x = INSET + MARGIN + i * (w + MARGIN);
    screen.buttons[i].w = w;
  }
}

// Add player: a page of saved profiles, then New player, More and Back.
void layoutSeat(AtlasScreen &screen) {
  for (uint8_t i = 0; i < seatRowCount; ++i) {
    addKey(screen, TouchAction::SeatProfile, seatRows[i].label, MARGIN, SCREEN_BODY_Y + i * (BUTTON_ROW_H + MARGIN),
        FULL_WIDTH, BUTTON_ROW_H, i);
  }
  ButtonSpec lower[3];
  uint8_t n = 0;
  lower[n++] = {TouchAction::NewPlayer, "New player", 0, 3};
  if (seatPages > 1) lower[n++] = {TouchAction::SeatMore, "More", 0, 2};
  lower[n++] = {TouchAction::CloseScreen, "Back", 0, 2};
  addRow(screen, BUTTON_ROW_Y, lower, n);
}

// New player: six keys a row over the body (each about 47 x 56 px; Space
// takes two), then Cancel, Delete and Add.
void layoutName(AtlasScreen &screen) {
  constexpr uint8_t COLS = 6;
  constexpr int16_t GAP = 4;
  constexpr int16_t KEY_W = (FULL_WIDTH - (COLS - 1) * GAP) / COLS;
  constexpr int16_t KEY_H = (BUTTON_ROW_Y - MARGIN - SCREEN_BODY_Y - 4 * GAP) / 5;
  for (uint8_t i = 0; i < NAME_KEY_COUNT; ++i) {
    const int16_t x = MARGIN + (i % COLS) * (KEY_W + GAP);
    const int16_t y = SCREEN_BODY_Y + (i / COLS) * (KEY_H + GAP);
    const int16_t w = NAME_KEYS[i] == ' ' ? 2 * KEY_W + GAP : KEY_W;
    addKey(screen, TouchAction::NameKey, NAME_KEY_LABELS[i], x, y, w, KEY_H, i);
  }
  const ButtonSpec lower[] = {{TouchAction::CloseScreen, "Cancel", 0, 2}, {TouchAction::NameBack, "Delete", 0, 2},
      {TouchAction::NameDone, "Add", 0, 2}};
  addRow(screen, BUTTON_ROW_Y, lower, 3);
}

// A profile's PIN: a phone-style keypad (1-9, Del, 0, OK), then Cancel.
void layoutPin(AtlasScreen &screen) {
  constexpr int16_t KEY_W = (FULL_WIDTH - 2 * MARGIN) / 3;
  constexpr int16_t KEY_H = (BUTTON_ROW_Y - MARGIN - SCREEN_BODY_Y - 3 * MARGIN) / 4;
  for (uint8_t i = 0; i < 12; ++i) {
    const int16_t x = MARGIN + (i % 3) * (KEY_W + MARGIN);
    const int16_t y = SCREEN_BODY_Y + (i / 3) * (KEY_H + MARGIN);
    if (i == 9) {
      addKey(screen, TouchAction::PinBack, "Del", x, y, KEY_W, KEY_H, 0);
    } else if (i == 11) {
      addKey(screen, TouchAction::PinDone, "OK", x, y, KEY_W, KEY_H, 0);
    } else {
      const uint8_t digit = i == 10 ? 0 : i + 1;
      addKey(screen, TouchAction::PinKey, DIGIT_LABELS[digit], x, y, KEY_W, KEY_H, digit);
    }
  }
  const ButtonSpec lower[] = {{TouchAction::CloseScreen, "Cancel", 0, 1}};
  addRow(screen, BUTTON_ROW_Y, lower, 1);
}

// A zone of `seat`'s chip on the status screen, as a touch target (chips
// are drawn as tiles, not buttons). In the lobby the whole chip opens the
// player's screen; in a game its name band does, and the halves below change
// life. Eliminated players' chips act on nothing.
bool chipZone(const PlayerSeat &seat, TouchAction action, uint32_t nowMs, TouchButton &out) {
  if (seat.controllerId == INVALID_ID || activeScreen(nowMs) != ScreenKind::Status) return false;
  PlayerSeat seats[MAX_SCREEN_PLAYERS];
  const uint8_t count = chipSeats(seats);
  for (uint8_t i = 0; i < count; ++i) {
    if (!sameSeat(seats[i], seat)) continue;
    const bool inGame = table().hubState != HubState::Lobby;
    if (inGame && table().game.isEliminated(seats[i].playerNumber)) return false;
    out = TouchButton();
    out.action = action;
    screenChipCell(i, count, out.x, out.y, out.w, out.h);
    if (!inGame) return action == TouchAction::OpenPlayer;
    if (action == TouchAction::OpenPlayer) {
      out.h = CHIP_NAME_BAND_H;
      return true;
    }
    if (action != TouchAction::ChipLifeMinus && action != TouchAction::ChipLifePlus) return false;
    out.y += CHIP_NAME_BAND_H;
    out.h -= CHIP_NAME_BAND_H;
    out.w /= 2;
    if (action == TouchAction::ChipLifePlus) out.x += out.w;
    return true;
  }
  return false;
}

// The seat and chip zone at (x, y), if any.
TouchAction chipAt(int16_t x, int16_t y, uint32_t nowMs, PlayerSeat &found) {
  static const TouchAction zones[] = {TouchAction::OpenPlayer, TouchAction::ChipLifeMinus, TouchAction::ChipLifePlus};
  PlayerSeat seats[MAX_SCREEN_PLAYERS];
  const uint8_t count = chipSeats(seats);
  for (uint8_t i = 0; i < count; ++i) {
    for (TouchAction zone : zones) {
      TouchButton chip;
      if (chipZone(seats[i], zone, nowMs, chip) && chip.contains(x, y)) {
        found = seats[i];
        return zone;
      }
    }
  }
  return TouchAction::None;
}

bool chipAction(TouchAction action) {
  return action == TouchAction::OpenPlayer || action == TouchAction::ChipLifeMinus || action == TouchAction::ChipLifePlus;
}

// The buttons for the open screen and the table state.
void layoutContentButtons(AtlasScreen &screen, uint32_t nowMs) {
  screen.buttonCount = 0;
  switch (activeScreen(nowMs)) {
    // Home is the table at a glance; the tab bar is the way on, and
    // Mana Tap is there for between turns or games.
    case ScreenKind::Home: {
      static char play[32];
      if (manaBest()) snprintf(play, sizeof(play), "Play Mana Tap (best %u)", manaBest());
      else snprintf(play, sizeof(play), "Play Mana Tap");
      const ButtonSpec row[] = {{TouchAction::OpenManaTap, play, 0, 1}};
      addRow(screen, BUTTON_ROW_Y, row, 1);
      return;
    }
    case ScreenKind::ManaTap: {
      static const char *const ACTION_LABELS[] = {"Start", "Pause", "Resume", "Play again"};
      const char *label = ACTION_LABELS[static_cast<uint8_t>(manaTap.phase())];
      if (manaTap.phase() == TurnHub::ManaPhase::Playing) {
        // Beside the title, clear of the field.
        addKey(screen, TouchAction::ManaAction, label, ATLAS_SCREEN_WIDTH - MARGIN - 112, SCREEN_HERO_Y + 8, 112, 52, 0);
      } else {
        const ButtonSpec row[] = {{TouchAction::ManaAction, label, 0, 3}, {TouchAction::ManaQuit, "Quit", 0, 2}};
        addRow(screen, BUTTON_ROW_Y, row, 2);
      }
      return;
    }
    case ScreenKind::Confirm: layoutConfirm(screen); return;
    case ScreenKind::NewGame: layoutNewGame(screen); return;
    case ScreenKind::Overview: {
      const uint8_t count = table().hubState == HubState::Lobby ? table().lobby.playerCount() : table().game.playerCount();
      if (count > MAX_SCREEN_PLAYERS) {
        const ButtonSpec row[] = {{TouchAction::OverviewNext, "More players", 0, 1}};
        addRow(screen, BUTTON_ROW_Y, row, 1);
      }
      return;
    }
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
    case ScreenKind::Table:
      layoutTable(screen);
      return;
    case ScreenKind::Menu:
      layoutMenu(screen);
      return;
    case ScreenKind::Themes: layoutThemes(screen); return;
    case ScreenKind::Device:
      layoutDevice(screen);
      return;
    case ScreenKind::Player:
      layoutPlayer(screen);
      return;
    case ScreenKind::Seat: layoutSeat(screen); return;
    case ScreenKind::Name: layoutName(screen); return;
    case ScreenKind::Pin: layoutPin(screen); return;
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
            {TouchAction::SkipSetup, "Skip", 0, 2}, {TouchAction::OpenSettings, "Settings", 0, 2}};
        addRow(screen, BUTTON_ROW_Y, row, 3);
      }
      return;
    }
    case ScreenKind::Status:
      break;
  }

  switch (table().hubState) {
    // Add player, Start (New game: the game's options, then Start game) and
    // Clear; Pair and QR codes are under the Settings tab.
    case HubState::Lobby: {
      ButtonSpec row[3];
      uint8_t n = 0;
      row[n++] = {TouchAction::OpenSeat, "Add player", 0, 3};
      row[n++] = {TouchAction::OpenNewGame, "Start", 0, 3};
      if (table().lobby.playerCount() >= 1) row[n++] = {TouchAction::ClearLobby, "Clear", ASK, 2};
      addRow(screen, BUTTON_ROW_Y, row, n);
      break;
    }
    // Pass is a tap: it keeps its grace period, so the seat can still cancel.
    // The master pass and End match sit on the Actions screen.
    case HubState::Running: {
      const ButtonSpec row[] = {{TouchAction::PassTurn, "Pass", 0, 3},
          {TouchAction::Pause, "Pause", 0, 2}, {TouchAction::OpenTable, "Actions", 0, 3}};
      addRow(screen, BUTTON_ROW_Y, row, 3);
      break;
    }
    case HubState::Paused: {
      const ButtonSpec row[] = {{TouchAction::Resume, "Resume", 0, 3}, {TouchAction::OpenTable, "Actions", 0, 2}};
      addRow(screen, BUTTON_ROW_Y, row, 2);
      break;
    }
    case HubState::GameOver: {
      const ButtonSpec row[] = {{TouchAction::Rematch, "Rematch", 0, 3}, {TouchAction::ResetTable, "Reset", ASK, 2}};
      addRow(screen, BUTTON_ROW_Y, row, 2);
      break;
    }
    case HubState::Starting: {
      const ButtonSpec row[] = {{TouchAction::CancelStart, "Cancel start", 0, 1}};
      addRow(screen, BUTTON_ROW_Y, row, 1);
      break;
    }
  }
}

// The same destinations as the app and Sigil; actions remain role-specific.
void layoutButtons(AtlasScreen &screen, uint32_t nowMs) {
  layoutContentButtons(screen, nowMs);
  const auto kind = activeScreen(nowMs);
  if (kind == ScreenKind::Code || kind == ScreenKind::PairCode || kind == ScreenKind::Name ||
      kind == ScreenKind::Pin || kind == ScreenKind::Setup || kind == ScreenKind::Confirm) return;
  const TouchAction actions[] = {TouchAction::OpenHome, TouchAction::OpenGame,
      TouchAction::OpenOverview, TouchAction::OpenSettings};
  const uint8_t selected = kind == ScreenKind::Home || kind == ScreenKind::ManaTap ? 0 : kind == ScreenKind::Status || kind == ScreenKind::Player ||
      kind == ScreenKind::Table || kind == ScreenKind::Seat || kind == ScreenKind::NewGame ? 1
      : kind == ScreenKind::Overview ? 2 : 3;
  for (uint8_t i=0; i<TurnHubUi::PAGE_COUNT; ++i) {
    addKey(screen,actions[i],TurnHubUi::pageTitle(static_cast<TurnHubUi::Page>(i)),i*80,432,80,48,0);
    screen.buttons[screen.buttonCount-1].selected = i == selected;
  }
}

// The current layout's button for an action, or nullptr if it is gone.
const TouchButton *currentButton(TouchAction action, AtlasScreen &layout, uint32_t nowMs) {
  if (chipAction(action)) {
    static TouchButton chip;
    return chipZone(pressedSeat, action, nowMs, chip) ? &chip : nullptr;
  }
  layoutButtons(layout, nowMs);
  for (uint8_t i = 0; i < layout.buttonCount; ++i) {
    if (layout.buttons[i].action == action && layout.buttons[i].arg == pressedArg) return &layout.buttons[i];
  }
  return nullptr;
}

TouchAction buttonAt(int16_t x, int16_t y, uint32_t nowMs) {
  AtlasScreen layout;
  layoutButtons(layout, nowMs);
  for (uint8_t i = 0; i < layout.buttonCount; ++i) {
    if (!layout.buttons[i].contains(x, y)) continue;
    pressedArg = layout.buttons[i].arg;
    return layout.buttons[i].action;
  }
  pressedArg = 0;
  pressedSeat = PlayerSeat();
  return chipAt(x, y, nowMs, pressedSeat);
}

const char *actionName(TouchAction action) {
  switch (action) {
    case TouchAction::OpenHome: return "OPEN_HOME";
    case TouchAction::OpenGame: return "OPEN_GAME";
    case TouchAction::OpenOverview: return "OPEN_OVERVIEW";
    case TouchAction::OpenPlayerControls: return "OPEN_PLAYER_CONTROLS";
    case TouchAction::PlayerControlsBack: return "PLAYER_CONTROLS_BACK";
    case TouchAction::PlayerControlsPrev: return "PLAYER_CONTROLS_PREV";
    case TouchAction::PlayerControlsNext: return "PLAYER_CONTROLS_NEXT";
    case TouchAction::ControlMinus: return "CONTROL_MINUS";
    case TouchAction::ControlPlus: return "CONTROL_PLUS";
    case TouchAction::OpenSettings: return "OPEN_SETTINGS";
    case TouchAction::OverviewNext: return "OVERVIEW_NEXT";
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
    case TouchAction::OpenQr: return "OPEN_QR";
    case TouchAction::CloseScreen: return "CLOSE_SCREEN";
    case TouchAction::QrWifi: return "QR_WIFI";
    case TouchAction::QrPortal: return "QR_PORTAL";
    case TouchAction::QrSignIn: return "QR_SIGN_IN";
    case TouchAction::OpenPlayer: return "OPEN_PLAYER";
    case TouchAction::ChipLifeMinus: return "CHIP_LIFE_MINUS";
    case TouchAction::ChipLifePlus: return "CHIP_LIFE_PLUS";
    case TouchAction::PassTurn: return "PASS_TURN";
    case TouchAction::LifeMinus5: return "LIFE_MINUS_5";
    case TouchAction::LifeMinus1: return "LIFE_MINUS_1";
    case TouchAction::LifePlus1: return "LIFE_PLUS_1";
    case TouchAction::LifePlus5: return "LIFE_PLUS_5";
    case TouchAction::Concede: return "CONCEDE";
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
    case TouchAction::OpenThemes: return "OPEN_THEMES";
    case TouchAction::InvertDisplay: return "INVERT_DISPLAY";
    case TouchAction::UsbDrive: return "USB_DRIVE";
    case TouchAction::HomeWifi: return "HOME_WIFI";
    case TouchAction::ThemeGraphite: return "THEME_GRAPHITE";
    case TouchAction::ThemeDaylight: return "THEME_DAYLIGHT";
    case TouchAction::ThemeBrass: return "THEME_BRASS";
    case TouchAction::ThemeContrast: return "THEME_CONTRAST";
    case TouchAction::UnpairSigils: return "UNPAIR_SIGILS";
    case TouchAction::FactoryResetAtlas: return "FACTORY_RESET_ATLAS";
    case TouchAction::SleepAtlas: return "SLEEP_ATLAS";
    case TouchAction::OpenSeat: return "OPEN_SEAT";
    case TouchAction::SeatProfile: return "SEAT_PROFILE";
    case TouchAction::SeatMore: return "SEAT_MORE";
    case TouchAction::NewPlayer: return "NEW_PLAYER";
    case TouchAction::NameKey: return "NAME_KEY";
    case TouchAction::NameBack: return "NAME_BACK";
    case TouchAction::NameDone: return "NAME_DONE";
    case TouchAction::PinKey: return "PIN_KEY";
    case TouchAction::PinBack: return "PIN_BACK";
    case TouchAction::PinDone: return "PIN_DONE";
    case TouchAction::ConfirmYes: return "CONFIRM_YES";
    case TouchAction::ConfirmNo: return "CONFIRM_NO";
    case TouchAction::OpenNewGame: return "OPEN_NEW_GAME";
    case TouchAction::GameType: return "GAME_TYPE";
    case TouchAction::GameMode: return "GAME_MODE";
    case TouchAction::GameLife: return "GAME_LIFE";
    case TouchAction::GameTimer: return "GAME_TIMER";
    case TouchAction::GameHouseRules: return "GAME_HOUSE_RULES";
    case TouchAction::OpenManaTap: return "OPEN_MANA_TAP";
    case TouchAction::ManaAction: return "MANA_ACTION";
    case TouchAction::ManaQuit: return "MANA_QUIT";
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

// Fills seatRows with page seatPage of the saved profiles that could sit
// down: not archived and not at either table. Reads the store, so only on
// opening Add player, paging, and after a seat.
void loadSeatPage() {
  char ids[TurnHubProfiles::MAX_LOGIN_PROFILES][TurnHubProfiles::PROFILE_ID_LENGTH + 1];
  const size_t count = TurnHubProfiles::ready() ? TurnHubProfiles::listProfileIds(ids, TurnHubProfiles::MAX_LOGIN_PROFILES) : 0;
  uint8_t eligible = 0;
  seatRowCount = 0;
  for (size_t i = 0; i < count; ++i) {
    const String id(ids[i]);
    TurnHubAccounts::Account account;
    if (tableForProfile(id) >= 0 || (TurnHubAccounts::load(id, account) && account.archived)) continue;
    const uint8_t page = eligible++ / SEAT_PAGE_ROWS;
    if (page != seatPage) continue;
    SeatRow &row = seatRows[seatRowCount++];
    snprintf(row.profileId, sizeof(row.profileId), "%s", ids[i]);
    row.needsPin = !TurnHubWebApi::physicalUseAllowed(id);
    char name[SCREEN_NAME_LENGTH + 1];
    snprintf(name, sizeof(name), "%s", TurnHubProfiles::nameForProfile(id).c_str());
    snprintf(row.label, sizeof(row.label), row.needsPin ? "%s (PIN)" : "%s", name);
  }
  seatPages = (eligible + SEAT_PAGE_ROWS - 1) / SEAT_PAGE_ROWS;
  if (seatPage > 0 && seatPage >= seatPages) {
    seatPage = 0;
    loadSeatPage();
  }
}

void openSeatScreen(uint8_t page) {
  seatPage = page;
  loadSeatPage();
  openScreen = ScreenKind::Seat;
}

// A name key: capitals start each word, the rest are lower case; no leading
// or doubled spaces.
void typeNameKey(uint8_t index) {
  if (index >= NAME_KEY_COUNT) return;
  const size_t length = strlen(typedName);
  if (length >= SCREEN_NAME_LENGTH) return;
  char c = NAME_KEYS[index];
  const char before = length ? typedName[length - 1] : ' ';
  if (c == ' ' && before == ' ') return;
  if (c >= 'A' && c <= 'Z' && before != ' ' && before != '-') c = static_cast<char>(c - 'A' + 'a');
  typedName[length] = c;
  typedName[length + 1] = '\0';
}

// Screen changes: no Intent, no table state, no notice. Back leaves the
// Settings pages (QR codes, Theme, Device) for Settings, and everything else
// for the Game page.
bool navigate(TouchAction action) {
  switch (action) {
    case TouchAction::OpenPlayerControls: playerControlsOpen=true; playerControlsPage=0; return true;
    case TouchAction::PlayerControlsBack: playerControlsOpen=false; return true;
    case TouchAction::PlayerControlsPrev: if (playerControlsPage) --playerControlsPage; return true;
    case TouchAction::PlayerControlsNext: if (playerControlsPage<3) ++playerControlsPage; return true;
    case TouchAction::OpenHome: openScreen = ScreenKind::Home; return true;
    case TouchAction::OpenGame: openScreen = ScreenKind::Status; return true;
    case TouchAction::OpenOverview: openScreen = ScreenKind::Overview; return true;
    case TouchAction::OpenSettings: openScreen = ScreenKind::Menu; return true;
    case TouchAction::OverviewNext: overviewPage = overviewPage ? 0 : 1; return true;
    case TouchAction::OpenNewGame: openScreen = ScreenKind::NewGame; return true;
    case TouchAction::ConfirmNo: confirmAction = TouchAction::None; return true;
    case TouchAction::OpenQr: openScreen = ScreenKind::Qr; return true;
    case TouchAction::OpenTable: openScreen = ScreenKind::Table; return true;
    case TouchAction::OpenThemes: openScreen = ScreenKind::Themes; return true;
    case TouchAction::UsbDrive: {
      const bool share = !sdCardUsbShareWanted();
      if (!shareSdCardOverUsb(share)) showNotice(millis(), "Insert a working SD card first");
      else showNotice(millis(), share ? "Sharing SD card over USB" : "SD card back on Atlas");
      serialLog.print("ATLAS|TOUCH|USB_DRIVE|");
      serialLog.println(share ? "ON" : "OFF");
      return true;
    }
    case TouchAction::HomeWifi: {
      // The network name and password are typed in the portal or the app.
      const bool on = !homeWifiEnabled();
      if (on && !homeWifiConfigured()) showNotice(millis(), "Add your home network in the app first");
      else if (!setHomeWifiEnabled(on)) showNotice(millis(), "Home Wi-Fi could not be saved");
      else showNotice(millis(), on ? "Home Wi-Fi on: checks between games" : "Home Wi-Fi off");
      serialLog.print("ATLAS|TOUCH|HOME_WIFI|");
      serialLog.println(on ? "ON" : "OFF");
      return true;
    }
    case TouchAction::InvertDisplay:
      if (!chooseAtlasDisplayInverted(!atlasDisplayInverted())) showNotice(millis(), "Inversion could not be saved");
      return true;
    case TouchAction::ThemeGraphite:
    case TouchAction::ThemeDaylight:
    case TouchAction::ThemeBrass:
    case TouchAction::ThemeContrast: {
      const auto id = static_cast<TurnHubTheme::Id>(static_cast<uint8_t>(action) - static_cast<uint8_t>(TouchAction::ThemeGraphite));
      const bool saved = chooseAtlasDisplayTheme(id);
      if (!saved) showNotice(millis(), "Theme could not be saved");
      return true;
    }
    case TouchAction::OpenDevice: openScreen = ScreenKind::Device; return true;
    case TouchAction::OpenPlayer:
      openScreen = ScreenKind::Player;
      shownSeat = pressedSeat;
      playerControlsOpen=false;
      return true;
    case TouchAction::OpenSeat: openSeatScreen(0); return true;
    case TouchAction::SeatMore: openSeatScreen(seatPages ? (seatPage + 1) % seatPages : 0); return true;
    case TouchAction::NewPlayer:
      typedName[0] = '\0';
      openScreen = ScreenKind::Name;
      return true;
    case TouchAction::NameKey: typeNameKey(pressedArg); return true;
    case TouchAction::NameBack: {
      const size_t length = strlen(typedName);
      if (length) typedName[length - 1] = '\0';
      return true;
    }
    case TouchAction::PinKey: {
      const size_t length = strlen(typedPin);
      if (length < TYPED_PIN_LENGTH && pressedArg <= 9) {
        typedPin[length] = static_cast<char>('0' + pressedArg);
        typedPin[length + 1] = '\0';
      }
      return true;
    }
    case TouchAction::PinBack: {
      const size_t length = strlen(typedPin);
      if (length) typedPin[length - 1] = '\0';
      return true;
    }
    case TouchAction::CloseScreen: {
      // The keyboard and keypad go back to the list; the list to the table.
      if (openScreen == ScreenKind::Name || openScreen == ScreenKind::Pin) {
        typedPin[0] = '\0';
        openSeatScreen(seatPage);
        return true;
      }
      const bool fromMenuScreen = openScreen == ScreenKind::Qr || openScreen == ScreenKind::Device ||
          openScreen == ScreenKind::Themes;
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

// The next value of one New game row, in the app's order; nullptr when it
// stepped, or why it cannot. Atlas's handler applies official rules after.
const char *stepGameSetting(TouchAction action, TurnHub::GameSettings &next) {
  using TurnHub::GameProfile;
  switch (action) {
    // Commander, Standard, Yu-Gi-Oh!, Other: a fresh game of the next one.
    case TouchAction::GameType: {
      const GameProfile now = TurnHub::commanderRules(next.profile) ? GameProfile::Commander : next.profile;
      next.profile = now == GameProfile::Commander ? GameProfile::Magic : now == GameProfile::Magic ? GameProfile::Yugioh
          : now == GameProfile::Yugioh ? GameProfile::Generic : GameProfile::Commander;
      next.twoHeadedGiant = false;
      next.houseRules = false;
      if (next.profile == GameProfile::Generic) next.startingLife = 20;
      return nullptr;
    }
    // Magic: Free-for-all and Two-Headed Giant. Commander adds Bag of Doom
    // and Star Commander (their own profiles).
    case TouchAction::GameMode:
      if (next.profile == GameProfile::Magic) {
        next.twoHeadedGiant = !next.twoHeadedGiant;
        return nullptr;
      }
      if (!TurnHub::commanderRules(next.profile)) return "This game has one mode";
      if (next.profile == GameProfile::Commander && !next.twoHeadedGiant) {
        next.twoHeadedGiant = true;
        next.startingLife = TurnHub::TEAM_LIFE_COMMANDER;
      } else if (next.profile == GameProfile::Commander) {
        next.twoHeadedGiant = false;
        next.profile = GameProfile::DoomCommander;
        next.startingLife = 40;
      } else if (next.profile == GameProfile::DoomCommander) {
        next.profile = GameProfile::StarCommander;
        next.startingLife = TurnHub::STAR_DEFAULT_LIFE;
      } else {
        next.profile = GameProfile::Commander;
      }
      return nullptr;
    // Official games keep their life unless House rules are on.
    case TouchAction::GameLife: {
      TurnHub::OfficialRules rules{};
      if (TurnHub::lockedRules(next, rules)) return "Official rules set it: turn on House rules";
      static const int32_t CARDS[] = {4000, 8000, 16000};
      static const int32_t LIFE[] = {20, 25, 30, 40, 60};
      const bool yugioh = next.profile == GameProfile::Yugioh;
      const int32_t *presets = yugioh ? CARDS : LIFE;
      const uint8_t count = yugioh ? 3 : 5;
      int32_t chosen = presets[0];
      for (uint8_t i = 0; i < count; ++i) {
        if (presets[i] > next.startingLife) { chosen = presets[i]; break; }
      }
      next.startingLife = chosen;
      return nullptr;
    }
    case TouchAction::GameTimer: {
      uint8_t i = 0;
      while (i < TurnHub::TURN_TIMER_PRESET_COUNT && TurnHub::TURN_TIMER_PRESETS_MS[i] <= next.turnTimerMs) ++i;
      next.turnTimerMs = TurnHub::TURN_TIMER_PRESETS_MS[i < TurnHub::TURN_TIMER_PRESET_COUNT ? i : 0];
      return nullptr;
    }
    case TouchAction::GameHouseRules:
      next.houseRules = !next.houseRules;
      return nullptr;
    default:
      return "Unknown setting";
  }
}

// Adapter: turns one touch button into its Intent. Pause and Resume act for
// the active seat; the rest act for the table (origin only, no seat).
// Canceling a presence code changes no table state.
void dispatchTouchAction(uint32_t nowMs, TouchAction action) {
  // The popup's Confirm: the action it asked about, if the screen underneath
  // still offers it (the table may have moved on).
  if (action == TouchAction::ConfirmYes) {
    const TouchAction asked = confirmAction;
    confirmAction = TouchAction::None;
    AtlasScreen layout;
    pressedArg = 0;
    serialLog.println("ATLAS|TOUCH|CONFIRM_YES");
    if (asked == TouchAction::None || currentButton(asked, layout, nowMs) == nullptr) {
      showNotice(nowMs, "That is no longer available");
      return;
    }
    dispatchTouchAction(nowMs, asked);
    return;
  }
  if (action == TouchAction::OpenManaTap || action == TouchAction::ManaAction || action == TouchAction::ManaQuit) {
    if (action == TouchAction::OpenManaTap) {
      openManaTap(nowMs);
    } else if (action == TouchAction::ManaQuit) {
      keepManaBest();
      manaTap = TurnHub::ManaTap();
      openScreen = ScreenKind::Home;
    } else if (manaTap.phase() == TurnHub::ManaPhase::Playing) {
      manaTap.pause();
      keepManaBest();
    } else if (manaTap.phase() == TurnHub::ManaPhase::Paused) {
      manaTap.resume(nowMs);
    } else {
      manaTap.start(nowMs);
    }
    if (action != TouchAction::OpenManaTap) manaTurnPaused = false;
    serialLog.print("ATLAS|TOUCH|");
    serialLog.println(actionName(action));
    return;
  }
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
    // Hold to pass: the active player's own pass, as the app's shared table
    // and their Sigil send it (Atlas keeps the usual grace period).
    case TouchAction::ControlMinus:
    case TouchAction::ControlPlus: {
      PlayerSeat seat;
      if (!currentShownSeat(seat) || pressedArg>=TurnHubUi::PLAYER_CONTROL_COUNT ||
          !(playerControlsFor(seat.playerNumber).allowed & (1u<<pressedArg))) return;
      const int8_t value=action==TouchAction::ControlPlus ? 1 : TurnHubUi::playerControlToggle(pressedArg) ? 0 : -1;
      result=intents.dispatch(playerControlIntent(seat,IntentOrigin::AtlasHardware,pressedArg,value));
      break;
    }
    case TouchAction::PassTurn: {
      const PlayerSeat *active = table().game.activePlayer();
      result = active == nullptr ? IntentResult::reject(IntentStatus::InvalidState, "There is no active player")
          : dispatchSeatIntent(IntentType::Pass, IntentOrigin::AtlasHardware, *active);
      break;
    }
    // A tap on a tile's lower half: one life down or up for that player, the
    // small step their own controls use.
    case TouchAction::ChipLifeMinus:
    case TouchAction::ChipLifePlus: {
      TouchButton zone;
      if (!chipZone(pressedSeat, action, nowMs, zone)) {
        result = IntentResult::reject(IntentStatus::InvalidActor, "That player is not in this game");
        break;
      }
      Intent intent;
      intent.type = IntentType::ChangeLife;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      intent.actor.controllerId = pressedSeat.controllerId;
      intent.actor.slot = pressedSeat.slot;
      intent.actor.playerNumber = pressedSeat.playerNumber;
      intent.payload.targetPlayer = pressedSeat.playerNumber;
      const int32_t step = table().game.settings().profile == TurnHub::GameProfile::Yugioh ? 100 : 1;
      intent.payload.value = action == TouchAction::ChipLifeMinus ? -step : step;
      result = intents.dispatch(intent);
      break;
    }
    case TouchAction::Pause:
    case TouchAction::Resume: {
      const PlayerSeat *active = table().game.activePlayer();
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
    case TouchAction::Concede: {
      PlayerSeat seat;
      if (!currentShownSeat(seat) || table().hubState == HubState::Lobby) {
        result = IntentResult::reject(IntentStatus::InvalidActor, "That player is not in this game");
        break;
      }
      Intent intent;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      intent.actor.controllerId = seat.controllerId;
      intent.actor.slot = seat.slot;
      intent.actor.playerNumber = seat.playerNumber;
      if (action == TouchAction::Concede) {
        intent.type = IntentType::Concede;
      } else {
        intent.type = IntentType::ChangeLife;
        intent.payload.targetPlayer = seat.playerNumber;
        // The small and big steps (LifeMinus1 / LifeMinus5 name the usual ones).
        const int32_t step = table().game.settings().profile == TurnHub::GameProfile::Yugioh ? 100 : 1;
        const int32_t bigStep = step == 100 ? 1000 : 5;
        intent.payload.value = action == TouchAction::LifeMinus5 ? -bigStep
            : action == TouchAction::LifeMinus1 ? -step : action == TouchAction::LifePlus1 ? step : bigStep;
      }
      result = intents.dispatch(intent);
      if (action == TouchAction::Concede && result.accepted()) openScreen = ScreenKind::Status;
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
    // Remove on a lobby Player screen (confirmed): that seat leaves the lobby.
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
    // Add player: a saved profile sits down (its PIN first when it needs
    // one, as on the tablet), or New player makes a profile with no PIN
    // and seats it. The JoinProfile handler decides; this screen stays up
    // for the next player.
    case TouchAction::SeatProfile:
    case TouchAction::PinDone:
    case TouchAction::NameDone: {
      String profile;
      if (action == TouchAction::SeatProfile) {
        if (pressedArg >= seatRowCount) {
          result = IntentResult::reject(IntentStatus::InvalidActor, "Choose a player");
          break;
        }
        if (seatRows[pressedArg].needsPin) {
          pinRow = pressedArg;
          typedPin[0] = '\0';
          openScreen = ScreenKind::Pin;
          return;
        }
        profile = seatRows[pressedArg].profileId;
      } else if (action == TouchAction::PinDone) {
        String message;
        profile = seatRows[pinRow].profileId;
        const bool accepted = TurnHubWebApi::checkTablePin(profile, String(typedPin), message);
        typedPin[0] = '\0';
        if (!accepted) {
          // The result keeps a pointer to its message.
          static char refused[sizeof(noticeText)];
          snprintf(refused, sizeof(refused), "%s", message.c_str());
          result = IntentResult::reject(IntentStatus::Unauthorized, refused);
          break;
        }
      } else {
        String name(typedName);
        name.trim();
        if (name.length() == 0) {
          result = IntentResult::reject(IntentStatus::InvalidActor, "Type the player's name");
          break;
        }
        if (TurnHubWebApi::profileNameTaken(name)) {
          result = IntentResult::reject(IntentStatus::Conflict, "That name has a profile; pick it");
          break;
        }
        profile = TurnHubProfiles::ready() ? TurnHubProfiles::createProfileWithName(name) : String();
        if (profile.length() == 0) {
          result = IntentResult::reject(IntentStatus::Conflict, "Could not save the profile");
          break;
        }
        serialLog.print("ATLAS|TOUCH|NEW_PROFILE|");
        serialLog.println(profile);
      }
      Intent intent;
      intent.type = IntentType::JoinProfile;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      snprintf(intent.payload.profileId, sizeof(intent.payload.profileId), "%s", profile.c_str());
      result = intents.dispatch(intent);
      if (result.accepted()) openSeatScreen(seatPage);
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
    // Settings > Device, confirmed: the BOOT button's Unpair and Factory reset, as the
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
    // New game: one row steps one setting; Atlas validates and saves it.
    case TouchAction::GameType:
    case TouchAction::GameMode:
    case TouchAction::GameLife:
    case TouchAction::GameTimer:
    case TouchAction::GameHouseRules: {
      TurnHub::GameSettings next = table().nextGameSettings;
      const char *refused = stepGameSetting(action, next);
      if (refused != nullptr) {
        result = IntentResult::reject(IntentStatus::Rejected, refused);
        break;
      }
      Intent intent;
      intent.type = IntentType::ConfigureGame;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      intent.payload.flags = static_cast<uint32_t>(next.profile) |
          (next.twoHeadedGiant ? TurnHub::GAME_FLAG_TWO_HEADED_GIANT : 0) |
          (next.autoEliminateAtZeroLife ? 0 : TurnHub::GAME_FLAG_MANUAL_ELIMINATION) |
          (next.autoEliminateAtPoisonLimit ? 0 : TurnHub::GAME_FLAG_MANUAL_POISON) |
          (next.autoEliminateAtCommanderDamage ? 0 : TurnHub::GAME_FLAG_MANUAL_COMMANDER) |
          (next.houseRules ? TurnHub::GAME_FLAG_HOUSE_RULES : 0);
      intent.payload.value = next.startingLife;
      intent.payload.poisonLimit = next.poisonLimit;
      intent.payload.durationMs = next.turnTimerMs;
      result = intents.dispatch(intent);
      // The row shows the new value; only a refusal needs words.
      if (result.accepted()) {
        serialLog.print("ATLAS|TOUCH|");
        serialLog.print(actionName(action));
        serialLog.println("|ACCEPTED");
        return;
      }
      break;
    }
    case TouchAction::SleepAtlas: {
      Intent intent;
      intent.type = IntentType::Sleep;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      result = intents.dispatch(intent);
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
  PlayerSeat seats[MAX_PLAYERS];
  uint8_t count = 0;
  const bool inGame = table().game.hasPlayers() && table().hubState != HubState::Lobby;
  if (inGame) {
    for (uint8_t i = 0; i < table().game.playerCount() && count < MAX_PLAYERS; ++i) {
      const PlayerSeat *seat = table().game.playerAt(i);
      if (seat != nullptr) seats[count++] = *seat;
    }
  } else {
    count = table().lobby.buildPlayers(seats, MAX_PLAYERS);
  }
  PlayerSeat starter;
  const bool hasStarter = !inGame && table().lobby.selectedStarter(starter);
  const uint8_t waitingOn = table().game.hasWinClaim() ? table().game.nextWinConfirmationPlayerNumber() : 0;
  screen.showLife = inGame;
  const uint8_t first = screen.kind == ScreenKind::Overview && overviewPage && count > MAX_SCREEN_PLAYERS ? MAX_SCREEN_PLAYERS : 0;
  for (uint8_t i = first; i < count && screen.playerCount < MAX_SCREEN_PLAYERS; ++i) {
    const PlayerSeat &seat = seats[i];
    ScreenPlayer &p = screen.players[screen.playerCount++];
    p.number = seat.playerNumber;
    // Phone-joined players included (they showed as "Player N" in the lobby).
    const String profile = profileIdForTableSeat(seat, inGame);
    playerName(seat, profile, nowMs, p.name);
    // Current Atlas uses a tiny approved thumbnail; future LCD layouts use the JPEG master.
    const uint8_t avatar = TurnHubProfiles::avatarForProfile(
        profile.length() ? profile : TurnHubControllers::profileForSeat(seat.controllerId, seat.slot));
    if (TurnHubAvatars::validPresetAvatar(avatar)) p.avatar = avatar;
    if (avatar == TurnHubAvatars::AVATAR_CUSTOM) {
      p.avatarPixels = TurnHubProfiles::artworkThumbnail(profile, p.imageRevision);
      if (p.avatarPixels) p.avatar = avatar;
    }
    if (inGame) {
      p.life = table().game.lifeTotal(seat.playerNumber);
      uint32_t turnMs = 0;
      if (const TurnHub::PlayerStats *stats = table().game.statsForPlayer(seat.playerNumber)) turnMs = stats->totalTurnMs;
      // Two-Headed Giant: both teammates hold the turn.
      const bool hasTurn = table().hubState != HubState::GameOver && table().game.hasTurn(seat.playerNumber) &&
          !table().game.isEliminated(seat.playerNumber);
      if (hasTurn) {
        turnMs += table().game.currentTurnElapsedMs(nowMs);
      }
      formatClock(p.turnTime, sizeof(p.turnTime), turnMs);
      if (hasTurn) p.flags |= CHIP_ACTIVE;
      if (table().game.isEliminated(seat.playerNumber)) p.flags |= CHIP_OUT;
      if (table().hubState == HubState::GameOver && table().game.isWinner(seat.playerNumber)) p.flags |= CHIP_WINNER;
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
  const uint8_t team = table().game.teamOf(number);
  if (team) snprintf(out, size, "Team %u%s", static_cast<unsigned>(team), suffix);
  else snprintf(out, size, "%s%s", nameOfPlayer(screen, number), suffix);
}

// Two-Headed Giant: the starting team skips the draw of its first turn.
bool startingTeamFirstTurn() {
  if (!table().game.twoHeadedGiant() || !table().game.hasTurn(table().game.starterPlayerNumber())) return false;
  const TurnHub::PlayerStats *stats = table().game.statsForPlayer(table().game.starterPlayerNumber());
  return stats != nullptr && stats->turnsCompleted == 0;
}

// The round: each living player's completed turns, where the starter's turn
// opens a new round. The highest count among the living is the rounds
// finished by the players furthest along; if the active player is one of
// them, their turn opens the next round.
void formatTurnClock(AtlasScreen &screen, uint32_t nowMs) {
  const bool over = table().hubState == HubState::GameOver && table().game.hasPlayers();
  if (table().hubState != HubState::Running && table().hubState != HubState::Paused && !over) return;
  // A finished game keeps its final round and length in the header.
  screen.round = table().game.currentRound();
  formatClock(screen.gameClock, sizeof(screen.gameClock), table().game.gameElapsedMs(nowMs));
  if (over) return;
  if (table().game.turnTimerMs() > 0) {
    const uint32_t left = table().game.turnRemainingMs(nowMs);
    screen.timerPermille = static_cast<int16_t>(static_cast<uint64_t>(left) * 1000 / table().game.turnTimerMs());
    const TurnHub::TurnTimerPhase phase = table().game.turnTimerPhase(nowMs);
    screen.timerWarning = phase == TurnHub::TurnTimerPhase::Warning ||
        phase == TurnHub::TurnTimerPhase::Expired;
    formatClock(screen.clock, sizeof(screen.clock), left);
  } else {
    formatClock(screen.clock, sizeof(screen.clock), table().game.currentTurnElapsedMs(nowMs));
  }
}

void formatStatus(AtlasScreen &screen, uint32_t nowMs) {
  addPlayers(screen, nowMs);
  formatTurnClock(screen, nowMs);
  const uint8_t players = table().game.hasPlayers() ? table().game.playerCount() : table().lobby.playerCount();
  switch (table().hubState) {
    case HubState::Lobby: {
      snprintf(screen.badge, sizeof(screen.badge), "LOBBY");
      snprintf(screen.title, sizeof(screen.title), "Lobby");
      const uint32_t pairingMs = pairingRemainingMs(nowMs);
      if (pairingMs > 0) {
        snprintf(screen.detail, sizeof(screen.detail), "Pairing open: %lu s left",
            static_cast<unsigned long>((pairingMs + 999) / 1000));
      } else if (table().nextGameSettings.twoHeadedGiant) {
        snprintf(screen.detail, sizeof(screen.detail), "%u %s, Two-Headed Giant",
            static_cast<unsigned>(players), players == 1 ? "player" : "players");
      } else {
        snprintf(screen.detail, sizeof(screen.detail), "%u %s, %u %s",
            static_cast<unsigned>(players), players == 1 ? "player" : "players",
            static_cast<unsigned>(screen.sigilsOnline), screen.sigilsOnline == 1 ? "Sigil" : "Sigils");
      }
      if (screen.playerCount == 0) {
        // An empty table: how to join. The portal's QR code is under Settings.
        snprintf(screen.lines[0], sizeof(screen.lines[0]), "Tap Add player to seat someone,");
        snprintf(screen.lines[1], sizeof(screen.lines[1]), "or join from the app or a Sigil.");
        snprintf(screen.lines[2], sizeof(screen.lines[2]), "Settings: pair Sigils, QR codes.");
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
      formatSide(screen.title, sizeof(screen.title), screen, table().game.activePlayerNumber(), "'s turn");
      if (table().pendingPass.active) {
        // Count down the grace period so the table sees the pass is still
        // cancelable, and for how long (turntest, 2026-09-26).
        const uint32_t elapsed = millis() - table().pendingPass.requestedAtMs;
        const uint32_t leftMs = elapsed < PASS_GRACE_MS ? PASS_GRACE_MS - elapsed : 0;
        snprintf(screen.detail, sizeof(screen.detail), "Passing in %lus: that seat can cancel",
            static_cast<unsigned long>((leftMs + 999) / 1000));
      } else if (startingTeamFirstTurn()) {
        snprintf(screen.detail, sizeof(screen.detail), "Starting team: skip your first draw");
      } else if (table().game.turnTimerMs() > 0) {
        snprintf(screen.detail, sizeof(screen.detail), "Turn time left %s", screen.clock);
      } else {
        snprintf(screen.detail, sizeof(screen.detail), "Turn time %s", screen.clock);
      }
      break;
    }
    case HubState::Paused:
      snprintf(screen.badge, sizeof(screen.badge), "PAUSED");
      snprintf(screen.title, sizeof(screen.title), "Paused");
      if (table().game.hasWinClaim()) {
        snprintf(screen.detail, sizeof(screen.detail), "Win claim: waiting on %s",
            nameOfPlayer(screen, table().game.nextWinConfirmationPlayerNumber()));
      } else if (table().eliminationTargetPlayer != 0) {
        snprintf(screen.detail, sizeof(screen.detail), "Eliminate %s? Their Sigil decides",
            nameOfPlayer(screen, table().eliminationTargetPlayer));
      } else {
        formatSide(screen.detail, sizeof(screen.detail), screen, table().game.activePlayerNumber(), "'s turn");
      }
      break;
    case HubState::GameOver:
      snprintf(screen.badge, sizeof(screen.badge), "GAME OVER");
      snprintf(screen.title, sizeof(screen.title), "Game over");
      if (table().game.endedInDraw()) {
        snprintf(screen.detail, sizeof(screen.detail), "The match ended in a draw");
      } else {
        uint8_t winners[MAX_PLAYERS];
        const uint8_t count = table().game.winnerPlayers(winners, MAX_PLAYERS);
        if (count == 2) {
          // Star Commander: a shared victory names both winners.
          char first[24] = {};
          snprintf(first, sizeof(first), "%s", nameOfPlayer(screen, winners[0]));
          snprintf(screen.detail, sizeof(screen.detail), "%s and %s win", first, nameOfPlayer(screen, winners[1]));
        } else if (count > 2) {
          snprintf(screen.detail, sizeof(screen.detail), "%u players share the victory", static_cast<unsigned>(count));
        } else {
          formatSide(screen.detail, sizeof(screen.detail), screen, table().game.winnerPlayerNumber(), " wins");
        }
      }
      break;
  }
  // Sigil updates run between games; their progress and outcome, in words,
  // take the detail line so a failure is never silent at the table.
  if (table().hubState == HubState::Lobby || table().hubState == HubState::GameOver) {
    char update[sizeof(screen.detail)];
    if (sigilUpdateNotice(update, sizeof(update), nowMs)) {
      snprintf(screen.detail, sizeof(screen.detail), "%s", update);
    }
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
  snprintf(screen.title, sizeof(screen.title), "Game actions");
  if (table().hubState != HubState::Running) {
    snprintf(screen.detail, sizeof(screen.detail), "Resume to use Master pass");
    return;
  }
  if (table().game.hasWinClaim() || table().eliminationTargetPlayer != 0) {
    snprintf(screen.detail, sizeof(screen.detail), "Finish the table decision first");
    return;
  }
  const PlayerSeat *active = table().game.activePlayer();
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
  snprintf(screen.badge, sizeof(screen.badge), "SETTINGS");
  snprintf(screen.title, sizeof(screen.title), "Settings");
  const TurnHub::UpdateKind update = firmwareUpdateKind();
  if (update != TurnHub::UpdateKind::None) {
    formatUpdateLine(screen.detail, sizeof(screen.detail), update);
    return;
  }
  snprintf(screen.detail, sizeof(screen.detail), "%s",
      table().hubState == HubState::Lobby ? "Pair Sigils, share codes, the look" : "Share codes, the look");
}

void formatDevice(AtlasScreen &screen) {
  snprintf(screen.badge, sizeof(screen.badge), "DEVICE");
  snprintf(screen.title, sizeof(screen.title), "Atlas device");
  snprintf(screen.detail, sizeof(screen.detail), "TurnHub Atlas v%s", TurnHubFirmware::VERSION);
}

// Home: the table at a glance, like the app's home card. The status card
// says what the table is doing in a headline and a line (lines 0-1) over the
// players' tokens; three tiles count players and Sigils and name the game
// (lines 2-3). Nothing here ticks: the turn clock lives on the Game tab.
// Battery, SD and updates are in the header, and the network on QR codes.
void formatHome(AtlasScreen &screen, uint32_t nowMs) {
  formatStatus(screen, nowMs);
  // Status lines and clocks are the Game tab's; Home shows no countdowns.
  char headline[sizeof(screen.title)], line[sizeof(screen.detail)];
  snprintf(headline, sizeof(headline), "%s", screen.title);
  snprintf(line, sizeof(line), "%s", screen.detail);
  screen.clock[0] = '\0';
  screen.timerPermille = -1;
  screen.timerWarning = false;
  for (uint8_t i = 0; i < screen.playerCount; ++i) screen.players[i].turnTime[0] = '\0';
  const bool lobby = table().hubState == HubState::Lobby;
  const TurnHub::GameSettings &settings = lobby ? table().nextGameSettings : table().game.settings();
  snprintf(screen.title, sizeof(screen.title), "TurnHub");
  snprintf(screen.detail, sizeof(screen.detail), "Your table, Game 1");
  switch (table().hubState) {
    case HubState::Lobby:
      if (screen.playerCount == 0) {
        snprintf(screen.lines[0], sizeof(screen.lines[0]), "Ready for players");
        snprintf(screen.lines[1], sizeof(screen.lines[1]), "Join from the app or a Sigil");
      } else {
        snprintf(screen.lines[0], sizeof(screen.lines[0]), "%u %s at the table", static_cast<unsigned>(screen.playerCount),
            screen.playerCount == 1 ? "player" : "players");
        snprintf(screen.lines[1], sizeof(screen.lines[1]), "%s", screen.playerCount < 2 ? "Start needs two players"
            : "Start a game from the Game tab");
      }
      break;
    case HubState::Running:
      snprintf(screen.lines[0], sizeof(screen.lines[0]), "%s", headline);
      if (screen.round) snprintf(screen.lines[1], sizeof(screen.lines[1]), "Round %u", static_cast<unsigned>(screen.round));
      else snprintf(screen.lines[1], sizeof(screen.lines[1]), "Match in progress");
      break;
    case HubState::GameOver:
      snprintf(screen.lines[0], sizeof(screen.lines[0]), "%s", line);
      snprintf(screen.lines[1], sizeof(screen.lines[1]), "Rematch or Reset on the Game tab");
      break;
    default:  // Starting and Paused say it in their own words.
      snprintf(screen.lines[0], sizeof(screen.lines[0]), "%s", headline);
      snprintf(screen.lines[1], sizeof(screen.lines[1]), "%s", line);
      break;
  }
  snprintf(screen.lines[2], sizeof(screen.lines[2]), "%s", gameName(settings));
  snprintf(screen.lines[3], sizeof(screen.lines[3]), "%ld life", static_cast<long>(settings.startingLife));
  screen.lineCount = 4;
}

void formatManaTap(AtlasScreen &screen) {
  const uint16_t best = manaTap.best();
  snprintf(screen.badge, sizeof(screen.badge), "PLAY");
  snprintf(screen.title, sizeof(screen.title), "Mana Tap");
  switch (manaTap.phase()) {
    case TurnHub::ManaPhase::Ready:
      snprintf(screen.detail, sizeof(screen.detail), "Tap orbs marked A, let the rest float");
      break;
    case TurnHub::ManaPhase::Playing:
      snprintf(screen.detail, sizeof(screen.detail), "Tap the orbs marked A");
      break;
    case TurnHub::ManaPhase::Paused:
      snprintf(screen.detail, sizeof(screen.detail), "%s", manaTurnPaused ? "Paused: the turn passed" : "Paused");
      break;
    case TurnHub::ManaPhase::Over:
      if (manaTap.newBest()) snprintf(screen.detail, sizeof(screen.detail), "Game over. New best: %u", best);
      else snprintf(screen.detail, sizeof(screen.detail), "Game over. Best %u", best);
      break;
  }
  screen.mana = manaTap;
  screen.manaTurnPaused = manaTurnPaused;
}

// Runs Mana Tap: pauses it when its screen is not up, or when a turn passes
// in this table's match (and shows the Game tab), otherwise moves the orbs.
void serviceManaTap(uint32_t nowMs) {
  const uint8_t active = table().hubState == HubState::Running ? table().game.activePlayerNumber() : 0;
  const bool passed = active != 0 && manaTurnPlayer != 0 && active != manaTurnPlayer;
  manaTurnPlayer = active;
  if (manaTap.phase() != TurnHub::ManaPhase::Playing) return;
  if (passed || activeScreen(nowMs) != ScreenKind::ManaTap) {
    manaTap.pause();
    keepManaBest();
    manaTurnPaused = passed;
    if (passed && openScreen == ScreenKind::ManaTap) openScreen = ScreenKind::Status;
    return;
  }
  manaTap.update(nowMs);
  if (manaTap.phase() == TurnHub::ManaPhase::Over) keepManaBest();
}

// The confirm popup: what the action will do, in words.
void formatConfirm(AtlasScreen &screen, uint32_t nowMs) {
  snprintf(screen.badge, sizeof(screen.badge), "CONFIRM");
  char name[SCREEN_NAME_LENGTH + 1] = "this player";
  PlayerSeat seat;
  if (currentShownSeat(seat)) playerName(seat, profileIdForTableSeat(seat, table().hubState != HubState::Lobby), nowMs, name);
  const PlayerSeat *active = table().game.activePlayer();
  char turn[SCREEN_NAME_LENGTH + 1] = "";
  if (active != nullptr && matchInProgress()) playerName(*active, String(active->profileId), nowMs, turn);
  switch (confirmAction) {
    case TouchAction::EndMatch:
      snprintf(screen.title, sizeof(screen.title), "End the match?");
      snprintf(screen.detail, sizeof(screen.detail), "It ends for everyone as a draw.");
      break;
    case TouchAction::MasterPass:
      if (turn[0]) snprintf(screen.title, sizeof(screen.title), "Pass for %s?", turn);
      else snprintf(screen.title, sizeof(screen.title), "Pass this turn?");
      snprintf(screen.detail, sizeof(screen.detail), "For a stuck turn: it passes now.");
      break;
    case TouchAction::ClearLobby:
      snprintf(screen.title, sizeof(screen.title), "Clear the lobby?");
      snprintf(screen.detail, sizeof(screen.detail), "Everyone leaves the table.");
      break;
    case TouchAction::ResetTable:
      snprintf(screen.title, sizeof(screen.title), "Reset the table?");
      snprintf(screen.detail, sizeof(screen.detail), "Everyone leaves for an empty lobby.");
      break;
    case TouchAction::RemoveSeat:
      snprintf(screen.title, sizeof(screen.title), "Remove %s?", name);
      snprintf(screen.detail, sizeof(screen.detail), "They leave the lobby.");
      break;
    case TouchAction::Concede:
      snprintf(screen.title, sizeof(screen.title), "Concede for %s?", name);
      snprintf(screen.detail, sizeof(screen.detail), "Their game ends.");
      break;
    case TouchAction::UnpairSigils:
      snprintf(screen.title, sizeof(screen.title), "Unpair every Sigil?");
      snprintf(screen.detail, sizeof(screen.detail), "Each one must pair again to play.");
      break;
    case TouchAction::FactoryResetAtlas:
      snprintf(screen.title, sizeof(screen.title), "Erase this Atlas?");
      snprintf(screen.detail, sizeof(screen.detail), "Profiles, stats and settings are deleted.");
      break;
    default:
      snprintf(screen.title, sizeof(screen.title), "Are you sure?");
      break;
  }
}

// New game: the next match's settings; the rows carry the values.
void formatNewGame(AtlasScreen &screen) {
  snprintf(screen.badge, sizeof(screen.badge), "NEW GAME");
  snprintf(screen.title, sizeof(screen.title), "New game");
  const uint8_t players = table().lobby.playerCount();
  if (players < 2) snprintf(screen.detail, sizeof(screen.detail), "Seat two players to start");
  else snprintf(screen.detail, sizeof(screen.detail), "%u players. Tap a row to change it", static_cast<unsigned>(players));
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
  const bool inLobby = table().hubState == HubState::Lobby;
  snprintf(screen.badge, sizeof(screen.badge), inLobby ? "ORDER" : "PLAYER");
  char name[SCREEN_NAME_LENGTH + 1];
  playerName(seat, profileIdForTableSeat(seat, !inLobby), nowMs, name);
  snprintf(screen.title, sizeof(screen.title), "%s", name);
  if (inLobby && table().nextGameSettings.twoHeadedGiant) {
    // Two-Headed Giant teams are neighbours in turn order (1+2, 3+4, ...).
    snprintf(screen.detail, sizeof(screen.detail), "Turn order: %u of %u, Team %u",
        static_cast<unsigned>(seat.playerNumber), static_cast<unsigned>(table().lobby.playerCount()),
        static_cast<unsigned>((seat.playerNumber + 1) / 2));
  } else if (inLobby) {
    // Show each player's position and the shared Sigil's physical seating.
    snprintf(screen.detail, sizeof(screen.detail), "Turn order: %u of %u%s",
        static_cast<unsigned>(seat.playerNumber), static_cast<unsigned>(table().lobby.playerCount()),
        table().lobby.hasSecondary(seat.controllerId) ?
            (table().lobby.secondaryFirst(seat.controllerId) ? "; B left, before A" : "; B right, after A") : "");
  } else if (playerControlsOpen) {
    snprintf(screen.detail,sizeof(screen.detail),"Counters & status / page %u",playerControlsPage+1);
  } else {
    snprintf(screen.detail, sizeof(screen.detail), "%s %ld%s",
        table().game.twoHeadedGiant() ? "Team life" : "Life",
        static_cast<long>(table().game.lifeTotal(seat.playerNumber)),
        table().game.hasTurn(seat.playerNumber) ? ", their turn" : "");
  }
}

// Add player's three screens: the list, the name being typed, and the PIN
// (as dots, never the digits).
void formatSeat(AtlasScreen &screen) {
  snprintf(screen.badge, sizeof(screen.badge), "ADD PLAYER");
  switch (screen.kind) {
    case ScreenKind::Seat:
      snprintf(screen.title, sizeof(screen.title), "Who's playing?");
      if (seatRowCount == 0) {
        snprintf(screen.detail, sizeof(screen.detail), "No one else saved: tap New player");
      } else if (seatPages > 1) {
        snprintf(screen.detail, sizeof(screen.detail), "Tap a name to seat them (%u of %u)",
            static_cast<unsigned>(seatPage + 1), static_cast<unsigned>(seatPages));
      } else {
        snprintf(screen.detail, sizeof(screen.detail), "Tap a name to seat them");
      }
      return;
    case ScreenKind::Name:
      snprintf(screen.title, sizeof(screen.title), "%s", typedName[0] ? typedName : "New player");
      snprintf(screen.detail, sizeof(screen.detail), "Type their name. No PIN needed");
      return;
    default: {
      char name[SCREEN_NAME_LENGTH + 1];
      snprintf(name, sizeof(name), "%s", TurnHubProfiles::nameForProfile(seatRows[pinRow].profileId).c_str());
      snprintf(screen.title, sizeof(screen.title), "%s's PIN", name);
      char dots[2 * TYPED_PIN_LENGTH + 1] = {};
      for (size_t i = 0; typedPin[i]; ++i) strcat(dots, i ? " *" : "*");
      snprintf(screen.detail, sizeof(screen.detail), "%s", dots[0] ? dots : "Enter it, then OK");
      return;
    }
  }
}

}  // namespace

void buildAtlasScreen(uint32_t nowMs, AtlasScreen &screen) {
  screen = AtlasScreen();
  screen.kind = activeScreen(nowMs);
  screen.sdOnUsb = sdCardOnUsb();
  screen.sdMissing = !screen.sdOnUsb && !sdCardReady();
  const TurnHub::BatteryReading &battery = atlasBattery();
  screen.batteryPercent = battery.present ? static_cast<int8_t>(battery.percent) : -1;
  screen.batteryLow = battery.present && battery.low;
  screen.batteryCharging = battery.present && battery.charging;
  screen.update = firmwareUpdateKind();
  screen.sigilsOnline = sigilBus.activeCount(nowMs);
  switch (screen.kind) {
    case ScreenKind::Home:
      formatHome(screen, nowMs);
      break;
    case ScreenKind::ManaTap: formatManaTap(screen); break;
    case ScreenKind::Confirm: formatConfirm(screen, nowMs); break;
    case ScreenKind::NewGame: formatNewGame(screen); break;
    case ScreenKind::Overview:
      formatStatus(screen,nowMs);
      snprintf(screen.title,sizeof(screen.title),"Table");
      snprintf(screen.detail,sizeof(screen.detail),"Read only / %s",overviewPage ? "players 9-16" : "players 1-8");
      break;
    case ScreenKind::Status: formatStatus(screen, nowMs); break;
    case ScreenKind::Qr: formatQr(screen, nowMs); break;
    case ScreenKind::Code: formatCode(screen, nowMs); break;
    case ScreenKind::PairCode: formatPairCode(screen, nowMs); break;
    case ScreenKind::Table: formatTable(screen, nowMs); break;
    case ScreenKind::Menu: formatMenu(screen); break;
    case ScreenKind::Themes:
      snprintf(screen.badge, sizeof(screen.badge), "THEME");
      snprintf(screen.title, sizeof(screen.title), "%s", TurnHubTheme::palette(atlasDisplayTheme()).label);
      snprintf(screen.detail, sizeof(screen.detail), "Choose the look for this Atlas");
      break;
    case ScreenKind::Device: formatDevice(screen); break;
    case ScreenKind::Player: formatPlayer(screen, nowMs); break;
    case ScreenKind::Setup: formatSetup(screen, nowMs); break;
    case ScreenKind::Seat:
    case ScreenKind::Name:
    case ScreenKind::Pin: formatSeat(screen); break;
  }
  layoutButtons(screen, nowMs);
  if (noticeText[0] != '\0' && nowMs - noticeAtMs < TOUCH_NOTICE_MS) {
    snprintf(screen.notice, sizeof(screen.notice), "%s", noticeText);
  }
  if (!touchDown || !pressInside) return;
  for (uint8_t i = 0; i < screen.buttonCount; ++i) {
    const TouchButton &button = screen.buttons[i];
    if (button.action != pressedAction || button.arg != pressedArg) continue;
    screen.pressed = pressedAction;
    screen.pressedArg = pressedArg;
  }
}

void updateTouchControls(uint32_t nowMs, bool touched, int16_t x, int16_t y) {
  serviceManaTap(nowMs);
  AtlasScreen layout;
  if (touched) {
    lastContactAtMs = nowMs;
    if (!touchDown) {
      touchDown = true;
      pressedAction = buttonAt(x, y, nowMs);
      // A press in Mana Tap's field aims at an orb as it lands.
      const TurnHub::ManaField &field = manaTap.field();
      if (pressedAction == TouchAction::None && manaTap.phase() == TurnHub::ManaPhase::Playing &&
          activeScreen(nowMs) == ScreenKind::ManaTap && y >= field.top && y < field.bottom) {
        manaTap.tap(x, y);
      }
    }
    const TouchButton *button = currentButton(pressedAction, layout, nowMs);
    pressInside = button != nullptr && button->contains(x, y, TOUCH_SLOP_PX);
    return;
  }

  if (!touchDown || nowMs - lastContactAtMs < TOUCH_RELEASE_MS) return;
  touchDown = false;
  const TouchButton *button = currentButton(pressedAction, layout, nowMs);
  if (button != nullptr && pressInside) {
    if (button->confirm) {
      // Ask first: the popup covers this screen until Confirm or Cancel.
      confirmOver = baseScreen(nowMs);
      confirmAction = pressedAction;
      serialLog.print("ATLAS|TOUCH|ASK|");
      serialLog.println(actionName(pressedAction));
    } else {
      dispatchTouchAction(nowMs, pressedAction);
    }
  }
  pressedAction = TouchAction::None;
  pressedArg = 0;
  pressInside = false;
}

void resetTouchControls(bool startAtHome) {
  touchDown = false;
  pressInside = false;
  pressedAction = TouchAction::None;
  pressedArg = 0;
  noticeText[0] = '\0';
  seatRowCount = seatPage = seatPages = pinRow = 0;
  typedName[0] = typedPin[0] = '\0';
  openScreen = startAtHome ? ScreenKind::Home : ScreenKind::Status;
  qrChoice = TouchAction::QrPortal;
  shownSeat = pressedSeat = PlayerSeat();
  confirmAction = TouchAction::None;
  playerControlsOpen=false; playerControlsPage=0;
  setupSkipped = false;
  manaTap = TurnHub::ManaTap();
  manaTurnPaused = false;
  manaTurnPlayer = 0;
  for (auto &cache : names) cache = NameCache();
}

}  // namespace TurnHubAtlas
