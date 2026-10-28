// The LVGL Sigil screen (sigil_display_lvgl.h). Presentation only: every
// screen is rebuilt from the latest snapshot Atlas sent, and the on-screen
// keys are labelled from the menu view; pressing them is main.cpp's job.

#if defined(TURNHUB_SIGIL_LVGL) || defined(TURNHUB_SCREEN_PREVIEW)

#include "sigil_display_lvgl.h"

#include <stdio.h>
#include <string.h>

#include "commander_damage.h"
#include "commander_flow_view.h"
#include "secure_link.h"

extern "C" {
extern const lv_font_t th_eyebrow_13;
extern const lv_font_t th_semi_16;
extern const lv_font_t th_semi_20;
extern const lv_font_t th_bold_28;
extern const lv_font_t th_num_40;
extern const lv_font_t th_num_64;
}

namespace TurnHubSigil {
namespace {

constexpr int16_t W = SIGIL_SCREEN_W;
constexpr int16_t M = 10;  // Side margin.
constexpr int16_t HEADER_H = 30;

TurnHubTheme::Id styleTheme = TurnHubTheme::Id::Graphite;
bool styleInverted = false;

const TurnHubTheme::Palette &pal() { return TurnHubTheme::palette(styleTheme); }

lv_color_t rgb(uint32_t c) {
  if (styleInverted) c = 0xFFFFFF - (c & 0xFFFFFF);
  return lv_color_hex(c);
}

lv_obj_t *box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

void card(lv_obj_t *o, uint32_t fill, uint32_t frame, int16_t radius = 14) {
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, rgb(fill), 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_border_color(o, rgb(frame), 0);
}

int32_t textWidth(const char *s, const lv_font_t *font) {
  lv_point_t size;
  lv_text_get_size(&size, s, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}

// One line, cut with an ellipsis.
lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *font, uint32_t color, int16_t x,
    int16_t y, int16_t w, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_remove_style_all(l);
  lv_label_set_text(l, s);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, rgb(color), 0);
  lv_obj_set_style_text_align(l, align, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_size(l, w, lv_font_get_line_height(font));
  return l;
}

// The largest of the fonts that fits the width.
const lv_font_t *fitted(const char *s, int16_t width, const lv_font_t *const *fonts, uint8_t count) {
  for (uint8_t i = 0; i < count; ++i)
    if (textWidth(s, fonts[i]) <= width) return fonts[i];
  return fonts[count - 1];
}

const lv_font_t *nameFont(const char *s, int16_t width) {
  static const lv_font_t *const fonts[] = {&th_bold_28, &th_semi_20, &th_semi_16};
  return fitted(s, width, fonts, 3);
}

// A rounded tag: the app's status pill.
void pill(lv_obj_t *parent, const char *s, uint32_t fill, uint32_t ink, int16_t y) {
  const int16_t w = static_cast<int16_t>(textWidth(s, &th_eyebrow_13) + 24);
  lv_obj_t *p = box(parent, (W - w) / 2, y, w, 22);
  card(p, fill, fill, 11);
  text(p, s, &th_eyebrow_13, ink, 0, 3, w, LV_TEXT_ALIGN_CENTER);
}

// Life totals use the digit-only numeral fonts.
void lifeNumber(lv_obj_t *parent, int32_t life, int16_t y, bool large) {
  char digits[12];
  snprintf(digits, sizeof(digits), "%ld", static_cast<long>(life));
  text(parent, digits, large ? &th_num_64 : &th_num_40, pal().text, 0, y, W, LV_TEXT_ALIGN_CENTER);
}

}  // namespace

void LvglSigilDisplay::setTheme(TurnHubTheme::Id theme) {
  SigilDisplay::setTheme(theme);
  styleTheme = theme;
}

void LvglSigilDisplay::setInverted(bool inverted) {
  SigilDisplay::setInverted(inverted);
  styleInverted = inverted;
}

bool LvglSigilDisplay::lifeKeysShown() const {
  return !lifeRequestShown() && menu_.life &&
      menu_.compass[static_cast<uint8_t>(Key::Left)] == MENU_NONE &&
      menu_.compass[static_cast<uint8_t>(Key::Right)] == MENU_NONE;
}

// A fresh screen with its header: the Sigil number, a title and the turn.
lv_obj_t *LvglSigilDisplay::frame(const char *title, uint8_t sigilId, uint8_t turn) {
  styleTheme = theme_;
  styleInverted = inverted_;
  lv_obj_t *screen = lv_screen_active();
  lv_obj_clean(screen);
  for (auto &k : keys_) k = nullptr;
  lv_obj_set_style_bg_color(screen, rgb(pal().bg), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *bar = box(screen, 0, 0, W, HEADER_H);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(bar, rgb(pal().surface1), 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_color(bar, rgb(pal().separator), 0);
  char left[12] = "SIGIL";
  if (sigilId != 0xFF) snprintf(left, sizeof(left), "SIGIL %u", static_cast<unsigned>(sigilId + 1));
  text(bar, left, &th_eyebrow_13, pal().accent, M, 8, 70);
  text(bar, title, &th_semi_16, pal().text, 70, 5, 100, LV_TEXT_ALIGN_CENTER);
  if (turn) {
    char t[12];
    snprintf(t, sizeof(t), "TURN %u", static_cast<unsigned>(turn));
    text(bar, t, &th_eyebrow_13, pal().textSecondary, W - 80, 8, 80 - M, LV_TEXT_ALIGN_RIGHT);
  }
  if (const char *update = updateNoticeText(life_.update)) {
    text(screen, update, &th_eyebrow_13, pal().info, M, PAD_TOP - 16, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  }
  return screen;
}

// The five keys. Blank keys stay as faint outlines so the layout never moves.
void LvglSigilDisplay::pad(const PadLabels &labels) {
  lv_obj_t *screen = lv_screen_active();
  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    const Key key = static_cast<Key>(i);
    const PadRect r = padRect(key);
    lv_obj_t *b = box(screen, r.x, r.y, r.w, r.h);
    keys_[i] = b;
    const char *label = labels.text[i];
    if (!label || !label[0]) {
      lv_obj_set_style_radius(b, 12, 0);
      lv_obj_set_style_border_width(b, 1, 0);
      lv_obj_set_style_border_color(b, rgb(pal().separator), 0);
      continue;
    }
    const bool accent = labels.accent[i];
    card(b, accent ? pal().accent : pal().surface2, accent ? pal().accent : pal().separatorStrong, 12);
    const uint32_t ink = accent ? pal().onAccent : pal().text;
    const char *note = labels.note[i];
    const lv_font_t *font = textWidth(label, &th_semi_20) <= r.w - 8 ? &th_semi_20 : &th_semi_16;
    const int16_t lh = static_cast<int16_t>(lv_font_get_line_height(font));
    // A label too long for one line wraps onto two when there is no note.
    const bool wrap = !note && textWidth(label, font) > r.w - 8;
    const int16_t top = note ? (r.h - lh - 14) / 2 : wrap ? (r.h - 2 * lh) / 2 : (r.h - lh) / 2;
    lv_obj_t *t = text(b, label, font, ink, 4, top, r.w - 8, LV_TEXT_ALIGN_CENTER);
    if (wrap) {
      lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
      lv_obj_set_height(t, 2 * lh);
    }
    if (note) text(b, note, &th_eyebrow_13, accent ? pal().onAccent : pal().textSecondary, 4, top + lh, r.w - 8,
        LV_TEXT_ALIGN_CENTER);
  }
  setPressedKey(pressed_);
}

void LvglSigilDisplay::setPressedKey(Key key) {
  pressed_ = key;
  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    if (!keys_[i]) continue;
    const bool down = static_cast<uint8_t>(key) == i;
    lv_obj_set_style_outline_width(keys_[i], down ? 3 : 0, 0);
    lv_obj_set_style_outline_pad(keys_[i], 0, 0);
    lv_obj_set_style_outline_color(keys_[i], rgb(pal().focus), 0);
  }
}

// The menu view's keys, as the e-ink legend names them.
PadLabels LvglSigilDisplay::menuLabels() const {
  PadLabels l;
  if (!menu_.active) return l;
  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    const uint8_t action = menu_.compass[i];
    if (action == MENU_NONE) continue;
    l.text[i] = sigilActionLabel(static_cast<TurnHubProtocol::SigilAction>(action));
    if (menuActionNeedsHold(action)) l.note[i] = "HOLD";
  }
  const uint8_t left = static_cast<uint8_t>(Key::Left), right = static_cast<uint8_t>(Key::Right);
  if (lifeRequestShown()) {
    l.text[left] = "Deny";
    l.text[right] = "Approve";
    l.note[left] = l.note[right] = nullptr;
    l.accent[right] = true;
  } else if (lifeKeysShown()) {
    l.text[left] = "-1";
    l.text[right] = "+1";
    l.note[left] = l.note[right] = "LIFE";
  }
  l.accent[static_cast<uint8_t>(Key::Select)] = !menu_.deviceMenu && l.text[static_cast<uint8_t>(Key::Select)];
  return l;
}

void LvglSigilDisplay::finish(const PadLabels &labels) {
  pad(labels);
  present();
}

void LvglSigilDisplay::statusScreen(const char *title, const char *line1, const char *line2,
    uint8_t sigilId, PadLabels labels) {
  lv_obj_t *s = frame("TurnHub", sigilId);
  text(s, title, &th_bold_28, pal().text, M, 62, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  if (line1) text(s, line1, &th_semi_16, pal().textSecondary, M, 104, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  if (line2) text(s, line2, &th_semi_16, pal().textSecondary, M, 126, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  finish(labels);
}

// Open Menu replaces the screen; the keys hold its entries.
bool LvglSigilDisplay::drawDeviceMenu(uint8_t sigilId) {
  if (!menu_.active || !menu_.deviceMenu) return false;
  char detail[24] = "";
  if (menu_.recovery) {
    snprintf(detail, sizeof(detail), "%s%s", pal().label, inverted() ? ", inverted" : "");
  } else if (menu_.pageCount > 1) {
    snprintf(detail, sizeof(detail), "Page %u of %u", static_cast<unsigned>(menu_.page + 1),
        static_cast<unsigned>(menu_.pageCount));
  }
  statusScreen(menu_.recovery ? "Device" : "Menu", detail[0] ? detail : nullptr, "Choose below", sigilId,
      menuLabels());
  return true;
}

void LvglSigilDisplay::showBooting() { statusScreen("Starting", "Looking for Atlas", nullptr, 0xFF, PadLabels()); }

void LvglSigilDisplay::showUnpaired() {
  if (drawDeviceMenu(0xFF)) return;
  PadLabels l = menuLabels();
  // Holding the click opens pairing while unpaired (main.cpp's joystick hold).
  l.text[static_cast<uint8_t>(Key::Select)] = "Pair";
  l.note[static_cast<uint8_t>(Key::Select)] = "HOLD";
  l.accent[static_cast<uint8_t>(Key::Select)] = true;
  statusScreen("Unpaired", "Hold Pair, then confirm", "the code on Atlas", 0xFF, l);
}

void LvglSigilDisplay::showReady(uint8_t sigilId) {
  if (drawDeviceMenu(sigilId)) return;
  statusScreen("Ready", "Welcome to TurnHub!", "Join from the keys below", sigilId, menuLabels());
}

void LvglSigilDisplay::showAtlasLost(uint8_t sigilId) {
  if (drawDeviceMenu(sigilId)) return;
  statusScreen("Atlas lost", "Searching for Atlas", nullptr, sigilId, menuLabels());
}

void LvglSigilDisplay::showPairingCode(uint16_t code) {
  char digits[5];
  TurnHubSecureLink::formatPairingCode(code, digits);
  lv_obj_t *s = frame("Pairing");
  text(s, "Pairing code", &th_semi_16, pal().textSecondary, M, 44, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  lv_obj_t *c = box(s, M + 20, 70, W - 2 * M - 40, 76);
  card(c, pal().surface2, pal().accent);
  text(c, digits, &th_num_64, pal().accent, 0, 2, W - 2 * M - 40, LV_TEXT_ALIGN_CENTER);
  text(s, "Confirm it on Atlas", &th_semi_16, pal().text, M, 152, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  finish(PadLabels());
}

void LvglSigilDisplay::showUpdate(const char *status, int8_t percent) {
  lv_obj_t *s = frame("Update");
  text(s, "Updating", &th_bold_28, pal().text, M, 62, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  text(s, status, &th_semi_16, pal().textSecondary, M, 104, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  if (percent >= 0) {
    lv_obj_t *track = box(s, M + 10, 134, W - 2 * M - 20, 8);
    card(track, pal().fillStrong, pal().fillStrong, 4);
    lv_obj_t *fill = box(track, 0, 0, (W - 2 * M - 20) * percent / 100, 8);
    card(fill, pal().accent, pal().accent, 4);
  }
  finish(PadLabels());
}

void LvglSigilDisplay::showSleeping() {
  statusScreen("Asleep", "Press BOOT or reset", "to wake", 0xFF, PadLabels());
}

bool LvglSigilDisplay::setSeatName(uint8_t slot, const char *name) {
  if (slot > 1) return false;
  char next[sizeof(seatName_[0])];
  snprintf(next, sizeof(next), "%s", name ? name : "");
  if (strcmp(next, seatName_[slot]) == 0) return false;
  memcpy(seatName_[slot], next, sizeof(next));
  return true;
}

void LvglSigilDisplay::showGame(const TurnHubProtocol::GameDisplayPacket &g) {
  if (drawDeviceMenu(g.sigilId)) return;
  using namespace TurnHubProtocol;
  const bool active = hasDisplayFlag(g.state, DISPLAY_FLAG_ACTIVE);
  const bool shared = displaySecondaryPlayer(g.state) != 0;
  const bool seatB = hasDisplayFlag(g.state, DISPLAY_FLAG_PRIMARY_B);
  const bool cmd = commanderDamageShown(g);
  lv_obj_t *s = frame(g.commander ? "Commander" : "Game", g.sigilId, displayTurnNumber(g.state));

  if (lifeRequestShown()) {
    char ask[32];
    snprintf(ask, sizeof(ask), "P%u asks %+ld life", static_cast<unsigned>(life_.request.requester),
        static_cast<long>(life_.request.delta));
    text(s, ask, &th_semi_20, pal().warning, M, 36, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  } else {
    text(s, g.primary.name, nameFont(g.primary.name, W - 2 * M), pal().text, M, 34, W - 2 * M,
        LV_TEXT_ALIGN_CENTER);
  }
  if (life_.passPending) pill(s, "PASSING", pal().warning, pal().bg, 68);
  else if (active) pill(s, "YOUR TURN", pal().turn, pal().onTurn, 68);
  else pill(s, "WAITING", pal().fill, pal().textSecondary, 68);

  // The life card; smaller when another seat or commander damage needs room.
  const bool compact = shared || cmd;
  const int16_t cardH = compact ? 56 : 80;
  lv_obj_t *lifeCard = box(s, M, 96, W - 2 * M, cardH);
  card(lifeCard, pal().surface1, active ? pal().turn : pal().separator);
  char label[16] = "LIFE";
  if (shared) snprintf(label, sizeof(label), "SEAT %c LIFE", seatB ? 'B' : 'A');
  text(lifeCard, label, &th_eyebrow_13, pal().textTertiary, 10, 6, 100);
  char digits[12];
  snprintf(digits, sizeof(digits), "%ld", static_cast<long>(g.primary.life));
  text(lifeCard, digits, compact ? &th_num_40 : &th_num_64, pal().text, 0, compact ? 10 : 8, W - 2 * M,
      LV_TEXT_ALIGN_CENTER);
  if (life_.pending) {
    char delta[12];
    snprintf(delta, sizeof(delta), "%+ld", static_cast<long>(life_.pending));
    text(lifeCard, delta, &th_semi_20, life_.pending > 0 ? pal().good : pal().critical, W - 2 * M - 70, 4, 60,
        LV_TEXT_ALIGN_RIGHT);
  }

  int16_t y = 96 + cardH + 4;
  if (shared && y + 20 <= PAD_TOP) {
    char other[32];
    snprintf(other, sizeof(other), "%c: %s", seatB ? 'A' : 'B', g.secondary.name);
    text(s, other, &th_semi_16, pal().textSecondary, M, y, W - 2 * M - 50);
    snprintf(digits, sizeof(digits), "%ld", static_cast<long>(g.secondary.life));
    text(s, digits, &th_semi_16, pal().text, W - M - 50, y, 50, LV_TEXT_ALIGN_RIGHT);
    y += 20;
  }
  if (cmd && y + 18 <= PAD_TOP) {
    // Damage taken, as many sources as fit on one line.
    char line[48] = "CMD";
    for (uint8_t i = 0; i < g.sourceCount; ++i) {
      char damage[24];
      formatCommanderDamage(g.sources[i], damage, sizeof(damage));
      const size_t used = strlen(line);
      snprintf(line + used, sizeof(line) - used, "  %.6s %s", g.sources[i].name, damage);
    }
    text(s, line, &th_eyebrow_13, pal().critical, M, y, W - 2 * M);
  }
  finish(menuLabels());
}

void LvglSigilDisplay::showState(uint8_t sigilId, TurnHubProtocol::DisplayMode mode, uint8_t primaryPlayer,
    uint8_t secondaryPlayer, uint8_t turnNumber, uint8_t flags) {
  using namespace TurnHubProtocol;
  if (drawDeviceMenu(sigilId)) return;
  const bool active = flags & DISPLAY_FLAG_ACTIVE;
  const bool starter = flags & DISPLAY_FLAG_STARTER;
  const bool winner = flags & DISPLAY_FLAG_WINNER;
  const bool attention = flags & DISPLAY_FLAG_ATTENTION;
  const char *title = "TurnHub";
  const char *status = "WAITING";
  bool lit = false;
  switch (mode) {
    case DisplayMode::Lobby:
      title = "Lobby";
      status = secondaryPlayer ? "SHARED SIGIL" : (starter ? "STARTER" : "IN LOBBY");
      lit = starter;
      break;
    case DisplayMode::Starting:
      title = "Starting";
      status = starter ? "GO FIRST" : "GET READY";
      lit = starter;
      break;
    case DisplayMode::Running:
      title = "Game";
      status = active ? "YOUR TURN" : "WAITING";
      lit = active;
      break;
    case DisplayMode::Paused:
      title = "Paused";
      status = attention ? "ACTION NEEDED" : "GAME PAUSED";
      lit = attention;
      break;
    case DisplayMode::GameOver:
      title = "Game over";
      status = winner ? "WINNER!" : "GAME COMPLETE";
      lit = winner;
      break;
    case DisplayMode::Ready:
    default:
      showReady(sigilId);
      return;
  }
  lv_obj_t *s = frame(title, sigilId, turnNumber);
  const bool focusB = (flags & DISPLAY_FLAG_PRIMARY_B) != 0;
  auto seat = [&](const char *tag, const char *name, int16_t y, int16_t h, bool focus) {
    lv_obj_t *c = box(s, M, y, W - 2 * M, h);
    card(c, pal().surface1, focus ? pal().turn : pal().separator);
    if (focus) lv_obj_set_style_border_width(c, 2, 0);
    text(c, tag, &th_eyebrow_13, pal().textTertiary, 10, 6, 120);
    const char *shown = name[0] ? name : "Guest";
    text(c, shown, nameFont(shown, W - 2 * M - 20), pal().text, 10, h > 50 ? 26 : 20, W - 2 * M - 20);
  };
  if (secondaryPlayer) {
    seat("SEAT A", seatName_[0], 38, 52, lit && !focusB);
    seat("SEAT B", seatName_[1], 94, 52, lit && focusB);
  } else {
    char tag[16] = "READY";
    if (primaryPlayer) snprintf(tag, sizeof(tag), "PLAYER %u", static_cast<unsigned>(primaryPlayer));
    seat(tag, seatName_[0], 38, 92, lit);
  }
  if (lit) pill(s, status, mode == DisplayMode::GameOver ? pal().accent : pal().turn,
      mode == DisplayMode::GameOver ? pal().onAccent : pal().onTurn, 150);
  else pill(s, status, pal().fill, pal().textSecondary, 150);
  finish(menuLabels());
}

void LvglSigilDisplay::showCommander(const TurnHubProtocol::CommanderFlowPacket &page) {
  using TurnHubProtocol::CommanderStage;
  const CommanderFlowView v = commanderFlowView(page);
  lv_obj_t *s = frame("Cmd damage", page.sigilId);
  text(s, v.title, &th_eyebrow_13, pal().accent, M, 38, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  text(s, v.lines[0], &th_semi_16, pal().textSecondary, M, 60, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  text(s, v.lines[1], &th_semi_16, pal().textSecondary, M, 82, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  text(s, v.lines[2], &th_semi_20, pal().text, M, 110, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  if (v.lines[3][0] && page.stage >= CommanderStage::Confirm)
    text(s, v.lines[3], &th_semi_20, pal().text, M, 136, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  PadLabels l;
  const uint8_t up = 0, down = 1, left = 2, right = 3, select = 4;
  static_assert(static_cast<uint8_t>(Key::Select) == 4 && static_cast<uint8_t>(Key::Left) == 2, "key order");
  switch (page.stage) {
    case CommanderStage::Source:
      l.text[left] = "Prev"; l.text[right] = "Next"; l.text[select] = "Choose";
      l.text[up] = l.text[down] = "Cancel";
      break;
    case CommanderStage::Commander:
      l.text[left] = "C1"; l.text[right] = "C2"; l.text[select] = "Next"; l.text[up] = "Back";
      break;
    case CommanderStage::Amount:
      l.text[left] = "-1"; l.text[right] = "+1"; l.text[select] = "Next"; l.text[up] = "Back";
      break;
    case CommanderStage::Confirm:
    case CommanderStage::UndoConfirm:
      l.text[select] = page.stage == CommanderStage::UndoConfirm ? "Undo" : "Apply";
      l.text[up] = "Back"; l.text[down] = "Cancel";
      break;
    case CommanderStage::Result:
      l.text[select] = "Close";
      break;
    default:
      break;
  }
  l.accent[select] = l.text[select] != nullptr;
  finish(l);
}

// Atlas's profile picker: Up, Right and Down are the names on the page.
void LvglSigilDisplay::showPicker(const TurnHubProtocol::ProfilePickerPacket &page, uint8_t) {
  using namespace TurnHubProtocol;
  const bool confirm = page.mode == PickerMode::Confirm;
  lv_obj_t *s = frame(confirm ? "Join" : "Who plays?", page.sigilId);
  const char *notice = nullptr;
  switch (page.notice) {
    case PickerNotice::NeedsPhone: notice = "Sign in on your phone"; break;
    case PickerNotice::Unavailable: notice = "Not available"; break;
    case PickerNotice::TableFull: notice = "The table is full"; break;
    case PickerNotice::Failed: notice = "Try again"; break;
    default: break;
  }
  PadLabels l;
  const uint8_t left = static_cast<uint8_t>(Key::Left), select = static_cast<uint8_t>(Key::Select);
  if (confirm) {
    text(s, "Join as", &th_semi_16, pal().textSecondary, M, 50, W - 2 * M, LV_TEXT_ALIGN_CENTER);
    text(s, page.items[0].name, nameFont(page.items[0].name, W - 2 * M), pal().text, M, 76, W - 2 * M,
        LV_TEXT_ALIGN_CENTER);
    l.text[select] = "Join";
    l.accent[select] = true;
    l.text[left] = "Back";
  } else {
    text(s, "Choose a name below", &th_semi_16, pal().textSecondary, M, 50, W - 2 * M, LV_TEXT_ALIGN_CENTER);
    if (page.pageCount > 1) {
      char pages[20];
      snprintf(pages, sizeof(pages), "Page %u of %u", static_cast<unsigned>(page.page + 1),
          static_cast<unsigned>(page.pageCount));
      text(s, pages, &th_eyebrow_13, pal().textTertiary, M, 74, W - 2 * M, LV_TEXT_ALIGN_CENTER);
      l.text[select] = "More";
    }
    static const Key ROW_KEYS[] = {Key::Up, Key::Right, Key::Down};
    for (uint8_t i = 0; i < page.itemCount && i < PICKER_PAGE_ITEMS; ++i) {
      const uint8_t k = static_cast<uint8_t>(ROW_KEYS[i]);
      const uint8_t flags = page.items[i].flags;
      l.text[k] = page.items[i].name;
      l.note[k] = (flags & PICKER_ITEM_GUEST) ? "GUEST" : (flags & PICKER_ITEM_LOCKED) ? "SIGN-IN" :
          (flags & PICKER_ITEM_PLAYING) ? "SEATED" : nullptr;
    }
    l.text[left] = page.page > 0 ? "Back" : "Cancel";
  }
  if (notice) pill(s, notice, pal().warning, pal().bg, 120);
  finish(l);
}

}  // namespace TurnHubSigil

#endif
