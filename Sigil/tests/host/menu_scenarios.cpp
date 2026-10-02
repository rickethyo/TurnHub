#include "sigil_menu.h"
#include "picker_list.h"
#include "life_adjust.h"
#include "three_part_button.h"
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
  return encodeMenuState2(f);
}
static uint8_t id(A a) { return static_cast<uint8_t>(a); }

int main() {
  // One button, three gestures: quick press pairs (on release), a medium hold
  // unpairs once, a long hold factory resets once; nothing more on release.
  {
    using G = ButtonGesture;
    ThreePartButton b;
    // Held through a reset: ignored until seen released.
    assert(b.update(true, 0) == G::None && b.update(true, FACTORY_RESET_HOLD_MS + 5000) == G::None);
    assert(b.update(false, 20000) == G::None);
    // Quick press: nothing while held, Pair on release.
    assert(b.update(true, 30000) == G::None && b.down());
    assert(b.update(true, 30000 + UNPAIR_HOLD_MS - 1) == G::None);
    assert(b.update(false, 30000 + UNPAIR_HOLD_MS - 1) == G::Pair && !b.down());
    assert(b.update(false, 40000) == G::None);
    // Medium hold: Unpair fires once at the threshold, no Pair on release.
    assert(b.update(true, 50000) == G::None);
    assert(b.update(true, 50000 + UNPAIR_HOLD_MS) == G::Unpair);
    assert(b.heldMs(50000 + UNPAIR_HOLD_MS + 100) == UNPAIR_HOLD_MS + 100);
    assert(b.update(true, 50000 + UNPAIR_HOLD_MS + 500) == G::None);
    assert(b.update(false, 50000 + UNPAIR_HOLD_MS + 600) == G::None && b.heldMs(60000) == 0);
    // Long hold: Unpair at 3 s, then FactoryReset once at 10 s, nothing on release.
    assert(b.update(true, 70000) == G::None);
    assert(b.update(true, 70000 + UNPAIR_HOLD_MS) == G::Unpair);
    assert(b.update(true, 70000 + FACTORY_RESET_HOLD_MS - 1) == G::None);
    assert(b.update(true, 70000 + FACTORY_RESET_HOLD_MS) == G::FactoryReset);
    assert(b.update(true, 70000 + FACTORY_RESET_HOLD_MS + 5000) == G::None);
    assert(b.update(false, 70000 + FACTORY_RESET_HOLD_MS + 6000) == G::None);
    // A stalled loop that first sees the long hold goes straight to factory reset.
    assert(b.update(true, 100000) == G::None);
    assert(b.update(true, 100000 + FACTORY_RESET_HOLD_MS + 1) == G::FactoryReset);
    assert(b.update(false, 120000) == G::None);
    // The millis() counter wrapping mid-press changes nothing.
    assert(b.update(true, 0xFFFFFFFFu - 1000) == G::None);
    assert(b.update(true, 0xFFFFFFFFu - 1000 + UNPAIR_HOLD_MS) == G::Unpair);
    assert(b.update(false, 0xFFFFFFFFu - 1000 + UNPAIR_HOLD_MS + 1) == G::None);
    assert(UNPAIR_HOLD_MS < FACTORY_RESET_HOLD_MS && UNPAIR_HOLD_MS > 2000);
  }

  // Wire format: mask, default and revision; bad defaults read as none.
  {
    const MenuStateFields f = decodeMenuState2(menu({A::Pass, A::Pause, A::LinkPhone}, A::Pass, 5));
    assert(f.actions == (sigilActionBit(A::Pass) | sigilActionBit(A::Pause) | sigilActionBit(A::LinkPhone)));
    assert(f.defaultAction == id(A::Pass) && f.revision == 5);
    assert(decodeMenuState2(menu({A::Pause}, A::Pass, 1)).defaultAction == SIGIL_ACTION_NONE);
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
    // A shared Sigil's Switch seat gets a free key on the compass too, and
    // never Left/Right, which change life.
    const uint32_t shared = sigilActionBit(A::Pause) | sigilActionBit(A::AdjustLife) |
        sigilActionBit(A::SwitchSeat);
    assert(SigilMenu::compassAction(shared, Key::Select) == id(A::SwitchSeat));
    assert(SigilMenu::compassAction(shared, Key::Left) == MENU_NONE &&
        SigilMenu::compassAction(shared, Key::Right) == MENU_NONE);
    assert(SigilMenu::compassAction(paused | sigilActionBit(A::AdjustLife) | sigilActionBit(A::SwitchSeat),
        Key::Down) == id(A::ClaimWin));
    const uint32_t pausedWaiting = sigilActionBit(A::Resume) | sigilActionBit(A::BeginElimination) |
        sigilActionBit(A::AdjustLife) | sigilActionBit(A::SwitchSeat);
    assert(SigilMenu::compassAction(pausedWaiting, Key::Down) == id(A::SwitchSeat));
  }

  SigilMenu compass(false);
  // No menu yet: inactive, keys ignored (main.cpp falls back to gestures).
  compass.keyDown(Key::Select, 0);
  assert(!compass.active() && !compass.update(0).ready && !compass.view().active);

  compass.applyMenuState2(menu({A::Pass, A::Pause, A::ClaimWin}, A::Pass, 7), 0);
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
  compass.applyMenuState2(menu({A::Resume}, A::Resume, 8), 21000);
  assert(!compass.update(30000).ready && compass.holdProgress(30000) == 0);
  compass.keyUp(Key::Down, 30000);

  // Compass view: the legend's actions per key.
  compass.applyMenuState2(menu({A::ConfirmWin, A::DenyWin, A::AdjustLife}, A::ConfirmWin, 9), 0);
  MenuView v = compass.view();
  assert(v.active && v.compass[static_cast<uint8_t>(Key::Select)] == id(A::ConfirmWin) && v.life);
  assert(v.compass[static_cast<uint8_t>(Key::Left)] == id(A::DenyWin) && v.compass[0] == MENU_NONE);

  // Device menu (both Sigils, owner 2026-10-02): outside a game, the first of
  // Up/Down with nothing on it is Menu. The device menu is a compass too:
  // the click holds Unpair, Down holds Factory reset, Left goes back.
  SigilMenu dev(true);  // The OLED: names the held action on screen.
  dev.applyMenuState2(menu({A::Join}, A::Join, 3), 0);
  v = dev.view();
  assert(v.compass[static_cast<uint8_t>(Key::Select)] == id(A::Join) &&
      v.compass[static_cast<uint8_t>(Key::Up)] == MENU_LOCAL_DEVICE_MENU &&
      v.compass[static_cast<uint8_t>(Key::Down)] == MENU_NONE && !v.deviceMenu);
  assert(strcmp(sigilActionLabel(static_cast<A>(MENU_LOCAL_DEVICE_MENU)), "Menu") == 0 &&
      strcmp(sigilActionLabel(static_cast<A>(MENU_LOCAL_BACK)), "Back") == 0 &&
      strcmp(sigilActionLabel(static_cast<A>(MENU_LOCAL_UNPAIR)), "Unpair") == 0 &&
      strcmp(sigilActionLabel(static_cast<A>(MENU_LOCAL_FACTORY_RESET)), "Factory reset") == 0);
  assert(menuActionNeedsHold(MENU_LOCAL_FACTORY_RESET) && menuActionNeedsHold(MENU_LOCAL_UNPAIR) &&
      !menuActionNeedsHold(MENU_LOCAL_DEVICE_MENU) && !menuActionNeedsHold(MENU_LOCAL_BACK) &&
      !menuActionNeedsHold(id(A::Pass)) && menuActionNeedsHold(id(A::ClaimWin)));
  assert(MENU_UNPAIR_HOLD_MS < MENU_FACTORY_RESET_HOLD_MS);
  // Up taken (Random start): Menu moves to Down; both taken: no Menu.
  assert(dev.keyAction(Key::Up) == MENU_LOCAL_DEVICE_MENU);
  dev.applyMenuState2(menu({A::Join, A::RandomStarter}, A::Join, 4), 0);
  assert(dev.keyAction(Key::Down) == MENU_LOCAL_DEVICE_MENU && dev.keyAction(Key::Up) == id(A::RandomStarter));
  dev.applyMenuState2(menu({A::RandomStarter, A::Leave}, A::Leave, 5), 0);
  assert(dev.keyAction(Key::Up) != MENU_LOCAL_DEVICE_MENU && dev.keyAction(Key::Down) != MENU_LOCAL_DEVICE_MENU);
  // In a game (AdjustLife offered) there is no Menu, even with Up free.
  dev.applyMenuState2(menu({A::Pass, A::AdjustLife}, A::Pass, 6), 0);
  assert(dev.keyAction(Key::Up) == MENU_NONE && dev.keyAction(Key::Down) == MENU_NONE && dev.lifeOffered());
  // Open it: nothing is sent; the compass now holds the device menu.
  dev.applyMenuState2(menu({A::Join}, A::Join, 7), 0);
  dev.keyDown(Key::Up, 1000);
  assert(!dev.update(1000).ready && dev.deviceMenuOpen());
  dev.keyUp(Key::Up, 1050);
  v = dev.view();
  assert(v.deviceMenu && v.compass[static_cast<uint8_t>(Key::Select)] == MENU_LOCAL_UNPAIR &&
      v.compass[static_cast<uint8_t>(Key::Down)] == MENU_LOCAL_FACTORY_RESET &&
      v.compass[static_cast<uint8_t>(Key::Left)] == MENU_LOCAL_BACK &&
      v.compass[static_cast<uint8_t>(Key::Up)] == MENU_LOCAL_SLEEP &&
      v.compass[static_cast<uint8_t>(Key::Right)] == MENU_NONE);
  assert(strcmp(sigilActionLabel(static_cast<A>(MENU_LOCAL_SLEEP)), "Sleep") == 0 &&
      !menuActionNeedsHold(MENU_LOCAL_SLEEP));
  // Back closes it.
  dev.keyDown(Key::Left, 1100);
  assert(!dev.update(1100).ready && !dev.deviceMenuOpen());
  dev.keyUp(Key::Left, 1150);
  // Releasing Factory reset early abandons it; held to the end it is chosen
  // (main.cpp erases the Sigil; the choice is never sent to Atlas).
  dev.keyDown(Key::Up, 2000); dev.keyUp(Key::Up, 2050);
  dev.keyDown(Key::Down, 2100);
  assert(dev.view().holdAction == MENU_LOCAL_FACTORY_RESET && dev.holdProgress(2100 + 2500) > 100);
  dev.keyUp(Key::Down, 2700);
  assert(dev.view().holdAction == MENU_NONE && !dev.update(2700 + MENU_FACTORY_RESET_HOLD_MS).ready);
  dev.keyDown(Key::Down, 3000);
  assert(!dev.update(3000 + MENU_FACTORY_RESET_HOLD_MS - 1).ready);
  c = dev.update(3000 + MENU_FACTORY_RESET_HOLD_MS);
  assert(c.ready && static_cast<uint8_t>(c.action) == MENU_LOCAL_FACTORY_RESET && !dev.deviceMenuOpen());
  dev.keyUp(Key::Down, 9000);
  // Unpair: the click, held MENU_UNPAIR_HOLD_MS (main.cpp forgets Atlas).
  dev.keyDown(Key::Up, 9100); dev.keyUp(Key::Up, 9150);
  dev.keyDown(Key::Select, 9200);
  assert(dev.view().holdAction == MENU_LOCAL_UNPAIR && !dev.update(9200 + MENU_UNPAIR_HOLD_MS - 1).ready);
  c = dev.update(9200 + MENU_UNPAIR_HOLD_MS);
  assert(c.ready && static_cast<uint8_t>(c.action) == MENU_LOCAL_UNPAIR && !dev.deviceMenuOpen());
  dev.keyUp(Key::Select, 9900);
  // Sleep: a tap on Up, chosen at once (main.cpp sleeps until a click).
  dev.keyDown(Key::Up, 9950); dev.keyUp(Key::Up, 9960);
  assert(dev.deviceMenuOpen());
  dev.keyDown(Key::Up, 9970);
  c = dev.update(9970);
  assert(c.ready && static_cast<uint8_t>(c.action) == MENU_LOCAL_SLEEP && !dev.deviceMenuOpen());
  dev.keyUp(Key::Up, 9980);
  // An idle device menu closes by itself; a game starting closes it too.
  dev.keyDown(Key::Up, 10000); dev.keyUp(Key::Up, 10050);
  dev.update(10000 + MENU_DEVICE_IDLE_MS - 1); assert(dev.deviceMenuOpen());
  dev.update(10000 + MENU_DEVICE_IDLE_MS); assert(!dev.deviceMenuOpen());
  dev.keyDown(Key::Up, 30000); dev.keyUp(Key::Up, 30050);
  dev.applyMenuState2(menu({A::Pass, A::AdjustLife}, A::Pass, 8), 30100);
  assert(!dev.deviceMenuOpen() && dev.lifeOffered());
  // The e-ink (no on-screen hold) shows the same compass, never the hold.
  SigilMenu eink(false);
  eink.applyMenuState2(menu({A::Join}, A::Join, 1), 0);
  eink.keyDown(Key::Up, 0); eink.keyUp(Key::Up, 10);
  eink.keyDown(Key::Select, 20);
  assert(eink.view().deviceMenu && eink.view().holdAction == MENU_NONE && eink.holdProgress(1000) > 0);
  eink.keyUp(Key::Select, 1000);

  // Unpaired: back to inactive, device menu closed.
  dev.applyMenuState2(menu({A::Join}, A::Join, 9), 40000);
  dev.keyDown(Key::Up, 40000); dev.keyUp(Key::Up, 40050);
  assert(dev.deviceMenuOpen());
  dev.clear();
  assert(!dev.active() && !dev.view().active && !dev.deviceMenuOpen());

  // MenuState2 carries Leave (action 21) and later actions; Leave is a
  // long-press hold on Down, and a waiting phone link outranks it.
  {
    MenuStateFields f;
    f.actions = sigilActionBit(A::CycleStarter) | sigilActionBit(A::AddSeatB) | sigilActionBit(A::Leave);
    f.defaultAction = id(A::Leave);
    f.revision = 5;
    const MenuStateFields back = decodeMenuState2(encodeMenuState2(f));
    assert(back.actions == f.actions && back.defaultAction == id(A::Leave) && back.revision == 5);
    assert(SigilMenu::compassAction(f.actions, Key::Down) == id(A::Leave));
    assert(SigilMenu::compassAction(f.actions | sigilActionBit(A::LinkPhone), Key::Down) == id(A::LinkPhone));
    assert(sigilActionHold(A::Leave) == ActionHold::Long);
    SigilMenu leaver(false);
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
    // A game counted in hundreds: each step is 100 life; the ring counts steps.
    assert(lifeUnitFor(40) == 1 && lifeUnitFor(8000) == 100 && lifeUnitFor(0) == 1);
    life.press(1, 2, 40000); life.release(40010);
    life.setUnit(100);  // A new unit drops the unsent total.
    assert(life.pending() == 0);
    life.press(-1, 2, 41000); life.release(41010);
    life.press(-1, 2, 41100); life.release(41110);
    assert(life.pending() == -200 && life.pendingSteps() == -2);
    assert(life.update(43110, delta, player) && delta == -200);
  }
  {
    // E-ink counts slower than the OLED, and a longer hold preference slows
    // either one (playtest 2026-09-29, item 6).
    using TurnHubProtocol::DEFAULT_LONG_PRESS_MS;
    const LifePace oled = lifePaceFor(false, DEFAULT_LONG_PRESS_MS);
    assert(oled.repeatDelayMs == TurnHubProtocol::LIFE_ADJUST_REPEAT_DELAY_MS &&
        oled.repeatMs == TurnHubProtocol::LIFE_ADJUST_REPEAT_MS);
    const LifePace eink = lifePaceFor(true, DEFAULT_LONG_PRESS_MS);
    assert(eink.repeatDelayMs == 700 && eink.repeatMs == 300 && eink.fastStep == 1);
    assert(oled.fastStep == TurnHubProtocol::LIFE_ADJUST_FAST_STEP);
    const LifePace patient = lifePaceFor(true, 4000);
    assert(patient.repeatDelayMs == 1400 && patient.repeatMs == 600 && patient.fastAfterMs == 6000);
    LifeAdjuster slow;
    slow.setPace(eink);
    int32_t delta = 0;
    uint8_t player = 0;
    slow.press(1, 2, 0);
    for (uint32_t t = 0; t <= 2000; t += 10) assert(!slow.update(t, delta, player));
    assert(slow.pending() == 1 + 5);  // Tap, then +1 at 0.7, 1.0, 1.3, 1.6 and 1.9 s.
    // A long hold never jumps: ones all the way (+1 every 0.3 s to 4 s).
    for (uint32_t t = 2010; t <= 4000; t += 10) assert(!slow.update(t, delta, player));
    assert(slow.pending() == 1 + 12);
  }

  // Every action has a short label.
  for (uint8_t a = 0; a < static_cast<uint8_t>(A::Count); ++a) {
    const char *label = sigilActionLabel(static_cast<A>(a));
    assert(label[0] && std::strlen(label) <= 12);
  }
  {
    // Both Sigils: Select passes, and pressed again in the grace period it
    // undoes the pass (Undo pass is on the click, like Atlas's "pass again").
    SigilMenu sigil(true);
    sigil.applyMenuState2(menu({A::Pass, A::Pause, A::ClaimWin, A::LinkPhone}, A::Pass, 1), 0);
    sigil.keyDown(Key::Select, 10); sigil.keyUp(Key::Select, 20);
    MenuChoice c = sigil.update(20);
    assert(c.ready && c.action == A::Pass);
    sigil.applyMenuState2(menu({A::CancelPass, A::Pause, A::ClaimWin, A::LinkPhone}, A::CancelPass, 2), 30);
    assert(sigil.keyAction(Key::Select) == id(A::CancelPass));
    sigil.keyDown(Key::Select, 40); sigil.keyUp(Key::Select, 50);
    c = sigil.update(50);
    assert(c.ready && c.action == A::CancelPass);
  }
  std::cout << "Menu wire format, compass keys, device menu, holds and stale menus passed\n";
}
