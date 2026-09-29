// Host preview of the Sigil screens: runs the firmware's own display class
// (epaper_display.cpp, or oled_display.cpp with TURNHUB_DISPLAY_OLED) over
// Adafruit GFX's 1-bit canvas (preview_stubs/) and saves one PNG per scene.
// Presentation only; the host suites cover behavior. Build and run with
// render-sigil-screens.sh.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "protocol.h"
#include "sigil_display.h"
#include "sigil_menu.h"
#ifdef TURNHUB_DISPLAY_OLED
#include "oled_display.h"
#else
#include <SPI.h>
#include "epaper_display.h"
#endif

#ifdef TURNHUB_DISPLAY_OLED
TwoWire Wire;
#else
SPIClass SPI;
#endif
SerialStub Serial;
uint32_t previewNowMs = 0;

namespace {

using namespace TurnHubProtocol;
using namespace TurnHubSigil;

// Uncompressed PNG ("stored" deflate blocks): no zlib needed.
uint32_t crc32(const std::vector<uint8_t> &data, size_t from) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = from; i < data.size(); ++i) {
    crc ^= data[i];
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
void be32(std::vector<uint8_t> &out, uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(v >> s));
}
void chunk(std::vector<uint8_t> &png, const char *type, const std::vector<uint8_t> &body) {
  be32(png, static_cast<uint32_t>(body.size()));
  const size_t start = png.size();
  png.insert(png.end(), type, type + 4);
  png.insert(png.end(), body.begin(), body.end());
  be32(png, crc32(png, start));
}
bool writePng(const std::string &path, int w, int h, const std::vector<uint8_t> &gray) {
  std::vector<uint8_t> raw;
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), gray.begin() + y * w, gray.begin() + (y + 1) * w);
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
  for (size_t i = 0; i < raw.size(); i += 65535) {
    const size_t n = std::min<size_t>(65535, raw.size() - i);
    z.push_back(i + n == raw.size() ? 1 : 0);
    z.push_back(static_cast<uint8_t>(n)); z.push_back(static_cast<uint8_t>(n >> 8));
    z.push_back(static_cast<uint8_t>(~n)); z.push_back(static_cast<uint8_t>(~n >> 8));
    z.insert(z.end(), raw.begin() + i, raw.begin() + i + n);
  }
  be32(z, (b << 16) | a);
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> ihdr;
  be32(ihdr, w); be32(ihdr, h);
  ihdr.insert(ihdr.end(), {8, 0, 0, 0, 0});
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", z);
  chunk(png, "IEND", {});
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  const bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
  fclose(f);
  return ok;
}

// The canvas the display drew into: e-ink black on white, OLED lit on black.
bool save(GFXcanvas1 &canvas, const std::string &path) {
  const int w = canvas.width(), h = canvas.height();
  std::vector<uint8_t> gray(w * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) gray[y * w + x] = canvas.getPixel(x, y) ? 255 : 0;
  return writePng(path, w, h, gray);
}

void name(char *out, const char *value) { snprintf(out, DISPLAY_NAME_MAX_LENGTH + 1, "%s", value); }

GameDisplayPacket game(bool active, int32_t life, uint8_t primary = 3, uint8_t secondary = 0) {
  GameDisplayPacket p;
  memset(&p, 0, sizeof(p));
  p.version = VERSION;
  p.type = PacketType::GameDisplay;
  p.sigilId = 2;
  p.state = encodeDisplayState(DisplayMode::Running, primary, secondary, 7, active ? DISPLAY_FLAG_ACTIVE : 0);
  p.primary.life = life;
  name(p.primary.name, "Rowan");
  return p;
}

MenuView compass(bool active) {
  MenuView m;
  m.active = true;
  if (active) m.compass[static_cast<uint8_t>(Key::Select)] = static_cast<uint8_t>(SigilAction::Pass);
  m.compass[static_cast<uint8_t>(Key::Up)] = static_cast<uint8_t>(SigilAction::Pause);
  m.compass[static_cast<uint8_t>(Key::Down)] = static_cast<uint8_t>(SigilAction::BeginElimination);
  m.life = true;
  return m;
}

}  // namespace

