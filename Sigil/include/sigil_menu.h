#pragma once

// Sigil action menu (CAPABILITY_MENU). Atlas sends which actions this Sigil
// may use (MenuState); this module lets the player choose one with five keys
// and yields a SelectAction to send. It decides nothing about the game: Atlas
// validates every choice. Pure logic, host-tested.
//
// Two layouts over the same actions:
//   Compass (e-ink): every action has a fixed key, so the screen only has to
//     redraw when the menu changes. Click (Select) is the likely action.
//   List (OLED): any key opens the list at the default action; Up/Down move,
//     Select/Right choose, Left closes.
// Deliberate actions (ActionHold) are sent only once their key is held.

#include <stdint.h>

#include "protocol.h"

namespace TurnHubSigil {

enum class Key : uint8_t { Up, Down, Left, Right, Select, Count };
constexpr uint8_t KEY_COUNT = static_cast<uint8_t>(Key::Count);
enum class MenuLayout : uint8_t { Compass, List };

constexpr uint8_t MENU_NONE = TurnHubProtocol::SIGIL_ACTION_NONE;
constexpr uint8_t MENU_MAX_ITEMS = static_cast<uint8_t>(TurnHubProtocol::SigilAction::Count);
// An open list closes by itself after this long without a key.
constexpr uint32_t MENU_LIST_IDLE_MS = 10000;

// Short label for an action (at most 12 characters).
const char *sigilActionLabel(TurnHubProtocol::SigilAction action);

// Everything a display needs to draw the menu; copied to the display task.
struct MenuView {
  bool active = false;          // Atlas sends menus (else legacy gestures).
  uint8_t compass[KEY_COUNT];   // Action per key (MENU_NONE if none).
  bool listOpen = false;
  uint8_t items[MENU_MAX_ITEMS];
  uint8_t itemCount = 0;
  uint8_t cursor = 0;           // Index into items.
  uint8_t holdAction = MENU_NONE;  // Being held right now (hold feedback).
  bool life = false;            // AdjustLife offered: Left/Right change life when free.

  MenuView();
  bool operator==(const MenuView &o) const;
  bool operator!=(const MenuView &o) const { return !(*this == o); }
};

struct MenuChoice {
  bool ready = false;
  TurnHubProtocol::SigilAction action = TurnHubProtocol::SigilAction::Count;
  uint8_t revision = 0;
};

class SigilMenu {
 public:
  explicit SigilMenu(MenuLayout layout) : layout_(layout) {}

  void applyMenuState(int32_t value, uint32_t nowMs);
  void applyMenuState2(int32_t value, uint32_t nowMs);  // MenuState2 (Leave and later).
  void clear();  // Unpaired: back to legacy gestures until Atlas sends a menu.
  void setHoldTimes(uint16_t longPressMs, uint16_t winHoldMs);

  bool active() const { return active_; }
  bool lifeOffered() const {
    return active_ && (actions_ & TurnHubProtocol::sigilActionBit(TurnHubProtocol::SigilAction::AdjustLife)) != 0;
  }
  bool listOpen() const { return listOpen_; }
  uint32_t actions() const { return actions_; }

  void keyDown(Key key, uint32_t nowMs);
  void keyUp(Key key, uint32_t nowMs);
  // Completes holds and idle-closes the list; returns a choice to send once.
  MenuChoice update(uint32_t nowMs);

  // 0-255 progress of a held deliberate action; 0 when none.
  uint8_t holdProgress(uint32_t nowMs) const;
  MenuView view() const;

  // The compass key for an action given the other actions on offer (fixed
  // preferences, first free; Link phone takes whatever is left).
  static uint8_t compassAction(uint32_t actions, Key key);

 private:
  // AdjustLife is not a list item or compass slot: it frees Left/Right
  // (lifeOffered, main.cpp's LifeAdjuster).
  bool offered(uint8_t action) const {
    return action < MENU_MAX_ITEMS && action != static_cast<uint8_t>(TurnHubProtocol::SigilAction::AdjustLife) &&
        (actions_ & (1u << action)) != 0;
  }
  void applyFields(const TurnHubProtocol::MenuStateFields &f, uint32_t nowMs);
  uint8_t itemAt(uint8_t index) const;
  uint8_t itemCount() const;
  uint8_t indexOf(uint8_t action) const;
  void choose(uint8_t action, Key key, uint32_t nowMs);
  void emit(uint8_t action);
  void closeList() { listOpen_ = false; }

  const MenuLayout layout_;
  bool active_ = false;
  uint32_t actions_ = 0;
  uint8_t defaultAction_ = MENU_NONE;
  uint8_t revision_ = 0;
  uint16_t longPressMs_ = TurnHubProtocol::DEFAULT_LONG_PRESS_MS;
  uint16_t winHoldMs_ = TurnHubProtocol::DEFAULT_WIN_HOLD_MS;

  bool listOpen_ = false;
  uint8_t cursor_ = 0;
  uint32_t lastKeyMs_ = 0;

  bool holding_ = false;
  Key holdKey_ = Key::Select;
  uint8_t holdAction_ = MENU_NONE;
  uint32_t holdStartMs_ = 0;
  uint32_t holdMs_ = 0;

  MenuChoice pending_;
};

}  // namespace TurnHubSigil
