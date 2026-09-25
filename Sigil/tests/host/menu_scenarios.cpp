#include "sigil_menu.h"
#include "picker_list.h"
#include "life_adjust.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace TurnHubSigil;
using namespace TurnHubProtocol;
using A = SigilAction;

static int32_t menu(std::initializer_list<A> actions, A fallback, uint8_t revision) {
  MenuStateFields f;
  for (A a : actions) f.actions |= sigilActionBit(a);
  f.defaultAction = static_cast<uint8_t>(fallback);
  f.revision = revision;
  return encodeMenuState(f);
}
static uint8_t id(A a) { return static_cast<uint8_t>(a); }

int main() {
  // Wire format: mask, default and revision; bad defaults read as none.
  {
    const MenuStateFields f = decodeMenuState(menu({A::Pass, A::Pause, A::LinkPhone}, A::Pass, 45));
    assert(f.actions == (sigilActionBit(A::Pass) | sigilActionBit(A::Pause) | sigilActionBit(A::LinkPhone)));
    assert(f.defaultAction == id(A::Pass) && f.revision == 45);
    assert(decodeMenuState(menu({A::Pause}, A::Pass, 1)).defaultAction == SIGIL_ACTION_NONE);
    const int32_t select = encodeSelectAction(A::ClaimWin, 63);
    assert(selectedAction(select) == id(A::ClaimWin) && selectedRevision(select) == 63);
  }

  // Compass: fixed keys; Link phone takes the first free key.
  {
    const uint32_t running = sigilActionBit(A::Pass) | sigilActionBit(A::Pause) | sigilActionBit(A::ClaimWin);
    assert(SigilMenu::compassAction(running, Key::Select) == id(A::Pass));
    assert(SigilMenu::compassAction(running, Key::Up) == id(A::Pause));
    assert(SigilMenu::compassAction(running, Key::Down) == id(A::ClaimWin));
    assert(SigilMenu::compassAction(running, Key::Left) == MENU_NONE);
    assert(SigilMenu::compassAction(running | sigilActionBit(A::LinkPhone), Key::Right) == id(A::LinkPhone));
    const uint32_t lobby = sigilActionBit(A::StartGame) | sigilActionBit(A::RandomStarter) |
        sigilActionBit(A::CycleStarter) | sigilActionBit(A::AddSeatB) | sigilActionBit(A::LinkPhone);
    assert(SigilMenu::compassAction(lobby, Key::Down) == id(A::LinkPhone));
    assert(SigilMenu::compassAction(lobby, Key::Left) == id(A::AddSeatB));
    assert(SigilMenu::compassAction(lobby, Key::Right) == id(A::CycleStarter));
    // Paused: "I'm out" takes the click, Down stays for the claim.
    const uint32_t paused = sigilActionBit(A::Resume) | sigilActionBit(A::BeginElimination) | sigilActionBit(A::ClaimWin);
    assert(SigilMenu::compassAction(paused, Key::Select) == id(A::BeginElimination));
    assert(SigilMenu::compassAction(paused, Key::Down) == id(A::ClaimWin));
  }

  SigilMenu compass(MenuLayout::Compass);
  // No menu yet: inactive, keys ignored (main.cpp falls back to gestures).
  compass.keyDown(Key::Select, 0);
  assert(!compass.active() && !compass.update(0).ready && !compass.view().active);

  compass.applyMenuState(menu({A::Pass, A::Pause, A::ClaimWin}, A::Pass, 7), 0);
  assert(compass.active());
  compass.keyDown(Key::Select, 100);
  MenuChoice c = compass.update(100);
  assert(c.ready && c.action == A::Pass && c.revision == 7 && !compass.update(101).ready);
  compass.keyUp(Key::Select, 150);
  compass.keyDown(Key::Left, 200);  // Nothing on Left.
  assert(!compass.update(200).ready);

  // Claim win needs the win hold; letting go early abandons it.
  compass.setHoldTimes(2000, 5000);
  compass.keyDown(Key::Down, 1000);
  assert(!compass.update(3000).ready && compass.holdProgress(3500) > 100 && compass.holdProgress(3500) < 155);
  compass.keyUp(Key::Down, 3500);
  assert(!compass.update(7000).ready && compass.holdProgress(7000) == 0);
  compass.keyDown(Key::Down, 8000);
  assert(!compass.update(12999).ready);
  c = compass.update(13000);
  assert(c.ready && c.action == A::ClaimWin);
  // The compass view never carries hold state (no e-ink refresh for it).
  compass.keyDown(Key::Down, 20000);
  assert(compass.view().holdAction == MENU_NONE && compass.holdProgress(21000) > 0);
  // A held action that stops being offered is dropped.
  compass.applyMenuState(menu({A::Resume}, A::Resume, 8), 21000);
  assert(!compass.update(30000).ready && compass.holdProgress(30000) == 0);
  compass.keyUp(Key::Down, 30000);

  // Compass view: the legend's actions per key.
  compass.applyMenuState(menu({A::ConfirmWin, A::DenyWin}, A::ConfirmWin, 9), 0);
  MenuView v = compass.view();
  assert(v.active && v.compass[static_cast<uint8_t>(Key::Select)] == id(A::ConfirmWin));
  assert(v.compass[static_cast<uint8_t>(Key::Left)] == id(A::DenyWin) && v.compass[0] == MENU_NONE);

  // List: the first key opens at the default; Up/Down move; Select chooses.
  SigilMenu list(MenuLayout::List);
  list.applyMenuState(menu({A::Pass, A::Pause, A::ClaimWin}, A::Pass, 3), 0);
  assert(!list.view().listOpen);
  list.keyDown(Key::Down, 0);
  v = list.view();
  assert(v.listOpen && v.itemCount == 3 && v.items[v.cursor] == id(A::Pass) && !list.update(0).ready);
  list.keyDown(Key::Down, 100);
  assert(list.view().items[list.view().cursor] == id(A::Pause));
  list.keyDown(Key::Down, 200); list.keyDown(Key::Down, 300);  // Clamped at the end.
  assert(list.view().items[list.view().cursor] == id(A::ClaimWin));
  // Deliberate: the list shows the hold on screen.
  list.setHoldTimes(2000, 5000);
  list.keyDown(Key::Select, 400);
  assert(list.view().holdAction == id(A::ClaimWin));
  list.keyUp(Key::Select, 1000);
  assert(list.view().holdAction == MENU_NONE && list.view().listOpen);
  list.keyDown(Key::Up, 1100);
  list.keyDown(Key::Right, 1200);  // Right chooses too.
  c = list.update(1200);
  assert(c.ready && c.action == A::Pause && c.revision == 3 && !list.view().listOpen);
  // Left closes; an idle list closes by itself.
  list.keyDown(Key::Select, 2000); assert(list.view().listOpen);
  list.keyDown(Key::Left, 2100); assert(!list.view().listOpen);
  list.keyDown(Key::Select, 3000); assert(list.view().listOpen);
  list.update(3000 + MENU_LIST_IDLE_MS); assert(!list.view().listOpen);
  // A new menu keeps the cursor on a surviving action, else the default.
  list.keyDown(Key::Select, 20000); list.keyDown(Key::Down, 20100);
  assert(list.view().items[list.view().cursor] == id(A::Pause));
  list.applyMenuState(menu({A::Pause, A::Resume, A::ClaimWin}, A::Resume, 4), 20200);
  assert(list.view().items[list.view().cursor] == id(A::Pause));
  list.applyMenuState(menu({A::Resume, A::BeginElimination}, A::Resume, 5), 20300);
  assert(list.view().items[list.view().cursor] == id(A::Resume));
  list.applyMenuState(menu({}, A::Count, 6), 20400);
  assert(!list.view().listOpen);
  list.keyDown(Key::Select, 20500);  // Nothing to open.
  assert(!list.view().listOpen);

  // Unpaired: back to inactive.
  list.clear();
  assert(!list.active() && !list.view().active);

  // MenuState2 carries Leave (action 21), which MenuState cannot; Leave is a
  // long-press hold on Down, and a waiting phone link outranks it.
  {
    MenuStateFields f;
    f.actions = sigilActionBit(A::CycleStarter) | sigilActionBit(A::AddSeatB) | sigilActionBit(A::Leave);
    f.defaultAction = id(A::Leave);
    f.revision = 5;
    const MenuStateFields back = decodeMenuState2(encodeMenuState2(f));
    assert(back.actions == f.actions && back.defaultAction == id(A::Leave) && back.revision == 5);
    assert((decodeMenuState(encodeMenuState(f)).actions & sigilActionBit(A::Leave)) == 0);
    assert(SigilMenu::compassAction(f.actions, Key::Down) == id(A::Leave));
    assert(SigilMenu::compassAction(f.actions | sigilActionBit(A::LinkPhone), Key::Down) == id(A::LinkPhone));
    assert(sigilActionHold(A::Leave) == ActionHold::Long);
    SigilMenu leaver(MenuLayout::Compass);
    leaver.applyMenuState2(encodeMenuState2(f), 0);
    leaver.keyDown(Key::Down, 10);
    assert(!leaver.update(1000).ready);
    const MenuChoice left = leaver.update(10 + DEFAULT_LONG_PRESS_MS);
    assert(left.ready && left.action == A::Leave && left.revision == 5);
  }

  // OLED picker list: names, More, Back; a chosen row becomes the compass key.
  {
    ProfilePickerPacket page{};
    page.mode = PickerMode::List; page.itemCount = 3; page.page = 0; page.pageCount = 2;
    PickerRows rows = pickerRows(page);
    assert(rows.count == 5 && rows.rows[3] == PickerRow::More && rows.rows[4] == PickerRow::Back);
    uint8_t cursor = 0;
    PickerKeyCode code = PickerKeyCode::Count;
    assert(!pickerListKey(page, cursor, Key::Up, code) && cursor == 0);
    assert(pickerListKey(page, cursor, Key::Select, code) && code == PickerKeyCode::Up);
    assert(!pickerListKey(page, cursor, Key::Down, code) && cursor == 1);
    assert(pickerListKey(page, cursor, Key::Right, code) && code == PickerKeyCode::Right);
    cursor = 2; assert(pickerListKey(page, cursor, Key::Select, code) && code == PickerKeyCode::Down);
    cursor = 3; assert(pickerListKey(page, cursor, Key::Select, code) && code == PickerKeyCode::Select);
    for (int i = 0; i < 5; ++i) pickerListKey(page, cursor, Key::Down, code);
    assert(cursor == 4 && pickerListKey(page, cursor, Key::Select, code) && code == PickerKeyCode::Left);
    assert(pickerListKey(page, cursor, Key::Left, code) && code == PickerKeyCode::Left);
    page.pageCount = 1; page.itemCount = 2;
    assert(pickerRows(page).count == 3);  // Two names and Back; no More.
    page.mode = PickerMode::Confirm; page.itemCount = 1;
    rows = pickerRows(page);
    assert(rows.count == 2 && rows.rows[0] == PickerRow::Yes && rows.rows[1] == PickerRow::Back);
    cursor = 0; assert(pickerListKey(page, cursor, Key::Select, code) && code == PickerKeyCode::Select);
    cursor = 9; assert(!pickerListKey(page, cursor, Key::Up, code) && cursor == 0);  // Stale cursor resets.
  }

  // Life presses batch: a tap is 1, a hold repeats then speeds to 5s, and one
  // total goes out 2 s after the last change (never while a key is held).
  {
    LifeAdjuster life;
    int32_t delta = 0;
    uint8_t player = 0;
    life.press(-1, 2, 0); life.release(50);
    life.press(-1, 2, 300); life.release(350);
    assert(life.pending() == -2 && !life.update(2000, delta, player));
    assert(life.update(2350, delta, player) && delta == -2 && player == 2 && life.pending() == 0);
    // Hold Right: +1, repeats +1 every 150 ms from 500 ms, then +5 from 1.5 s.
    life.press(1, 2, 10000);
    for (uint32_t t = 10000; t <= 12000; t += 10) assert(!life.update(t, delta, player));
    const int32_t held = life.pending();
    assert(held == 1 + 7 + 4 * 5);  // Tap, seven +1 repeats (0.5-1.4 s), four +5 (1.55-2 s).
    life.release(12000);
    assert(!life.update(13999, delta, player) && life.update(14000, delta, player) && delta == held);
    // A different player drops the old total; cancel drops everything.
    life.press(1, 2, 20000); life.release(20010);
    life.press(-1, 3, 20100); life.release(20110);
    assert(life.pending() == -1 && life.player() == 3);
    life.cancel(); assert(life.pending() == 0 && !life.update(30000, delta, player));
  }

  // Every action has a short label.
  for (uint8_t a = 0; a < static_cast<uint8_t>(A::Count); ++a) {
    const char *label = sigilActionLabel(static_cast<A>(a));
    assert(label[0] && std::strlen(label) <= 12);
  }
  std::cout << "Menu wire format, compass keys, list navigation, holds and stale menus passed\n";
}
