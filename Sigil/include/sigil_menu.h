#pragma once

// Sigil action menu (every Sigil). Atlas sends which actions this Sigil
// may use (MenuState); this module lets the player choose one with five keys
// and yields a SelectAction to send. It decides nothing about the game: Atlas
// validates every choice. Pure logic, host-tested.
//
// Two styles (owner, 2026-10-02):
// - Compass (e-ink): every action has a fixed key, so muscle memory works and
//   the e-ink only redraws when the menu changes. Click (Select) is the likely
//   action. In Commander games, Up opens a game submenu for damage, Undo,
//   Pause/Resume and Claim win; Down switches shared seats. Outside a game, the first of Up/Down with nothing on it is Menu: a
//   device menu, also a compass, with Unpair (held) on the click, Sleep on Up,
//   Factory reset (held longer) on Down and Back on Left. While Atlas is lost
//   (setOffline) only that Menu is offered, on Up.
// - List (OLED): the click still does the likely action (Pass in a game), as
//   the compass would, Left/Right change life in a game, and Up opens Menu at
//   any time: one scrolling list of every action Atlas offers, then Sleep,
//   Device recovery and Back. Device recovery is one list deeper (owner,
//   2026-10-02) and holds Unpair, Factory reset and Back. Up/Down move, the
//   click (or Right) chooses, Left goes back a level. While Atlas is lost the
//   list holds only the device entries.
// Deliberate actions (ActionHold) are sent only once their key is held.
#include <stdint.h>

#include "protocol.h"

namespace TurnHubSigil {

enum class Key : uint8_t { Up, Down, Left, Right, Select, Count };
constexpr uint8_t KEY_COUNT = static_cast<uint8_t>(Key::Count);

constexpr uint8_t MENU_NONE = TurnHubProtocol::SIGIL_ACTION_NONE;
constexpr uint8_t MENU_MAX_ITEMS = static_cast<uint8_t>(TurnHubProtocol::SigilAction::Count);
// Device-local entries: never in Atlas's MenuState (the mask names only Atlas actions)
// and never sent to Atlas. Held in the device menu, Unpair makes main.cpp
// forget the saved Atlas pairing, and Factory reset erases this Sigil. They
// stand beside the Pair button's holds, which also work when the screen or
// menu is not.
constexpr uint8_t MENU_LOCAL_FACTORY_RESET = MENU_MAX_ITEMS;
constexpr uint8_t MENU_LOCAL_DEVICE_MENU = MENU_MAX_ITEMS + 1;  // Opens the device menu.
constexpr uint8_t MENU_LOCAL_BACK = MENU_MAX_ITEMS + 2;         // Closes it.
constexpr uint8_t MENU_LOCAL_UNPAIR = MENU_MAX_ITEMS + 3;
// Deep sleep until the joystick is clicked (main.cpp's enterSleep). A tap:
// nothing is lost, and the Sigil reconnects when it wakes.
constexpr uint8_t MENU_LOCAL_SLEEP = MENU_MAX_ITEMS + 4;
// OLED list only: opens the Device recovery list (Unpair, Factory reset).
constexpr uint8_t MENU_LOCAL_RECOVERY = MENU_MAX_ITEMS + 5;
static_assert(MENU_LOCAL_RECOVERY < TurnHubProtocol::SIGIL_ACTION_NONE, "menu ids must stay below MENU_NONE");
constexpr uint32_t MENU_UNPAIR_HOLD_MS = 3000;
constexpr uint32_t MENU_FACTORY_RESET_HOLD_MS = 5000;
// The OLED list's most rows: every Atlas action but AdjustLife, then Sleep,
// Device recovery and Back.
constexpr uint8_t MENU_LIST_MAX = MENU_MAX_ITEMS - 1 + 3;
// True for actions chosen by holding a key (shown "(hold)" in the legend).
bool menuActionNeedsHold(uint8_t action);
// An open device menu (or OLED list) closes by itself after this long without a key.
constexpr uint32_t MENU_DEVICE_IDLE_MS = 10000;

// Short label for an action or device-local entry (at most 12 characters).
const char *sigilActionLabel(TurnHubProtocol::SigilAction action);

// Everything a display needs to draw the menu; copied to the display task.
struct MenuView {
  bool active = false;          // Atlas has sent a menu (none yet after pairing).
  uint8_t compass[KEY_COUNT];   // Action per key (MENU_NONE if none).
  bool deviceMenu = false;      // The device menu is open (compass holds its entries).
  uint8_t holdAction = MENU_NONE;  // Being held right now (OLED hold feedback).
  bool life = false;            // AdjustLife offered: Left/Right change life when free.
  // List style (OLED) with the menu open: its rows (actions and device-local
  // entries, top first) and the highlighted row. Empty otherwise.
  bool list = false;
  bool recovery = false;  // The Device recovery list (one level down) is shown.
  uint8_t rowCount = 0;
  uint8_t cursor = 0;
  uint8_t rows[MENU_LIST_MAX];

