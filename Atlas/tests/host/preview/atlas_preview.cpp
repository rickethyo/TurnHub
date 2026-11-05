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

void renderShell(AtlasScreen &s,const ScreenStyle &style,bool keepBanner=false) {
  if(s.kind!=ScreenKind::Code && s.kind!=ScreenKind::PairCode && s.kind!=ScreenKind::Setup && s.kind!=ScreenKind::Confirm) {
    // As touch_controls.cpp's layoutButtons: Home, Game, Planes (in a Planechase game), Settings.
    const TouchAction actions[]={TouchAction::OpenHome,TouchAction::OpenGame,TouchAction::OpenPlanes,TouchAction::OpenSettings};
    const char *names[]={"Home","Game","Planes","Settings"};
    const uint8_t selected=s.kind==ScreenKind::Home||s.kind==ScreenKind::ManaTap?0:s.kind==ScreenKind::Planes?2:
        s.kind==ScreenKind::Menu||s.kind==ScreenKind::Device||s.kind==ScreenKind::Themes?3:1;
    const uint8_t count=s.planes.on?4:3; const int16_t w=WIDTH/count; uint8_t slot=0;
    for(uint8_t i=0;i<4;++i) {
      if(i==2&&!s.planes.on) continue;
      auto &b=s.buttons[s.buttonCount++];b.action=actions[i];b.label=names[i];b.x=slot*w;b.y=432;b.w=slot+1==count?WIDTH-slot*w:w;b.h=48;b.selected=i==selected;++slot;
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
    // The Game tab with no hub: the steps to start a game.
    AtlasScreen s = base("READY", "No game yet", "2 Sigils online");
    const char *lines[] = {"Open TurnHub on your phone", "Start a game: it shows up here", "Pair Sigils in Settings"};
    for (auto *l : lines) snprintf(s.lines[s.lineCount++], 40, "%s", l);
    renderShell(s, style); save("ready" + suffix);
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
  {
    // Planechase: the Planes screen (digital deck, a phenomenon, the players'
    // own planes), then the planar die rolling over the game.
    auto planes = [&](AtlasScreen &s) {
      s.kind = ScreenKind::Planes;
      row(s, BUTTON_UPPER_ROW_Y, {{TouchAction::PlanarRoll, "Roll: 1 mana"}});
      row(s, BUTTON_ROW_Y, {{TouchAction::PlanarWalk, "Planeswalk"}, {TouchAction::OpenPlaneCard, "Card"}}, {3, 2});
    };
    AtlasScreen s = base("PLANES", "Planechase", "Casey rolled chaos");
    s.planes.on = s.planes.digital = true;
    snprintf(s.planes.plane, sizeof(s.planes.plane), "Sokenzan, Crucible of Defiance");
    s.planes.deckLeft = 31; s.planes.planeswalks = 4; s.planes.chaos = 2; s.planes.counters = 3; s.planes.rollCost = 1;
    planes(s);
    renderShell(s, style); save("planes" + suffix);
    AtlasScreen ph = base("PLANES", "Planechase", "Phenomenon: resolve it first");
    ph.planes = s.planes; ph.planes.phenomenon = true; ph.planes.counters = 0;
    snprintf(ph.planes.plane, sizeof(ph.planes.plane), "Interplanar Tunnel");
    ph.kind = ScreenKind::Planes;
    row(ph, BUTTON_UPPER_ROW_Y, {{TouchAction::PlanarResolve, "Resolve phenomenon"}});
    row(ph, BUTTON_ROW_Y, {{TouchAction::OpenPlaneCard, "Full screen card"}});
    renderShell(ph, style); save("planes-phenomenon" + suffix);
    AtlasScreen own = base("PLANES", "Planechase", "The active player rolls");
    own.planes.on = true; own.planes.planeswalks = 1;
    planes(own);
    renderShell(own, style); save("planes-own" + suffix);
    // The die: Sam rolls planeswalk from a Sigil while the game shows.
    AtlasScreen g = base("PLAYING", "Sam's turn", "Turn 3");
    g.round = 3; players(g, 4, true);
    row(g, BUTTON_ROW_Y, {{TouchAction::PassTurn, "Pass"}, {TouchAction::Pause, "Pause"}}, {3, 2, 3});
    g.planes.on = true; g.planes.roll = 0;
    renderShell(g, style);
    g.planes.roll = 1; g.planes.face = 2; g.planes.cost = 1;
    snprintf(g.planes.roller, sizeof(g.planes.roller), "Sam");
    renderShell(g, style, true);
    nowMs += 520; lv_timer_handler(); save("planar-die-tumble" + suffix);
    nowMs += 900; lv_timer_handler(); save("planar-die-landed" + suffix);
    g.planes.roll = 2; g.planes.face = 1; g.planes.cost = 0; g.planes.fromEffect = true;
    renderShell(g, style, true);
    nowMs += 1420; lv_timer_handler(); save("planar-die-chaos" + suffix);
    nowMs += 4000; lv_timer_handler();
    lv_obj_clean(lv_layer_top());
    g.planes = ScreenPlanes(); renderShell(g, style);
  }
  {
    // The card view, landscape: made-up plane text over generated scenery
    // (no real card art or text in the repo), then with no art yet, then the
    // die rolling over it.
    static std::vector<uint16_t> art(TurnHubCards::PLANE_ART_WIDTH * TurnHubCards::PLANE_ART_HEIGHT);
    for (int y = 0; y < TurnHubCards::PLANE_ART_HEIGHT; ++y)
      for (int x = 0; x < TurnHubCards::PLANE_ART_WIDTH; ++x) {
        const int ridge = 150 + static_cast<int>(40 * sin(x / 37.0) + 18 * sin(x / 11.0));
        int r, g, b;
        if (y < ridge) { r = 40 + y / 3; g = 70 + y / 2; b = 140 + y / 3; }
        else { r = 30 + (y - ridge) / 4; g = 50 + (x % 60) / 6; b = 60; }
        art[y * TurnHubCards::PLANE_ART_WIDTH + x] = static_cast<uint16_t>((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
      }
    static TurnHubCards::PlaneCard card;
    snprintf(card.id, sizeof(card.id), "00000000-0000-0000-0000-000000000001");
    snprintf(card.type, sizeof(card.type), "Plane - Glasswing");
    snprintf(card.artist, sizeof(card.artist), "A. Painter");
    snprintf(card.text, sizeof(card.text), "Artifact creatures you control get +1/+1.\nAt the beginning of your upkeep, you may scry 1.");
    snprintf(card.chaos, sizeof(card.chaos), "Whenever chaos ensues, each player creates a 1/1 colorless Thopter artifact creature token with flying.");
    card.art = true;
    AtlasScreen s = base("PLANES", "Planechase", "");
    s.kind = ScreenKind::PlaneCard;
    s.planes.on = s.planes.digital = true; s.planes.counters = 2; s.planes.roll = 2;
    snprintf(s.planes.plane, sizeof(s.planes.plane), "Glasswing Spires");
    s.planes.card = &card; s.planes.art = art.data();
    lv_display_set_resolution(lv_display_get_default(), HEIGHT, WIDTH);
    renderShell(s, style); save("plane-card" + suffix);
    AtlasScreen bare = s; bare.planes.art = nullptr; bare.planes.counters = 0;
    renderShell(bare, style); save("plane-card-no-art" + suffix);
    s.planes.roll = 3; s.planes.face = 1; snprintf(s.planes.roller, sizeof(s.planes.roller), "Casey");
    renderShell(s, style, true);
    nowMs += 1420; lv_timer_handler(); save("plane-card-roll" + suffix);
    nowMs += 4000; lv_timer_handler();
    lv_obj_clean(lv_layer_top());
    lv_display_set_resolution(lv_display_get_default(), WIDTH, HEIGHT);
    s.planes = ScreenPlanes(); s.kind = ScreenKind::Status; renderShell(s, style);
  }
  {
    AtlasScreen s = base("QR", "Join this table", "Scan with a phone camera");
    s.kind = ScreenKind::Qr;
    snprintf(s.qr, sizeof(s.qr), "WIFI:T:WPA;S:TurnHub-Atlas;P:TurnHub-Setup;;");
    snprintf(s.qrCaption, sizeof(s.qrCaption), "Wi-Fi TurnHub-Atlas");
    row(s, BUTTON_ROW_Y, {{TouchAction::CloseScreen, "Back"}});
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
