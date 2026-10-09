#pragma once

// Sigil action menu (every Sigil). Atlas sends which actions this Sigil
// may use (MenuState); this module lets the player choose one with five keys
// and yields a SelectAction to send. It decides nothing about the game: Atlas
// validates every choice. Pure logic, host-tested.
//
// One rule set for every stage and both displays (owner, 2026-10-06):
// - Click: the one obvious next step (Join, Start, Pass, Resume, Rematch,
//   Confirm). Left/Right: life in a game; Seat B and Next starter in the
//   lobby; Deny/Cancel and Other seat in a table decision. Down: Switch seat
//   on a shared Sigil. Up: Menu, always.
// - Menu holds every other action Atlas offers, in a fixed order, then
//   Device (Sleep, Unpair, Factory reset). With nothing else on offer (Atlas
//   lost) Menu opens on the Device entries.
// - The OLED shows Menu as a scrolling list: Up/Down move, the click or Right
//   chooses, Left goes back. E-ink shows it as compass pages: the click, Up,
//   Right and Down hold up to four entries (Down becomes More when there are
//   more), Left goes back a page or closes it. One full refresh per page.
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
constexpr uint8_t MENU_LOCAL_DEVICE_MENU = MENU_MAX_ITEMS + 1;  // Opens Menu (Up).
constexpr uint8_t MENU_LOCAL_BACK = MENU_MAX_ITEMS + 2;         // Back a level, page or out.
constexpr uint8_t MENU_LOCAL_UNPAIR = MENU_MAX_ITEMS + 3;
// Deep sleep until the joystick is clicked (main.cpp's enterSleep). A tap:
// nothing is lost, and the Sigil reconnects when it wakes.
constexpr uint8_t MENU_LOCAL_SLEEP = MENU_MAX_ITEMS + 4;
// Opens the Device entries (Sleep, Unpair, Factory reset, Theme).
constexpr uint8_t MENU_LOCAL_DEVICE = MENU_MAX_ITEMS + 5;
// E-ink only: the next page of Menu.
constexpr uint8_t MENU_LOCAL_MORE = MENU_MAX_ITEMS + 6;
// Cycles the local display appearance without closing Menu; never sent to Atlas.
constexpr uint8_t MENU_LOCAL_THEME = MENU_MAX_ITEMS + 7;
constexpr uint8_t MENU_LOCAL_INVERT = MENU_MAX_ITEMS + 8;
static_assert(MENU_LOCAL_INVERT < TurnHubProtocol::SIGIL_ACTION_NONE, "menu ids must stay below MENU_NONE");
constexpr uint32_t MENU_UNPAIR_HOLD_MS = 3000;
constexpr uint32_t MENU_FACTORY_RESET_HOLD_MS = 5000;
// The most Menu rows: every Atlas action but AdjustLife, then Device and Back.
constexpr uint8_t MENU_LIST_MAX = MENU_MAX_ITEMS - 1 + 2;
// True for actions chosen by holding a key (shown "(hold)" in the legend).
bool menuActionNeedsHold(uint8_t action);
// An open Menu closes by itself after this long without a key.
constexpr uint32_t MENU_DEVICE_IDLE_MS = 10000;

// Short label for an action or device-local entry (at most 12 characters).
const char *sigilActionLabel(TurnHubProtocol::SigilAction action);

// Everything a display needs to draw the menu; copied to the display task.
struct MenuView {
  bool active = false;          // Atlas has sent a menu (none yet after pairing).
  uint8_t compass[KEY_COUNT];   // Action per key (MENU_NONE if none).
  bool deviceMenu = false;      // Menu is open (the compass holds its entries).
  uint8_t holdAction = MENU_NONE;  // Being held right now (OLED hold feedback).
  bool life = false;            // AdjustLife offered: Left/Right change life when free.
  bool list = false;            // List style (OLED).
  bool recovery = false;        // The Device entries are shown.
  uint8_t page = 0;             // E-ink: the Menu page shown (0 first).
  uint8_t pageCount = 0;        // E-ink: Menu pages (0 when closed).
  // List style with Menu open: its rows (top first) and the highlighted row.
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
  void cancelInput() { holding_ = false; pending_ = MenuChoice(); closeMenu(); }
  // Atlas lost: drop Atlas's menu; Up still opens Menu, on the Device entries.
  // endOffline() takes it away again, unless Atlas has sent a menu since.
  void setOffline();
  void endOffline();
  bool offline() const { return offline_; }
  void setHoldTimes(uint16_t longPressMs, uint16_t winHoldMs);