int main(int argc, char **argv) {
  const std::string dir = argc > 1 ? argv[1] : "screens";
#ifdef TURNHUB_DISPLAY_OLED
  OledDisplay display;
  const std::string prefix = dir + "/oled-";
#else
  EpaperDisplay display;
  const std::string prefix = dir + "/eink-";
#endif
  display.begin();
  GFXcanvas1 &canvas = static_cast<GFXcanvas1 &>(display.previewGfx());
  int failures = 0;
  auto shot = [&](const char *scene) {
    if (!save(canvas, prefix + scene + ".png")) {
      printf("FAIL %s\n", scene);
      ++failures;
    }
  };
  LifeOverlay overlay;
  overlay.startingLife = 40;
  display.setSeatName(1, "Rowan");
  display.setSeatName(2, "Mae");

  display.showBooting(); shot("booting");
  display.showUnpaired(); shot("unpaired");
  display.setMenuView(MenuView());
  display.showReady(2); shot("ready");
  display.showAtlasLost(2); shot("atlas-lost");

  display.setMenuView(compass(true));
  display.setLifeOverlay(overlay);
  display.showGame(game(true, 32)); shot("your-turn");

  display.setMenuView(compass(false));
  display.showGame(game(false, 27)); shot("waiting");

  GameDisplayPacket shared = game(true, 18, 3, 4);
  name(shared.secondary.name, "Mae");
  shared.secondary.life = 20;
  overlay.startingLife = 20;
  display.setLifeOverlay(overlay);
  display.setMenuView(compass(true));
  display.showGame(shared); shot("shared-seat");

  GameDisplayPacket cmd = game(true, 29);
  cmd.commander = 1;
  cmd.sourceCount = 2;
  cmd.sources[0].player = 1;
  name(cmd.sources[0].name, "Priya");
  cmd.sources[0].damage[0] = 7;
  cmd.sources[1].player = 4;
  name(cmd.sources[1].name, "Bartholomew");
  cmd.sources[1].damage[0] = 12;
  overlay.startingLife = 40;
  display.setLifeOverlay(overlay);
  display.showGame(cmd); shot("commander");

  LifeOverlay asking = overlay;
  asking.request.target = 3;
  asking.request.requester = 2;
  asking.request.delta = -3;
  display.setLifeOverlay(asking);
  display.showGame(game(false, 27)); shot("life-request");

  LifeOverlay passing = overlay;
  passing.passPending = true;
  display.setLifeOverlay(passing);
  display.showGame(game(true, 32)); shot("passing");

  display.setLifeOverlay(overlay);
  MenuView lobbyMenu;
  lobbyMenu.active = true;
  lobbyMenu.compass[static_cast<uint8_t>(Key::Select)] = static_cast<uint8_t>(SigilAction::StartGame);
  lobbyMenu.compass[static_cast<uint8_t>(Key::Up)] = static_cast<uint8_t>(SigilAction::CycleStarter);
  lobbyMenu.compass[static_cast<uint8_t>(Key::Left)] = static_cast<uint8_t>(SigilAction::Leave);
  display.setMenuView(lobbyMenu);
  display.showState(2, DisplayMode::Lobby, 3, 0, 0, DISPLAY_FLAG_STARTER); shot("lobby");
  display.showState(2, DisplayMode::Lobby, 3, 4, 0, 0); shot("lobby-shared");
  display.setMenuView(MenuView());
  display.showState(2, DisplayMode::Starting, 3, 0, 0, DISPLAY_FLAG_STARTER); shot("starting");
  MenuView winMenu;
  winMenu.active = true;
  winMenu.compass[static_cast<uint8_t>(Key::Select)] = static_cast<uint8_t>(SigilAction::ConfirmWin);
  winMenu.compass[static_cast<uint8_t>(Key::Left)] = static_cast<uint8_t>(SigilAction::DenyWin);
  display.setMenuView(winMenu);
  display.showState(2, DisplayMode::Paused, 3, 0, 9, DISPLAY_FLAG_ATTENTION); shot("paused-attention");
  display.setMenuView(MenuView());
  display.showState(2, DisplayMode::GameOver, 3, 0, 12, DISPLAY_FLAG_WINNER); shot("game-over-winner");

  ProfilePickerPacket picker;
  memset(&picker, 0, sizeof(picker));
  picker.mode = PickerMode::List;
  picker.pageCount = 2;
  picker.itemCount = 3;
  name(picker.items[0].name, "Rowan");
  name(picker.items[1].name, "Guest");
  picker.items[1].flags = PICKER_ITEM_GUEST;
  name(picker.items[2].name, "Isolde");
  picker.items[2].flags = PICKER_ITEM_LOCKED;
  display.showPicker(picker, 0); shot("picker");
  picker.mode = PickerMode::Confirm;
  picker.itemCount = 1;
  display.showPicker(picker, 0); shot("picker-confirm");

  printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
