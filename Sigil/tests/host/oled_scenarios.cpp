#include "oled_display.h"
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
  for (int pin : {-1, 0, 1, 3, 6, 7, 8, 9, 10, 11, 13, 14, 19, 20, 21, 24, 25, 26,
      27, 28, 29, 30, 31, 32, 33, 34, 39}) {
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
  d.showUnpaired(); assert(has("UNPAIRED") && has("Press Pair on both"));
  d.showReady(7); assert(has("SIGIL 8") && has("Ready for game"));
  assert(d.setSeatName(1, "ABCDEFGHIJKLmore"));
  assert(!d.setSeatName(1, "ABCDEFGHIJKL"));
  assert(!d.setSeatName(3, "ignored"));
  assert(d.setSeatName(2, "Second"));
  d.showState(7, DisplayMode::Lobby, 1, 2, 255, DISPLAY_FLAG_HOST);
  assert(has("A: ABCDEFGHIJKL") && has("B: Second") && has("SHARED SIGIL"));
  assert(highlighted("LOBBY") && highlighted("S8 T255"));
  assert(!highlighted("SHARED SIGIL"));
  d.showState(0, DisplayMode::Starting, 2, 1, 1, DISPLAY_FLAG_STARTER);
  assert(highlighted("GO FIRST: B"));
  d.showState(0, DisplayMode::Running, 1, 2, 1, DISPLAY_FLAG_ACTIVE);
  assert(highlighted("YOUR TURN: A"));
  d.showState(0, DisplayMode::Paused, 2, 1, 1, DISPLAY_FLAG_ATTENTION);
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
  assert(has("Ready for game"));

  GameDisplayPacket s{};
  s.version = VERSION; s.type = PacketType::GameDisplay; s.sigilId = 7;
  std::strcpy(s.primary.name, "ABCDEFGHIJKL");
  std::strcpy(s.secondary.name, "MNOPQRSTUVWX");
  for (int32_t value : {-1000000, -1, 0, 20, 1000000}) {
    for (bool active : {false, true}) {
      for (bool shared : {false, true}) {
        s.state = encodeDisplayState(DisplayMode::Running, 2, shared ? 1 : 0, 255,
            active ? DISPLAY_FLAG_ACTIVE : 0);
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
          // The other seat's name may shorten; its life total never does.
          assert(startsWith("A: MNOP"));
          std::snprintf(life, sizeof(life), "\x03%ld", long(-value));
          assert(has(life));
        }
      }
    }
  }
  s.state = encodeDisplayState(DisplayMode::Running, 1, 2, 1, DISPLAY_FLAG_ACTIVE);
  s.commander = 1; d.showGame(s);
  assert(has("A: ABCDEFGHIJKL") && startsWith("B: MNOP") && highlighted("COMMANDER"));
  d.showState(0, DisplayMode::Paused, 1, 0, 1, 0);
  assert(!has("1000000")); // State-only packets must not retain stale life.

  c = fixture(); c.bus = OledBus::I2c; c.sda = 4; c.scl = 22;
  c.reset = -1; c.rotation = 2; c.i2cClockHz = 100000;
  rejected(c); // Address remains explicitly unset.
  c.i2cAddress = 0x3c;
  resetTrace();
  { OledDisplay i2c(c); i2c.begin(); i2c.showBooting(); }
  assert(!panel.spi && Wire.calls == 1 && Wire.sda == 4 && Wire.scl == 22);
  assert(panel.address == 0x3c && panel.rotation == 2 && !panel.resetRequested);
  assert(panel.clockDuring == 100000 && panel.clockAfter == 100000 && has("Booting"));
  resetTrace(); Wire.succeeds = false;
  { OledDisplay i2c(c); i2c.begin(); i2c.showBooting(); }
  assert(panel.constructors == 0 && panel.frames == 0);
  assert(Serial.output.find("I2C_INIT_FAILED") != std::string::npos);
  // The open menu list replaces the screen, scrolled to the cursor.
  resetTrace();
  {
    OledDisplay d(fixture());
    d.begin();
    SigilMenu m(MenuLayout::List);
    MenuStateFields f;
    for (SigilAction a : {SigilAction::Pass, SigilAction::Pause, SigilAction::Resume,
                          SigilAction::ClaimWin, SigilAction::LinkPhone}) f.actions |= sigilActionBit(a);
    f.defaultAction = static_cast<uint8_t>(SigilAction::Pass);
    m.applyMenuState(encodeMenuState(f), 0);
    d.setMenuView(m.view());
    d.showState(0, DisplayMode::Running, 1, 0, 1, DISPLAY_FLAG_ACTIVE);
    assert(has("YOUR TURN") && !has("MENU"));  // Closed: the normal screen.
    m.keyDown(Key::Up, 0);  // Select would pass; the other keys open the list.
    d.setMenuView(m.view());
    d.showState(0, DisplayMode::Running, 1, 0, 1, DISPLAY_FLAG_ACTIVE);
    assert(highlighted("MENU") && highlighted("1/5") && !has("YOUR TURN"));
    assert(highlighted("Pass turn") && has("Claim win (hold)") && !has("Link phone"));
    for (uint32_t t = 1; t <= 4; ++t) m.keyDown(Key::Down, t);
    d.setMenuView(m.view());
    GameDisplayPacket g{}; g.sigilId = 0; g.state = encodeDisplayState(DisplayMode::Running, 1, 0, 1, DISPLAY_FLAG_ACTIVE);
    d.showGame(g);
    assert(highlighted("Link phone") && highlighted("5/5") && !has("Pass turn") && has("Pause"));
    m.setHoldTimes(2000, 5000);
    m.keyDown(Key::Up, 10); m.keyDown(Key::Select, 20);
    d.setMenuView(m.view()); d.showReady(0);
    assert(highlighted("HOLD: Claim win"));
  }
  std::cout << "OLED configuration, failure, lifecycle, shared-seat and numeric-bound scenarios passed\n";
}
