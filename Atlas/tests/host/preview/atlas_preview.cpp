// Renders the S3 Atlas screen (atlas_screen_lvgl.cpp) to PNG-ready frames on
// the host, from hand-built AtlasScreen fixtures, so layouts can be reviewed
// without the board. Output: build/preview/<name>.ppm (render-atlas-lvgl.sh
// converts them to PNG when Pillow is available).

#include <lvgl.h>
#include <math.h>
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
  const int w = lv_area_get_width(area), stride = lv_display_get_horizontal_resolution(disp);
  for (int y = area->y1; y <= area->y2; ++y)
    for (int x = area->x1; x <= area->x2; ++x) frame[y * stride + x] = src[(y - area->y1) * w + (x - area->x1)];
  lv_display_flush_ready(disp);
}

void save(const std::string &name) {
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(nullptr);
  const std::string path = "build/preview/" + name + ".ppm";
  FILE *f = fopen(path.c_str(), "wb");
  fprintf(f, "P6\n%d %d\n255\n", static_cast<int>(lv_display_get_horizontal_resolution(nullptr)),
          static_cast<int>(lv_display_get_vertical_resolution(nullptr)));
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
         std::vector<int> weights = {}, bool confirm = false) {
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
    b.x = x; b.y = y; b.w = w; b.h = BUTTON_ROW_H; b.confirm = confirm;
    x += w + margin;
  }
}
void column(AtlasScreen &s, std::vector<std::pair<TouchAction, const char *>> items) {
  for (size_t i = 0; i < items.size(); ++i) row(s, SCREEN_BODY_Y + i * (BUTTON_ROW_H + 8), {items[i]});
}
// touch_controls.cpp's addList: touching rows, at most LIST_ROW_H tall.
void list(AtlasScreen &s, std::vector<std::pair<TouchAction, const char *>> items, int16_t bottom = BUTTON_ROW_Y - 8) {
  int16_t h = (bottom - SCREEN_BODY_Y) / static_cast<int16_t>(items.size());
  if (h > LIST_ROW_H) h = LIST_ROW_H;
  for (size_t i = 0; i < items.size(); ++i) {
    row(s, SCREEN_BODY_Y + i * h, {items[i]});
    s.buttons[s.buttonCount - 1].h = h;
  }
}

