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
    case SigilAction::AddSeatB:
    case SigilAction::RemoveSeatB:
    case SigilAction::CancelStart:
    case SigilAction::CancelPass:
    case SigilAction::DenyWin:
    case SigilAction::CancelElimination: return {{L, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE}};
    case SigilAction::CycleStarter:
    case SigilAction::NextTarget: return {{R, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE}};
    case SigilAction::LinkPhone: return {{D, R, L, U, C}};
    default: return {{KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE}};
  }
}

}  // namespace

const char *sigilActionLabel(SigilAction action) {
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
    default: return "";
  }
}

MenuView::MenuView() {
  memset(compass, MENU_NONE, sizeof(compass));
  memset(items, MENU_NONE, sizeof(items));
}

bool MenuView::operator==(const MenuView &o) const {
  return active == o.active && listOpen == o.listOpen && itemCount == o.itemCount &&
      cursor == o.cursor && holdAction == o.holdAction &&
      memcmp(compass, o.compass, sizeof(compass)) == 0 && memcmp(items, o.items, sizeof(items)) == 0;
}

uint8_t SigilMenu::compassAction(uint32_t actions, Key key) {
  // Assign in action order (Link phone, the only movable one, comes last).
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

void SigilMenu::applyMenuState(int32_t value, uint32_t nowMs) {
  const TurnHubProtocol::MenuStateFields f = TurnHubProtocol::decodeMenuState(value);
  const uint8_t cursorAction = listOpen_ ? itemAt(cursor_) : MENU_NONE;
  const bool changed = !active_ || f.actions != actions_;
  active_ = true;
  actions_ = f.actions;
  defaultAction_ = f.defaultAction;
  revision_ = f.revision;
  // A held action that is no longer offered stops counting.
  if (holding_ && !offered(holdAction_)) holding_ = false;
  if (!changed) return;
  if (itemCount() == 0) {
    closeList();
  } else if (listOpen_) {
    // Keep the cursor on the same action if it survived, else the default.
    const uint8_t keep = offered(cursorAction) ? cursorAction : defaultAction_;
    cursor_ = keep == MENU_NONE ? 0 : indexOf(keep);
    lastKeyMs_ = nowMs;
  }
}

void SigilMenu::clear() {
  active_ = false;
  actions_ = 0;
  defaultAction_ = MENU_NONE;
  listOpen_ = false;
  holding_ = false;
  pending_ = MenuChoice();
}

void SigilMenu::setHoldTimes(uint16_t longPressMs, uint16_t winHoldMs) {
  longPressMs_ = longPressMs;
  winHoldMs_ = winHoldMs;
}

uint8_t SigilMenu::itemCount() const {
  uint8_t count = 0;
  for (uint8_t a = 0; a < MENU_MAX_ITEMS; ++a) count += offered(a);
  return count;
}

uint8_t SigilMenu::itemAt(uint8_t index) const {
  for (uint8_t a = 0; a < MENU_MAX_ITEMS; ++a) {
    if (!offered(a)) continue;
    if (index-- == 0) return a;
  }
  return MENU_NONE;
}

uint8_t SigilMenu::indexOf(uint8_t action) const {
  uint8_t index = 0;
  for (uint8_t a = 0; a < action && a < MENU_MAX_ITEMS; ++a) index += offered(a);
  return index;
}

void SigilMenu::emit(uint8_t action) {
  pending_.ready = true;
  pending_.action = static_cast<SigilAction>(action);
  pending_.revision = revision_;
}

void SigilMenu::choose(uint8_t action, Key key, uint32_t nowMs) {
  if (!offered(action)) return;
  const ActionHold hold = TurnHubProtocol::sigilActionHold(static_cast<SigilAction>(action));
  if (hold == ActionHold::None) {
    emit(action);
    closeList();
    return;
  }
  holding_ = true;
  holdKey_ = key;
  holdAction_ = action;
  holdStartMs_ = nowMs;
  holdMs_ = hold == ActionHold::Win ? winHoldMs_ : longPressMs_;
}

void SigilMenu::keyDown(Key key, uint32_t nowMs) {
  if (!active_ || key >= Key::Count) return;
  lastKeyMs_ = nowMs;
  if (layout_ == MenuLayout::Compass) {
    choose(compassAction(actions_, key), key, nowMs);
    return;
  }
  const uint8_t count = itemCount();
  if (!listOpen_) {
    // The first key only opens the list, at the default action.
    if (count == 0) return;
    listOpen_ = true;
    cursor_ = defaultAction_ == MENU_NONE ? 0 : indexOf(defaultAction_);
    return;
  }
  switch (key) {
    case Key::Up: if (cursor_ > 0) --cursor_; break;
    case Key::Down: if (cursor_ + 1 < count) ++cursor_; break;
    case Key::Left: closeList(); break;
    case Key::Right:
    case Key::Select: choose(itemAt(cursor_), key, nowMs); break;
    default: break;
  }
}

void SigilMenu::keyUp(Key key, uint32_t) {
  // Releasing early abandons a deliberate action.
  if (holding_ && key == holdKey_) holding_ = false;
}

MenuChoice SigilMenu::update(uint32_t nowMs) {
  if (holding_ && nowMs - holdStartMs_ >= holdMs_) {
    holding_ = false;
    emit(holdAction_);
    closeList();
  }
  if (listOpen_ && !holding_ && nowMs - lastKeyMs_ >= MENU_LIST_IDLE_MS) closeList();
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
  for (uint8_t k = 0; k < KEY_COUNT; ++k) v.compass[k] = compassAction(actions_, static_cast<Key>(k));
  v.itemCount = itemCount();
  for (uint8_t i = 0; i < v.itemCount; ++i) v.items[i] = itemAt(i);
  v.listOpen = listOpen_;
  v.cursor = cursor_;
  // Only the list shows hold progress on screen: an e-ink compass would pay
  // a full refresh for it, so it relies on the status light instead.
  v.holdAction = holding_ && layout_ == MenuLayout::List ? holdAction_ : MENU_NONE;
  return v;
}

}  // namespace TurnHubSigil
