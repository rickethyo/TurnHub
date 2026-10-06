#include "sigil_menu.h"
#include "picker_list.h"
#include "life_adjust.h"
#include "three_part_button.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

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
    assert(f.defaultAction == SIGIL_ACTION_NONE && f.revision == 5);
    assert(decodeMenuState2(menu({A::Pause}, A::Pass, 1)).defaultAction == SIGIL_ACTION_NONE);
    const int32_t select = encodeSelectAction(A::ClaimWin, 63);
    assert(selectedAction(select) == id(A::ClaimWin) && selectedRevision(select) == 63);
  }

  // Bare keys, the same on both displays: the click is the obvious step,
  // Left/Right the lobby's seat B and starter (or a decision's answers),
  // Down Switch seat, Up always Menu.
  {
    auto bits = [](std::initializer_list<A> actions) {
      uint32_t b = 0;
      for (A a : actions) b |= sigilActionBit(a);
      return b;
    };
    const uint32_t lobby = bits({A::StartGame, A::CycleStarter, A::AddSeatB, A::RandomStarter, A::Leave});
    assert(SigilMenu::bareAction(lobby, Key::Select) == id(A::StartGame));
    assert(SigilMenu::bareAction(lobby, Key::Left) == id(A::AddSeatB));
    assert(SigilMenu::bareAction(lobby, Key::Right) == id(A::CycleStarter));
    assert(SigilMenu::bareAction(lobby, Key::Down) == MENU_NONE);
    assert(SigilMenu::bareAction(lobby, Key::Up) == MENU_LOCAL_DEVICE_MENU);
    uint8_t e[MENU_LIST_MAX];
    uint8_t n = SigilMenu::menuEntries(lobby, e);
    assert(n == 3 && e[0] == id(A::RandomStarter) && e[1] == id(A::Leave) && e[2] == MENU_LOCAL_DEVICE);
    // A Commander game, own turn, shared Sigil: same keys as any game.
    const uint32_t cmd = bits({A::Pass, A::ClaimWin, A::Pause, A::AdjustLife, A::CommanderDamage,
        A::UndoCommanderHit, A::AddPartner, A::SwitchSeat});
    assert(SigilMenu::bareAction(cmd, Key::Select) == id(A::Pass));
    assert(SigilMenu::bareAction(cmd, Key::Down) == id(A::SwitchSeat));
    assert(SigilMenu::bareAction(cmd, Key::Left) == MENU_NONE && SigilMenu::bareAction(cmd, Key::Right) == MENU_NONE);
    n = SigilMenu::menuEntries(cmd, e);
    const uint8_t want[] = {id(A::CommanderDamage), id(A::UndoCommanderHit), id(A::Pause), id(A::ClaimWin),
        id(A::AddPartner), MENU_LOCAL_DEVICE};
    assert(n == sizeof(want) && std::memcmp(e, want, n) == 0);
    // Paused: Resume on the click; I'm out in Menu.
    const uint32_t paused = bits({A::Resume, A::BeginElimination, A::AdjustLife});
    assert(SigilMenu::bareAction(paused, Key::Select) == id(A::Resume));
    n = SigilMenu::menuEntries(paused, e);
    assert(n == 2 && e[0] == id(A::BeginElimination) && e[1] == MENU_LOCAL_DEVICE);
    // Decisions keep their answers on bare keys.
    const uint32_t out = bits({A::Eliminate, A::NextTarget, A::CancelElimination});
    assert(SigilMenu::bareAction(out, Key::Select) == id(A::Eliminate) &&
        SigilMenu::bareAction(out, Key::Right) == id(A::NextTarget) &&
        SigilMenu::bareAction(out, Key::Left) == id(A::CancelElimination));
    // Nothing else on offer: Menu is the Device entries.
    n = SigilMenu::menuEntries(0, e);
    assert(n == 3 && e[0] == MENU_LOCAL_SLEEP && e[1] == MENU_LOCAL_UNPAIR && e[2] == MENU_LOCAL_FACTORY_RESET);
    // Every offered action is reachable: on a bare key or in Menu.
    for (uint8_t a = 0; a < MENU_MAX_ITEMS; ++a) {
      if (a == id(A::AdjustLife)) continue;
      const uint32_t only = 1u << a;
      bool found = false;
      for (Key k : {Key::Select, Key::Left, Key::Right, Key::Down}) found = found || SigilMenu::bareAction(only, k) == a;
      n = SigilMenu::menuEntries(only, e);
      for (uint8_t i = 0; i < n; ++i) found = found || e[i] == a;
      assert(found);
    }
  }

  // No menu yet: inactive, keys ignored.
  {
    SigilMenu idle(MenuStyle::Compass);
    assert(!idle.active() && idle.keyAction(Key::Select) == MENU_NONE);
    idle.keyDown(Key::Select, 0);
    assert(!idle.update(10).ready);
  }

  // E-ink Menu: compass pages (click, Up, Right, Down; Down is More while more
  // follow), Left back a page or out. Choosing an entry closes Menu.
  {
    SigilMenu cmd(MenuStyle::Compass);
    cmd.applyMenuState2(menu({A::Pass, A::ClaimWin, A::Pause, A::AdjustLife, A::CommanderDamage,
        A::UndoCommanderHit, A::AddPartner}, A::Pass, 3), 0);
    assert(cmd.lifeOffered() && cmd.keyAction(Key::Up) == MENU_LOCAL_DEVICE_MENU);
    cmd.keyDown(Key::Up, 0); cmd.keyUp(Key::Up, 10);
    assert(cmd.deviceMenuOpen() && !cmd.update(10).ready);
    MenuView v = cmd.view();
    assert(v.deviceMenu && v.page == 0 && v.pageCount == 2 && !v.recovery);
    assert(cmd.keyAction(Key::Select) == id(A::CommanderDamage) && cmd.keyAction(Key::Up) == id(A::UndoCommanderHit) &&
        cmd.keyAction(Key::Right) == id(A::Pause) && cmd.keyAction(Key::Down) == MENU_LOCAL_MORE &&
        cmd.keyAction(Key::Left) == MENU_LOCAL_BACK);
    cmd.keyDown(Key::Down, 20); cmd.keyUp(Key::Down, 30);
    assert(cmd.view().page == 1 && cmd.keyAction(Key::Select) == id(A::ClaimWin) &&
        cmd.keyAction(Key::Up) == id(A::AddPartner) && cmd.keyAction(Key::Right) == MENU_LOCAL_DEVICE &&
        cmd.keyAction(Key::Down) == MENU_NONE);
    // Device, then Back to the page that opened it, Back to page 1, Back out.
    cmd.keyDown(Key::Right, 40); cmd.keyUp(Key::Right, 50);
    assert(cmd.view().recovery && cmd.keyAction(Key::Select) == MENU_LOCAL_SLEEP &&
        cmd.keyAction(Key::Up) == MENU_LOCAL_UNPAIR && cmd.keyAction(Key::Right) == MENU_LOCAL_FACTORY_RESET);
    cmd.keyDown(Key::Left, 60); assert(!cmd.view().recovery && cmd.view().page == 1);
    cmd.keyDown(Key::Left, 70); assert(cmd.view().page == 0);
    cmd.keyDown(Key::Left, 80); assert(!cmd.deviceMenuOpen());
    // Choose Cmd damage: sent once, Menu closes.
    cmd.keyDown(Key::Up, 100); cmd.keyDown(Key::Select, 110);
    MenuChoice c = cmd.update(120);
    assert(c.ready && c.action == A::CommanderDamage && c.revision == 3 && !cmd.deviceMenuOpen());
    // Claim win from Menu needs the win hold; letting go early abandons it.
    cmd.keyDown(Key::Up, 200); cmd.keyDown(Key::Down, 210); cmd.keyDown(Key::Select, 220);
    assert(!cmd.update(220 + DEFAULT_WIN_HOLD_MS - 1).ready);
    cmd.keyUp(Key::Select, 220 + DEFAULT_WIN_HOLD_MS - 1);
    assert(!cmd.update(220 + DEFAULT_WIN_HOLD_MS).ready && cmd.deviceMenuOpen());
    cmd.keyDown(Key::Select, 5000);
    assert(cmd.holdProgress(5000 + DEFAULT_WIN_HOLD_MS / 2) > 0);
    c = cmd.update(5000 + DEFAULT_WIN_HOLD_MS);
    assert(c.ready && c.action == A::ClaimWin && !cmd.deviceMenuOpen());
    // The compass view never carries hold state (no e-ink refresh for it).
    cmd.keyDown(Key::Up, 6000); cmd.keyDown(Key::Down, 6010); cmd.keyDown(Key::Select, 6020);
    assert(cmd.view().holdAction == MENU_NONE);
    // A held action that stops being offered is dropped.
    cmd.applyMenuState2(menu({A::Pause, A::AdjustLife}, A::Pass, 4), 6030);
    assert(!cmd.update(6020 + DEFAULT_WIN_HOLD_MS).ready);
    // Fewer entries: the page is kept in range.
    assert(cmd.view().page == 0 && cmd.view().pageCount == 1);
    // An idle Menu closes by itself.
    assert(!cmd.update(6020 + MENU_DEVICE_IDLE_MS).ready && !cmd.deviceMenuOpen());
  }

  // Device entries: Sleep is a tap; Unpair and Factory reset are held.
  {
    SigilMenu dev(MenuStyle::Compass);
    dev.applyMenuState2(menu({A::Join}, A::Join, 1), 0);
    dev.keyDown(Key::Up, 0);  // Menu is the Device entries: nothing else offered.
    assert(dev.view().recovery && dev.keyAction(Key::Select) == MENU_LOCAL_SLEEP);
    dev.keyDown(Key::Select, 10);
    MenuChoice c = dev.update(20);
    assert(c.ready && static_cast<uint8_t>(c.action) == MENU_LOCAL_SLEEP && !dev.deviceMenuOpen());
    dev.keyDown(Key::Up, 100); dev.keyDown(Key::Right, 110);
    assert(!dev.update(110 + MENU_FACTORY_RESET_HOLD_MS - 1).ready);
    c = dev.update(110 + MENU_FACTORY_RESET_HOLD_MS);
    assert(c.ready && static_cast<uint8_t>(c.action) == MENU_LOCAL_FACTORY_RESET && !dev.deviceMenuOpen());
    dev.keyDown(Key::Up, 20000); dev.keyDown(Key::Up, 20010);
    c = dev.update(20010 + MENU_UNPAIR_HOLD_MS);
    assert(c.ready && static_cast<uint8_t>(c.action) == MENU_LOCAL_UNPAIR);
    // Back from the Device entries opened directly closes Menu.
    dev.keyDown(Key::Up, 30000); dev.keyDown(Key::Left, 30010);
    assert(!dev.deviceMenuOpen());
    // A game starting closes Menu.
    dev.applyMenuState2(menu({A::Leave, A::RandomStarter}, A::Join, 2), 31000);
    dev.keyDown(Key::Up, 31000); assert(dev.deviceMenuOpen());
    dev.applyMenuState2(menu({A::Pass, A::AdjustLife}, A::Pass, 3), 31010);
    assert(!dev.deviceMenuOpen());
    // Unpaired: inactive, Menu closed.
    dev.clear(); assert(!dev.active() && !dev.deviceMenuOpen());
  }

  // Atlas lost: Atlas's menu (even mid-game) gives way to Menu on Up, on the
  // Device entries; a menu from Atlas ends it.
  {
    SigilMenu lost(MenuStyle::Compass);
    lost.applyMenuState2(menu({A::Pass, A::Pause, A::AdjustLife}, A::Pass, 1), 0);
    lost.setOffline();
    assert(lost.offline() && !lost.lifeOffered() && lost.keyAction(Key::Select) == MENU_NONE &&
        lost.keyAction(Key::Up) == MENU_LOCAL_DEVICE_MENU);
    lost.keyDown(Key::Up, 10);
    assert(lost.view().recovery && lost.keyAction(Key::Up) == MENU_LOCAL_UNPAIR);
    lost.applyMenuState2(menu({A::Pass, A::AdjustLife}, A::Pass, 2), 20);
    assert(!lost.offline() && lost.keyAction(Key::Select) == id(A::Pass));
    lost.setOffline(); lost.endOffline();
    assert(!lost.active());
  }

  // Leave lobby is a long-press hold from Menu.
  {
    SigilMenu leaver(MenuStyle::Compass);
    leaver.applyMenuState2(menu({A::CycleStarter, A::AddSeatB, A::Leave}, A::CycleStarter, 5), 0);
    leaver.keyDown(Key::Up, 0);
    assert(leaver.keyAction(Key::Select) == id(A::Leave));
    leaver.keyDown(Key::Select, 10);
    assert(!leaver.update(10 + DEFAULT_LONG_PRESS_MS - 1).ready);
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
  for (MenuStyle style : {MenuStyle::Compass, MenuStyle::List}) {
    // Both Sigils: Select passes, and pressed again in the grace period it
    // undoes the pass (Undo pass is on the click, like Atlas's "pass again").
    SigilMenu sigil(style);
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
  // OLED: the same bare keys; Menu is a scrolling list of the same entries,
  // then Back. Up/Down move, the click or Right chooses, Left goes back.
  {
    SigilMenu oled(MenuStyle::List);
    oled.applyMenuState2(menu({A::Pass, A::ClaimWin, A::Pause, A::AdjustLife, A::CommanderDamage,
        A::DropPartner, A::SwitchSeat}, A::Pass, 2), 0);
    assert(oled.keyAction(Key::Select) == id(A::Pass) && oled.keyAction(Key::Down) == id(A::SwitchSeat) &&
        oled.keyAction(Key::Up) == MENU_LOCAL_DEVICE_MENU && oled.keyAction(Key::Left) == MENU_NONE);
    oled.keyDown(Key::Up, 0);
    MenuView v = oled.view();
    const uint8_t want[] = {id(A::CommanderDamage), id(A::Pause), id(A::ClaimWin), id(A::DropPartner),
        MENU_LOCAL_DEVICE, MENU_LOCAL_BACK};
    assert(v.deviceMenu && v.list && v.rowCount == sizeof(want) && std::memcmp(v.rows, want, v.rowCount) == 0);
    assert(v.cursor == 0 && oled.keyAction(Key::Select) == id(A::CommanderDamage) &&
        oled.keyAction(Key::Right) == id(A::CommanderDamage) && oled.keyAction(Key::Left) == MENU_LOCAL_BACK);
    // An unsent life total survives the open list; its keys are the list's.
    assert(oled.lifeOffered());
    for (int i = 0; i < 3; ++i) oled.keyDown(Key::Down, 10 + i);
    assert(oled.view().cursor == 3 && oled.keyAction(Key::Select) == id(A::DropPartner));
    oled.keyDown(Key::Down, 20);
    oled.keyDown(Key::Select, 30);  // Device.
    v = oled.view();
    assert(v.recovery && v.rowCount == 4 && v.rows[0] == MENU_LOCAL_SLEEP && v.rows[3] == MENU_LOCAL_BACK);
    oled.keyDown(Key::Left, 40);  // Back onto the Device row.
    v = oled.view();
    assert(!v.recovery && v.rows[v.cursor] == MENU_LOCAL_DEVICE);
    oled.keyDown(Key::Up, 50); oled.keyDown(Key::Up, 55); oled.keyDown(Key::Up, 60);
    oled.keyDown(Key::Right, 70);
    MenuChoice c = oled.update(80);
    assert(c.ready && c.action == A::Pause && !oled.deviceMenuOpen());
    // Holds show on the list (the OLED redraws cheaply).
    oled.keyDown(Key::Up, 100); oled.keyDown(Key::Down, 110); oled.keyDown(Key::Down, 120);
    oled.keyDown(Key::Select, 130);
    assert(oled.view().holdAction == id(A::ClaimWin));
    c = oled.update(130 + DEFAULT_WIN_HOLD_MS);
    assert(c.ready && c.action == A::ClaimWin && !oled.deviceMenuOpen());
    // Rows never repeat.
    oled.keyDown(Key::Up, 200);
    v = oled.view();
    for (uint8_t i = 0; i < v.rowCount; ++i)
      for (uint8_t j = 0; j < i; ++j) assert(v.rows[i] != v.rows[j]);
  }
  std::cout << "Menu wire format, bare keys, e-ink pages, OLED list, Device, holds and stale menus passed\n";
}
