#include "oled_display.h"
#include "life_heart.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <type_traits>

SerialStub Serial;
TwoWire Wire;
PanelTrace panel;
using namespace TurnHubSigil;
using namespace TurnHubProtocol;

static OledConfig fixture() {
  OledConfig c;
  c.controller = OledController::Sh1106;
  c.bus = OledBus::SoftwareSpi;
  c.width = 128; c.height = 64; c.rotation = 0;
  c.power = OledPower::InternalChargePump;
  c.reset = 22; c.mosi = 23; c.sclk = 18; c.dc = 16; c.cs = 17;
  return c;
}
static bool has(const char *text) {
  for (const auto &line : panel.lines) if (line.text == text) return true;
  return false;
}
// Highlighted text is drawn black on a filled bar.
static bool highlighted(const char *text) {
  for (const auto &line : panel.lines)
    if (line.text == text && line.color == SH110X_BLACK) return true;
  return false;
}
static bool startsWith(const char *prefix) {
  for (const auto &line : panel.lines) if (line.text.rfind(prefix, 0) == 0) return true;
  return false;
}
static void resetTrace() { panel = {}; Wire = {}; Serial.output.clear(); }
static void rejected(const OledConfig &c) {
  resetTrace();
  OledDisplay d(c);
  d.begin(); d.showBooting(); d.showUnpaired(); d.showReady(0);
  GameDisplayPacket s{}; d.showGame(s);
  d.showState(0, DisplayMode::Running, 1, 0, 1, DISPLAY_FLAG_ACTIVE);
  assert(panel.constructors == 0 && panel.frames == 0 && Wire.calls == 0);
  assert(Serial.output.find("UNCONFIGURED_OR_INVALID") != std::string::npos);
}

