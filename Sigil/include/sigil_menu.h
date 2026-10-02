#pragma once

// Sigil action menu (every Sigil). Atlas sends which actions this Sigil
// may use (MenuState); this module lets the player choose one with five keys
// and yields a SelectAction to send. It decides nothing about the game: Atlas
// validates every choice. Pure logic, host-tested.
//
// A compass on both Sigils (owner, 2026-10-02): every action has a fixed key,
// so muscle memory works and the e-ink only redraws when the menu changes.
// Click (Select) is the likely action. Outside a game, the first of Up/Down
// with nothing on it is Menu: a device menu, also a compass, with Unpair
// (held) on the click, Factory reset (held longer) on Down and Back on Left.
// Deliberate actions (ActionHold) are sent only once their key is held.

#include <stdint.h>

#include "protocol.h"

namespace TurnHubSigil {

enum class Key : uint8_t { Up, Down, Left, Right, Select, Count };
constexpr uint8_t KEY_COUNT = static_cast<uint8_t>(Key::Count);

constexpr uint8_t MENU_NONE = TurnHubProtocol::SIGIL_ACTION_NONE;
constexpr uint8_t MENU_MAX_ITEMS = static_cast<uint8_t>(TurnHubProtocol::SigilAction::Count);
// Device-local entries: never in Atlas's MenuState (that 24-bit mask is full)
// and never sent to Atlas. Held in the device menu, Unpair makes main.cpp
// forget the saved Atlas pairing, and Factory reset erases this Sigil. They
// stand beside the Pair button's holds, which also work when the screen or
// menu is not.
constexpr uint8_t MENU_LOCAL_FACTORY_RESET = MENU_MAX_ITEMS;
constexpr uint8_t MENU_LOCAL_DEVICE_MENU = MENU_MAX_ITEMS + 1;  // Opens the device menu.
constexpr uint8_t MENU_LOCAL_BACK = MENU_MAX_ITEMS + 2;         // Closes it.
constexpr uint8_t MENU_LOCAL_UNPAIR = MENU_MAX_ITEMS + 3;
static_assert(MENU_LOCAL_UNPAIR < TurnHubProtocol::SIGIL_ACTION_NONE, "menu ids must stay below MENU_NONE");
constexpr uint32_t MENU_UNPAIR_HOLD_MS = 3000;
constexpr uint32_t MENU_FACTORY_RESET_HOLD_MS = 5000;
// True for actions chosen by holding a key (shown "(hold)" in the legend).
bool menuActionNeedsHold(uint8_t action);
// An open device menu closes by itself after this long without a key.
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
  // holdOnScreen: the view names the action being held (OLED). The e-ink
  // leaves hold progress to the status light: a refresh for it costs seconds.
  explicit SigilMenu(bool holdOnScreen) : holdOnScreen_(holdOnScreen) {}

  void applyMenuState2(int32_t value, uint32_t nowMs);
  void clear();  // Unpaired: no menu until Atlas sends one.
  void setHoldTimes(uint16_t longPressMs, uint16_t winHoldMs);

  bool active() const { return active_; }
  // AdjustLife is offered and the device menu is closed.
  bool lifeOffered() const { return active_ && !deviceMenuOpen_ && inGame(); }
  bool deviceMenuOpen() const { return deviceMenuOpen_; }
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

 private:
  // Atlas offers AdjustLife only during a game.
  bool inGame() const {
    return (actions_ & TurnHubProtocol::sigilActionBit(TurnHubProtocol::SigilAction::AdjustLife)) != 0;
  }
  // AdjustLife is not a compass slot: it frees Left/Right (lifeOffered,
  // main.cpp's LifeAdjuster).
  bool offered(uint8_t action) const {
    if (action == MENU_LOCAL_FACTORY_RESET || action == MENU_LOCAL_UNPAIR || action == MENU_LOCAL_BACK) {
      return active_ && deviceMenuOpen_;
    }
    if (action == MENU_LOCAL_DEVICE_MENU) return active_ && !deviceMenuOpen_ && menuKey() != MENU_NONE;
    return action < MENU_MAX_ITEMS && action != static_cast<uint8_t>(TurnHubProtocol::SigilAction::AdjustLife) &&
        (actions_ & (1u << action)) != 0;
  }
  // The key that opens the device menu: the first of Up/Down with nothing on
  // it, outside a game (AdjustLife not offered); MENU_NONE if neither.
  uint8_t menuKey() const;
  void applyFields(const TurnHubProtocol::MenuStateFields &f);
  void choose(uint8_t action, Key key, uint32_t nowMs);
  void emit(uint8_t action);
  void closeDeviceMenu() { deviceMenuOpen_ = false; }

  const bool holdOnScreen_;
  bool active_ = false;
  uint32_t actions_ = 0;
  uint8_t revision_ = 0;
  uint16_t longPressMs_ = TurnHubProtocol::DEFAULT_LONG_PRESS_MS;
  uint16_t winHoldMs_ = TurnHubProtocol::DEFAULT_WIN_HOLD_MS;

  bool deviceMenuOpen_ = false;
  uint32_t lastKeyMs_ = 0;

  bool holding_ = false;
  Key holdKey_ = Key::Select;
  uint8_t holdAction_ = MENU_NONE;
  uint32_t holdStartMs_ = 0;
  uint32_t holdMs_ = 0;

  MenuChoice pending_;
};

}  // namespace TurnHubSigil
