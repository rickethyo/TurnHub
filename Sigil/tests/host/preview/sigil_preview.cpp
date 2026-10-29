// Renders the LVGL Sigil screen (src/sigil_display_lvgl.cpp) on the host from
// hand-built snapshots, so layouts can be reviewed without the board.
// Output: build/preview/<name>.ppm (render-sigil-lvgl.sh converts to PNG).

#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "sigil_display_lvgl.h"

using namespace TurnHubSigil;
using namespace TurnHubProtocol;

namespace {

constexpr int16_t WIDTH = SIGIL_SCREEN_W, HEIGHT = SIGIL_SCREEN_H;
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
  // A turn banner belongs to the frame it was saved in.
  lv_obj_clean(lv_layer_top());
  fclose(f);
  printf("%s\n", path.c_str());
}

MenuView menu(std::initializer_list<std::pair<Key, uint8_t>> keys, bool life = false) {
  MenuView v;
  v.active = true;
  v.life = life;
  for (auto &k : keys) v.compass[static_cast<uint8_t>(k.first)] = k.second;
  return v;
}

uint8_t A(SigilAction a) { return static_cast<uint8_t>(a); }

GameDisplayPacket game(const char *name, int32_t life, bool active, uint8_t turn) {
  GameDisplayPacket g{};
  g.version = VERSION;
  g.type = PacketType::GameDisplay;
  g.sigilId = 2;
  g.state = static_cast<int32_t>(static_cast<uint32_t>(DisplayMode::Running) | (active ? DISPLAY_FLAG_ACTIVE : 0) |
      (1u << 8) | (static_cast<uint32_t>(turn) << 24));
  snprintf(g.primary.name, sizeof(g.primary.name), "%s", name);
  g.primary.life = life;
  return g;
}

}  // namespace

int main() {
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return nowMs; });
  lv_display_t *disp = lv_display_create(WIDTH, HEIGHT);
  static uint16_t buf[WIDTH * 40];
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush);

  for (uint8_t t = 0; t < 2; ++t) {
    LvglSigilDisplay d;
    const auto theme = t == 0 ? TurnHubTheme::Id::Graphite : TurnHubTheme::Id::Brass;
    const std::string suffix = t == 0 ? "" : "-brass";
    d.setTheme(theme);
    d.showHome(2,true,"Alexandria",static_cast<uint8_t>(DisplayMode::Running)); save("home"+suffix);
    TableSnapshot roster;roster.valid=true;roster.total=4;
    for(uint8_t i=0;i<4;++i) { auto &r=roster.rows[i];r.player=i+1;r.life=40-i*3;snprintf(r.name,sizeof(r.name),"Player %u",i+1); }
    roster.rows[1].flags=TABLE_ACTIVE;
    d.showTable(2,roster,0,true); save("table"+suffix);
    d.showTable(2,roster,1,true); save("table-next"+suffix);
    SigilMenu device;device.openDeviceMenu(0);
    d.setMenuView(device.view());d.showSettings(2);save("settings"+suffix);
    d.setMenuView(MenuView());
    d.showUnpaired(); save("unpaired" + suffix);
    MenuView pairing;
    pairing.pairPercent = 50;
    d.setMenuView(pairing);
    d.showUnpaired(); save("pair-hold" + suffix);
    pairing.pairPercent = 0;
    pairing.pairing = true;
    d.setMenuView(pairing);
    d.showUnpaired(); save("pair-search" + suffix);
    d.setMenuView(MenuView());
    d.showPairingCode(4271); save("pairing-code" + suffix);
    d.setMenuView(menu({{Key::Up, MENU_LOCAL_DEVICE_MENU}, {Key::Select, A(SigilAction::Join)}}));
    d.showReady(2); save("ready" + suffix);
    d.setSeatName(0, "Ricky");
    d.setMenuView(menu({{Key::Up, MENU_LOCAL_DEVICE_MENU}, {Key::Select, A(SigilAction::StartGame)},
        {Key::Left, A(SigilAction::AddSeatB)}, {Key::Right, A(SigilAction::CycleStarter)}}));
    d.showState(2, DisplayMode::Lobby, 1, 0, 0, DISPLAY_FLAG_STARTER); save("lobby" + suffix);
    d.setMenuView(menu({{Key::Up, MENU_LOCAL_DEVICE_MENU}, {Key::Select, A(SigilAction::Pass)}}, true));
    d.showGame(game("Ricky", 40, true, 5));
    nowMs += 400; lv_timer_handler(); save("turn-banner" + suffix);
    d.showGame(game("Ricky", 40, true, 5)); save("game-turn" + suffix);
    LifeOverlay o;
    o.pending = -3;
    d.setLifeOverlay(o);
    d.setMenuView(menu({{Key::Up, MENU_LOCAL_DEVICE_MENU}}, true));
    d.showGame(game("Alexandria", 17, false, 6)); save("game-waiting" + suffix);
    o = LifeOverlay();
    o.request.target = 1; o.request.requester = 3; o.request.delta = -5;
    d.setLifeOverlay(o);
    d.showGame(game("Ricky", 22, false, 7)); save("game-request" + suffix);
    d.setLifeOverlay(LifeOverlay());
    MenuView open = menu({{Key::Select, A(SigilAction::Pause)}, {Key::Up, A(SigilAction::ClaimWin)},
        {Key::Right, A(SigilAction::BeginElimination)}, {Key::Down, MENU_LOCAL_DEVICE}, {Key::Left, MENU_LOCAL_BACK}});
    open.deviceMenu = true;
    d.setMenuView(open);
    d.showGame(game("Ricky", 22, false, 7)); save("menu" + suffix);
    open.holdAction = A(SigilAction::ClaimWin);
    open.holdPercent = 50;
    d.setMenuView(open);
    d.showGame(game("Ricky", 22, false, 7)); save("action-hold" + suffix);
    d.setMenuView(MenuView());
    ProfilePickerPacket p{};
    p.sigilId = 2; p.mode = PickerMode::List; p.page = 0; p.pageCount = 2; p.itemCount = 3;
    snprintf(p.items[0].name, sizeof(p.items[0].name), "Ricky");
    snprintf(p.items[1].name, sizeof(p.items[1].name), "Sam");
    p.items[1].flags = PICKER_ITEM_LOCKED;
    snprintf(p.items[2].name, sizeof(p.items[2].name), "Guest");
    p.items[2].flags = PICKER_ITEM_GUEST;
    d.showPicker(p, 0); save("picker" + suffix);
  }
  return 0;
}