AtlasScreen base(const char *badge, const char *title, const char *detail) {
  AtlasScreen s;
  snprintf(s.badge, sizeof(s.badge), "%s", badge);
  snprintf(s.title, sizeof(s.title), "%s", title);
  snprintf(s.detail, sizeof(s.detail), "%s", detail);
  s.sigilsOnline = 2;
  s.batteryPercent = 72; s.hotspotStations = 1; s.homeWifiOn = true; s.homeWifiLinked = true;
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

void renderShell(AtlasScreen &s, const ScreenStyle &style, bool keepBanner = false) {
  if (s.kind != ScreenKind::Code && s.kind != ScreenKind::PairCode && s.kind != ScreenKind::Setup && s.kind != ScreenKind::Confirm) {
    const TouchAction actions[] = {TouchAction::OpenHome, TouchAction::OpenGame, TouchAction::OpenSettings};
    const TurnHubUi::Page pages[] = {TurnHubUi::Page::Home, TurnHubUi::Page::Game, TurnHubUi::Page::Settings};
    const uint8_t selected = s.kind == ScreenKind::Home || s.kind == ScreenKind::ManaTap ? 0 : s.kind == ScreenKind::Status ? 1 : 2;
    for (uint8_t i = 0; i < 3; ++i) {
      auto &b = s.buttons[s.buttonCount++]; b.action = actions[i]; b.label = TurnHubUi::pageTitle(pages[i]);
      b.x = i * 106; b.y = 432; b.w = i == 2 ? 108 : 106; b.h = 48; b.selected = i == selected;
    }
  }
  renderAtlasScreen(s, style);
  if (!keepBanner) lv_obj_clean(lv_layer_top());
}

void renderAll(const ScreenStyle &style, const std::string &suffix) {
  {
    auto s=base("PLAYING","TurnHub","Your table, Game 1");s.kind=ScreenKind::Home;
    const char *names[]={"Ricky","Avery","Sam","Jordan"};
    for(uint8_t i=0;i<4;++i){auto &p=s.players[s.playerCount++];p.number=i+1;snprintf(p.name,sizeof(p.name),"%s",names[i]);p.flags=i==1?CHIP_ACTIVE:i==3?CHIP_OUT:0;}
    s.sigilsOnline=3;s.round=4;
    const char *lines[]={"Avery's turn","Round 4","Commander","40 life"};
    for(auto line:lines) snprintf(s.lines[s.lineCount++],sizeof(s.lines[0]),"%s",line);
    row(s,BUTTON_ROW_Y,{{TouchAction::OpenManaTap,"Play Mana Tap (best 24)"}});
    renderShell(s,style);save("home"+suffix);
  }
  {
    // Mana Tap: ready, mid-run (Pause beside the title), and paused by a pass.
    TurnHub::ManaTap mana; mana.open(TurnHub::MANA_ATLAS_FIELD,1,24,7);
    auto ready=base("PLAY","Mana Tap","Tap orbs marked A, let the rest float");ready.kind=ScreenKind::ManaTap;ready.mana=mana;
    row(ready,BUTTON_ROW_Y,{{TouchAction::ManaAction,"Start"},{TouchAction::ManaQuit,"Quit"}},{3,2});
    renderShell(ready,style);save("mana-ready"+suffix);
    uint32_t ms=0; mana.start(ms);
    for(int n=0;n<240;++n) {
      ms+=20; mana.update(ms);
      for(uint8_t i=0;i<TurnHub::MANA_ORBS && n%37==0 && n<150;++i)
        if(mana.orb(i).live && mana.orb(i).target) mana.tap(mana.orb(i).x,mana.orb(i).y());
    }
    auto play=base("PLAY","Mana Tap","Tap the orbs marked A");play.kind=ScreenKind::ManaTap;play.mana=mana;
    auto &pause=play.buttons[play.buttonCount++];pause.action=TouchAction::ManaAction;pause.label="Pause";
    pause.x=WIDTH-8-112;pause.y=SCREEN_HERO_Y+8;pause.w=112;pause.h=52;
    renderShell(play,style);save("mana-playing"+suffix);
    mana.pause();
    auto paused=base("PLAY","Mana Tap","Paused: the turn passed");paused.kind=ScreenKind::ManaTap;paused.mana=mana;
    paused.manaTurnPaused=true;
    row(paused,BUTTON_ROW_Y,{{TouchAction::ManaAction,"Resume"},{TouchAction::ManaQuit,"Quit"}},{3,2});
    renderShell(paused,style);save("mana-paused"+suffix);
  }
  {
    AtlasScreen s = base("SETUP", "Welcome to TurnHub", "Set up this table from a phone");
    s.kind = ScreenKind::Setup;
    snprintf(s.qr, sizeof(s.qr), "WIFI:T:WPA;S:TurnHub-Atlas;P:TurnHub-Setup;;");
    snprintf(s.qrCaption, sizeof(s.qrCaption), "Scan with your phone camera");
    const char *lines[] = {"Scan to join the setup Wi-Fi.", "Then open TurnHub: Set up Atlas."};
    for (auto *l : lines) snprintf(s.lines[s.lineCount++], 40, "%s", l);
    row(s, BUTTON_ROW_Y, {{TouchAction::Pair, "Pair a Sigil"}, {TouchAction::SkipSetup, "Skip"}, {TouchAction::OpenSettings, "Settings"}}, {3, 2, 2});
    renderShell(s, style); save("setup" + suffix);
  }
  {
    AtlasScreen s = base("SETTINGS", "Settings", "Pair Sigils, share codes, the look");
    s.kind = ScreenKind::Menu;
    list(s, {{TouchAction::OpenSetup, "Setup"}, {TouchAction::Pair, "Pair a Sigil"}, {TouchAction::OpenQr, "QR codes"},
        {TouchAction::OpenThemes, "Theme: Graphite"}, {TouchAction::OpenDevice, "Device"}}, HEIGHT - 48 - 8);
    renderShell(s, style); save("menu" + suffix);
  }
  {
    AtlasScreen s = base("DEVICE", "Atlas device", "Unpair and Factory reset ask first");
    s.kind = ScreenKind::Device;
    list(s, {{TouchAction::UsbDrive, "USB drive: off"}, {TouchAction::SleepAtlas, "Sleep"},
        {TouchAction::UnpairSigils, "Unpair Sigils"}, {TouchAction::FactoryResetAtlas, "Factory reset"}});
    s.buttons[2].confirm = s.buttons[3].confirm = true;
    row(s, BUTTON_ROW_Y, {{TouchAction::CloseScreen, "Back"}});
    renderShell(s, style); save("device" + suffix);
  }
  {
    AtlasScreen s = base("THEME", "Graphite", "Choose the look for this Atlas");
    s.kind = ScreenKind::Themes;
    list(s, {{TouchAction::ThemeGraphite, "Graphite"}, {TouchAction::ThemeDaylight, "Daylight"}, {TouchAction::ThemeBrass, "Brass"},
        {TouchAction::ThemeContrast, "High contrast"}, {TouchAction::InvertDisplay, "Invert colours: off"}});
    s.buttons[0].selected = true;
    row(s, BUTTON_ROW_Y, {{TouchAction::CloseScreen, "Back"}});
    renderShell(s, style); save("themes" + suffix);
  }
  {
    AtlasScreen s = base("CONFIRM", "Unpair every Sigil?", "Each one must pair again to play.");
    s.kind = ScreenKind::Confirm;
    row(s, CONFIRM_BUTTON_Y, {{TouchAction::ConfirmNo, "Cancel"}, {TouchAction::ConfirmYes, "Unpair all"}});
    const int16_t w = (WIDTH - 32 - 24) / 2;
    for (uint8_t i = 0; i < 2; ++i) { s.buttons[i].x = 24 + i * (w + 8); s.buttons[i].w = w; }
    renderShell(s, style); save("confirm" + suffix);
  }
  {
    auto s = base("READY", "No game yet", "Connect a phone to show a game");
    s.kind = ScreenKind::Status;
    renderShell(s, style); save("no-hub" + suffix);
    players(s, 4, true); s.round = 3;
    snprintf(s.badge, sizeof(s.badge), "PLAYING");
    snprintf(s.title, sizeof(s.title), "Sam's turn");
    snprintf(s.detail, sizeof(s.detail), "Hosted by Owner");
    row(s, BUTTON_ROW_Y, {{TouchAction::PassTurn, "Pass"}, {TouchAction::Pause, "Pause"}}, {3, 2});
    renderShell(s, style); save("hosted-game" + suffix);
  }
  {
    auto s = base("SETUP", "Set up this Atlas", "For Owner (60 s)");
    s.kind = ScreenKind::Code;
    snprintf(s.code, sizeof(s.code), "001 234");
    snprintf(s.qr, sizeof(s.qr), "001234");
    snprintf(s.qrCaption, sizeof(s.qrCaption), "Scan or enter on that phone");
    row(s, BUTTON_ROW_Y, {{TouchAction::CancelCode, "Cancel"}});
    renderShell(s, style); save("verification-code" + suffix);
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
