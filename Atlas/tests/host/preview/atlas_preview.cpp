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

void renderShell(AtlasScreen &s,const ScreenStyle &style) {
  if(s.kind!=ScreenKind::Code && s.kind!=ScreenKind::PairCode && s.kind!=ScreenKind::Name && s.kind!=ScreenKind::Pin && s.kind!=ScreenKind::Setup) {
    const TouchAction actions[]={TouchAction::OpenHome,TouchAction::OpenGame,TouchAction::OpenOverview,TouchAction::OpenSettings};
    const uint8_t selected=s.kind==ScreenKind::Home?0:s.kind==ScreenKind::Overview?2:s.kind==ScreenKind::Menu?3:1;
    for(uint8_t i=0;i<4;++i) { auto &b=s.buttons[s.buttonCount++];b.action=actions[i];b.label=TurnHubUi::pageTitle(static_cast<TurnHubUi::Page>(i));b.x=i*80;b.y=432;b.w=80;b.h=48;b.selected=i==selected; }
  }
  renderAtlasScreen(s,style);
}

void renderAll(const ScreenStyle &style, const std::string &suffix) {
  {
    auto s=base("ATLAS","TurnHub","Your table controller");s.kind=ScreenKind::Home;
    const TouchAction targets[]={TouchAction::OpenGame,TouchAction::OpenOverview,TouchAction::OpenSettings};
    const char *labels[]={"Game","Table","Settings"};
    for(uint8_t i=0;i<3;++i){ auto &b=s.buttons[s.buttonCount++];b.action=targets[i];b.label=labels[i];b.x=8;b.y=112+i*72;b.w=304;b.h=64;b.arg=1; }

    const char *lines[]={"RUNNING / 4 players","2 Sigils connected","Game: play and manage the match","Table: see everyone, read only","Settings: pair, connect, appearance"};
    for(auto line:lines) snprintf(s.lines[s.lineCount++],sizeof(s.lines[0]),"%s",line);
    renderShell(s,style);save("home"+suffix);
  }
  {
    auto s=base("PLAYING","Table","Read only / players 1-8");s.kind=ScreenKind::Overview;players(s,4,true);
    renderShell(s,style);save("table-overview"+suffix);
  }

  {
    AtlasScreen s = base("SETUP", "Welcome to TurnHub", "Set up this table from a phone");
    s.kind = ScreenKind::Setup;
    const char *lines[] = {"1. Open the TurnHub app, tap Connect;", "   it joins this table's Wi-Fi itself.",
        "2. No app? Join Wi-Fi TurnHub-Atlas,", "   password TurnHub-Setup,", "   then open 192.168.4.1"};
    for (auto *l : lines) snprintf(s.lines[s.lineCount++], 40, "%s", l);
    row(s, BUTTON_ROW_Y, {{TouchAction::Pair, "Pair a Sigil"}, {TouchAction::SkipSetup, "Skip"}, {TouchAction::OpenMenu, "Menu"}}, {3, 2, 2});
    renderShell(s, style); save("setup" + suffix);
  }
  {
    AtlasScreen s = base("LOBBY", "Lobby", "0 players, 2 Sigils");
    const char *lines[] = {"Tap Add player to seat someone,", "or join from the app or a Sigil.", "Menu: pair, QR codes, info."};
    for (auto *l : lines) snprintf(s.lines[s.lineCount++], 40, "%s", l);
    row(s, BUTTON_ROW_Y, {{TouchAction::OpenSeat, "Add player"}, {TouchAction::OpenMenu, "Menu"}}, {3, 2});
    renderShell(s, style); save("lobby-empty" + suffix);
  }
  {
    AtlasScreen s = base("LOBBY", "Lobby", "4 players, 2 Sigils");
    players(s, 4, false);
    row(s, BUTTON_ROW_Y, {{TouchAction::OpenSeat, "Add player"}, {TouchAction::StartGame, "Start"}, {TouchAction::OpenMenu, "Menu"}}, {3, 3, 2});
    renderShell(s, style); save("lobby-4" + suffix);
  }
  {
    AtlasScreen s = base("MENU", "Table menu", "Pair Sigils, share codes, table info");
    s.kind = ScreenKind::Menu;
    column(s, {{TouchAction::OpenSetup, "Setup"}, {TouchAction::Pair, "Pair a Sigil"}, {TouchAction::OpenQr, "QR codes"}});
    row(s, BUTTON_ROW_Y, {{TouchAction::OpenInfo, "Info"}, {TouchAction::OpenDevice, "Device"}, {TouchAction::CloseScreen, "Back"}});
    renderShell(s, style); save("menu" + suffix);
  }
  {
    AtlasScreen s = base("ADD PLAYER", "Who's playing?", "Tap a name to seat them (1 of 2)");
    s.kind = ScreenKind::Seat;
    column(s, {{TouchAction::SeatProfile, "Jordan"}, {TouchAction::SeatProfile, "Casey (PIN)"},
        {TouchAction::SeatProfile, "Morgan"}, {TouchAction::SeatProfile, "Riley"}});
    for (uint8_t i = 0; i < 4; ++i) s.buttons[i].arg = i;
    row(s, BUTTON_ROW_Y, {{TouchAction::NewPlayer, "New player"}, {TouchAction::SeatMore, "More"}, {TouchAction::CloseScreen, "Back"}}, {3, 2, 2});
    renderShell(s, style); save("seat" + suffix);
  }
  {
    // touch_controls.cpp's layoutName: six 47 x 56 keys a row, Space two wide.
    AtlasScreen s = base("ADD PLAYER", "Jordan", "Type their name. No PIN needed");
    s.kind = ScreenKind::Name;
    static const char *keys[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
        "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "-", "'", "Space"};
    const int16_t kw = (WIDTH - 16 - 20) / 6, kh = (BUTTON_ROW_Y - 8 - SCREEN_BODY_Y - 16) / 5;
    for (uint8_t i = 0; i < 29; ++i) {
      TouchButton &b = s.buttons[s.buttonCount++];
      b.action = TouchAction::NameKey; b.label = keys[i]; b.arg = i;
      b.x = 8 + (i % 6) * (kw + 4); b.y = SCREEN_BODY_Y + (i / 6) * (kh + 4); b.w = i == 28 ? 2 * kw + 4 : kw; b.h = kh;
    }
    s.pressed = TouchAction::NameKey; s.pressedArg = 13;
    row(s, BUTTON_ROW_Y, {{TouchAction::CloseScreen, "Cancel"}, {TouchAction::NameBack, "Delete"}, {TouchAction::NameDone, "Add"}});
    renderShell(s, style); save("name" + suffix);
  }
  {
    AtlasScreen s = base("ADD PLAYER", "Casey's PIN", "* * *");
    s.kind = ScreenKind::Pin;
    static const char *keys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "Del", "0", "OK"};
    const int16_t kw = (WIDTH - 16 - 16) / 3, kh = (BUTTON_ROW_Y - 8 - SCREEN_BODY_Y - 24) / 4;
    for (uint8_t i = 0; i < 12; ++i) {
      TouchButton &b = s.buttons[s.buttonCount++];
      b.action = i == 9 ? TouchAction::PinBack : i == 11 ? TouchAction::PinDone : TouchAction::PinKey; b.label = keys[i];
      b.x = 8 + (i % 3) * (kw + 8); b.y = SCREEN_BODY_Y + (i / 3) * (kh + 8); b.w = kw; b.h = kh;
    }
    row(s, BUTTON_ROW_Y, {{TouchAction::CloseScreen, "Cancel"}});
    renderShell(s, style); save("pin" + suffix);
  }
  for (int n : {3, 4, 6, 8}) {
    AtlasScreen s = base("PLAYING", "Sam's turn", "Hold Pass on your Sigil to pass");
    s.round = 3; snprintf(s.gameClock, sizeof(s.gameClock), "42:10"); snprintf(s.clock, sizeof(s.clock), "1:23");
    s.timerPermille = 600;
    players(s, n, true);
    row(s, BUTTON_ROW_Y, {{TouchAction::PassTurn, "Hold to pass"}, {TouchAction::Pause, "Pause"}, {TouchAction::OpenTable, "Table"}}, {4, 2, 2});
    s.buttons[0].holdMs = TOUCH_PASS_HOLD_MS;
    renderShell(s, style); save("game-" + std::to_string(n) + suffix);
  }
  {
    AtlasScreen s = base("QR", "Join this table", "Scan with a phone camera");
    s.kind = ScreenKind::Qr;
    snprintf(s.qr, sizeof(s.qr), "WIFI:T:WPA;S:TurnHub-Atlas;P:TurnHub-Setup;;");
    snprintf(s.qrCaption, sizeof(s.qrCaption), "Wi-Fi TurnHub-Atlas");
    row(s, BUTTON_ROW_Y, {{TouchAction::QrWifi, "Wi-Fi"}, {TouchAction::QrPortal, "Portal"}, {TouchAction::QrSignIn, "Sign in"}, {TouchAction::CloseScreen, "Back"}});
    s.buttons[0].selected = true;
    renderShell(s, style); save("qr" + suffix);
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
