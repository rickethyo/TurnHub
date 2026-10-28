// Renders the S3 Atlas screen (atlas_screen_lvgl.cpp) to PNG-ready frames on
// the host, from hand-built AtlasScreen fixtures, so layouts can be reviewed
// without the board. Output: build/preview/<name>.ppm (render-atlas-lvgl.sh
// converts them to PNG when Pillow is available).

#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "atlas_screen_lvgl.h"

using namespace TurnHubAtlas;

namespace {

constexpr int16_t WIDTH = ATLAS_SCREEN_WIDTH, HEIGHT = ATLAS_SCREEN_HEIGHT;
std::vector<uint16_t> frame(WIDTH * HEIGHT);
uint32_t nowMs = 0;

void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels) {
  const auto *src = reinterpret_cast<const uint16_t *>(pixels);
  const int w = lv_area_get_width(area);
  for (int y = area->y1; y <= area->y2; ++y)
    for (int x = area->x1; x <= area->x2; ++x) frame[y * WIDTH + x] = src[(y - area->y1) * w + (x - area->x1)];
  lv_display_flush_ready(disp);
}

void save(const std::string &name) {
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(nullptr);
  const std::string path = "build/preview/" + name + ".ppm";
  FILE *f = fopen(path.c_str(), "wb");
  fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
  for (uint16_t c : frame) {
    const uint8_t rgb[3] = {static_cast<uint8_t>((c >> 11 & 31) * 255 / 31),
        static_cast<uint8_t>((c >> 5 & 63) * 255 / 63), static_cast<uint8_t>((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  printf("%s\n", path.c_str());
}

// Buttons laid as touch_controls.cpp's addRow/addColumn do.
void row(AtlasScreen &s, int16_t y, std::vector<std::pair<TouchAction, const char *>> items,
         std::vector<int> weights = {}, uint32_t holdMs = 0) {
  const int16_t margin = 8, full = WIDTH - 16;
  int total = 0;
  if (weights.empty()) weights.assign(items.size(), 1);
  for (int w : weights) total += w;
  const int16_t usable = full - (items.size() - 1) * margin;
  int16_t x = margin;
  for (size_t i = 0; i < items.size(); ++i) {
    TouchButton &b = s.buttons[s.buttonCount++];
    const int16_t w = i + 1 == items.size() ? margin + full - x : usable * weights[i] / total;
    b.action = items[i].first; b.label = items[i].second;
    b.x = x; b.y = y; b.w = w; b.h = BUTTON_ROW_H; b.holdMs = holdMs;
    x += w + margin;
  }
}
void column(AtlasScreen &s, std::vector<std::pair<TouchAction, const char *>> items) {
  for (size_t i = 0; i < items.size(); ++i) row(s, SCREEN_BODY_Y + i * (BUTTON_ROW_H + 8), {items[i]});
}

AtlasScreen base(const char *badge, const char *title, const char *detail) {
  AtlasScreen s;
  snprintf(s.badge, sizeof(s.badge), "%s", badge);
  snprintf(s.title, sizeof(s.title), "%s", title);
  snprintf(s.detail, sizeof(s.detail), "%s", detail);
  s.sigilsOnline = 2;
  return s;
}

void players(AtlasScreen &s, int count, bool game) {
  static const char *names[] = {"Ricky", "Sam", "Alex", "Jordan", "Casey", "Morgan", "Riley", "Quinn"};
  for (int i = 0; i < count; ++i) {
    ScreenPlayer &p = s.players[s.playerCount++];
    p.number = i + 1;
    snprintf(p.name, sizeof(p.name), "%s", names[i]);
    p.life = game ? 40 - i * 3 : 0;
    if (game && i == 1) p.flags |= CHIP_ACTIVE;
    if (game && i == count - 1 && count > 2) p.flags |= CHIP_OUT, p.life = 0;
    if (!game && i == 0) p.flags |= CHIP_STARTER;
    if (game) snprintf(p.turnTime, sizeof(p.turnTime), "%d:%02d", 3 + i, 10 * i % 60);
  }
  s.showLife = game;
}

void renderAll(const ScreenStyle &style, const std::string &suffix) {
  {
    AtlasScreen s = base("SETUP", "Welcome to TurnHub", "Set up this table from a phone");
    s.kind = ScreenKind::Setup;
    const char *lines[] = {"1. Open the TurnHub app, tap Connect;", "   it joins this table's Wi-Fi itself.",
        "2. No app? Join Wi-Fi TurnHub-Atlas,", "   password TurnHub-Setup,", "   then open 192.168.4.1"};
    for (auto *l : lines) snprintf(s.lines[s.lineCount++], 40, "%s", l);
    row(s, BUTTON_ROW_Y, {{TouchAction::Pair, "Pair a Sigil"}, {TouchAction::SkipSetup, "Skip"}, {TouchAction::OpenMenu, "Menu"}}, {3, 2, 2});
    renderAtlasScreen(s, style); save("setup" + suffix);
  }
  {
    AtlasScreen s = base("LOBBY", "Lobby", "0 players, 2 Sigils");
    const char *lines[] = {"Join from a Sigil's menu,", "or on the table portal.", "Menu: pair, QR codes, info."};
    for (auto *l : lines) snprintf(s.lines[s.lineCount++], 40, "%s", l);
    row(s, BUTTON_ROW_Y, {{TouchAction::OpenMenu, "Menu"}});
    renderAtlasScreen(s, style); save("lobby-empty" + suffix);
  }
  {
    AtlasScreen s = base("LOBBY", "Lobby", "4 players, 2 Sigils");
    players(s, 4, false);
    row(s, BUTTON_ROW_Y, {{TouchAction::StartGame, "Start"}, {TouchAction::ClearLobby, "Clear"}, {TouchAction::OpenMenu, "Menu"}}, {3, 2, 2});
    s.buttons[1].holdMs = 2000;
    renderAtlasScreen(s, style); save("lobby-4" + suffix);
  }
  {
    AtlasScreen s = base("MENU", "Table menu", "Pair Sigils, share codes, table info");
    s.kind = ScreenKind::Menu;
    column(s, {{TouchAction::OpenSetup, "Setup"}, {TouchAction::Pair, "Pair a Sigil"}, {TouchAction::OpenQr, "QR codes"}});
    row(s, BUTTON_ROW_Y, {{TouchAction::OpenInfo, "Info"}, {TouchAction::OpenDevice, "Device"}, {TouchAction::CloseScreen, "Back"}});
    renderAtlasScreen(s, style); save("menu" + suffix);
  }
  for (int n : {3, 4, 6, 8}) {
    AtlasScreen s = base("PLAYING", "Sam's turn", "Hold Pass on your Sigil to pass");
    s.round = 3; snprintf(s.gameClock, sizeof(s.gameClock), "42:10"); snprintf(s.clock, sizeof(s.clock), "1:23");
    s.timerPermille = 600;
    players(s, n, true);
    row(s, BUTTON_ROW_Y, {{TouchAction::Pause, "Pause"}, {TouchAction::OpenTable, "Table"}}, {3, 2});
    renderAtlasScreen(s, style); save("game-" + std::to_string(n) + suffix);
  }
  {
    AtlasScreen s = base("QR", "Join this table", "Scan with a phone camera");
    s.kind = ScreenKind::Qr;
    snprintf(s.qr, sizeof(s.qr), "WIFI:T:WPA;S:TurnHub-Atlas;P:TurnHub-Setup;;");
    snprintf(s.qrCaption, sizeof(s.qrCaption), "Wi-Fi TurnHub-Atlas");
    row(s, BUTTON_ROW_Y, {{TouchAction::QrWifi, "Wi-Fi"}, {TouchAction::QrPortal, "Portal"}, {TouchAction::QrSignIn, "Sign in"}, {TouchAction::CloseScreen, "Back"}});
    s.buttons[0].selected = true;
    renderAtlasScreen(s, style); save("qr" + suffix);
  }
}

}  // namespace

int main() {
  lv_init();
  lv_tick_set_cb([]() { return nowMs; });
  lv_display_t *disp = lv_display_create(WIDTH, HEIGHT);
  static std::vector<uint16_t> buffer(WIDTH * HEIGHT);
  lv_display_set_buffers(disp, buffer.data(), nullptr, buffer.size() * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush);
  renderAll({TurnHubTheme::Id::Graphite, false}, "");
  renderAll({TurnHubTheme::Id::Brass, false}, "-brass");
  renderAll({TurnHubTheme::Id::Daylight, false}, "-daylight");
  return 0;
}