int main() {
  static_assert(std::is_abstract<SigilDisplay>::value, "Driver-independent interface required");
  assert(&getSigilDisplay() == &getSigilDisplay());
  // The shipped profile is the owner-verified Sigil carrier wiring.
  assert(OLED_CONFIG.controller == OledController::Sh1106 &&
      OLED_CONFIG.bus == OledBus::SoftwareSpi && OLED_CONFIG.width == 128 &&
      OLED_CONFIG.height == 64 && OLED_CONFIG.sclk == 18 &&
      OLED_CONFIG.mosi == 23 && OLED_CONFIG.reset == 22 &&
      OLED_CONFIG.dc == 16 && OLED_CONFIG.cs == 17);
  resetTrace();
  { OledDisplay shipped; shipped.begin();
    assert(panel.begins == 1 && panel.spi && panel.rotation == OLED_CONFIG.rotation); }
  rejected(OledConfig{});
  auto c = fixture();
  c.controller = OledController::Unspecified; rejected(c);
  c = fixture(); c.bus = OledBus::Unspecified; rejected(c);
  c = fixture(); c.width = 0; rejected(c);
  c = fixture(); c.height = 32; rejected(c);
  c = fixture(); c.rotation = 1; rejected(c);
  c = fixture(); c.power = OledPower::Unspecified; rejected(c);
  c = fixture(); c.reset = -2; rejected(c);
  c = fixture(); c.dc = c.cs; rejected(c);
  c = fixture(); c.reset = c.cs; rejected(c);
  for (int pin : {-1, 0, 1, 3, 4, 6, 7, 8, 9, 10, 11, 13, 19, 20, 21, 24, 26,
      28, 29, 30, 31, 32, 33, 34, 35, 39}) {
    c = fixture(); c.mosi = pin; rejected(c);
  }

  resetTrace();
  panel.beginSucceeds = false;
  { OledDisplay d(fixture()); d.begin(); d.showBooting(); }
  assert(panel.begins == 1 && panel.frames == 0);
  assert(Serial.output.find("INIT_FAILED") != std::string::npos);

  resetTrace();
  OledDisplay display(fixture());
  SigilDisplay &d = display;
  d.begin(); d.begin();
  assert(panel.begins == 1 && panel.spi && Wire.calls == 0);
  assert(panel.mosi == 23 && panel.sclk == 18 && panel.cs == 17 && panel.dc == 16);
  assert(panel.reset == 22 && panel.resetRequested);
  d.showBooting(); assert(has("Booting") && has("TurnHub") && panel.shapes > 0);
  d.showUnpaired(); assert(has("UNPAIRED") && has("Hold joystick to") && has("enter pairing mode"));
  d.showReady(7); assert(has("SIGIL 8") && has("Welcome to TurnHub!"));
  d.showUpdate("Downloading", 40); assert(has("UPDATE") && has("40%") && has("Downloading") && has("Keep it powered"));
  d.showUpdate("Joining Atlas Wi-Fi", -1); assert(has("...") && has("Joining Atlas Wi-Fi"));
  assert(d.setSeatName(1, "ABCDEFGHIJKLmore"));
  assert(!d.setSeatName(1, "ABCDEFGHIJKL"));
  assert(!d.setSeatName(3, "ignored"));
  assert(d.setSeatName(2, "Second"));
  d.showState(7, DisplayMode::Lobby, 1, 2, 255, 0);
  assert(has("A: ABCDEFGHIJKL") && !has("B: Second") && has("SHARED SIGIL"));
  assert(highlighted("LOBBY") && highlighted("S8 R255"));
  assert(!highlighted("SHARED SIGIL"));
  d.showState(0, DisplayMode::Starting, 2, 1, 1, DISPLAY_FLAG_STARTER | DISPLAY_FLAG_PRIMARY_B);
  assert(highlighted("GO FIRST: B") && has("B: Second") && !has("A: ABCDEFGHIJKL"));
  d.showState(0, DisplayMode::Running, 1, 2, 1, DISPLAY_FLAG_ACTIVE);
  assert(highlighted("YOUR TURN: A"));
  d.showState(0, DisplayMode::Paused, 2, 1, 1, DISPLAY_FLAG_ATTENTION | DISPLAY_FLAG_PRIMARY_B);
  assert(highlighted("ACTION NEEDED: B"));
  d.showState(0, DisplayMode::GameOver, 1, 2, 1, DISPLAY_FLAG_WINNER);
  assert(highlighted("WINNER!: A"));
  d.showState(0, DisplayMode::Paused, 1, 0, 1, 0);
  assert(has("GAME PAUSED") && !highlighted("GAME PAUSED"));
  d.showState(0, DisplayMode::GameOver, 1, 0, 1, 0);
  assert(has("GAME COMPLETE"));
  assert(d.setSeatName(1, nullptr));
  d.showState(0, DisplayMode::Lobby, 4, 0, 1, 0);
  assert(has("Player 4"));
  d.showState(0, DisplayMode::Ready, 0, 0, 0, 0);
  assert(has("Welcome to TurnHub!"));

  CommanderFlowPacket flow{};
  flow.version=VERSION; flow.type=PacketType::CommanderFlow; flow.sigilId=0;
  flow.stage=CommanderStage::Confirm; flow.recipient=2; flow.recipientSlot=2;
  flow.source=3; flow.commander=2; flow.amount=5; flow.life=40; flow.damage=12;
  std::strcpy(flow.recipientName,"Mae"); std::strcpy(flow.sourceName,"Alex");
  assert(validCommanderFlow(flow)); d.showCommander(flow);
  assert(has("To: Mae (B)") && has("Alex C2: 5") && has("Life 40 -> 35") && has("Cmd 12 -> 17") && has("Click: apply hit"));
  flow.stage=CommanderStage::UndoConfirm; flow.life=35; flow.damage=17;
  d.showCommander(flow); assert(has("Life 35 -> 40") && has("Cmd 17 -> 12") && has("Click: undo hit"));
  for (CommanderStage stage : {CommanderStage::Source,CommanderStage::Commander,CommanderStage::Amount,CommanderStage::Result}) {
    flow.stage=stage; d.showCommander(flow);
  }

  GameDisplayPacket s{};
  s.version = VERSION; s.type = PacketType::GameDisplay; s.sigilId = 7;
  std::strcpy(s.primary.name, "ABCDEFGHIJKL");
  std::strcpy(s.secondary.name, "MNOPQRSTUVWX");
  for (int32_t value : {-1000000, -1, 0, 20, 1000000}) {
    for (bool active : {false, true}) {
      for (bool shared : {false, true}) {
        s.state = encodeDisplayState(DisplayMode::Running, 2, shared ? 1 : 0, 255,
            (active ? DISPLAY_FLAG_ACTIVE : 0) | (shared ? DISPLAY_FLAG_PRIMARY_B : 0));
        s.primary.life = value; s.secondary.life = -value;
        assert(validGameDisplay(s));
        const auto before = s;
        d.showGame(s);
        assert(std::memcmp(&before, &s, sizeof(s)) == 0);
        assert(active ? highlighted("YOUR TURN") :
            has("WAITING FOR TURN") && !highlighted("WAITING FOR TURN"));
        assert(has(shared ? "B: ABCDEFGHIJKL" : "ABCDEFGHIJKL"));
        char life[32]; std::snprintf(life, sizeof(life), "%ld", long(value));
        assert(has(life));
        if (shared) {
          assert(!startsWith("A: MNOP"));
        }
      }
    }
  }
  s.state = encodeDisplayState(DisplayMode::Running, 1, 2, 1, DISPLAY_FLAG_ACTIVE);
  s.commander = 1; d.showGame(s);
  // A can have the higher number; B can have the lower number.
  s.state = encodeDisplayState(DisplayMode::Running, 4, 3, 1, DISPLAY_FLAG_ACTIVE);
  d.showGame(s); assert(has("A: ABCDEFGHIJKL"));
  s.state = encodeDisplayState(DisplayMode::Running, 3, 4, 1,
      DISPLAY_FLAG_ACTIVE | DISPLAY_FLAG_PRIMARY_B);
  d.showGame(s); assert(has("B: ABCDEFGHIJKL"));
  s.state = encodeDisplayState(DisplayMode::Running, 1, 2, 1, DISPLAY_FLAG_ACTIVE);
  d.showGame(s);
  assert(has("A: ABCDEFGHIJKL") && !startsWith("B: MNOP") && highlighted("COMMANDER"));
  d.showState(0, DisplayMode::Paused, 1, 0, 1, 0);
  assert(!has("1000000")); // State-only packets must not retain stale life.

  c = fixture(); c.bus = OledBus::I2c; c.sda = 25; c.scl = 22;
  c.reset = -1; c.rotation = 2; c.i2cClockHz = 100000;
  rejected(c); // Address remains explicitly unset.
  c.i2cAddress = 0x3c;
  resetTrace();
  { OledDisplay i2c(c); i2c.begin(); i2c.showBooting(); }
  assert(!panel.spi && Wire.calls == 1 && Wire.sda == 25 && Wire.scl == 22);
  assert(panel.address == 0x3c && panel.rotation == 2 && !panel.resetRequested);
  assert(panel.clockDuring == 100000 && panel.clockAfter == 100000 && has("Booting"));
  resetTrace(); Wire.succeeds = false;
  { OledDisplay i2c(c); i2c.begin(); i2c.showBooting(); }
  assert(panel.constructors == 0 && panel.frames == 0);
  assert(Serial.output.find("I2C_INIT_FAILED") != std::string::npos);
  // The key legend: one line at the bottom, a key at a time (click first),
  // stepped by the display task's idle work. Up opens the menu list.
  resetTrace();
  {
    OledDisplay d(fixture());
    d.begin();
    SigilMenu m(MenuStyle::List);
    MenuStateFields f;
    for (SigilAction a : {SigilAction::Pass, SigilAction::Pause, SigilAction::ClaimWin}) f.actions |= sigilActionBit(a);
    f.defaultAction = static_cast<uint8_t>(SigilAction::Pass);
    m.applyMenuState2(encodeMenuState2(f), 0);
    d.setMenuView(m.view());
    d.showState(0, DisplayMode::Running, 1, 0, 1, DISPLAY_FLAG_ACTIVE);
    assert(has("YOUR TURN") && has("\x09 Pass turn") && !has("Pause"));
    assert(d.idleWorkDueInMs(1000) == OledDisplay::LEGEND_STEP_MS);
    assert(d.idleWorkDueInMs(1000 + OledDisplay::LEGEND_STEP_MS) == 0);
    d.idleWork(1000 + OledDisplay::LEGEND_STEP_MS);
    assert(has("\x18 Menu"));
    // A redraw keeps the step; a new menu starts again from the click.
    d.showState(0, DisplayMode::Running, 1, 0, 1, DISPLAY_FLAG_ACTIVE);
    assert(has("\x18 Menu"));
    // The list, over the game: every action not on a bare key, then Device
    // and Back; the row under the cursor highlighted and counted in the
    // header, held rows marked.
    m.keyDown(Key::Up, 2000); m.keyUp(Key::Up, 2050);
    d.setMenuView(m.view());
    resetTrace(); d.showState(0, DisplayMode::Running, 1, 0, 1, DISPLAY_FLAG_ACTIVE);
    assert(highlighted("MENU") && has("1/4") && highlighted("Pause") && has("Claim win (hold)") &&
        has("Device") && !has("Pass turn") && !has("YOUR TURN") && has("\x18\x19 Scroll the list"));
    d.idleWork(3000);
    assert(has("\x09 Choose  \x1b Back"));
    for (int i = 0; i < 2; ++i) { m.keyDown(Key::Down, 3100 + i * 20); m.keyUp(Key::Down, 3110 + i * 20); }
    d.setMenuView(m.view());
    resetTrace(); d.showState(0, DisplayMode::Running, 1, 0, 1, DISPLAY_FLAG_ACTIVE);
    assert(has("3/4") && highlighted("Device") && has("\x09 Choose  \x1b Back"));
    // Device: Sleep, Unpair and Factory reset, one list deeper.
    m.keyDown(Key::Select, 3200); m.keyUp(Key::Select, 3210);
    d.setMenuView(m.view());
    resetTrace(); d.showState(0, DisplayMode::Running, 1, 0, 1, DISPLAY_FLAG_ACTIVE);
    assert(highlighted("DEVICE") && has("1/4") && highlighted("Sleep") && has("Unpair (hold)") &&
        has("Factory reset (hold)") && has("Back"));
    m.keyDown(Key::Down, 3300); m.keyUp(Key::Down, 3310);
    m.keyDown(Key::Down, 3320); m.keyUp(Key::Down, 3330);
    m.keyDown(Key::Select, 3400);
    d.setMenuView(m.view());
    resetTrace(); d.showState(0, DisplayMode::Running, 1, 0, 1, DISPLAY_FLAG_ACTIVE);
    assert(highlighted("HOLD: Factory reset"));
    m.keyUp(Key::Select, 3500);
    m.keyDown(Key::Left, 3600); m.keyUp(Key::Left, 3610);  // Up to the menu,
    m.keyDown(Key::Left, 3620); m.keyUp(Key::Left, 3630);  // then closed.
    // Outside a game, Join on the click and Menu on Up: with nothing else
    // on offer, Menu opens on the Device entries.
    MenuStateFields ready;
    ready.actions = sigilActionBit(SigilAction::Join);
    m.applyMenuState2(encodeMenuState2(ready), 0);
    d.setMenuView(m.view());
    resetTrace(); d.showReady(0);
    assert(has("Welcome to TurnHub!") && has("\x09 Join game"));
    d.idleWork(20000);
    assert(has("\x18 Menu"));
    m.keyDown(Key::Up, 21000); m.keyUp(Key::Up, 21050);
    d.setMenuView(m.view());
    resetTrace(); d.showReady(0);
    assert(highlighted("DEVICE") && highlighted("Sleep") && has("Unpair (hold)") && !has("Join game") &&
        !has("Welcome to TurnHub!"));
    // Screens without a legend (the picker) do not step it.
    m.keyDown(Key::Left, 23000);
    d.setMenuView(m.view());
    ProfilePickerPacket page{};
    page.mode = PickerMode::List; page.itemCount = 1; page.pageCount = 1;
    resetTrace(); d.showPicker(page, 0);
    assert(d.idleWorkDueInMs(24000) == UINT32_MAX);
    // Sleep: says how to wake, then the panel is switched off.
    resetTrace(); d.showSleeping();
    assert(panel.lastCommand == SH110X_DISPLAYOFF && d.idleWorkDueInMs(25000) == UINT32_MAX);
  }
  resetTrace();
  {
    // Turntest (2026-09-26): one line of key help, a visible pending pass,
    // and a heart that drains or grows against the starting life.
    OledDisplay d(fixture());
    d.begin();
    SigilMenu m(MenuStyle::List);
    MenuStateFields f;
    for (SigilAction a : {SigilAction::Pass, SigilAction::Pause, SigilAction::AdjustLife}) f.actions |= sigilActionBit(a);
    f.defaultAction = static_cast<uint8_t>(SigilAction::Pass);
    m.applyMenuState2(encodeMenuState2(f), 0);
    d.setMenuView(m.view());
    GameDisplayPacket g{}; g.sigilId = 1;
    g.state = encodeDisplayState(DisplayMode::Running, 1, 0, 7, DISPLAY_FLAG_ACTIVE);
    g.primary.life = 1000000; std::strcpy(g.primary.name, "Michael12345");
    LifeOverlay life;
    life.startingLife = 40;
    d.setLifeOverlay(life);
    d.showGame(g);  // Also checks nothing overlaps at the widest life total.
    assert(highlighted("YOUR TURN") && has("\x09 Pass turn") && has("1000000"));
    life.passPending = true;
    d.setLifeOverlay(life);
    g.primary.life = 29; d.showGame(g);
    assert(highlighted("PASSING...") && has("Click again to undo") && !has("YOUR TURN"));
    // Waiting Sigils see another player's pending pass too.
    life.passPending = false; life.passingPlayer = 3; d.setLifeOverlay(life);
    g.state = encodeDisplayState(DisplayMode::Running, 1, 0, 7, 0);
    d.showGame(g);
    assert(has("P3 PASSING...") && !has("WAITING FOR TURN") && !has("Click again to undo"));
    life.passingPlayer = 0;
    g.state = encodeDisplayState(DisplayMode::Running, 1, 0, 7, DISPLAY_FLAG_ACTIVE);
    // The life dial: fewer lit pixels as life drains, more as it grows
    // (an outer arc above the starting life).
    const auto heartPixels = [&](int32_t lifeTotal) {
      g.primary.life = lifeTotal; resetTrace(); d.showGame(g); return panel.shapes;
    };
    life.passPending = false; d.setLifeOverlay(life);
    const int low = heartPixels(5), full = heartPixels(40), big = heartPixels(80);
    assert(low < full && full < big);
    assert(lifeHeartLook(0, 40).fill == 0 && lifeHeartLook(1, 40).fill == 16 &&
        lifeHeartLook(20, 40).fill == 127 && lifeHeartLook(40, 40).fill == 255 &&
        lifeHeartLook(40, 40).sizePercent == 100 && lifeHeartLook(60, 40).sizePercent == 125 &&
        lifeHeartLook(500, 40).sizePercent == 150 && lifeHeartLook(7, 0).fill == 255);
  }
  std::cout << "OLED configuration, failure, lifecycle, shared-seat and numeric-bound scenarios passed\n";
}
