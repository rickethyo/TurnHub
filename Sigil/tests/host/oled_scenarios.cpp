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
  // Synthetic host-only values, NOT a proposed wiring diagram or hardware BOM.
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
  rejected(OLED_CONFIG);
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
  for (int pin : {-1, 1, 3, 6, 7, 8, 9, 10, 11, 13, 14, 19, 20, 24, 25, 26,
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
  d.showBooting(); assert(has("Booting") && has("TurnHub"));
  d.showUnpaired(); assert(has("Unpaired") && has("Press Pair on both"));
  d.showReady(7); assert(has("Sigil 8") && has("Ready for game"));
  assert(d.setSeatName(1, "ABCDEFGHIJKLmore"));
  assert(!d.setSeatName(1, "ABCDEFGHIJKL"));
  assert(!d.setSeatName(3, "ignored"));
  assert(d.setSeatName(2, "Second"));
  d.showState(7, DisplayMode::Lobby, 1, 2, 255, DISPLAY_FLAG_HOST);
  assert(has("A: ABCDEFGHIJKL") && has("B: Second") && has("SHARED SIGIL"));
  assert(has("Lobby S8 T255 H"));
  d.showState(0, DisplayMode::Starting, 2, 1, 1, DISPLAY_FLAG_STARTER);
  assert(has("GO FIRST: B") && panel.inversions == 1);
  d.showState(0, DisplayMode::Running, 1, 2, 1, DISPLAY_FLAG_ACTIVE);
  assert(has("YOUR TURN: A"));
  d.showState(0, DisplayMode::Paused, 2, 1, 1, DISPLAY_FLAG_ATTENTION);
  assert(has("ACTION NEEDED: B"));
  d.showState(0, DisplayMode::GameOver, 1, 2, 1, DISPLAY_FLAG_WINNER);
  assert(has("WINNER!: A"));
  d.showState(0, DisplayMode::Paused, 1, 0, 1, 0);
  assert(has("GAME PAUSED"));
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
        assert(has(active ? "YOUR TURN" : "WAITING FOR TURN"));
        assert(panel.inversions == (active ? 1 : 0));
        assert(has(shared ? "B: ABCDEFGHIJKL" : "ABCDEFGHIJKL"));
        char life[32]; std::snprintf(life, sizeof(life), "LIFE %ld", long(value));
        assert(has(life));
        if (shared) {
          assert(has("A: MNOPQRSTUVWX"));
          std::snprintf(life, sizeof(life), "LIFE %ld", long(-value));
          assert(has(life));
        }
      }
    }
  }
  s.state = encodeDisplayState(DisplayMode::Running, 1, 2, 1, DISPLAY_FLAG_ACTIVE);
  s.commander = 1; d.showGame(s);
  assert(has("A: ABCDEFGHIJKL") && has("B: MNOPQRSTUVWX") && has("CMD: see companion"));
  d.showState(0, DisplayMode::Paused, 1, 0, 1, 0);
  assert(!has("LIFE 1000000")); // State-only packets must not retain stale life.

  c = fixture(); c.bus = OledBus::I2c; c.sda = 21; c.scl = 22;
  c.reset = -1; c.rotation = 2; c.i2cClockHz = 100000;
  rejected(c); // Address remains explicitly unset.
  c.i2cAddress = 0x3c;
  resetTrace();
  { OledDisplay i2c(c); i2c.begin(); i2c.showBooting(); }
  assert(!panel.spi && Wire.calls == 1 && Wire.sda == 21 && Wire.scl == 22);
  assert(panel.address == 0x3c && panel.rotation == 2 && !panel.resetRequested);
  assert(panel.clockDuring == 100000 && panel.clockAfter == 100000 && has("Booting"));
  resetTrace(); Wire.succeeds = false;
  { OledDisplay i2c(c); i2c.begin(); i2c.showBooting(); }
  assert(panel.constructors == 0 && panel.frames == 0);
  assert(Serial.output.find("I2C_INIT_FAILED") != std::string::npos);
  std::cout << "OLED configuration, failure, lifecycle, shared-seat and numeric-bound scenarios passed\n";
}