  bool active() const { return active_; }
  // AdjustLife is offered. An unsent life total survives an open Menu; its
  // keys are Menu's while it is open (keyAction).
  bool lifeOffered() const { return active_ && inGame(); }
  // Menu is open.
  bool deviceMenuOpen() const { return menuOpen_; }
  bool listStyle() const { return list_; }
  uint32_t actions() const { return actions_; }
  // What a key does right now (an action, a device-local entry or MENU_NONE).
  uint8_t keyAction(Key key) const;

  void keyDown(Key key, uint32_t nowMs);
  void keyUp(Key key, uint32_t nowMs);
  // Completes holds and idle-closes Menu; returns a choice to send once.
  MenuChoice update(uint32_t nowMs);

  // 0-255 progress of a held deliberate action; 0 when none.
  uint8_t holdProgress(uint32_t nowMs) const;
  MenuView view() const;

  // The action on a key with Menu closed (Up is always Menu).
  static uint8_t bareAction(uint32_t actions, Key key);
  // Menu's entries for these actions: every offered action not on a bare key,
  // in a fixed order, then Device. With none, the Device entries themselves.
  // device: the Device entries (Sleep, Unpair, Factory reset). No Back row.
  static uint8_t menuEntries(uint32_t actions, uint8_t out[MENU_LIST_MAX], bool device = false);

 private:
  // Atlas offers AdjustLife only during a game.
  bool inGame() const {
    return (actions_ & TurnHubProtocol::sigilActionBit(TurnHubProtocol::SigilAction::AdjustLife)) != 0;
  }
  // Atlas offers this action now (AdjustLife is never chosen: it frees Left/Right).
  bool offered(uint8_t action) const {
    return action < MENU_MAX_ITEMS && action != static_cast<uint8_t>(TurnHubProtocol::SigilAction::AdjustLife) &&
        (actions_ & (1u << action)) != 0;
  }
  void applyFields(const TurnHubProtocol::MenuStateFields &f);
  // The entries of the open level (Menu or Device); returns the count.
  uint8_t levelEntries(uint8_t out[MENU_LIST_MAX]) const;
  // E-ink: Menu pages for count entries, and the entry on a key of a page.
  static uint8_t pageCount(uint8_t count);
  static uint8_t pageAction(const uint8_t entries[], uint8_t count, uint8_t page, Key key);
  void choose(uint8_t action, Key key, uint32_t nowMs);
  void emit(uint8_t action);
  void openMenu();
  void closeMenu() { menuOpen_ = false; deviceOpen_ = false; deviceDirect_ = false; page_ = 0; }
  void back();
  // The open list's highlighted row: the remembered action if still listed,
  // else the remembered position (clamped).
  uint8_t cursorRow(const uint8_t rows[], uint8_t count) const;
  void moveCursor(int8_t step);
  void listRows(uint8_t rows[MENU_LIST_MAX], uint8_t &count) const;

  const bool list_;
  const bool holdOnScreen_;
  bool active_ = false;
  bool offline_ = false;
  uint32_t actions_ = 0;
  uint8_t revision_ = 0;
  uint16_t longPressMs_ = TurnHubProtocol::DEFAULT_LONG_PRESS_MS;
  uint16_t winHoldMs_ = TurnHubProtocol::DEFAULT_WIN_HOLD_MS;

  bool menuOpen_ = false;
  bool deviceOpen_ = false;    // The Device entries are shown.
  bool deviceDirect_ = false;  // Menu opened straight onto them (nothing else offered).
  uint8_t page_ = 0;           // E-ink page.
  uint32_t lastKeyMs_ = 0;
  uint8_t cursor_ = 0;                // List row last highlighted.
  uint8_t cursorAction_ = MENU_NONE;  // What it held.

  bool holding_ = false;
  bool holdFromMenu_ = false;  // Close Menu when the hold completes.
  Key holdKey_ = Key::Select;
  uint8_t holdAction_ = MENU_NONE;
  uint32_t holdStartMs_ = 0;
  uint32_t holdMs_ = 0;

  MenuChoice pending_;
};

}  // namespace TurnHubSigil
