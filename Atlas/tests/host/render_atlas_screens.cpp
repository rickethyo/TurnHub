// Host preview of the Atlas touchscreen art: runs the firmware's own
// atlas_art.cpp against an off-screen LovyanGFX sprite, writes one PNG per
// scene and checks that incremental redraws land on the same pixels as a
// full redraw. Presentation only: the scenes are hand-built AtlasScreens
// (the host scenarios cover how touch_controls.cpp builds them).
//
// Build and run with render-atlas-screens.sh (needs PlatformIO's LovyanGFX
// download under Atlas/.pio/libdeps/atlas). Output: build/screens/*.png.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "atlas_art.h"

uint32_t testNow = 0;  // The Arduino stub's millis().

using namespace TurnHubAtlas;

namespace {

struct Spec {
  TouchAction action;
  const char *label;
  uint32_t holdMs;
  uint8_t weight;
};

// Lays a row the way touch_controls.cpp's addRow does.
void row(AtlasScreen &s, int16_t y, std::vector<Spec> specs) {
  constexpr int16_t MARGIN = 8, FULL = ATLAS_SCREEN_WIDTH - 2 * MARGIN;
  uint16_t total = 0;
  for (const Spec &spec : specs) total += spec.weight;
  const int16_t usable = FULL - (static_cast<int16_t>(specs.size()) - 1) * MARGIN;
  int16_t x = MARGIN;
  for (size_t i = 0; i < specs.size(); ++i) {
    const int16_t w = i + 1 == specs.size() ? MARGIN + FULL - x : static_cast<int16_t>(usable * specs[i].weight / total);
    TouchButton &b = s.buttons[s.buttonCount++];
    b.action = specs[i].action;
    b.label = specs[i].label;
    b.x = x;
    b.y = y;
    b.w = w;
    b.h = BUTTON_ROW_H;
    b.holdMs = specs[i].holdMs;
    x += w + MARGIN;
  }
}

void text(char *out, size_t size, const char *value) { snprintf(out, size, "%s", value); }

ScreenPlayer player(uint8_t number, const char *name, int32_t life, uint8_t flags, const char *time,
    uint8_t avatar = 0) {
  ScreenPlayer p;
  p.number = number;
  text(p.name, sizeof(p.name), name);
  p.life = life;
  p.flags = flags;
  p.avatar = avatar;
  text(p.turnTime, sizeof(p.turnTime), time);
  return p;
}

AtlasScreen base(const char *badge, const char *title, const char *detail) {
  AtlasScreen s;
  s.kind = ScreenKind::Status;
  text(s.badge, sizeof(s.badge), badge);
  text(s.title, sizeof(s.title), title);
  text(s.detail, sizeof(s.detail), detail);
  s.sigilsOnline = 4;
  return s;
}

AtlasScreen playing() {
  AtlasScreen s = base("PLAYING", "Rowan's turn", "Turn time left 1:23");
  s.round = 3;
  text(s.gameClock, sizeof(s.gameClock), "42:10");
  s.showLife = true;
  s.players[0] = player(1, "Rowan", 32, CHIP_ACTIVE, "11:52", 1);
  s.players[1] = player(2, "Priya", 27, 0, "9:40", 3);
  s.players[2] = player(3, "Tobias", 0, CHIP_OUT, "6:05");
  s.players[3] = player(4, "Mae", 38, 0, "12:31", 5);
  s.playerCount = 4;
  s.timerPermille = 620;
  text(s.clock, sizeof(s.clock), "1:23");
  row(s, BUTTON_ROW_Y, {{TouchAction::Pause, "Pause", 0, 3}, {TouchAction::OpenTable, "Table", 0, 2}});
  return s;
}

std::vector<std::pair<std::string, AtlasScreen>> scenes() {
  std::vector<std::pair<std::string, AtlasScreen>> out;
  out.push_back({"playing", playing()});

  AtlasScreen warn = playing();
  warn.timerPermille = 75;
  warn.timerWarning = true;
  text(warn.clock, sizeof(warn.clock), "0:09");
  text(warn.detail, sizeof(warn.detail), "Turn time left 0:09");
  out.push_back({"timer-low", warn});

  AtlasScreen passing = playing();
  text(passing.detail, sizeof(passing.detail), "Passing in 2s: that seat can cancel");
  passing.timerPermille = -1;
  text(passing.clock, sizeof(passing.clock), "3:41");
  out.push_back({"timer-off-passing", passing});

  AtlasScreen paused = playing();
  text(paused.badge, sizeof(paused.badge), "PAUSED");
  text(paused.title, sizeof(paused.title), "Paused");
  text(paused.detail, sizeof(paused.detail), "Win claim: waiting on Priya");
  paused.players[1].flags = CHIP_WAITING;
  paused.buttonCount = 0;
  row(paused, BUTTON_ROW_Y, {{TouchAction::Resume, "Resume", 0, 3}, {TouchAction::OpenTable, "Table", 0, 2}});
  out.push_back({"paused", paused});

  AtlasScreen six = playing();
  six.sdMissing = true;
  six.round = 12;
  text(six.gameClock, sizeof(six.gameClock), "1:08:44");
  six.players[4] = player(5, "Isolde", 21, 0, "10:02", 7);
  six.players[5] = player(6, "Bartholomew", 40, 0, "8:15");
  six.playerCount = 6;
  out.push_back({"six-players-no-sd", six});

  AtlasScreen lobby = base("LOBBY", "Lobby", "3 players, 3 Sigils");
  lobby.sigilsOnline = 3;
  lobby.players[0] = player(1, "Rowan", 0, CHIP_STARTER, "", 1);
  lobby.players[1] = player(2, "Priya", 0, 0, "", 3);
  lobby.players[2] = player(3, "Mae", 0, 0, "", 5);
  lobby.playerCount = 3;
  row(lobby, BUTTON_ROW_Y, {{TouchAction::StartGame, "Start", 0, 3},
      {TouchAction::ClearLobby, "Clear", 3000, 2}, {TouchAction::OpenMenu, "Menu", 0, 2}});
  out.push_back({"lobby", lobby});

  AtlasScreen empty = base("LOBBY", "Lobby", "0 players, 2 Sigils");
  empty.sigilsOnline = 2;
  text(empty.lines[0], sizeof(empty.lines[0]), "Join from a Sigil's menu,");
  text(empty.lines[1], sizeof(empty.lines[1]), "or on the table portal.");
  text(empty.lines[2], sizeof(empty.lines[2]), "Menu: pair, QR codes, info.");
  empty.lineCount = 3;
  row(empty, BUTTON_ROW_Y, {{TouchAction::OpenMenu, "Menu", 0, 2}});
  out.push_back({"lobby-empty", empty});

  AtlasScreen pairing = empty;
  text(pairing.detail, sizeof(pairing.detail), "Pairing open: 12 s left");
  text(pairing.notice, sizeof(pairing.notice), "Pairing window opened");
  out.push_back({"lobby-pairing", pairing});

  AtlasScreen menu = base("MENU", "Table menu", "Pair Sigils, share codes, table info");
  menu.kind = ScreenKind::Menu;
  row(menu, BUTTON_UPPER_ROW_Y, {{TouchAction::Pair, "Pair a Sigil", 0, 1}, {TouchAction::OpenQr, "QR codes", 0, 1}});
  row(menu, BUTTON_ROW_Y, {{TouchAction::OpenTests, "Tests", 0, 1}, {TouchAction::OpenInfo, "Info", 0, 1},
      {TouchAction::CloseScreen, "Back", 0, 1}});
  out.push_back({"menu", menu});

  AtlasScreen over = base("GAME OVER", "Game over", "Priya wins");
  over.round = 9;
  text(over.gameClock, sizeof(over.gameClock), "58:02");
  over.showLife = true;
  over.players[0] = player(1, "Rowan", 0, CHIP_OUT, "16:40", 1);
  over.players[1] = player(2, "Priya", 14, CHIP_WINNER, "14:12", 3);
  over.players[2] = player(3, "Tobias", 0, CHIP_OUT, "9:31");
  over.players[3] = player(4, "Mae", 0, CHIP_OUT, "17:39", 5);
  over.playerCount = 4;
  row(over, BUTTON_ROW_Y, {{TouchAction::Rematch, "Rematch", 0, 3}, {TouchAction::ResetTable, "Reset", 0, 2},
      {TouchAction::OpenMenu, "Menu", 0, 2}});
  out.push_back({"game-over", over});

  AtlasScreen table = base("TABLE", "Table controls", "Stuck turn? Master pass skips Rowan");
  table.kind = ScreenKind::Table;
  row(table, BUTTON_UPPER_ROW_Y, {{TouchAction::MasterPass, "Master pass", 2000, 1}});
  row(table, BUTTON_ROW_Y, {{TouchAction::EndMatch, "End match", 5000, 3}, {TouchAction::CloseScreen, "Back", 0, 2}});
  table.pressed = TouchAction::EndMatch;
  table.holdSecondsLeft = 3;
  table.holdPermille = 400;
  out.push_back({"table-hold", table});

  AtlasScreen qr = base("QR CODES", "Table portal", "Scan on the Atlas Wi-Fi");
  qr.kind = ScreenKind::Qr;
  text(qr.qr, sizeof(qr.qr), "http://192.168.4.1/portal");
  text(qr.qrCaption, sizeof(qr.qrCaption), "192.168.4.1/portal");
  row(qr, BUTTON_ROW_Y, {{TouchAction::QrWifi, "Wi-Fi", 0, 1}, {TouchAction::QrPortal, "Portal", 0, 1},
      {TouchAction::QrSignIn, "Sign in", 0, 1}, {TouchAction::CloseScreen, "Back", 0, 1}});
  qr.buttons[1].selected = true;
  out.push_back({"qr-codes", qr});

  AtlasScreen code = base("VERIFY", "Admin code", "For Rowan (54 s)");
  code.kind = ScreenKind::Code;
  text(code.code, sizeof(code.code), "482 913");
  text(code.qr, sizeof(code.qr), "http://192.168.4.1/portal#code=482913");
  text(code.qrCaption, sizeof(code.qrCaption), "Enter it on that phone");
  row(code, BUTTON_ROW_Y, {{TouchAction::CancelCode, "Cancel", 0, 1}});
  out.push_back({"presence-code", code});

  AtlasScreen info = base("INFO", "Table info", "TurnHub Atlas v0.6.3-dev");
  info.kind = ScreenKind::Info;
  const char *lines[] = {"Wi-Fi: TurnHub-Atlas", "Portal: 192.168.4.1", "Sigils online: 4",
      "SD card: NOT INSERTED", "Up 1:12:09"};
  for (const char *line : lines) text(info.lines[info.lineCount++], sizeof(info.lines[0]), line);
  row(info, BUTTON_ROW_Y, {{TouchAction::OpenQr, "QR codes", 0, 3}, {TouchAction::CloseScreen, "Back", 0, 2}});
  out.push_back({"info", info});

  AtlasScreen pressed = lobby;
  pressed.pressed = TouchAction::StartGame;
  out.push_back({"lobby-start-pressed", pressed});
  return out;
}

bool writePng(lgfx::LGFX_Sprite &sprite, const std::string &path) {
  size_t length = 0;
  void *png = sprite.createPng(&length, 0, 0, ATLAS_SCREEN_WIDTH, ATLAS_SCREEN_HEIGHT);
  if (png == nullptr) return false;
  FILE *file = fopen(path.c_str(), "wb");
  const bool ok = file != nullptr && fwrite(png, 1, length, file) == length;
  if (file) fclose(file);
  free(png);
  return ok;
}

std::vector<uint16_t> pixels(lgfx::LGFX_Sprite &sprite) {
  std::vector<uint16_t> out(ATLAS_SCREEN_WIDTH * ATLAS_SCREEN_HEIGHT);
  for (int y = 0; y < ATLAS_SCREEN_HEIGHT; ++y)
    for (int x = 0; x < ATLAS_SCREEN_WIDTH; ++x) out[y * ATLAS_SCREEN_WIDTH + x] = sprite.readPixel(x, y);
  return out;
}

// Incremental redraw from `from` to `to` must match a full redraw of `to`.
int checkIncremental(lgfx::LGFX_Sprite &sprite, const char *name, const AtlasScreen &from, const AtlasScreen &to,
    uint32_t nowMs) {
  invalidateAtlasScreen();
  renderAtlasScreen(from, nowMs);
  renderAtlasScreen(to, nowMs);
  const std::vector<uint16_t> incremental = pixels(sprite);
  invalidateAtlasScreen();
  renderAtlasScreen(to, nowMs);
  const std::vector<uint16_t> full = pixels(sprite);
  int differ = 0;
  for (size_t i = 0; i < full.size(); ++i) differ += incremental[i] != full[i];
  printf("%s %s: %d pixels differ\n", differ ? "FAIL" : "PASS", name, differ);
  return differ ? 1 : 0;
}

}  // namespace

