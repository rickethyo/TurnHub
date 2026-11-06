// Atlas touchscreen: the TFT's screen model and its touch buttons. Games
// run on the hub phone (hosted_game.h): the Game tab shows the hub's game,
// and its few table controls queue inputs for the hub (hosted_sigils.h). The
// touch adapter's device actions build Intents (IntentOrigin::AtlasHardware);
// the handlers decide. The rest change no device state: Cancel takes a
// presence code off the screen (front_panel.cpp), the tabs and list rows
// switch screens, and the confirm popup asks before a hazardous action.
// Nothing acts on a hold. The board draws the screen model (atlas_board.h).

#include "touch_controls.h"

#include <stdio.h>
#include <algorithm>
#include <string.h>

#include "atlas_app.h"
#include "atlas_battery.h"
#include "atlas_display.h"
#include "avatars.h"
#include "config.h"
#include "speaker_settings.h"
#include "firmware_version.h"
#include "home_wifi.h"
#include "home_tls.h"
#include "hosted_game.h"
#include "hosted_sigils.h"
#include "optional_preferences.h"
#include "phone_links.h"
#include "profile_store.h"
#include "sd_card.h"
#include "serial_log.h"
#include "sigil_update_service.h"
#include "wifi_password_store.h"
#include <WiFi.h>

using TurnHub::serialLog;

