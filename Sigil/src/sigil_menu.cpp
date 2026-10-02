#include "sigil_menu.h"

#include <string.h>

namespace TurnHubSigil {

using TurnHubProtocol::ActionHold;
using TurnHubProtocol::SigilAction;

namespace {

constexpr uint8_t KEY_NONE = 0xFF;
constexpr uint8_t U = static_cast<uint8_t>(Key::Up);
constexpr uint8_t D = static_cast<uint8_t>(Key::Down);
constexpr uint8_t L = static_cast<uint8_t>(Key::Left);
constexpr uint8_t R = static_cast<uint8_t>(Key::Right);
constexpr uint8_t C = static_cast<uint8_t>(Key::Select);

// Compass keys each action prefers, best first. Fixed so muscle memory works:
// click = the likely action, Up = pause/resume, Down = deliberate (hold),
// Left = no/back/cancel, Right = yes/next.
struct Slots { uint8_t keys[KEY_COUNT]; };
Slots preferences(uint8_t action) {
  switch (static_cast<SigilAction>(action)) {
    case SigilAction::Join:
    case SigilAction::StartGame:
    case SigilAction::Pass:
    case SigilAction::ConfirmWin:
    case SigilAction::Eliminate:
    case SigilAction::Rematch: return {{C, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE}};
    case SigilAction::BeginElimination: return {{C, D, KEY_NONE, KEY_NONE, KEY_NONE}};
    case SigilAction::RandomStarter:
    case SigilAction::Pause:
    case SigilAction::Resume: return {{U, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE}};
    case SigilAction::ClaimWin:
    case SigilAction::ResetTable: return {{D, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE}};
    // Click again to undo a pass, like Atlas's "pass again" (Pass itself is
    // not offered during the grace period, so the click is free).
    case SigilAction::CancelPass: return {{C, L, KEY_NONE, KEY_NONE, KEY_NONE}};
    case SigilAction::AddSeatB:
    case SigilAction::RemoveSeatB:
    case SigilAction::CancelStart:
    case SigilAction::DenyWin:
    case SigilAction::CancelElimination: return {{L, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE}};
    case SigilAction::CycleStarter:
    case SigilAction::NextTarget: return {{R, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE}};
    case SigilAction::LinkPhone: return {{D, R, L, U, C}};
    case SigilAction::Leave: return {{D, L, KEY_NONE, KEY_NONE, KEY_NONE}};
    // Whatever is left of click, Up and Down (never Left/Right: they change
    // life). Waiting on another player's turn, that is the click.
    case SigilAction::SwitchSeat: return {{C, U, D, KEY_NONE, KEY_NONE}};
    default: return {{KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE}};
  }
}

}  // namespace

bool menuActionNeedsHold(uint8_t action) {
  return action == MENU_LOCAL_FACTORY_RESET || action == MENU_LOCAL_UNPAIR ||
      (action < MENU_MAX_ITEMS &&
       TurnHubProtocol::sigilActionHold(static_cast<SigilAction>(action)) != ActionHold::None);
}

const char *sigilActionLabel(SigilAction action) {
  switch (static_cast<uint8_t>(action)) {
    case MENU_LOCAL_FACTORY_RESET: return "Factory reset";
    case MENU_LOCAL_DEVICE_MENU: return "Menu";
    case MENU_LOCAL_BACK: return "Back";
    case MENU_LOCAL_UNPAIR: return "Unpair";
    case MENU_LOCAL_SLEEP: return "Sleep";
    default: break;
  }
  switch (action) {
    case SigilAction::Join: return "Join game";
    case SigilAction::CycleStarter: return "Next starter";
    case SigilAction::RandomStarter: return "Random start";
    case SigilAction::AddSeatB: return "Add seat B";
    case SigilAction::RemoveSeatB: return "Drop seat B";
    case SigilAction::StartGame: return "Start game";
    case SigilAction::CancelStart: return "Cancel start";
    case SigilAction::Pass: return "Pass turn";
    case SigilAction::CancelPass: return "Undo pass";
    case SigilAction::Pause: return "Pause";
    case SigilAction::Resume: return "Resume";
    case SigilAction::ClaimWin: return "Claim win";
    case SigilAction::ConfirmWin: return "Confirm win";
    case SigilAction::DenyWin: return "Deny win";
    case SigilAction::BeginElimination: return "I'm out";
    case SigilAction::NextTarget: return "Other seat";
    case SigilAction::Eliminate: return "Confirm out";
    case SigilAction::CancelElimination: return "Cancel";
    case SigilAction::Rematch: return "Rematch";
    case SigilAction::ResetTable: return "Reset table";
    case SigilAction::LinkPhone: return "Link phone";
    case SigilAction::Leave: return "Leave lobby";
    case SigilAction::AdjustLife: return "Change life";
    case SigilAction::SwitchSeat: return "Switch seat";
    default: return "";
  }
}

MenuView::MenuView() {
  memset(compass, MENU_NONE, sizeof(compass));
}

bool MenuView::operator==(const MenuView &o) const {
  return active == o.active && deviceMenu == o.deviceMenu && life == o.life &&
      holdAction == o.holdAction && memcmp(compass, o.compass, sizeof(compass)) == 0;
}

uint8_t SigilMenu::compassAction(uint32_t actions, Key key) {
  // Assign in action order (Link phone before Leave: a waiting phone
  // link outranks Leave for the last free key; Leave comes back after).
  uint8_t owner[KEY_COUNT];
  memset(owner, MENU_NONE, sizeof(owner));
  for (uint8_t action = 0; action < MENU_MAX_ITEMS; ++action) {
    if ((actions & (1u << action)) == 0) continue;
    const Slots slots = preferences(action);
    for (uint8_t k : slots.keys) {
      if (k == KEY_NONE) break;
      if (owner[k] == MENU_NONE) {
        owner[k] = action;
        break;
      }
    }
  }
  return owner[static_cast<uint8_t>(key)];
}

uint8_t SigilMenu::menuKey() const {
  if (!active_ || inGame()) return MENU_NONE;
  for (Key key : {Key::Up, Key::Down}) {
    if (compassAction(actions_, key) == MENU_NONE) return static_cast<uint8_t>(key);
  }
  return MENU_NONE;
}

uint8_t SigilMenu::keyAction(Key key) const {
  if (!active_ || key >= Key::Count) return MENU_NONE;
  if (deviceMenuOpen_) {
    // The milder action on the click; Down, the compass's deliberate key,
    // for the one that erases everything.
    if (key == Key::Select) return MENU_LOCAL_UNPAIR;
    if (key == Key::Up) return MENU_LOCAL_SLEEP;
    if (key == Key::Down) return MENU_LOCAL_FACTORY_RESET;
    if (key == Key::Left) return MENU_LOCAL_BACK;
    return MENU_NONE;
  }
  const uint8_t action = compassAction(actions_, key);
  if (action != MENU_NONE) return action;
  return menuKey() == static_cast<uint8_t>(key) ? MENU_LOCAL_DEVICE_MENU : MENU_NONE;
}

void SigilMenu::applyMenuState2(int32_t value, uint32_t nowMs) {
  (void)nowMs;
  applyFields(TurnHubProtocol::decodeMenuState2(value));
}

void SigilMenu::applyFields(const TurnHubProtocol::MenuStateFields &f) {
  active_ = true;
  actions_ = f.actions;
  revision_ = f.revision;
  // A game starting closes the device menu: its keys belong to the game now.
  if (deviceMenuOpen_ && inGame()) closeDeviceMenu();
  // A held action that is no longer offered stops counting.
  if (holding_ && !offered(holdAction_)) holding_ = false;
}

void SigilMenu::clear() {
  active_ = false;
  actions_ = 0;
  deviceMenuOpen_ = false;
  holding_ = false;
  pending_ = MenuChoice();
}

void SigilMenu::setHoldTimes(uint16_t longPressMs, uint16_t winHoldMs) {
  longPressMs_ = longPressMs;
  winHoldMs_ = winHoldMs;
}

void SigilMenu::emit(uint8_t action) {
  pending_.ready = true;
  pending_.action = static_cast<SigilAction>(action);
  pending_.revision = revision_;
}

void SigilMenu::choose(uint8_t action, Key key, uint32_t nowMs) {
  if (!offered(action)) return;
  if (action == MENU_LOCAL_DEVICE_MENU) {
    deviceMenuOpen_ = true;
    return;
  }
  if (action == MENU_LOCAL_BACK) {
    closeDeviceMenu();
    return;
  }
  // A tap: sleeping loses nothing (a click wakes the Sigil, which reconnects).
  if (action == MENU_LOCAL_SLEEP) {
    emit(action);
    closeDeviceMenu();
    return;
  }
  uint32_t holdMs = action == MENU_LOCAL_UNPAIR ? MENU_UNPAIR_HOLD_MS : MENU_FACTORY_RESET_HOLD_MS;
  if (action != MENU_LOCAL_FACTORY_RESET && action != MENU_LOCAL_UNPAIR) {
    const ActionHold hold = TurnHubProtocol::sigilActionHold(static_cast<SigilAction>(action));
    if (hold == ActionHold::None) {
      emit(action);
      return;
    }
    holdMs = hold == ActionHold::Win ? winHoldMs_ : longPressMs_;
  }
  holding_ = true;
  holdKey_ = key;
  holdAction_ = action;
  holdStartMs_ = nowMs;
  holdMs_ = holdMs;
}

void SigilMenu::keyDown(Key key, uint32_t nowMs) {
  if (!active_ || key >= Key::Count) return;
  lastKeyMs_ = nowMs;
  choose(keyAction(key), key, nowMs);
}

void SigilMenu::keyUp(Key key, uint32_t) {
  // Releasing early abandons a deliberate action.
  if (holding_ && key == holdKey_) holding_ = false;
}

MenuChoice SigilMenu::update(uint32_t nowMs) {
  if (holding_ && nowMs - holdStartMs_ >= holdMs_) {
    holding_ = false;
    emit(holdAction_);
    if (holdAction_ == MENU_LOCAL_FACTORY_RESET || holdAction_ == MENU_LOCAL_UNPAIR) closeDeviceMenu();
  }
  if (deviceMenuOpen_ && !holding_ && nowMs - lastKeyMs_ >= MENU_DEVICE_IDLE_MS) closeDeviceMenu();
  const MenuChoice choice = pending_;
  pending_ = MenuChoice();
  return choice;
}

uint8_t SigilMenu::holdProgress(uint32_t nowMs) const {
  if (!holding_ || holdMs_ == 0) return 0;
  const uint32_t elapsed = nowMs - holdStartMs_;
  if (elapsed >= holdMs_) return 255;
  const uint32_t level = elapsed * 255 / holdMs_;
  return static_cast<uint8_t>(level == 0 ? 1 : level);
}

MenuView SigilMenu::view() const {
  MenuView v;
  v.active = active_;
  if (!active_) return v;
  for (uint8_t k = 0; k < KEY_COUNT; ++k) v.compass[k] = keyAction(static_cast<Key>(k));
  v.deviceMenu = deviceMenuOpen_;
  v.holdAction = holding_ && holdOnScreen_ ? holdAction_ : MENU_NONE;
  v.life = lifeOffered();
  return v;
}

}  // namespace TurnHubSigil