int main(int argc, char **argv) {
  const std::string outDir = argc > 1 ? argv[1] : "build/screens";
  lgfx::LGFX_Sprite sprite;
  sprite.setColorDepth(16);
  if (sprite.createSprite(ATLAS_SCREEN_WIDTH, ATLAS_SCREEN_HEIGHT) == nullptr) return 2;
  const AtlasArtStatus status = beginAtlasArt(sprite);
  if (!status.fonts || !status.buffers) {
    printf("FAIL fonts=%d buffers=%d\n", status.fonts, status.buffers);
    return 1;
  }
  constexpr uint32_t NOW = 123456;

  drawAtlasSplash();
  int failures = writePng(sprite, outDir + "/splash.png") ? 0 : 1;
  for (const auto &scene : scenes()) {
    invalidateAtlasScreen();
    renderAtlasScreen(scene.second, NOW);
    if (!writePng(sprite, outDir + "/" + scene.first + ".png")) {
      printf("FAIL writing %s\n", scene.first.c_str());
      ++failures;
    }
  }

  // Region-by-region redraws: a clock tick, a pass to the next player, a
  // life change, an action message, a hold's progress, lobby to game.
  const AtlasScreen a = playing();
  AtlasScreen tick = a;
  tick.timerPermille = 610;
  text(tick.clock, sizeof(tick.clock), "1:22");
  text(tick.detail, sizeof(tick.detail), "Turn time left 1:22");
  text(tick.gameClock, sizeof(tick.gameClock), "42:11");
  text(tick.players[0].turnTime, sizeof(tick.players[0].turnTime), "11:53");
  failures += checkIncremental(sprite, "clock tick", a, tick, NOW);

  AtlasScreen passed = tick;
  passed.players[0].flags = 0;
  passed.players[1].flags = CHIP_ACTIVE;
  text(passed.title, sizeof(passed.title), "Priya's turn");
  passed.round = 3;
  failures += checkIncremental(sprite, "turn passes", tick, passed, NOW);

  AtlasScreen life = passed;
  life.players[3].life = 31;
  text(life.notice, sizeof(life.notice), "Mae: -7 life");
  failures += checkIncremental(sprite, "life change with notice", passed, life, NOW);

  AtlasScreen warned = life;
  warned.timerWarning = true;
  warned.timerPermille = 80;
  failures += checkIncremental(sprite, "timer warning", life, warned, NOW);

  const auto all = scenes();
  AtlasScreen hold = all[10].second;  // table-hold
  AtlasScreen holdMore = hold;
  holdMore.holdPermille = 700;
  holdMore.holdSecondsLeft = 2;
  failures += checkIncremental(sprite, "hold progress", hold, holdMore, NOW);
  failures += checkIncremental(sprite, "lobby to game", all[5].second, a, NOW);
  failures += checkIncremental(sprite, "game to game over", a, all[9].second, NOW);
  failures += checkIncremental(sprite, "empty lobby to pairing", all[6].second, all[7].second, NOW);
  failures += checkIncremental(sprite, "button press", all[5].second, all[14].second, NOW);
  printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