  MenuView();
  bool operator==(const MenuView &o) const;
  bool operator!=(const MenuView &o) const { return !(*this == o); }
};

struct MenuChoice {
  bool ready = false;
  TurnHubProtocol::SigilAction action = TurnHubProtocol::SigilAction::Count;
  uint8_t revision = 0;
};

enum class MenuStyle : uint8_t { Compass, List };

class SigilMenu {
 public:
  // The list (OLED) also names the action being held in its view. The e-ink
  // compass leaves hold progress to the status light: a refresh costs seconds.
  explicit SigilMenu(MenuStyle style) : list_(style == MenuStyle::List), holdOnScreen_(list_) {}

  void applyMenuState2(int32_t value, uint32_t nowMs);
  void clear();  // Unpaired: no menu until Atlas sends one.
  // Atlas lost: drop Atlas's menu and offer only the device menu (Menu on Up).
  // endOffline() takes it away again, unless Atlas has sent a menu since.
  void setOffline();
  void endOffline();
  bool offline() const { return offline_; }
  void setHoldTimes(uint16_t longPressMs, uint16_t winHoldMs);

  bool active() const { return active_; }
  // AdjustLife is offered and the device menu is closed. The OLED list may
  // be open over a game: an unsent life total survives it (its keys are the
  // list's while it is open, keyAction).
  bool lifeOffered() const { return active_ && inGame() && (list_ || !deviceMenuOpen_); }
  // The device menu (compass) or the OLED's list is open.
  bool deviceMenuOpen() const { return deviceMenuOpen_; }
  bool listStyle() const { return list_; }
  uint32_t actions() const { return actions_; }
  // What a key does right now (an action, a device-local entry or MENU_NONE).
  uint8_t keyAction(Key key) const;

  void keyDown(Key key, uint32_t nowMs);
  void keyUp(Key key, uint32_t nowMs);
  // Completes holds and idle-closes the device menu; returns a choice to send once.
  MenuChoice update(uint32_t nowMs);

  // 0-255 progress of a held deliberate action; 0 when none.
  uint8_t holdProgress(uint32_t nowMs) const;
  MenuView view() const;

  // The compass key for an action given the other actions on offer (fixed
  // preferences, first free; Link phone takes whatever is left).
  static uint8_t compassAction(uint32_t actions, Key key);
  // The OLED list's rows for these actions (a fixed order, then Sleep,
  // Device recovery and Back); returns the count. With none (Atlas lost) only
  // the device entries remain. recovery: the Device recovery list instead.
  static uint8_t listRows(uint32_t actions, uint8_t rows[MENU_LIST_MAX], bool recovery = false);

 private:
  // Atlas offers AdjustLife only during a game.
  bool inGame() const {
    return (actions_ & TurnHubProtocol::sigilActionBit(TurnHubProtocol::SigilAction::AdjustLife)) != 0;
  }
  // AdjustLife is not a compass slot: it frees Left/Right (lifeOffered,
  // main.cpp's LifeAdjuster).
  bool offered(uint8_t action) const {
    if (action == MENU_LOCAL_FACTORY_RESET || action == MENU_LOCAL_UNPAIR || action == MENU_LOCAL_BACK ||
        action == MENU_LOCAL_SLEEP || action == MENU_LOCAL_RECOVERY) {
      return active_ && deviceMenuOpen_;
    }
    if (action == MENU_LOCAL_DEVICE_MENU) return active_ && !deviceMenuOpen_ && (list_ || commanderMenu() || menuKey() != MENU_NONE);
    return action < MENU_MAX_ITEMS && action != static_cast<uint8_t>(TurnHubProtocol::SigilAction::AdjustLife) &&
        (actions_ & (1u << action)) != 0;
  }
  // The key that opens the device menu: the first of Up/Down with nothing on
  // it, outside a game (AdjustLife not offered); MENU_NONE if neither.
  bool commanderMenu() const { return (actions_ & TurnHubProtocol::sigilActionBit(TurnHubProtocol::SigilAction::CommanderDamage)) != 0; }
  uint8_t menuKey() const;
  void applyFields(const TurnHubProtocol::MenuStateFields &f);
  void choose(uint8_t action, Key key, uint32_t nowMs);
  void emit(uint8_t action);
  void closeDeviceMenu() { deviceMenuOpen_ = false; recoveryOpen_ = false; }
  // The open list's highlighted row: the remembered action if still listed,
  // else the remembered position (clamped).
  uint8_t cursorRow(const uint8_t rows[], uint8_t count) const;
  void moveCursor(int8_t step);

  const bool list_;
  const bool holdOnScreen_;
  bool active_ = false;
  bool offline_ = false;
  uint32_t actions_ = 0;
  uint8_t revision_ = 0;
  uint16_t longPressMs_ = TurnHubProtocol::DEFAULT_LONG_PRESS_MS;
  uint16_t winHoldMs_ = TurnHubProtocol::DEFAULT_WIN_HOLD_MS;

  bool deviceMenuOpen_ = false;
  bool recoveryOpen_ = false;  // The list shows Device recovery (OLED).
  uint32_t lastKeyMs_ = 0;
  uint8_t cursor_ = 0;                // List row last highlighted.
  uint8_t cursorAction_ = MENU_NONE;  // What it held.

  bool holding_ = false;
  Key holdKey_ = Key::Select;
  uint8_t holdAction_ = MENU_NONE;
  uint32_t holdStartMs_ = 0;
  uint32_t holdMs_ = 0;

  MenuChoice pending_;
};

}  // namespace TurnHubSigil
