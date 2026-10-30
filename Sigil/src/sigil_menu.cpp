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

constexpr uint8_t DEVICE_ENTRIES[] = {MENU_LOCAL_UNPAIR, MENU_LOCAL_FACTORY_RESET, MENU_LOCAL_THEME, MENU_LOCAL_INVERT, MENU_LOCAL_CALIBRATE
};

// Touch menu page slots, in legend order; Left is Back.
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

bool menuActionNeedsConfirm(uint8_t action) {
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
    case MENU_LOCAL_DEVICE: return "Device";
    case MENU_LOCAL_COUNTERS: return "Counters";
    case MENU_LOCAL_MORE: return "More";
    case MENU_LOCAL_THEME: return "Theme";
    case MENU_LOCAL_INVERT: return "Invert";
    case MENU_LOCAL_CALIBRATE: return "Calibrate";
    case MENU_LOCAL_CONFIRM: return "Confirm";
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
}

bool MenuView::operator==(const MenuView &o) const {
  return revision == o.revision && active == o.active && deviceMenu == o.deviceMenu && life == o.life &&
      confirmAction == o.confirmAction && memcmp(compass, o.compass, sizeof(compass)) == 0 && pairing == o.pairing &&
      recovery == o.recovery && page == o.page && pageCount == o.pageCount && canGoBack == o.canGoBack;
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
    if (actions & TurnHubProtocol::sigilActionBit(SigilAction::AdjustLife)) out[count++] = MENU_LOCAL_COUNTERS;
    if (count) return count;
  }
  if (!device && actions) return count;
  for (uint8_t local : DEVICE_ENTRIES) out[count++] = local;
  return count;
}

void SigilMenu::setPlayerControlsAvailable(bool available) {
  if (playerControlsAvailable_==available) return;
  playerControlsAvailable_=available; confirm_=MENU_NONE; page_=0;
}
uint8_t SigilMenu::levelEntries(uint8_t out[MENU_LIST_MAX]) const {
  const uint8_t count=menuEntries(offline_ ? 0 : actions_, out, deviceOpen_);
  uint8_t n=0;
  for(uint8_t i=0;i<count;++i)
    if (out[i]!=MENU_LOCAL_COUNTERS || playerControlsAvailable_) out[n++]=out[i];
  return n;
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

uint8_t SigilMenu::keyAction(Key key) const {
  if (!active_ || key >= Key::Count) return MENU_NONE;
  // The confirm popup: Select confirms, Left cancels, nothing else.
  if (confirm_ != MENU_NONE) return key == Key::Select ? MENU_LOCAL_CONFIRM : key == Key::Left ? MENU_LOCAL_BACK : MENU_NONE;
  if (!menuOpen_) return bareAction(offline_ ? 0 : actions_, key);
  if (key == Key::Left) return MENU_LOCAL_BACK;
  uint8_t entries[MENU_LIST_MAX];
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
  if (menuOpen_) {
    uint8_t entries[MENU_LIST_MAX];
    const uint8_t pages = pageCount(levelEntries(entries));
    if (page_ >= pages) page_ = pages - 1;
  }
  // A popup about an action that is no longer offered closes.
  if (confirm_ < MENU_MAX_ITEMS && !offered(confirm_)) confirm_ = MENU_NONE;
}

void SigilMenu::clear() {
  active_ = false;
  offline_ = false;
  actions_ = 0;
  closeMenu();
  confirm_ = MENU_NONE;
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

void SigilMenu::emit(uint8_t action) {
  pending_.ready = true;
  pending_.action = static_cast<SigilAction>(action);
  pending_.revision = revision_;
}

void SigilMenu::openMenu() {
  menuOpen_ = true;
  page_ = 0;
  uint8_t entries[MENU_LIST_MAX] = {MENU_NONE};
  // Nothing but Device on offer: open straight onto it.
  menuEntries(offline_ ? 0 : actions_, entries);
  deviceDirect_ = entries[0] == MENU_LOCAL_UNPAIR;
  deviceOpen_ = deviceDirect_;
}

void SigilMenu::back() {
  if (deviceOpen_ && !deviceDirect_) {
    // Up a level, onto the entry that opened it.
    deviceOpen_ = false;
    uint8_t entries[MENU_LIST_MAX];
    page_ = static_cast<uint8_t>(pageCount(levelEntries(entries)) - 1);
    return;
  }
  if (page_ > 0) {
    --page_;
    return;
  }
  closeMenu();
}

void SigilMenu::choose(uint8_t action, uint32_t nowMs) {
  (void)nowMs;
  if (action == MENU_NONE) return;
  if (action == MENU_LOCAL_CONFIRM) {
    const uint8_t asked = confirm_;
    confirm_ = MENU_NONE;
    if (asked >= MENU_MAX_ITEMS || offered(asked)) emit(asked);
    if (confirmFromMenu_) closeMenu();
    return;
  }
  // Left on the popup is Cancel: back to where it was asked.
  if (action == MENU_LOCAL_BACK && confirm_ != MENU_NONE) { confirm_ = MENU_NONE; return; }
  if (action == MENU_LOCAL_DEVICE_MENU) { openMenu(); return; }
  if (action == MENU_LOCAL_BACK) { back(); return; }
  if (action == MENU_LOCAL_MORE) { ++page_; return; }
  if (action == MENU_LOCAL_DEVICE) {
    deviceOpen_ = true;
    page_ = 0;
    return;
  }
  if (action == MENU_LOCAL_THEME || action == MENU_LOCAL_INVERT) { emit(action); return; }
  if (action == MENU_LOCAL_CALIBRATE || action == MENU_LOCAL_COUNTERS) {
    emit(action);
    closeMenu();
    return;
  }
  if (action != MENU_LOCAL_FACTORY_RESET && action != MENU_LOCAL_UNPAIR) {
    if (!offered(action)) return;
    if (!menuActionNeedsConfirm(action)) {
      emit(action);
      closeMenu();  // Chosen from Menu: back to the screen.
      return;
    }
  }
  // Ask first; the popup covers the screen until Confirm or Cancel.
  confirm_ = action;
  confirmFromMenu_ = menuOpen_;
}

void SigilMenu::keyDown(Key key, uint32_t nowMs) {
  if (!active_ || key >= Key::Count) return;
  lastKeyMs_ = nowMs;
  choose(keyAction(key), nowMs);
}

void SigilMenu::keyUp(Key, uint32_t) {}

MenuChoice SigilMenu::update(uint32_t nowMs) {
  // An unanswered popup or Menu closes by itself.
  if ((menuOpen_ || confirm_ != MENU_NONE) && nowMs - lastKeyMs_ >= MENU_DEVICE_IDLE_MS) {
    confirm_ = MENU_NONE;
    closeMenu();
  }
  const MenuChoice choice = pending_;
  pending_ = MenuChoice();
  return choice;
}

MenuView SigilMenu::view() const {
  MenuView v;
  v.revision = revision_;
  v.active = active_;
  if (!active_) return v;
  for (uint8_t k = 0; k < KEY_COUNT; ++k) v.compass[k] = keyAction(static_cast<Key>(k));
  v.deviceMenu = menuOpen_;
  v.confirmAction = confirm_;
  v.life = lifeOffered();
  if (menuOpen_) {
    v.recovery = deviceOpen_;
    uint8_t entries[MENU_LIST_MAX];
    v.page = page_;
    v.pageCount = pageCount(levelEntries(entries));
    v.canGoBack = (deviceOpen_ && !deviceDirect_) || page_ > 0;
  }
  return v;
}

}  // namespace TurnHubSigil
