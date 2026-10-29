// LVGL drawing of the touch_controls.cpp screen model in the app's style:
// design tokens, flat cards, seat-coloured player tiles. Presentation only:
// it draws an AtlasScreen and reads nothing else. Hardware-free, so the S3
// firmware (atlas_display_s3.cpp) and the preview renderer
// (tests/host/render-atlas-lvgl.sh) share it.

#if defined(TURNHUB_BOARD_ES3C40P) || defined(TURNHUB_SCREEN_PREVIEW)

#include "atlas_screen_lvgl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

extern "C" {
extern const lv_font_t th_eyebrow_13;
extern const lv_font_t th_semi_16;
extern const lv_font_t th_semi_20;
extern const lv_font_t th_bold_28;
extern const lv_font_t th_cinzel_32;
extern const lv_font_t th_num_40;
extern const lv_font_t th_num_64;
}

namespace TurnHubAtlas {
namespace {

constexpr int16_t W = ATLAS_SCREEN_WIDTH;
constexpr int16_t H = ATLAS_SCREEN_HEIGHT;

ScreenStyle style = {TurnHubTheme::Id::Graphite, false};

const TurnHubTheme::Palette &pal() { return TurnHubTheme::palette(style.theme); }

lv_color_t rgb(uint32_t c) {
  if (style.inverted) c = 0xFFFFFF - (c & 0xFFFFFF);
  return lv_color_hex(c);
}

lv_color_t mixRgb(uint32_t a, uint32_t b, uint8_t t) {
  return lv_color_mix(rgb(b), rgb(a), t);
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

// The app's elevation 2: a soft drop shadow under cards and buttons.
void lift(lv_obj_t *o) {
  lv_obj_set_style_shadow_width(o, 14, 0);
  lv_obj_set_style_shadow_offset_y(o, 4, 0);
  lv_obj_set_style_shadow_spread(o, -4, 0);
  lv_obj_set_style_shadow_opa(o, pal().light ? LV_OPA_20 : LV_OPA_50, 0);
  lv_obj_set_style_shadow_color(o, lv_color_black(), 0);
}

// An app card: a flat surface, hairline frame and rounded corners. Large
// areas stay flat because RGB565 bands visibly on long gradients.
void card(lv_obj_t *o, uint32_t fill, uint32_t frame, int16_t radius = 16) {
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, rgb(fill), 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_border_color(o, rgb(frame), 0);
}

// Buttons keep a gentle top sheen (short enough not to band).
void sheen(lv_obj_t *o, uint32_t base) {
  lv_obj_set_style_bg_color(o, mixRgb(base, 0xFFFFFF, pal().light ? 18 : 26), 0);
  lv_obj_set_style_bg_grad_color(o, rgb(base), 0);
  lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
}

// One line, cut with an ellipsis.
lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *font, uint32_t color,
               int16_t x, int16_t y, int16_t w, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
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

int32_t textWidth(const char *s, const lv_font_t *font) {
  lv_point_t size;
  lv_text_get_size(&size, s, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}

// Life totals use the digit-only numeral fonts; anything else is spelled.
bool digitsOnly(const char *s) {
  for (; *s; ++s) if ((*s < '0' || *s > '9') && *s != '-') return false;
  return true;
}

// Seat hues follow the app's avatar colours (Palette.avatarColor).
uint32_t hsl(float h, float s, float l) {
  const float c = (1 - fabsf(2 * l - 1)) * s, hp = h / 60, x = c * (1 - fabsf(fmodf(hp, 2.0f) - 1)), m = l - c / 2;
  float r = 0, g = 0, b = 0;
  if (hp < 1) { r = c; g = x; } else if (hp < 2) { r = x; g = c; } else if (hp < 3) { g = c; b = x; }
  else if (hp < 4) { g = x; b = c; } else if (hp < 5) { r = x; b = c; } else { r = c; b = x; }
  auto to = [&](float v) { return static_cast<uint32_t>(lroundf((v + m) * 255)); };
  return to(r) << 16 | to(g) << 8 | to(b);
}

uint32_t seatColor(uint8_t number) {
  static const float hues[] = {38, 168, 12, 205, 95, 280, 330, 60};
  const bool contrast = style.theme == TurnHubTheme::Id::Contrast;
  const float sat = contrast ? 0.0f : pal().ornament ? 0.42f : 0.5f;
  const float light = contrast ? 0.18f : pal().ornament ? 0.34f : pal().light ? 0.44f : 0.38f;
  return hsl(hues[(number + 7) % 8], sat, light);
}

const char *chipTag(const ScreenPlayer &p) {
  return (p.flags & CHIP_WINNER) ? "WINNER" : (p.flags & CHIP_OUT) ? "OUT" : (p.flags & CHIP_ACTIVE) ? "TURN"
      : (p.flags & CHIP_WAITING) ? "CONFIRM" : (p.flags & CHIP_STARTER) ? "STARTS" : "";
}

bool codeLayout(const AtlasScreen &s) {
  return s.kind == ScreenKind::Qr || s.kind == ScreenKind::Code || s.kind == ScreenKind::PairCode;
}

bool buttonsUseBody(const AtlasScreen &s) {
  return s.kind == ScreenKind::Table || s.kind == ScreenKind::Menu || s.kind == ScreenKind::Player ||
      s.kind == ScreenKind::Device || s.kind == ScreenKind::Themes || s.kind == ScreenKind::Seat ||
      s.kind == ScreenKind::Name || s.kind == ScreenKind::Pin;
}

bool primaryAction(TouchAction a) {
  switch (a) {
    case TouchAction::StartGame: case TouchAction::Rematch:
    case TouchAction::Resume: case TouchAction::Pair: case TouchAction::SetupPair:
    case TouchAction::PairConfirm: case TouchAction::NameDone:
    case TouchAction::PinDone:
      return true;
    default:
      return false;
  }
}

// Actions that end, remove or erase something: spelled in the critical colour
// (and they are holds or ask twice, so colour is never the only warning).
bool dangerAction(TouchAction a) {
  switch (a) {
    case TouchAction::EndMatch: case TouchAction::ClearLobby: case TouchAction::RemoveSeat:
    case TouchAction::Concede: case TouchAction::ConfirmConcede: case TouchAction::UnpairSigils:
    case TouchAction::FactoryResetAtlas: case TouchAction::PairReject:
      return true;
    default:
      return false;
  }
}

// A rounded tag, like the app's status pills.
lv_obj_t *pill(lv_obj_t *parent, const char *s, uint32_t fill, uint32_t ink) {
  lv_obj_t *o = box(parent, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(o, 8, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, rgb(fill), 0);
  lv_obj_set_style_pad_hor(o, 8, 0);
  lv_obj_set_style_pad_ver(o, 3, 0);
  lv_obj_t *l = lv_label_create(o);
  lv_obj_remove_style_all(l);
  lv_label_set_text(l, s);
  lv_obj_set_style_text_font(l, &th_eyebrow_13, 0);
  lv_obj_set_style_text_color(l, rgb(ink), 0);
  return o;
}

// A filled disc in the seat's hue holding the player's initial.
void seatToken(lv_obj_t *parent, const ScreenPlayer &p, int16_t x, int16_t y, int16_t side, bool ring) {
  lv_obj_t *disc = box(parent, x, y, side, side);
  lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(disc, rgb(seatColor(p.number)), 0);
  if (ring) {
    lv_obj_set_style_border_width(disc, 2, 0);
    lv_obj_set_style_border_color(disc, rgb(pal().turn), 0);
  }
  char initial[2] = {p.name[0] ? p.name[0] : '?', 0};
  lv_obj_t *l = lv_label_create(disc);
  lv_obj_remove_style_all(l);
  lv_label_set_text(l, initial);
  lv_obj_set_style_text_font(l, side >= 36 ? &th_semi_20 : &th_semi_16, 0);
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_obj_center(l);
}

// The cell at the header's right: an outline with its charge as a fill, the
// percent beside it, "LOW" in words as well as the warning colour, and a
// lightning bolt over the cell while charging. Returns the width it took.
int16_t drawBattery(lv_obj_t *bar, const AtlasScreen &s) {
  if (s.batteryPercent < 0) return 0;
  constexpr int16_t bodyW = 24, bodyH = 12, nubW = 3;
  const int16_t x = W - 14 - bodyW - nubW;
  const int16_t y = (SCREEN_HEADER_H - bodyH) / 2;
  const uint32_t ink = s.batteryLow ? pal().warning : pal().textSecondary;
  lv_obj_t *body = box(bar, x, y, bodyW, bodyH);
  lv_obj_set_style_radius(body, 3, 0);
  lv_obj_set_style_border_width(body, 1, 0);
  lv_obj_set_style_border_color(body, rgb(ink), 0);
  lv_obj_t *nub = box(bar, x + bodyW, y + 3, nubW, bodyH - 6);
  lv_obj_set_style_radius(nub, 1, 0);
  lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(nub, rgb(ink), 0);
  const int16_t fillW = (bodyW - 4) * s.batteryPercent / 100;
  if (fillW > 0) {
    lv_obj_t *fill = box(body, 2, 2, fillW, bodyH - 4);
    lv_obj_set_style_radius(fill, 1, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, rgb(s.batteryLow ? pal().warning : s.batteryCharging ? pal().good : ink), 0);
  }
  if (s.batteryCharging) {
    static const lv_point_precise_t bolt[] = {{14, 0}, {8, 7}, {13, 7}, {9, 14}};
    lv_obj_t *line = lv_line_create(bar);
    lv_obj_remove_style_all(line);
    lv_line_set_points(line, bolt, 4);
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_style_line_color(line, rgb(pal().text), 0);
    lv_obj_set_pos(line, x + 1, y - 1);
  }
  char label[12];
  snprintf(label, sizeof(label), s.batteryLow ? "LOW %d%%" : "%d%%", static_cast<int>(s.batteryPercent));
  const int16_t labelW = static_cast<int16_t>(textWidth(label, &th_semi_16));
  text(bar, label, &th_semi_16, ink, x - 6 - labelW, 7, labelW, LV_TEXT_ALIGN_RIGHT);
  return static_cast<int16_t>(bodyW + nubW + 6 + labelW + 12);
}

void drawHeader(lv_obj_t *root, const AtlasScreen &s) {
  lv_obj_t *bar = box(root, 0, 0, W, SCREEN_HEADER_H);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_color(bar, rgb(pal().separator), 0);
  text(bar, s.badge, &th_eyebrow_13, pal().accent, 14, 10, 110);
  char right[64] = {};
  size_t n = 0;
  if (s.round > 0) n += snprintf(right + n, sizeof(right) - n, "Round %u  ", static_cast<unsigned>(s.round));
  if (s.gameClock[0]) n += snprintf(right + n, sizeof(right) - n, "%s  ", s.gameClock);
  if (s.sdOnUsb) n += snprintf(right + n, sizeof(right) - n, "USB DRIVE  ");
  if (s.sdMissing) n += snprintf(right + n, sizeof(right) - n, "NO SD  ");
  if (s.update != TurnHub::UpdateKind::None) n += snprintf(right + n, sizeof(right) - n, "Update  ");
  snprintf(right + n, sizeof(right) - n, "Sigils %u", static_cast<unsigned>(s.sigilsOnline));
  const int16_t battery = drawBattery(bar, s);
  text(bar, right, &th_semi_16, s.sdMissing ? pal().warning : pal().textSecondary, 110, 7, W - 124 - battery,
       LV_TEXT_ALIGN_RIGHT);
}

void drawHero(lv_obj_t *root, const AtlasScreen &s) {
  const bool clock = s.clock[0] != '\0';
  const int16_t x = 14;
  const int16_t w = W - x - (clock ? 110 : 14);
  text(root, s.title, pal().ornament ? &th_cinzel_32 : &th_bold_28, pal().text, x, SCREEN_HERO_Y + 4, w);
  const bool notice = s.notice[0] != '\0';
  // A notice is marked in words by its leading arrow as well as its colour.
  char line[56];
  snprintf(line, sizeof(line), notice ? "> %s" : "%s", notice ? s.notice : s.detail);
  text(root, line, &th_semi_16, notice ? pal().accent : pal().textSecondary, x, SCREEN_HERO_Y + 40, w);
  if (!clock) return;
  // The turn clock and its countdown bar.
  const uint32_t ink = s.timerWarning ? pal().warning : pal().turn;
  text(root, s.clock, &th_bold_28, ink, W - 110, SCREEN_HERO_Y + 4, 96, LV_TEXT_ALIGN_RIGHT);
  if (s.timerPermille >= 0) {
    lv_obj_t *track = box(root, W - 110, SCREEN_HERO_Y + 44, 96, 8);
    lv_obj_set_style_radius(track, 4, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(track, rgb(pal().fill), 0);
    lv_obj_t *fill = box(track, 0, 0, 96 * s.timerPermille / 1000, 8);
    lv_obj_set_style_radius(fill, 4, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, rgb(ink), 0);
  }
}

// A player tile, like the app's seat panels: token and name in the top band
// (it opens the player's screen), the life total in the middle with faint
// - and + marking the halves that change it, and a status pill. The turn is
// the one thing that glows.
void drawChip(lv_obj_t *root, const ScreenPlayer &p, bool showLife, int16_t x, int16_t y, int16_t w, int16_t h, bool readOnly = false) {
  const bool active = p.flags & CHIP_ACTIVE;
  const bool out = p.flags & CHIP_OUT;
  const bool winner = p.flags & CHIP_WINNER;
  const bool glow = active || winner;
  lv_obj_t *tile = box(root, x, y, w, h);
  card(tile, glow ? pal().turnSoft : pal().surface2, glow ? pal().turn : pal().separator);
  if (glow) lv_obj_set_style_border_width(tile, 2, 0);
  lift(tile);
  const uint32_t ink = out ? pal().textTertiary : pal().text;
  const bool tall = h >= 90;
  const int16_t side = showLife ? 24 : (tall ? 36 : 28);
  const int16_t bandY = showLife ? (CHIP_NAME_BAND_H - side) / 2 : (tall ? 10 : 6);
  seatToken(tile, p, 8, bandY, side, glow);
  const int16_t nameX = 8 + side + 8;
  const char *tag = chipTag(p);
  // In a game the pill rides the name band's right end; in the lobby, the foot.
  int16_t pillW = 0;
  if (tag[0] && showLife) {
    const bool waiting = p.flags & CHIP_WAITING;
    lv_obj_t *tagPill = pill(tile, tag, glow ? pal().turn : waiting ? pal().accent : pal().surface3,
                             glow ? pal().onTurn : waiting ? pal().onAccent : pal().textSecondary);
    lv_obj_align(tagPill, LV_ALIGN_TOP_RIGHT, -8, (CHIP_NAME_BAND_H - 22) / 2);
    pillW = textWidth(tag, &th_eyebrow_13) + 24;
  }
  const lv_font_t *nameFont = showLife && (!tall || w < 200) ? &th_semi_16 : &th_semi_20;
  const int16_t nameY = showLife ? (CHIP_NAME_BAND_H - lv_font_get_line_height(nameFont)) / 2 : (tall ? 14 : 8);
  lv_obj_t *name = text(tile, p.name, nameFont, ink, nameX, nameY, w - nameX - 8 - pillW);
  if (out) lv_obj_set_style_text_decor(name, LV_TEXT_DECOR_STRIKETHROUGH, 0);
  if (!showLife) {
    if (!tag[0]) return;
    const bool waiting = p.flags & CHIP_WAITING;
    lv_obj_t *tagPill = pill(tile, tag, waiting ? pal().accent : pal().surface3, waiting ? pal().onAccent : pal().textSecondary);
    lv_obj_set_pos(tagPill, 10, h - 30);
    return;
  }
  // Life: centred in the area under the band, between - and +.
  const int16_t lowerY = CHIP_NAME_BAND_H, lowerH = h - CHIP_NAME_BAND_H;
  char life[12];
  snprintf(life, sizeof(life), "%ld", static_cast<long>(p.life));
  const lv_font_t *font = lowerH >= 100 && w >= 140 ? &th_num_64 : lowerH >= 46 ? &th_num_40 : &th_bold_28;
  const uint32_t lifeInk = out ? pal().textTertiary : (active ? pal().turn : pal().text);
  const int16_t lh = lv_font_get_line_height(font);
  const int16_t timeRoom = tall && p.turnTime[0] ? 20 : 0;
  text(tile, life, font, lifeInk, 0, lowerY + (lowerH - timeRoom - lh) / 2, w, LV_TEXT_ALIGN_CENTER);
  if (!out && !readOnly) {
    const int16_t signY = lowerY + (lowerH - timeRoom - lv_font_get_line_height(&th_bold_28)) / 2;
    text(tile, "-", &th_bold_28, pal().textTertiary, 12, signY, 24);
    text(tile, "+", &th_bold_28, pal().textTertiary, w - 30, signY, 24, LV_TEXT_ALIGN_RIGHT);
  }
  if (timeRoom) text(tile, p.turnTime, &th_semi_16, pal().textSecondary, 0, h - 24, w, LV_TEXT_ALIGN_CENTER);
}

// Body lines in a column: each wraps to the width and pushes the next down.
void drawLines(lv_obj_t *parent, const AtlasScreen &s, int16_t x, int16_t y, int16_t w) {
  if (s.lineCount == 0) return;
  lv_obj_t *col = box(parent, x, y, w, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 6, 0);
  for (uint8_t i = 0; i < s.lineCount; ++i) {
    const bool warning = strstr(s.lines[i], "NOT INSERTED") != nullptr;
    lv_obj_t *l = lv_label_create(col);
    lv_obj_remove_style_all(l);
    lv_label_set_text(l, s.lines[i]);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_font(l, &th_semi_16, 0);
    lv_obj_set_style_text_line_space(l, 2, 0);
    lv_obj_set_style_text_color(l, rgb(warning ? pal().critical : pal().text), 0);
  }
}

void drawQr(lv_obj_t *root, const char *data, int16_t x, int16_t y, int16_t size) {
  lv_obj_t *frame = box(root, x, y, size, size);
  lv_obj_set_style_radius(frame, 12, 0);
  lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
  // Always dark on white, whatever the theme, so phones can scan it.
  lv_obj_set_style_bg_color(frame, lv_color_white(), 0);
  lift(frame);
  lv_obj_t *qr = lv_qrcode_create(frame);
  lv_qrcode_set_size(qr, size - 20);
  lv_qrcode_set_dark_color(qr, lv_color_black());
  lv_qrcode_set_light_color(qr, lv_color_white());
  lv_qrcode_update(qr, data, strlen(data));
  lv_obj_center(qr);
}

void drawBody(lv_obj_t *root, const AtlasScreen &s) {
  if (codeLayout(s)) {
    // Code on top, the QR code centred under it, then its caption and lines.
    int16_t y = SCREEN_BODY_Y;
    if (s.code[0]) {
      text(root, s.code, digitsOnly(s.code) ? &th_num_64 : &th_bold_28, pal().accent, 12, y - 6, W - 24,
           LV_TEXT_ALIGN_CENTER);
      y += 76;
    }
    if (s.qr[0]) {
      int16_t size = BUTTON_ROW_Y - 12 - y - 26 - s.lineCount * 24;
      if (size > W - 80) size = W - 80;
      drawQr(root, s.qr, (W - size) / 2, y, size);
      y += size + 6;
      text(root, s.qrCaption, &th_semi_16, pal().textSecondary, 12, y, W - 24, LV_TEXT_ALIGN_CENTER);
      y += 26;
    }
    drawLines(root, s, 16, y, W - 32);
    return;
  }
  if (s.kind == ScreenKind::Home || buttonsUseBody(s)) return;
  for (uint8_t i = 0; i < s.playerCount; ++i) {
    int16_t x, y, w, h;
    screenChipCell(i, s.playerCount, x, y, w, h);
    drawChip(root, s.players[i], s.showLife, x, y, w, h, s.kind == ScreenKind::Overview);
  }
  if (s.playerCount > 0) return;
  const int16_t bodyH = BUTTON_ROW_Y - 8 - SCREEN_BODY_Y;
  if (s.lineCount == 0 && !s.qr[0]) return;
  // A card holding the lines, and any QR code centred under them.
  lv_obj_t *plate = box(root, 8, SCREEN_BODY_Y, W - 16, s.qr[0] ? bodyH : LV_SIZE_CONTENT);
  card(plate, pal().surface1, pal().separator);
  lv_obj_set_style_pad_all(plate, 14, 0);
  lv_obj_set_style_pad_bottom(plate, 16, 0);
  drawLines(plate, s, 0, 0, W - 16 - 28);
  if (!s.qr[0]) return;
  const int16_t top = 16 + s.lineCount * 24;
  int16_t size = bodyH - 28 - top;
  if (size > W - 96) size = W - 96;
  if (size >= 96) drawQr(plate, s.qr, (W - 16 - 28 - size) / 2, top, size);
}

void drawButton(lv_obj_t *root, const AtlasScreen &s, const TouchButton &b) {
    if (b.y == 432 && (b.action == TouchAction::OpenHome || b.action == TouchAction::OpenGame ||
        b.action == TouchAction::OpenOverview || b.action == TouchAction::OpenSettings)) {
    auto *tab=box(root,b.x,b.y,b.w,b.h);
    card(tab,b.selected ? pal().accentSoft : pal().surface1,pal().separator,0);
    if(b.selected) { auto *mark=box(tab,16,0,b.w-32,3); card(mark,pal().accent,pal().accent,1); }
    text(tab,b.label,&th_eyebrow_13,b.selected ? pal().accent : pal().textSecondary,0,18,b.w,LV_TEXT_ALIGN_CENTER);
    return;
  }

  const bool pressed = s.pressed == b.action && s.pressedArg == b.arg;
  // Keyboard keys stay flat (no shadow): thirty of them redraw per key press.
  const bool key = b.action == TouchAction::NameKey ||
      b.action == TouchAction::PinKey || b.action == TouchAction::PinBack || b.action == TouchAction::PinDone;
  const bool primary = primaryAction(b.action);
  const bool danger = dangerAction(b.action);
  // Hold to pass wears the turn colour, as in the app.
  const bool turn = b.action == TouchAction::PassTurn;
  lv_obj_t *o = box(root, b.x, b.y, b.w, b.h);
  const uint32_t base = turn ? (pressed ? pal().turnSoft : pal().turn)
      : pressed ? (primary ? pal().accentPressed : pal().fillStrong)
      : primary ? pal().accent : pal().surface3;
  card(o, base, b.selected || pressed ? pal().focus : danger ? pal().critical : pal().separatorStrong, 14);
  lv_obj_set_style_border_width(o, (primary || turn) && !pressed ? 0 : 1, 0);
  if (!pressed) {
    sheen(o, base);
    if (!key) lift(o);
  }
  // Pressed and the shown choice are also marked by a heavier frame.
  if (pressed || b.selected) lv_obj_set_style_border_width(o, 3, 0);
  const uint32_t ink = turn ? (pressed ? pal().text : pal().onTurn) : primary ? pal().onAccent
      : danger ? pal().critical : pal().text;
  char label[32];
  if (pressed && b.hold() && s.holdSecondsLeft > 0) {
    snprintf(label, sizeof(label), "Hold %u s", static_cast<unsigned>(s.holdSecondsLeft));
  } else {
    snprintf(label, sizeof(label), "%s", b.label);
  }
  // "hold" under a hold button, unless its label already says so.
  const bool saysHold = strncmp(b.label, "Hold", 4) == 0;
  const char *caption = b.selected && !pressed ? "shown" : (b.hold() && !pressed && !saysHold ? "hold" : "");
  // The largest type that fits on one line; two lines of the small one otherwise.
  const int16_t room = b.w - 16;
  const lv_font_t *font = textWidth(label, &th_semi_20) <= room ? &th_semi_20 : &th_semi_16;
  lv_obj_t *col = box(o, 0, 0, b.w, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *l = lv_label_create(col);
  lv_obj_remove_style_all(l);
  lv_label_set_text(l, label);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, rgb(ink), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  if (textWidth(label, font) > room) {
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, room);
  }
  if (caption[0]) {
    lv_obj_t *c = lv_label_create(col);
    lv_obj_remove_style_all(c);
    lv_label_set_text(c, caption);
    lv_obj_set_style_text_font(c, &th_eyebrow_13, 0);
    lv_obj_set_style_text_color(c, rgb(turn ? pal().onTurn : primary ? pal().onAccent : pal().textSecondary), 0);
  }
  lv_obj_align(col, LV_ALIGN_CENTER, 0, 0);
  if (pressed && b.hold()) {
    lv_obj_t *track = box(o, 12, b.h - 12, b.w - 24, 6);
    lv_obj_set_style_radius(track, 3, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(track, lv_color_black(), 0);
    lv_obj_t *fill = box(track, 0, 0, (b.w - 24) * s.holdPermille / 1000, 6);
    lv_obj_set_style_radius(fill, 3, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, rgb(pal().focus), 0);
  }
}

}  // namespace

void renderAtlasScreen(const AtlasScreen &s, const ScreenStyle &chosen) {
  style = chosen;
  lv_obj_t *root = lv_screen_active();
  lv_obj_clean(root);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(root, rgb(pal().bg), 0);
  lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_NONE, 0);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  drawHeader(root, s);
  drawHero(root, s);
  drawBody(root, s);
  for (uint8_t i = 0; i < s.buttonCount; ++i) drawButton(root, s, s.buttons[i]);
}

void renderAtlasSplash(const ScreenStyle &chosen, const char *firmware) {
  style = chosen;
  lv_obj_t *root = lv_screen_active();
  lv_obj_clean(root);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(root, rgb(pal().bg), 0);
  text(root, "TurnHub", &th_cinzel_32, pal().accent, 0, H / 2 - 40, W, LV_TEXT_ALIGN_CENTER);
  char version[40];
  snprintf(version, sizeof(version), "Atlas %s", firmware);
  text(root, version, &th_semi_16, pal().textSecondary, 0, H / 2 + 6, W, LV_TEXT_ALIGN_CENTER);
}


}  // namespace TurnHubAtlas

#endif