namespace TurnHubAtlas {

namespace {

constexpr int16_t MARGIN = 8;
constexpr int16_t FULL_WIDTH = ATLAS_SCREEN_WIDTH - 2 * MARGIN;

// Press state for the touch in progress (pressedArg: which of the keys or
// rows sharing an action).
bool touchDown = false;
bool pressInside = false;
TouchAction pressedAction = TouchAction::None;
uint8_t pressedArg = 0;
uint32_t lastContactAtMs = 0;

char noticeText[sizeof(AtlasScreen::notice)] = {};
char syncText[sizeof(AtlasScreen::sync)] = {};
uint32_t syncAtMs = 0;
uint32_t noticeAtMs = 0;

// Which screen is up.
ScreenKind openScreen = ScreenKind::Home;
// The confirm popup: the action it asks about, and the screen it covers. It
// closes by itself when that screen does (a code showing).
TouchAction confirmAction = TouchAction::None;
ScreenKind confirmOver = ScreenKind::Status;
// Skip for now on the setup Welcome screen: hidden until the next start-up,
// or until Setup under Menu. Presentation only; the stage stays Atlas's.
bool setupSkipped = false;

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
    x += w + MARGIN;
  }
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

// The hub's game is under way (Starting, Running or Paused).
bool matchInProgress() { return gameUnderway(millis()); }

// The hub's game, while a hub holds the table.
bool hostedShown(uint32_t nowMs) { return TurnHubHosted::hostedTable().live(nowMs); }

// The first Sigil waiting for its pairing code to be checked, or INVALID_ID.
// One at a time on this screen; the next shows once it is decided.
uint8_t waitingPairSlot() {
  if (!betweenGames(millis())) return INVALID_ID;
  for (uint8_t slot = 0; slot < MAX_PHYSICAL_SIGILS; ++slot) {
    if (sigilBus.pendingPairing(slot) != nullptr) return slot;
  }
  return INVALID_ID;
}

// First-run setup takes the Game tab between games until it is complete
// (Welcome can be skipped for now; "You're all set" waits for Done).
bool setupScreenDue() {
  if (!betweenGames(millis()) || setupStage == TurnHub::SetupStage::Complete) return false;
  return !(setupStage == TurnHub::SetupStage::Welcome && setupSkipped);
}

// The hub's Planechase, while its game is under way with Planechase on, or nullptr.
const turnhub::Json *hostedPlanechase(uint32_t nowMs) {
  if (!hostedShown(nowMs)) return nullptr;
  const turnhub::Game &game = TurnHubHosted::hostedTable().game();
  if (game.state != turnhub::TableState::Running && game.state != turnhub::TableState::Paused) return nullptr;
  if (!game.planechase.get("enabled").asBool()) return nullptr;
  return &game.planechase;
}

// The face-up card's catalog ID (Atlas's digital deck), or "" for the players' own cards.
const char *faceUpPlaneId(const turnhub::Json &planes) {
  const auto &up = planes.get("faceUp");
  return up.size() ? up[0].get("id").asString().c_str() : "";
}

// Screens this Atlas no longer has (the hub runs games) fall back to the Game
// tab, and so do the Planes screens once the hub's Planechase is over.
bool shownScreen(ScreenKind kind) {
  switch (kind) {
    case ScreenKind::Home: case ScreenKind::Status: case ScreenKind::Qr: case ScreenKind::Menu:
    case ScreenKind::Device: case ScreenKind::Power: case ScreenKind::Themes: case ScreenKind::ManaTap: case ScreenKind::Storage:
      return true;
    case ScreenKind::Planes: return hostedPlanechase(millis()) != nullptr;
    // The card view needs a digital plane to show.
    case ScreenKind::PlaneCard: {
      const turnhub::Json *planes = hostedPlanechase(millis());
      return planes && faceUpPlaneId(*planes)[0];
    }
    default: return false;
  }
}

// The screen under any confirm popup. A code a phone asked for shows over
// whatever screen is open, then a pairing code waiting to be checked.
ScreenKind baseScreen(uint32_t nowMs) {
  if (!shownScreen(openScreen)) openScreen = openScreen == ScreenKind::PlaneCard && shownScreen(ScreenKind::Planes)
      ? ScreenKind::Planes : ScreenKind::Status;
  if (pendingPresenceCode(nowMs) != nullptr) return ScreenKind::Code;
  if (waitingPairSlot() != INVALID_ID) return ScreenKind::PairCode;
  // A phone asking to pair waits for Allow over any screen, as a Sigil's code does.
  if (TurnHubPhones::waiting(nowMs)) return ScreenKind::PhonePair;
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
  ButtonSpec rows[7];
  uint8_t n = 0;
  if (betweenGames(millis()) && setupStage != TurnHub::SetupStage::Complete) {
    rows[n++] = {TouchAction::OpenSetup, "Setup", 0, 1};
  }
  if (betweenGames(millis())) rows[n++] = {TouchAction::Pair, "Pair a Sigil", 0, 1};
  rows[n++] = {TouchAction::OpenQr, "QR codes", 0, 1};
  rows[n++] = {TouchAction::OpenThemes, theme, 0, 1};
  rows[n++] = {TouchAction::HomeWifi, !homeOn ? "Home Wi-Fi: off" : homeWifiConnected() ? "Home Wi-Fi: connected" : "Home Wi-Fi: on", 0, 1};
  static char appLink[32];
  const char *pin = homeTlsPin();
  if (homeTlsRunning() && strlen(pin) == 64) {
    snprintf(appLink, sizeof(appLink), "App link: %.4s-%.4s-%.4s", pin, pin + 4, pin + 8);
    for (char *c = appLink + 10; *c; ++c) if (*c >= 'a' && *c <= 'f') *c -= 'a' - 'A';
    rows[n++] = {TouchAction::None, appLink, 0, 1};
  }
  rows[n++] = {TouchAction::OpenDevice, "Device", 0, 1};
  addList(screen, rows, n, LIST_BOTTOM_TABS);
  for (uint8_t i = 0; i < screen.buttonCount; ++i) {
    if (screen.buttons[i].action == TouchAction::HomeWifi) screen.buttons[i].selected = homeOn;
  }
}

// Settings > Device: USB drive, Sleep (between games; a touch wakes Atlas),
// Unpair Sigils (between games, as the handler requires) and Factory reset (not
// during a match). Unpair and Factory reset ask first in the popup; they
// stand in for the BOOT button's held gestures.
void layoutDevice(AtlasScreen &screen) {
  ButtonSpec rows[6];
  uint8_t n = 0;
  rows[n++] = {TouchAction::OpenPower, "Sound, screen, sleep", 0, 1};
  rows[n++] = {TouchAction::OpenStorage, "Storage & USB drive", 0, 1};
  if (!matchInProgress()) rows[n++] = {TouchAction::SleepAtlas, "Sleep", 0, 1};
  if (betweenGames(millis())) rows[n++] = {TouchAction::UnpairSigils, "Unpair Sigils", ASK, 1};
  if (TurnHubPhones::count()) rows[n++] = {TouchAction::ForgetPhones, "Forget paired phones", ASK, 1};
  if (!matchInProgress()) rows[n++] = {TouchAction::FactoryResetAtlas, "Factory reset", ASK, 1};
  addList(screen, rows, n);
  const ButtonSpec back[] = {{TouchAction::CloseScreen, "Back", 0, 1}};
  addRow(screen, BUTTON_ROW_Y, back, 1);
}

// Settings > Device > Sound, screen, sleep: the speaker volume and the three
// power rows, each tap the next choice, then Back to Device.
void layoutPower(AtlasScreen &screen) {
  static char volume[32], screenOff[32], inGames[32], autoSleep[32];
  snprintf(volume, sizeof(volume), "Volume: %s", TurnHub::speakerVolumeName(audio.speakerVolume()));
  snprintf(screenOff, sizeof(screenOff), "Screen off: %s", TurnHub::SCREEN_OFF_CHOICES[powerSettings.screenOff]);
  snprintf(inGames, sizeof(inGames), "Off in games: %s", TurnHub::GAME_SCREEN_CHOICES[powerSettings.gameScreen]);
  snprintf(autoSleep, sizeof(autoSleep), "Deep sleep: %s", TurnHub::AUTO_SLEEP_CHOICES[powerSettings.autoSleep]);
  const ButtonSpec rows[] = {{TouchAction::SpeakerVolume, volume, 0, 1}, {TouchAction::PowerScreenOff, screenOff, 0, 1},
      {TouchAction::PowerGameScreen, inGames, 0, 1}, {TouchAction::PowerAutoSleep, autoSleep, 0, 1}};
  addList(screen, rows, 4);
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

const char *confirmLabel(TouchAction action) {
  switch (action) {
    case TouchAction::UnpairSigils: return "Unpair all";
    case TouchAction::FactoryResetAtlas: return "Erase Atlas";
    case TouchAction::ForgetPhones: return "Forget phones";
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
    // The plane fills the body (the display draws it); the die and the
    // card-says-so planeswalk sit under it within thumb reach. The hub acts
    // for the active player, as its own Planes tab does.
    case ScreenKind::Planes: {
      const HostedScreenActions offered = hostedScreenActions();
      const bool card = shownScreen(ScreenKind::PlaneCard);
      if (offered.roll || offered.resolve) {
        static char roll[32];
        if (screen.planes.rollCost > 0) snprintf(roll, sizeof(roll), "Roll: %ld mana", static_cast<long>(screen.planes.rollCost));
        else snprintf(roll, sizeof(roll), "Roll the die: free");
        const ButtonSpec upper[] = {offered.resolve ? ButtonSpec{TouchAction::PlanarResolve, "Resolve phenomenon", 0, 1}
                                                    : ButtonSpec{TouchAction::PlanarRoll, roll, 0, 1}};
        addRow(screen, BUTTON_UPPER_ROW_Y, upper, 1);
      }
      ButtonSpec lower[2];
      uint8_t n = 0;
      if (offered.walk) lower[n++] = {TouchAction::PlanarWalk, "Planeswalk", 0, 3};
      if (card) lower[n++] = {TouchAction::OpenPlaneCard, n ? "Card" : "Full screen card", 0, 2};
      if (n) addRow(screen, BUTTON_ROW_Y, lower, n);
      return;
    }
    // The whole landscape panel is one button back to the Planes page.
    case ScreenKind::PlaneCard:
      addKey(screen, TouchAction::PlaneCardBack, "Back", 0, 0, ATLAS_SCREEN_HEIGHT, ATLAS_SCREEN_WIDTH, 0);
      return;
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
    case ScreenKind::PhonePair: {
      const ButtonSpec row[] = {{TouchAction::PhoneAllow, "Allow", 0, 3}, {TouchAction::PhoneRefuse, "Refuse", 0, 2}};
      addRow(screen, BUTTON_ROW_Y, row, 2);
      return;
    }
    case ScreenKind::Menu:
      layoutMenu(screen);
      return;
    case ScreenKind::Themes: layoutThemes(screen); return;
    case ScreenKind::Device:
      layoutDevice(screen);
      return;
    case ScreenKind::Power: layoutPower(screen); return;
    case ScreenKind::Storage: {
      const ButtonSpec row[] = {{TouchAction::UsbDrive, sdCardUsbShareWanted() ? "USB drive: on" : "USB drive: off", 0, 1},
          {TouchAction::CloseScreen, "Back", 0, 1}};
      addRow(screen, BUTTON_ROW_Y, row, 2);
      return;
    }
    case ScreenKind::Qr: {
      const ButtonSpec row[] = {{TouchAction::CloseScreen, "Back", 0, 1}};
      addRow(screen, BUTTON_ROW_Y, row, 1);
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
    default:
      return;
  }
  // A hosted game: the table controls the hub's game offers, queued for the hub.
  if (hostedShown(nowMs)) {
    const HostedScreenActions offered = hostedScreenActions();
    ButtonSpec row[2];
    uint8_t n = 0;
    if (offered.rematch) row[n++] = {TouchAction::Rematch, "Rematch", 0, 3};
    if (offered.reset) row[n++] = {TouchAction::ResetGame, "New players", 0, 2};
    if (offered.start) row[n++] = {TouchAction::StartGame, "Start game", 0, 3};
    if (offered.pass) row[n++] = {TouchAction::PassTurn, "Pass", 0, 3};
    if (offered.pause) row[n++] = {TouchAction::Pause, "Pause", 0, 2};
    if (offered.resume) row[n++] = {TouchAction::Resume, "Resume", 0, 3};
    if (n) addRow(screen, BUTTON_ROW_Y, row, n);
    return;
  }
}

// The tabs: Home, Game and Settings, as on the app and Sigil.
void layoutButtons(AtlasScreen &screen, uint32_t nowMs) {
  layoutContentButtons(screen, nowMs);
  const auto kind = activeScreen(nowMs);
  if (kind == ScreenKind::Code || kind == ScreenKind::PairCode || kind == ScreenKind::PhonePair ||
      kind == ScreenKind::Setup || kind == ScreenKind::Confirm || kind == ScreenKind::PlaneCard) return;
  // Planes joins the tabs while the hub's game plays Planechase, as on the app.
  const bool planesTab = hostedPlanechase(nowMs) != nullptr;
  const TouchAction actions[] = {TouchAction::OpenHome, TouchAction::OpenGame, TouchAction::OpenPlanes, TouchAction::OpenSettings};
  const char *labels[] = {TurnHubUi::pageTitle(TurnHubUi::Page::Home), TurnHubUi::pageTitle(TurnHubUi::Page::Game),
      "Planes", TurnHubUi::pageTitle(TurnHubUi::Page::Settings)};
  const uint8_t selected = kind == ScreenKind::Home || kind == ScreenKind::ManaTap ? 0 : kind == ScreenKind::Status ? 1
      : kind == ScreenKind::Planes ? 2 : 3;
  const uint8_t count = planesTab ? 4 : 3;
  const int16_t w = ATLAS_SCREEN_WIDTH / count;
  uint8_t slot = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    if (i == 2 && !planesTab) continue;
    addKey(screen, actions[i], labels[i], slot * w, 432, slot + 1 == count ? ATLAS_SCREEN_WIDTH - slot * w : w, 48, 0);
    screen.buttons[screen.buttonCount - 1].selected = i == selected;
    ++slot;
  }
}

// The current layout's button for an action, or nullptr if it is gone.
const TouchButton *currentButton(TouchAction action, AtlasScreen &layout, uint32_t nowMs) {
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
  return TouchAction::None;
}

const char *actionName(TouchAction action) {
  switch (action) {
    case TouchAction::OpenHome: return "OPEN_HOME";
    case TouchAction::OpenGame: return "OPEN_GAME";
    case TouchAction::OpenSettings: return "OPEN_SETTINGS";
    case TouchAction::Pair: return "PAIR";
    case TouchAction::Rematch: return "Rematch";
    case TouchAction::ResetGame: return "New players";
    case TouchAction::StartGame: return "START_GAME";
    case TouchAction::Pause: return "PAUSE";
    case TouchAction::Resume: return "RESUME";
    case TouchAction::CancelCode: return "CANCEL_CODE";
    case TouchAction::PairConfirm: return "PAIR_CONFIRM";
    case TouchAction::PhoneAllow: return "PHONE_ALLOW";
    case TouchAction::PhoneRefuse: return "PHONE_REFUSE";
    case TouchAction::ForgetPhones: return "FORGET_PHONES";
    case TouchAction::PairReject: return "PAIR_REJECT";
    case TouchAction::OpenQr: return "OPEN_QR";
    case TouchAction::CloseScreen: return "CLOSE_SCREEN";
    case TouchAction::PassTurn: return "PASS_TURN";
    case TouchAction::SkipSetup: return "SKIP_SETUP";
    case TouchAction::OpenSetup: return "OPEN_SETUP";
    case TouchAction::SetupPair: return "SETUP_PAIR";
    case TouchAction::SetupDone: return "SETUP_DONE";
    case TouchAction::OpenDevice: return "OPEN_DEVICE";
    case TouchAction::OpenStorage: return "OPEN_STORAGE";
    case TouchAction::OpenThemes: return "OPEN_THEMES";
    case TouchAction::InvertDisplay: return "INVERT_DISPLAY";
    case TouchAction::UsbDrive: return "USB_DRIVE";
    case TouchAction::SpeakerVolume: return "SPEAKER_VOLUME";
    case TouchAction::HomeWifi: return "HOME_WIFI";
    case TouchAction::ThemeGraphite: return "THEME_GRAPHITE";
    case TouchAction::ThemeDaylight: return "THEME_DAYLIGHT";
    case TouchAction::ThemeBrass: return "THEME_BRASS";
    case TouchAction::ThemeContrast: return "THEME_CONTRAST";
    case TouchAction::UnpairSigils: return "UNPAIR_SIGILS";
    case TouchAction::FactoryResetAtlas: return "FACTORY_RESET_ATLAS";
    case TouchAction::SleepAtlas: return "SLEEP_ATLAS";
    case TouchAction::OpenPower: return "OPEN_POWER";
    case TouchAction::PowerScreenOff: return "POWER_SCREEN_OFF";
    case TouchAction::PowerGameScreen: return "POWER_GAME_SCREEN";
    case TouchAction::PowerAutoSleep: return "POWER_AUTO_SLEEP";
    case TouchAction::ConfirmYes: return "CONFIRM_YES";
    case TouchAction::ConfirmNo: return "CONFIRM_NO";
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

// Screen changes: no Intent, no table state, no notice. Back leaves the
// Settings pages (QR codes, Theme, Device) for Settings, and everything else
// for the Game page.
bool navigate(TouchAction action) {
  switch (action) {
    case TouchAction::OpenHome: openScreen = ScreenKind::Home; return true;
    case TouchAction::OpenGame: openScreen = ScreenKind::Status; return true;
    case TouchAction::OpenSettings: openScreen = ScreenKind::Menu; return true;
    case TouchAction::OpenPlanes: openScreen = ScreenKind::Planes; return true;
    case TouchAction::OpenPlaneCard: openScreen = ScreenKind::PlaneCard; return true;
    case TouchAction::PlaneCardBack: openScreen = ScreenKind::Planes; return true;
    case TouchAction::ConfirmNo: confirmAction = TouchAction::None; return true;
    case TouchAction::OpenQr: openScreen = ScreenKind::Qr; return true;
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
      // The network name and password are typed in the app.
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
    case TouchAction::OpenStorage: openScreen = ScreenKind::Storage; return true;
    case TouchAction::OpenPower: openScreen = ScreenKind::Power; return true;
    case TouchAction::CloseScreen: {
      if (openScreen == ScreenKind::Power || openScreen == ScreenKind::Storage) {
        openScreen = ScreenKind::Device;
        return true;
      }
      const bool fromMenuScreen = openScreen == ScreenKind::Qr || openScreen == ScreenKind::Device ||
          openScreen == ScreenKind::Themes;
      openScreen = fromMenuScreen ? ScreenKind::Menu : ScreenKind::Status;
      return true;
    }
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
  // A hosted game: the hub decides, so the tap only queues an input for it.
  if (hostedShown(nowMs) && (action == TouchAction::StartGame || action == TouchAction::PassTurn ||
      action == TouchAction::Pause || action == TouchAction::Resume || action == TouchAction::PlanarRoll ||
      action == TouchAction::PlanarWalk || action == TouchAction::PlanarResolve ||
      action == TouchAction::Rematch || action == TouchAction::ResetGame)) {
    const HostedScreenAction hosted = action == TouchAction::Rematch ? HostedScreenAction::Rematch
        : action == TouchAction::ResetGame ? HostedScreenAction::Reset
        : action == TouchAction::StartGame ? HostedScreenAction::Start
        : action == TouchAction::PassTurn ? HostedScreenAction::Pass
        : action == TouchAction::Pause ? HostedScreenAction::Pause
        : action == TouchAction::PlanarRoll ? HostedScreenAction::PlanarRoll
        : action == TouchAction::PlanarWalk ? HostedScreenAction::PlanarWalk
        : action == TouchAction::PlanarResolve ? HostedScreenAction::PlanarResolve : HostedScreenAction::Resume;
    if (!queueHostedScreenAction(hosted)) showNotice(nowMs, "That is no longer available");
    serialLog.print("ATLAS|TOUCH|HOSTED|");
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
    // A phone asking to pair: Allow gives it a device key (phone_links.h).
    case TouchAction::PhoneAllow:
    case TouchAction::PhoneRefuse:
    case TouchAction::ForgetPhones: {
      Intent intent;
      intent.type = IntentType::PhonePairing;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      intent.payload.value = action == TouchAction::PhoneAllow ? TurnHub::PHONE_ALLOW
          : action == TouchAction::PhoneRefuse ? TurnHub::PHONE_REFUSE : TurnHub::PHONE_FORGET_ALL;
      result = intents.dispatch(intent);
      break;
    }
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
    case TouchAction::Pair: {
      Intent intent;
      intent.type = IntentType::PairRequest;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      result = intents.dispatch(intent);
      // From Settings: show the table the pairing countdown.
      if (result.accepted()) openScreen = ScreenKind::Status;
      break;
    }
    case TouchAction::PowerScreenOff:
    case TouchAction::PowerGameScreen:
    case TouchAction::PowerAutoSleep: {
      TurnHub::PowerSettings next = powerSettings;
      if (action == TouchAction::PowerScreenOff) next.screenOff = (next.screenOff + 1) % TurnHub::SCREEN_OFF_CHOICE_COUNT;
      else if (action == TouchAction::PowerGameScreen) next.gameScreen = (next.gameScreen + 1) % TurnHub::GAME_SCREEN_CHOICE_COUNT;
      else next.autoSleep = (next.autoSleep + 1) % TurnHub::AUTO_SLEEP_CHOICE_COUNT;
      Intent intent;
      intent.type = IntentType::ConfigurePower;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      intent.payload.value = TurnHub::packPowerSettings(next);
      result = intents.dispatch(intent);
      break;
    }
    case TouchAction::SpeakerVolume: {
      Intent intent;
      intent.type = IntentType::ConfigureSpeaker;
      intent.actor.origin = IntentOrigin::AtlasHardware;
      intent.payload.value = (audio.speakerVolume() + 1) % 4;
      result = intents.dispatch(intent);
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
    // handlers decide: Unpair waits until no game is under way.
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

// The Game tab: the hub's game (names, life, whose turn), or how to start one.
void formatStatus(AtlasScreen &screen, uint32_t nowMs) {
  if (!hostedShown(nowMs)) {
    snprintf(screen.badge, sizeof(screen.badge), "READY");
    snprintf(screen.title, sizeof(screen.title), "No game yet");
    const uint32_t pairingMs = pairingRemainingMs(nowMs);
    if (pairingMs > 0) {
      snprintf(screen.detail, sizeof(screen.detail), "Pairing open: %lu s left",
          static_cast<unsigned long>((pairingMs + 999) / 1000));
    } else {
      snprintf(screen.detail, sizeof(screen.detail), "%u %s online", static_cast<unsigned>(screen.sigilsOnline),
          screen.sigilsOnline == 1 ? "Sigil" : "Sigils");
    }
    // Three steps, drawn as numbered bubbles.
    snprintf(screen.lines[0], sizeof(screen.lines[0]), "Open TurnHub on your phone");
    snprintf(screen.lines[1], sizeof(screen.lines[1]), "Start a game: it shows up here");
    snprintf(screen.lines[2], sizeof(screen.lines[2]), "Pair Sigils in Settings");
    screen.lineCount = 3;
    char update[sizeof(screen.detail)];
    if (sigilUpdateNotice(update, sizeof(update), nowMs)) snprintf(screen.detail, sizeof(screen.detail), "%s", update);
    return;
  }
  const TurnHubHosted::HostedTable &hosted = TurnHubHosted::hostedTable();
  const turnhub::Game &game = hosted.game();
  const bool inGame = game.state != turnhub::TableState::Lobby;
  screen.showLife = inGame;
  screen.round = game.active ? static_cast<uint16_t>(std::min(game.players[*game.active].turnsCompleted + 1, 65535)) : 0;
  for (size_t i = 0; i < game.players.size() && screen.playerCount < MAX_SCREEN_PLAYERS; ++i) {
    const turnhub::Player &player = game.players[i];
    ScreenPlayer &chip = screen.players[screen.playerCount++];
    chip.number = static_cast<uint8_t>(i + 1);
    snprintf(chip.name, sizeof(chip.name), "%s", player.name.c_str());
    chip.life = player.life;
    // A player the hub linked to an Atlas profile shows that profile's avatar:
    // a preset, or the approved custom image's tiny thumbnail.
    const String profile = player.profileId.c_str();
    const uint8_t avatar = profile.length() ? TurnHubProfiles::avatarForProfile(profile) : 0;
    if (TurnHubAvatars::validPresetAvatar(avatar)) chip.avatar = avatar;
    if (avatar == TurnHubAvatars::AVATAR_CUSTOM) {
      chip.avatarPixels = TurnHubProfiles::artworkThumbnail(profile, chip.imageRevision);
      if (chip.avatarPixels) chip.avatar = avatar;
    }
    if (inGame && game.active && *game.active == static_cast<int>(i)) chip.flags |= CHIP_ACTIVE;
    if (player.eliminated) chip.flags |= CHIP_OUT;
    if (game.state == turnhub::TableState::GameOver &&
        ((game.winner && *game.winner == static_cast<int>(i)) ||
         std::find(game.winners.begin(), game.winners.end(), static_cast<int>(i)) != game.winners.end())) {
      chip.flags |= CHIP_WINNER;
    }
  }
  const char *badge = "LOBBY";
  switch (game.state) {
    case turnhub::TableState::Lobby: badge = "LOBBY"; break;
    case turnhub::TableState::Starting: badge = "STARTING"; break;
    case turnhub::TableState::Running: badge = "PLAYING"; break;
    case turnhub::TableState::Paused: badge = "PAUSED"; break;
    case turnhub::TableState::GameOver: badge = "GAME OVER"; break;
  }
  snprintf(screen.badge, sizeof(screen.badge), "%s", badge);
  if (inGame && game.active && *game.active >= 0 && *game.active < static_cast<int>(game.players.size()) &&
      game.state != turnhub::TableState::GameOver) {
    snprintf(screen.title, sizeof(screen.title), "%.24s's turn", game.players[*game.active].name.c_str());
  } else {
    snprintf(screen.title, sizeof(screen.title), "%s", game.state == turnhub::TableState::GameOver ? "Game over" : "Game setup");
  }
  snprintf(screen.detail, sizeof(screen.detail), "Hosted by %.36s", hosted.lease().name.c_str());
  // Sigil updates run between games; their progress and outcome, in words,
  // take the detail line so a failure is never silent at the table.
  char update[sizeof(screen.detail)];
  if (!inGame || game.state == turnhub::TableState::GameOver) {
    if (sigilUpdateNotice(update, sizeof(update), nowMs)) snprintf(screen.detail, sizeof(screen.detail), "%s", update);
  }
}

// Planechase: the face-up plane (the hub's digital deck) or the players' own,
// and the latest roll in words (the die animation is never the only place
// it shows). screen.planes was filled by fillPlanes.
void formatPlanes(AtlasScreen &screen) {
  const ScreenPlanes &planes = screen.planes;
  snprintf(screen.badge, sizeof(screen.badge), "PLANES");
  snprintf(screen.title, sizeof(screen.title), "Planechase");
  static const char *const faces[] = {"blank", "chaos", "planeswalk"};
  if (planes.phenomenon) snprintf(screen.detail, sizeof(screen.detail), "Phenomenon: resolve it first");
  else if (planes.roll > 0)
    snprintf(screen.detail, sizeof(screen.detail), "%s rolled %s", planes.roller[0] ? planes.roller : "A player",
        faces[planes.face < 3 ? planes.face : 0]);
  else snprintf(screen.detail, sizeof(screen.detail), "The active player rolls");
}

// The Planechase fields every screen carries in the hub's game (see
// ScreenPlanes), read from the hub's state. Atlas names a digital plane from
// the catalog the hub sent it (/api/planes).
void fillPlanes(AtlasScreen &screen, uint32_t nowMs) {
  const turnhub::Json *found = hostedPlanechase(nowMs);
  if (!found) return;
  const turnhub::Json &planes = *found;
  const turnhub::Game &game = TurnHubHosted::hostedTable().game();
  ScreenPlanes &out = screen.planes;
  out.on = true;
  const char *id = faceUpPlaneId(planes);
  out.digital = id[0] != '\0';
  const auto &up = planes.get("faceUp");
  for (size_t i = 0; i < up.size(); ++i) out.phenomenon = out.phenomenon || up[i].get("phenomenon").asBool();
  if (out.digital) {
    const TurnHubCards::Plane *card = TurnHubCards::findPlane(id);
    snprintf(out.plane, sizeof(out.plane), "%s", card ? card->name : out.phenomenon ? "A phenomenon" : "A plane");
  }
  out.deckLeft = static_cast<int32_t>(planes.get("deck").size());
  out.planeswalks = static_cast<int32_t>(planes.get("planeswalks").asInt());
  out.chaos = static_cast<int32_t>(planes.get("chaos").asInt());
  out.counters = static_cast<int32_t>(planes.get("counters").get(out.digital ? id : "physical").asInt());
  if (game.active) out.rollCost = static_cast<int32_t>(planes.get("rolls").get(std::to_string(*game.active)).asInt());
  const auto &roll = planes.get("lastRoll");
  if (roll.isObject()) {
    out.roll = static_cast<int32_t>(roll.get("number").asInt());
    const auto &face = roll.get("face").asString();
    out.face = face == "planeswalk" ? 2 : face == "chaos" ? 1 : 0;
    out.cost = static_cast<uint8_t>(std::min<int64_t>(roll.get("cost").asInt(), 255));
    out.fromEffect = roll.get("fromEffect").asBool();
    const int64_t player = roll.get("player").asInt(-1);
    if (player >= 0 && player < static_cast<int64_t>(game.players.size()))
      snprintf(out.roller, sizeof(out.roller), "%s", game.players[player].name.c_str());
  }
  // Only the card view reads the card (from the microSD card, once per plane).
  if (screen.kind == ScreenKind::PlaneCard && out.digital) {
    out.card = TurnHubCards::planeCard(id);
    if (out.card && out.card->art) out.art = planeArtPixels(id);
  }
}

// A presence code a signed-in phone asked for: scan it or type the digits.
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
  snprintf(screen.qr, sizeof(screen.qr), "%06lu", static_cast<unsigned long>(request->code));
  snprintf(screen.qrCaption, sizeof(screen.qrCaption), "Scan or enter on that phone");
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

// A phone asking to pair: its name and the code it shows, to compare.
void formatPhonePair(AtlasScreen &screen, uint32_t nowMs) {
  const TurnHubPhones::Request *request = TurnHubPhones::waiting(nowMs);
  if (!request) return;
  snprintf(screen.badge, sizeof(screen.badge), "PHONE");
  snprintf(screen.title, sizeof(screen.title), "Allow this phone?");
  const uint32_t elapsed = nowMs - request->startedMs;
  const uint32_t leftS = elapsed >= TurnHubPhones::REQUEST_MS ? 0 : (TurnHubPhones::REQUEST_MS - elapsed + 999) / 1000;
  snprintf(screen.detail, sizeof(screen.detail), "%.32s (%lu s)", request->name, static_cast<unsigned long>(leftS));
  snprintf(screen.code, sizeof(screen.code), "%03lu %03lu", static_cast<unsigned long>(request->code / 1000),
      static_cast<unsigned long>(request->code % 1000));
  if (request->homeCode[0]) snprintf(screen.code, sizeof(screen.code), "%s", request->homeCode);
  snprintf(screen.lines[0], sizeof(screen.lines[0]), "Same code on the phone?");
  snprintf(screen.lines[1], sizeof(screen.lines[1]), "%s", TurnHubPhones::count() ? "It can then host games here."
      : "It becomes this Atlas's owner.");
  screen.lineCount = 2;
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
      betweenGames(millis()) ? "Pair Sigils, share codes, the look" : "Share codes, the look");
}

void formatDevice(AtlasScreen &screen) {
  snprintf(screen.badge, sizeof(screen.badge), "DEVICE");
  snprintf(screen.title, sizeof(screen.title), "Atlas device");
  snprintf(screen.detail, sizeof(screen.detail), "TurnHub Atlas v%s", TurnHubFirmware::VERSION);
}

void formatStorage(AtlasScreen &screen) {
  const auto info = turnhub::Json::parse(sdCardDiagnosticsJson().c_str());
  const std::string state = info.get("state").asString();
  const bool usb = sdCardOnUsb();
  const bool ready = sdCardReady();
  snprintf(screen.badge, sizeof(screen.badge), "STORAGE");
  snprintf(screen.title, sizeof(screen.title), "microSD card");
  snprintf(screen.detail, sizeof(screen.detail), "%s", usb ? "Shared with your computer" : ready ? "Ready for Atlas" : "Storage unavailable");
  if (info.has("cardMB")) {
    snprintf(screen.lines[screen.lineCount++], sizeof(screen.lines[0]), "Card: %s", info.get("type").asString().c_str());
    snprintf(screen.lines[screen.lineCount++], sizeof(screen.lines[0]), "Capacity: %lld MB", static_cast<long long>(info.get("cardMB").asInt()));
    snprintf(screen.lines[screen.lineCount++], sizeof(screen.lines[0]), "Used at mount: %lld MB", static_cast<long long>(info.get("usedKB").asInt() / 1024));
  }
  snprintf(screen.lines[screen.lineCount++], sizeof(screen.lines[0]), "%s", usb ? "Eject on PC, then turn USB off." : ready ? "Plane cards and records available." : state == "no_card" ? "Insert a microSD card." : "Card check failed; reinsert card.");
  if (usb) snprintf(screen.lines[screen.lineCount++], sizeof(screen.lines[0]), "Atlas cannot use files during USB.");
}

// Home: the table at a glance, like the app's home card. The status card
// says what the table is doing in a headline and a line (lines 0-1) over the
// players' tokens; two tiles name the hub and the players (lines 2-3).
// Nothing here ticks. Battery, SD and updates are in the header, and the
// network on QR codes.
void formatHome(AtlasScreen &screen, uint32_t nowMs) {
  formatStatus(screen, nowMs);
  char headline[sizeof(screen.title)], line[sizeof(screen.detail)];
  snprintf(headline, sizeof(headline), "%s", screen.title);
  snprintf(line, sizeof(line), "%s", screen.detail);
  snprintf(screen.title, sizeof(screen.title), "TurnHub");
  snprintf(screen.detail, sizeof(screen.detail), "Your table");
  if (!hostedShown(nowMs)) {
    snprintf(screen.lines[0], sizeof(screen.lines[0]), "Ready for a game");
    snprintf(screen.lines[1], sizeof(screen.lines[1]), "Start one in the TurnHub app");
    snprintf(screen.lines[2], sizeof(screen.lines[2]), "No hub");
    snprintf(screen.lines[3], sizeof(screen.lines[3]), "%u %s", static_cast<unsigned>(screen.sigilsOnline),
        screen.sigilsOnline == 1 ? "Sigil" : "Sigils");
    screen.lineCount = 4;
    return;
  }
  snprintf(screen.lines[0], sizeof(screen.lines[0]), "%s", headline);
  snprintf(screen.lines[1], sizeof(screen.lines[1]), "%s", line);
  snprintf(screen.lines[2], sizeof(screen.lines[2]), "%.20s", TurnHubHosted::hostedTable().lease().name.c_str());
  snprintf(screen.lines[3], sizeof(screen.lines[3]), "%u %s", static_cast<unsigned>(screen.playerCount),
      screen.playerCount == 1 ? "player" : "players");
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
// in the hub's game (and shows the Game tab), otherwise moves the orbs.
void serviceManaTap(uint32_t nowMs) {
  const turnhub::Game &game = TurnHubHosted::hostedTable().game();
  const uint8_t active = hostedShown(nowMs) && game.state == turnhub::TableState::Running && game.active
      ? static_cast<uint8_t>(*game.active + 1) : 0;
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
void formatConfirm(AtlasScreen &screen) {
  snprintf(screen.badge, sizeof(screen.badge), "CONFIRM");
  switch (confirmAction) {
    case TouchAction::UnpairSigils:
      snprintf(screen.title, sizeof(screen.title), "Unpair every Sigil?");
      snprintf(screen.detail, sizeof(screen.detail), "Each one must pair again to play.");
      break;
    case TouchAction::FactoryResetAtlas:
      snprintf(screen.title, sizeof(screen.title), "Erase this Atlas?");
      snprintf(screen.detail, sizeof(screen.detail), "Profiles, stats and settings are deleted.");
      break;
    case TouchAction::ForgetPhones:
      snprintf(screen.title, sizeof(screen.title), "Forget paired phones?");
      snprintf(screen.detail, sizeof(screen.detail), "Each phone must pair again to host.");
      break;
    default:
      snprintf(screen.title, sizeof(screen.title), "Are you sure?");
      break;
  }
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

// The phone camera reads the standard Wi-Fi payload, including the password.
void setupWifiQr(AtlasScreen &screen, uint32_t nowMs) {
  const String &stored = customWifiPassword(nowMs);
  const String password = stored.length() ? stored : String(AtlasConfig::WIFI_DEFAULT_PASSWORD);
  size_t used = 0;
  auto append = [&](const char *text, bool escape) {
    for (; *text && used + 2 < sizeof(screen.qr); ++text) {
      if (escape && strchr("\\;,:\"", *text)) screen.qr[used++] = '\\';
      screen.qr[used++] = *text;
    }
    screen.qr[used] = '\0';
  };
  append("WIFI:T:WPA;S:", false); append(AtlasConfig::WIFI_SSID, true);
  append(";P:", false); append(password.c_str(), true); append(";;", false);
  snprintf(screen.qrCaption, sizeof(screen.qrCaption), "Scan with your phone camera");
}

void formatSetup(AtlasScreen &screen, uint32_t nowMs) {
  snprintf(screen.badge, sizeof(screen.badge), "SETUP");
  if (setupStage == TurnHub::SetupStage::Finished) {
    snprintf(screen.title, sizeof(screen.title), "You're all set");
    snprintf(screen.detail, sizeof(screen.detail), "This table is ready to play");
    snprintf(screen.lines[0], sizeof(screen.lines[0]), "Atlas uses your home Wi-Fi.");
    snprintf(screen.lines[1], sizeof(screen.lines[1]), "The setup hotspot is off.");
    snprintf(screen.lines[2], sizeof(screen.lines[2]), "Next: pair your Sigils, or tap Done.");
    screen.lineCount = 3;
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
  snprintf(screen.lines[0], sizeof(screen.lines[0]), "Scan to join the setup Wi-Fi.");
  snprintf(screen.lines[1], sizeof(screen.lines[1]), "Then open TurnHub: Set up Atlas.");
  screen.lineCount = 2;
  setupWifiQr(screen, nowMs);
}

// The Wi-Fi code carries the password. The shipped default is public, so it
// shows freely; an admin-set password needs someone verified at the table.
void formatQr(AtlasScreen &screen, uint32_t nowMs) {
  snprintf(screen.badge, sizeof(screen.badge), "QR CODES");
  snprintf(screen.title, sizeof(screen.title), "Join the Wi-Fi");
  if (setupStage == TurnHub::SetupStage::Welcome) {
    snprintf(screen.detail, sizeof(screen.detail), "Scan to join %s", AtlasConfig::WIFI_SSID);
    setupWifiQr(screen, nowMs);
  } else {
    snprintf(screen.title, sizeof(screen.title), "Setup hotspot is off");
    snprintf(screen.detail, sizeof(screen.detail), "Use your home Wi-Fi");
    snprintf(screen.lines[0], sizeof(screen.lines[0]), "Open TurnHub and find this Atlas");
    snprintf(screen.lines[1], sizeof(screen.lines[1]), "on the same Wi-Fi network.");
    screen.lineCount = 2;
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
  screen.hotspotOn = atlasHotspotOn();
  screen.hotspotStations = screen.hotspotOn ? static_cast<uint8_t>(WiFi.softAPgetStationNum()) : 0;
  screen.homeWifiOn = homeWifiEnabled() && homeWifiConfigured();
  screen.homeWifiLinked = screen.homeWifiOn && homeWifiConnected();
  // Every screen carries the roll, so the planar die shows over any of them.
  fillPlanes(screen, nowMs);
  switch (screen.kind) {
    case ScreenKind::Home:
      formatHome(screen, nowMs);
      break;
    case ScreenKind::ManaTap: formatManaTap(screen); break;
    case ScreenKind::Confirm: formatConfirm(screen); break;
    case ScreenKind::Status: formatStatus(screen, nowMs); break;
    case ScreenKind::Qr: formatQr(screen, nowMs); break;
    case ScreenKind::Code: formatCode(screen, nowMs); break;
    case ScreenKind::PairCode: formatPairCode(screen, nowMs); break;
    case ScreenKind::PhonePair: formatPhonePair(screen, nowMs); break;
    case ScreenKind::Menu: formatMenu(screen); break;
    case ScreenKind::Themes:
      snprintf(screen.badge, sizeof(screen.badge), "THEME");
      snprintf(screen.title, sizeof(screen.title), "%s", TurnHubTheme::palette(atlasDisplayTheme()).label);
      snprintf(screen.detail, sizeof(screen.detail), "Choose the look for this Atlas");
      break;
    case ScreenKind::Device: formatDevice(screen); break;
    case ScreenKind::Storage: formatStorage(screen); break;
    case ScreenKind::Power:
      snprintf(screen.badge, sizeof(screen.badge), "POWER");
      snprintf(screen.title, sizeof(screen.title), "Sound, screen, sleep");
      snprintf(screen.detail, sizeof(screen.detail), "Deep sleep runs on battery only");
      break;
    case ScreenKind::Setup: formatSetup(screen, nowMs); break;
    case ScreenKind::Planes: formatPlanes(screen); break;
    case ScreenKind::PlaneCard: snprintf(screen.title, sizeof(screen.title), "%s", screen.planes.plane); break;
    default: break;
  }
  layoutButtons(screen, nowMs);
  if (syncText[0] != '\0' && nowMs - syncAtMs < SYNC_TOAST_MS) snprintf(screen.sync, sizeof(screen.sync), "%s", syncText);
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

void showSyncStatus(uint32_t nowMs, const char *text) {
  snprintf(syncText, sizeof(syncText), "%s", text ? text : "");
  syncAtMs = nowMs;
}

void resetTouchControls(bool startAtHome) {
  syncText[0] = '\0';
  touchDown = false;
  pressInside = false;
  pressedAction = TouchAction::None;
  pressedArg = 0;
  noticeText[0] = '\0';
  openScreen = startAtHome ? ScreenKind::Home : ScreenKind::Status;
  confirmAction = TouchAction::None;
  setupSkipped = false;
  manaTap = TurnHub::ManaTap();
  manaTurnPaused = false;
  manaTurnPlayer = 0;
}

}  // namespace TurnHubAtlas
