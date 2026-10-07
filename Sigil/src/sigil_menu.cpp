#include "sigil_menu.h"

#include <string.h>

namespace TurnHubSigil {

using TurnHubProtocol::ActionHold;
using TurnHubProtocol::SigilAction;

namespace {

// Bare keys (Menu closed), first offered wins. Up is always Menu.
constexpr SigilAction CLICK_ORDER[] = {
    SigilAction::ConfirmWin, SigilAction::Eliminate, SigilAction::Pass, SigilAction::CancelPass,
    SigilAction::Join, SigilAction::StartGame, SigilAction::CancelStart, SigilAction::Resume,
    SigilAction::Rematch};
constexpr SigilAction LEFT_ORDER[] = {
    SigilAction::DenyWin, SigilAction::CancelElimination, SigilAction::AddSeatB, SigilAction::RemoveSeatB};
constexpr SigilAction RIGHT_ORDER[] = {SigilAction::NextTarget, SigilAction::CycleStarter};
constexpr SigilAction DOWN_ORDER[] = {SigilAction::SwitchSeat};

// Menu's order: the game's actions first, then the lobby's, then anything a
// bare key normally takes (listed so nothing offered is ever unreachable).
constexpr SigilAction MENU_ORDER[] = {
    SigilAction::CommanderDamage, SigilAction::UndoCommanderHit, SigilAction::Pause,
    SigilAction::Resume, SigilAction::ClaimWin, SigilAction::BeginElimination,
    SigilAction::AddPartner, SigilAction::DropPartner, SigilAction::RandomStarter,
    SigilAction::CycleStarter, SigilAction::AddSeatB, SigilAction::RemoveSeatB, SigilAction::Leave,
    SigilAction::SwitchTable,
    SigilAction::Rematch, SigilAction::ResetTable, SigilAction::LinkPhone, SigilAction::SwitchSeat,
    SigilAction::Pass, SigilAction::CancelPass, SigilAction::Join, SigilAction::StartGame,
    SigilAction::CancelStart, SigilAction::ConfirmWin, SigilAction::DenyWin, SigilAction::Eliminate,
    SigilAction::NextTarget, SigilAction::CancelElimination};
// Every action but AdjustLife (Left/Right, never an entry).
static_assert(sizeof(MENU_ORDER) / sizeof(MENU_ORDER[0]) == MENU_MAX_ITEMS - 1, "list every action once");

constexpr uint8_t DEVICE_ENTRIES[] = {MENU_LOCAL_SLEEP, MENU_LOCAL_UNPAIR, MENU_LOCAL_FACTORY_RESET};

// E-ink page slots, in legend order; Left is Back.
constexpr Key PAGE_KEYS[] = {Key::Select, Key::Up, Key::Right, Key::Down};
constexpr uint8_t PAGE_SLOTS = 4;

template <size_t N>
uint8_t firstOffered(uint32_t actions, const SigilAction (&order)[N]) {
  for (SigilAction a : order) {
    if (actions & TurnHubProtocol::sigilActionBit(a)) return static_cast<uint8_t>(a);
  }
  return MENU_NONE;
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
    case MENU_LOCAL_DEVICE: return "Device";
    case MENU_LOCAL_MORE: return "More";
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
    case SigilAction::CommanderDamage: return "Cmd damage";
    case SigilAction::UndoCommanderHit: return "Undo hit";
    case SigilAction::AddPartner: return "Partner: off";
    case SigilAction::DropPartner: return "Partner: on";
    case SigilAction::SwitchSeat: return "Switch seat";
    case SigilAction::SwitchTable: return "Switch game";
    default: return "";
  }
}

MenuView::MenuView() {
  memset(compass, MENU_NONE, sizeof(compass));
  memset(rows, MENU_NONE, sizeof(rows));
}

bool MenuView::operator==(const MenuView &o) const {
  return active == o.active && deviceMenu == o.deviceMenu && life == o.life &&
      holdAction == o.holdAction && memcmp(compass, o.compass, sizeof(compass)) == 0 &&
      list == o.list && recovery == o.recovery && page == o.page && pageCount == o.pageCount &&
      rowCount == o.rowCount && cursor == o.cursor && memcmp(rows, o.rows, rowCount) == 0;
}

uint8_t SigilMenu::bareAction(uint32_t actions, Key key) {
  switch (key) {
    case Key::Select: return firstOffered(actions, CLICK_ORDER);
    case Key::Left: return firstOffered(actions, LEFT_ORDER);
    case Key::Right: return firstOffered(actions, RIGHT_ORDER);
    case Key::Down: return firstOffered(actions, DOWN_ORDER);
    case Key::Up: return MENU_LOCAL_DEVICE_MENU;
    default: return MENU_NONE;
  }
}

uint8_t SigilMenu::menuEntries(uint32_t actions, uint8_t out[MENU_LIST_MAX], bool device) {
  uint8_t count = 0;
  if (!device) {
    for (SigilAction a : MENU_ORDER) {
      const uint8_t action = static_cast<uint8_t>(a);
      if ((actions & TurnHubProtocol::sigilActionBit(a)) == 0) continue;
      bool bare = false;
      for (Key key : {Key::Select, Key::Left, Key::Right, Key::Down}) bare = bare || bareAction(actions, key) == action;
      if (!bare) out[count++] = action;
    }
    if (count) {
      out[count++] = MENU_LOCAL_DEVICE;
      return count;
    }
  }
  for (uint8_t local : DEVICE_ENTRIES) out[count++] = local;
  return count;
}

uint8_t SigilMenu::levelEntries(uint8_t out[MENU_LIST_MAX]) const {
  return menuEntries(offline_ ? 0 : actions_, out, deviceOpen_);
}

// Every page but the last shows three entries and More on Down.
uint8_t SigilMenu::pageCount(uint8_t count) {
  if (count <= PAGE_SLOTS) return 1;
  return static_cast<uint8_t>(1 + (count - PAGE_SLOTS + (PAGE_SLOTS - 2)) / (PAGE_SLOTS - 1));
}

uint8_t SigilMenu::pageAction(const uint8_t entries[], uint8_t count, uint8_t page, Key key) {
  if (key == Key::Left) return MENU_LOCAL_BACK;
  const uint8_t first = page * (PAGE_SLOTS - 1);
  const bool more = page + 1 < pageCount(count);
  for (uint8_t slot = 0; slot < PAGE_SLOTS; ++slot) {
    if (PAGE_KEYS[slot] != key) continue;
    if (more && slot == PAGE_SLOTS - 1) return MENU_LOCAL_MORE;
    const uint8_t index = first + slot;
    return index < count ? entries[index] : MENU_NONE;
  }
  return MENU_NONE;
}

void SigilMenu::listRows(uint8_t rows[MENU_LIST_MAX], uint8_t &count) const {
  count = levelEntries(rows);
  rows[count++] = MENU_LOCAL_BACK;
}

uint8_t SigilMenu::cursorRow(const uint8_t rows[], uint8_t count) const {
  for (uint8_t i = 0; i < count; ++i) {
    if (rows[i] == cursorAction_) return i;
  }
  return cursor_ < count ? cursor_ : count - 1;
}

void SigilMenu::moveCursor(int8_t step) {
  uint8_t rows[MENU_LIST_MAX];
  uint8_t count = 0;
  listRows(rows, count);
  uint8_t row = cursorRow(rows, count);
  if (step < 0 && row > 0) --row;
  if (step > 0 && row + 1 < count) ++row;
  cursor_ = row;
  cursorAction_ = rows[row];
}

uint8_t SigilMenu::keyAction(Key key) const {
  if (!active_ || key >= Key::Count) return MENU_NONE;
  if (!menuOpen_) return bareAction(offline_ ? 0 : actions_, key);
  if (key == Key::Left) return MENU_LOCAL_BACK;
  uint8_t entries[MENU_LIST_MAX];
  if (list_) {
    // Up/Down move the cursor (keyDown); the click or Right chooses its row.
    if (key != Key::Select && key != Key::Right) return MENU_NONE;
    uint8_t count = 0;
    listRows(entries, count);
    return entries[cursorRow(entries, count)];
  }
  const uint8_t count = levelEntries(entries);
  return pageAction(entries, count, page_, key);
}

void SigilMenu::applyMenuState2(int32_t value, uint32_t nowMs) {
  (void)nowMs;
  applyFields(TurnHubProtocol::decodeMenuState2(value));
}

void SigilMenu::applyFields(const TurnHubProtocol::MenuStateFields &f) {
  const bool wasInGame = active_ && inGame();
  active_ = true;
  offline_ = false;
  actions_ = f.actions;
  revision_ = f.revision;
  // A game starting closes Menu: its keys belong to the game now.
  if (menuOpen_ && inGame() && !wasInGame) closeMenu();
  // Menu's entries may have changed: keep the page in range.
  if (menuOpen_ && !list_) {
    uint8_t entries[MENU_LIST_MAX];
    const uint8_t pages = pageCount(levelEntries(entries));
    if (page_ >= pages) page_ = pages - 1;
  }
  // A held action that is no longer offered stops counting.
  if (holding_ && holdAction_ < MENU_MAX_ITEMS && !offered(holdAction_)) holding_ = false;
}

void SigilMenu::clear() {
  active_ = false;
  offline_ = false;
  actions_ = 0;
  closeMenu();
  holding_ = false;
  pending_ = MenuChoice();
}

void SigilMenu::setOffline() {
  clear();
  // No Atlas actions: Up opens Menu straight onto the Device entries.
  active_ = true;
  offline_ = true;
}

void SigilMenu::endOffline() {
  if (offline_) clear();
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

void SigilMenu::openMenu() {
  menuOpen_ = true;
  page_ = 0;
  cursor_ = 0;
  cursorAction_ = MENU_NONE;
  uint8_t entries[MENU_LIST_MAX];
  // Nothing but Device on offer: open straight onto it.
  menuEntries(offline_ ? 0 : actions_, entries);
  deviceDirect_ = entries[0] == MENU_LOCAL_SLEEP;
  deviceOpen_ = deviceDirect_;
}

void SigilMenu::back() {
  if (deviceOpen_ && !deviceDirect_) {
    // Up a level, onto the entry that opened it.
    deviceOpen_ = false;
    cursorAction_ = MENU_LOCAL_DEVICE;
    uint8_t entries[MENU_LIST_MAX];
    page_ = list_ ? 0 : static_cast<uint8_t>(pageCount(levelEntries(entries)) - 1);
    return;
  }
  if (!list_ && page_ > 0) {
    --page_;
    return;
  }
  closeMenu();
}

void SigilMenu::choose(uint8_t action, Key key, uint32_t nowMs) {
  if (action == MENU_NONE) return;
  if (action == MENU_LOCAL_DEVICE_MENU) { openMenu(); return; }
  if (action == MENU_LOCAL_BACK) { back(); return; }
  if (action == MENU_LOCAL_MORE) { ++page_; return; }
  if (action == MENU_LOCAL_DEVICE) {
    deviceOpen_ = true;
    page_ = 0;
    cursor_ = 0;
    cursorAction_ = MENU_NONE;
    return;
  }
  // A tap: sleeping loses nothing (a click wakes the Sigil, which reconnects).
  if (action == MENU_LOCAL_SLEEP) {
    emit(action);
    closeMenu();
    return;
  }
  uint32_t holdMs = action == MENU_LOCAL_UNPAIR ? MENU_UNPAIR_HOLD_MS : MENU_FACTORY_RESET_HOLD_MS;
  if (action != MENU_LOCAL_FACTORY_RESET && action != MENU_LOCAL_UNPAIR) {
    if (!offered(action)) return;
    const ActionHold hold = TurnHubProtocol::sigilActionHold(static_cast<SigilAction>(action));
    if (hold == ActionHold::None) {
      emit(action);
      closeMenu();  // Chosen from Menu: back to the screen.
      return;
    }
    holdMs = hold == ActionHold::Win ? winHoldMs_ : longPressMs_;
  }
  holding_ = true;
  holdFromMenu_ = menuOpen_;
  holdKey_ = key;
  holdAction_ = action;
  holdStartMs_ = nowMs;
  holdMs_ = holdMs;
}

void SigilMenu::keyDown(Key key, uint32_t nowMs) {
  if (!active_ || key >= Key::Count) return;
  lastKeyMs_ = nowMs;
  if (list_ && menuOpen_ && (key == Key::Up || key == Key::Down)) {
    if (!holding_) moveCursor(key == Key::Up ? -1 : 1);
    return;
  }
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
    if (holdFromMenu_) closeMenu();
  }
  if (menuOpen_ && !holding_ && nowMs - lastKeyMs_ >= MENU_DEVICE_IDLE_MS) closeMenu();
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
  v.deviceMenu = menuOpen_;
  v.holdAction = holding_ && holdOnScreen_ ? holdAction_ : MENU_NONE;
  v.life = lifeOffered();
  v.list = list_;
  if (menuOpen_) {
    v.recovery = deviceOpen_;
    if (list_) {
      listRows(v.rows, v.rowCount);
      v.cursor = cursorRow(v.rows, v.rowCount);
    } else {
      uint8_t entries[MENU_LIST_MAX];
      v.page = page_;
      v.pageCount = pageCount(levelEntries(entries));
    }
  }
  return v;
}

}  // namespace TurnHubSigil
