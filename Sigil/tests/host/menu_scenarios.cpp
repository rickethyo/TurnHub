#include "sigil_menu.h"
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
  // A diagnostic overlay cancels a confirm popup and queued actions, preserving the
  // menu Atlas offered. Releasing a key afterward must not perform a choice.
  {
    SigilMenu m;
    m.applyMenuState2(menu({A::Join}, A::Join, 7), 0);
    m.keyDown(Key::Select, 10);
    m.cancelInput();
    m.keyUp(Key::Select, 10000);
    assert(!m.update(10000).ready);
    assert(m.active() && m.actions() == sigilActionBit(A::Join));
    m.keyDown(Key::Select, 11000);
    m.keyUp(Key::Select, 11010);
    assert(m.update(11010).ready);
  }
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
    assert(n == 2 && e[0] == id(A::RandomStarter) && e[1] == id(A::Leave));
    // A Commander game, own turn, shared Sigil: same keys as any game.
    const uint32_t cmd = bits({A::Pass, A::ClaimWin, A::Pause, A::AdjustLife, A::CommanderDamage,
        A::UndoCommanderHit, A::AddPartner, A::SwitchSeat});
    assert(SigilMenu::bareAction(cmd, Key::Select) == id(A::Pass));
    assert(SigilMenu::bareAction(cmd, Key::Down) == id(A::SwitchSeat));
    assert(SigilMenu::bareAction(cmd, Key::Left) == MENU_NONE && SigilMenu::bareAction(cmd, Key::Right) == MENU_NONE);
    n = SigilMenu::menuEntries(cmd, e);
    const uint8_t want[] = {id(A::CommanderDamage), id(A::UndoCommanderHit), id(A::Pause), id(A::ClaimWin),
        id(A::AddPartner), MENU_LOCAL_COUNTERS};
    assert(n == sizeof(want) && std::memcmp(e, want, n) == 0);
    // Paused: Resume on the click; I'm out in Menu.
    const uint32_t paused = bits({A::Resume, A::BeginElimination, A::AdjustLife});
    assert(SigilMenu::bareAction(paused, Key::Select) == id(A::Resume));
    n = SigilMenu::menuEntries(paused, e);
    assert(n == 2 && e[0] == id(A::BeginElimination) && e[1] == MENU_LOCAL_COUNTERS);
    // Decisions keep their answers on bare keys.
    const uint32_t out = bits({A::Eliminate, A::NextTarget, A::CancelElimination});
    assert(SigilMenu::bareAction(out, Key::Select) == id(A::Eliminate) &&
        SigilMenu::bareAction(out, Key::Right) == id(A::NextTarget) &&
        SigilMenu::bareAction(out, Key::Left) == id(A::CancelElimination));
    // Nothing else on offer: Menu is the Device entries.
    n = SigilMenu::menuEntries(0, e);
    assert(n == 5 && e[0] == MENU_LOCAL_UNPAIR && e[1] == MENU_LOCAL_FACTORY_RESET &&
        e[2] == MENU_LOCAL_THEME && e[3] == MENU_LOCAL_INVERT && e[4] == MENU_LOCAL_CALIBRATE);
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

  {
    SigilMenu m;
    m.applyMenuState2(menu({A::Pass,A::AdjustLife},A::Pass,1),0);
    m.keyDown(Key::Up,0);
    assert(m.keyAction(Key::Select)==MENU_NONE); // Generic game: no dead counter entry.
    m.setPlayerControlsAvailable(true);
    assert(m.keyAction(Key::Select)==MENU_LOCAL_COUNTERS);
    m.setPlayerControlsAvailable(false);
    assert(m.keyAction(Key::Select)==MENU_NONE);
  }

  // No menu yet: inactive, keys ignored.
  {
    SigilMenu idle;
    assert(!idle.active() && idle.keyAction(Key::Select) == MENU_NONE);
    idle.keyDown(Key::Select, 0);
    assert(!idle.update(10).ready);
  }

  // Menu: compass pages (click, Up, Right, Down; Down is More while more
  // follow), Left back a page or out. Choosing an entry closes Menu.
  {
    SigilMenu cmd;
    cmd.setPlayerControlsAvailable(true);
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
        cmd.keyAction(Key::Up) == id(A::AddPartner) && cmd.keyAction(Key::Right) == MENU_LOCAL_COUNTERS &&
        cmd.keyAction(Key::Down) == MENU_NONE);
    // Counter page is local; device settings never appear in the game menu.
    cmd.keyDown(Key::Right,40); cmd.keyUp(Key::Right,50);
    const auto counters=cmd.update(50);
    assert(counters.ready && static_cast<uint8_t>(counters.action)==MENU_LOCAL_COUNTERS && !cmd.deviceMenuOpen());
    // Choose Cmd damage: sent once, Menu closes.
    cmd.keyDown(Key::Up, 100); cmd.keyDown(Key::Select, 110);
    MenuChoice c = cmd.update(120);
    assert(c.ready && c.action == A::CommanderDamage && c.revision == 3 && !cmd.deviceMenuOpen());
    // Claim win from Menu asks first: a popup whose only keys are Confirm
    // (Select) and Cancel (Left). Holding does nothing more than a tap.
    cmd.keyDown(Key::Up, 200); cmd.keyDown(Key::Down, 210); cmd.keyDown(Key::Select, 220);
    assert(!cmd.update(220 + DEFAULT_WIN_HOLD_MS).ready && cmd.confirmOpen() &&
        cmd.view().confirmAction == id(A::ClaimWin));
    cmd.keyUp(Key::Select, 230);
    assert(cmd.keyAction(Key::Select) == MENU_LOCAL_CONFIRM && cmd.keyAction(Key::Left) == MENU_LOCAL_BACK &&
        cmd.keyAction(Key::Up) == MENU_NONE && cmd.keyAction(Key::Right) == MENU_NONE);
    // Cancel goes back to Menu, where it was asked.
    cmd.keyDown(Key::Left, 300);
    assert(!cmd.confirmOpen() && cmd.deviceMenuOpen() && !cmd.update(300).ready);
    cmd.keyDown(Key::Select, 5000); assert(cmd.confirmOpen());
    cmd.keyDown(Key::Select, 5100);
    c = cmd.update(5100);
    assert(c.ready && c.action == A::ClaimWin && !cmd.deviceMenuOpen() && !cmd.confirmOpen());
    // A popup about an action that stops being offered closes.
    cmd.keyDown(Key::Up, 6000); cmd.keyDown(Key::Down, 6010); cmd.keyDown(Key::Select, 6020);
    assert(cmd.view().confirmAction == id(A::ClaimWin));
    cmd.applyMenuState2(menu({A::Pause, A::AdjustLife}, A::Pass, 4), 6030);
    assert(!cmd.confirmOpen() && !cmd.update(6040).ready);
    // Fewer entries: the page is kept in range.
    assert(cmd.view().page == 0 && cmd.view().pageCount == 1);
    // An idle Menu closes by itself.
    assert(!cmd.update(6020 + MENU_DEVICE_IDLE_MS).ready && !cmd.deviceMenuOpen());
  }

  // Device settings remain available; destructive actions ask first.
  {
    SigilMenu dev;
    dev.applyMenuState2(menu({A::Join}, A::Join, 1), 0);
    dev.openDeviceMenu(0);
    assert(dev.view().recovery && dev.keyAction(Key::Select) == MENU_LOCAL_UNPAIR);
    assert(dev.keyAction(Key::Up) == MENU_LOCAL_FACTORY_RESET);
    assert(dev.keyAction(Key::Right) == MENU_LOCAL_THEME);
    assert(dev.keyAction(Key::Down) == MENU_LOCAL_MORE);
    dev.keyDown(Key::Right, 1);
    const auto theme = dev.update(1);
    assert(theme.ready && static_cast<uint8_t>(theme.action) == MENU_LOCAL_THEME && dev.deviceMenuOpen());
    dev.keyUp(Key::Right, 2);
    dev.keyDown(Key::Down, 5);
    assert(dev.view().page == 1 && dev.keyAction(Key::Select) == MENU_LOCAL_INVERT &&
        dev.keyAction(Key::Up) == MENU_LOCAL_CALIBRATE);
    dev.keyDown(Key::Select, 6);
    const auto invert = dev.update(6);
    assert(invert.ready && static_cast<uint8_t>(invert.action) == MENU_LOCAL_INVERT && dev.deviceMenuOpen());
    dev.keyUp(Key::Select, 7);
    dev.keyDown(Key::Up, 8);
    const auto calibrate = dev.update(8);
    assert(calibrate.ready && static_cast<uint8_t>(calibrate.action) == MENU_LOCAL_CALIBRATE && !dev.deviceMenuOpen());
    dev.keyUp(Key::Up, 9);
    dev.openDeviceMenu(100); dev.keyDown(Key::Up, 110);
    assert(!dev.update(5000).ready && dev.confirmOpen() && dev.view().confirmAction == MENU_LOCAL_FACTORY_RESET);
    // An unanswered popup closes by itself.
    assert(!dev.update(110 + MENU_DEVICE_IDLE_MS).ready && !dev.confirmOpen());
    dev.openDeviceMenu(15000); dev.keyDown(Key::Up, 15010); dev.keyDown(Key::Select, 15020);
    MenuChoice c = dev.update(15030);
    assert(c.ready && static_cast<uint8_t>(c.action) == MENU_LOCAL_FACTORY_RESET && !dev.deviceMenuOpen());
    dev.openDeviceMenu(20000); dev.keyDown(Key::Select, 20010); dev.keyDown(Key::Select, 20020);
    c = dev.update(20030);
    assert(c.ready && static_cast<uint8_t>(c.action) == MENU_LOCAL_UNPAIR);
    // Back from the Device entries opened directly closes Menu.
    dev.openDeviceMenu(30000); dev.keyDown(Key::Left, 30010);
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
    SigilMenu lost;
    lost.applyMenuState2(menu({A::Pass, A::Pause, A::AdjustLife}, A::Pass, 1), 0);
    lost.setOffline();
    assert(lost.offline() && !lost.lifeOffered() && lost.keyAction(Key::Select) == MENU_NONE &&
        lost.keyAction(Key::Up) == MENU_LOCAL_DEVICE_MENU);
    lost.keyDown(Key::Up, 10);
    assert(lost.view().recovery && lost.keyAction(Key::Select) == MENU_LOCAL_UNPAIR);
    lost.applyMenuState2(menu({A::Pass, A::AdjustLife}, A::Pass, 2), 20);
    assert(!lost.offline() && lost.keyAction(Key::Select) == id(A::Pass));
    lost.setOffline(); lost.endOffline();
    assert(!lost.active());
  }

  // Leave lobby from Menu asks first too.
  {
    SigilMenu leaver;
    leaver.applyMenuState2(menu({A::CycleStarter, A::AddSeatB, A::Leave}, A::CycleStarter, 5), 0);
    leaver.keyDown(Key::Up, 0);
    assert(leaver.keyAction(Key::Select) == id(A::Leave) && menuActionNeedsConfirm(id(A::Leave)));
    leaver.keyDown(Key::Select, 10);
    assert(!leaver.update(10 + DEFAULT_LONG_PRESS_MS).ready && leaver.confirmOpen());
    leaver.keyDown(Key::Select, 20);
    const MenuChoice left = leaver.update(20);
    assert(left.ready && left.action == A::Leave && left.revision == 5);
  }
  // A bare-key action that asks first (Confirm out) answers the same way,
  // and its Cancel keeps Left from changing life.
  {
    SigilMenu out;
    out.applyMenuState2(menu({A::Eliminate, A::CancelElimination, A::AdjustLife}, A::Eliminate, 6), 0);
    out.keyDown(Key::Select, 10);
    assert(out.confirmOpen() && !out.deviceMenuOpen() && out.keyAction(Key::Left) == MENU_LOCAL_BACK);
    out.keyDown(Key::Left, 20); assert(!out.confirmOpen() && !out.update(20).ready);
    out.keyDown(Key::Select, 30); out.keyDown(Key::Select, 40);
    const MenuChoice gone = out.update(40);
    assert(gone.ready && gone.action == A::Eliminate);
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
    // A longer hold preference slows the touch repeat and acceleration.
    using TurnHubProtocol::DEFAULT_LONG_PRESS_MS;
    const LifePace touch = lifePaceFor(DEFAULT_LONG_PRESS_MS);
    assert(touch.repeatDelayMs == TurnHubProtocol::LIFE_ADJUST_REPEAT_DELAY_MS &&
        touch.repeatMs == TurnHubProtocol::LIFE_ADJUST_REPEAT_MS &&
        touch.fastStep == TurnHubProtocol::LIFE_ADJUST_FAST_STEP);
    const LifePace patient = lifePaceFor(DEFAULT_LONG_PRESS_MS * 2);
    assert(patient.repeatDelayMs == touch.repeatDelayMs * 2 &&
        patient.repeatMs == touch.repeatMs * 2 && patient.fastAfterMs == touch.fastAfterMs * 2);
    const LifePace fallback = lifePaceFor(0);
    assert(fallback.repeatDelayMs == touch.repeatDelayMs && fallback.repeatMs == touch.repeatMs);

  }

  // Every action has a short label.
  for (uint8_t a = 0; a < static_cast<uint8_t>(A::Count); ++a) {
    const char *label = sigilActionLabel(static_cast<A>(a));
    assert(label[0] && std::strlen(label) <= 12);
  }
  {
    // Touch Select passes, and pressed again in the grace period it
    // undoes the pass (Undo pass is on the click, like Atlas's "pass again").
    SigilMenu sigil;
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
  std::cout << "Menu wire format, bare keys, touch pages, Device, holds, stale menus passed\n";
}
