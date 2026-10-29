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
extern const lv_image_dsc_t th_icon_home_24, th_icon_timer_24, th_icon_table_24, th_icon_sliders_24,
    th_icon_chevron_right_24, th_icon_chevron_left_24, th_icon_chevron_up_24, th_icon_chevron_down_24,
    th_icon_sparkle_24, th_icon_sigil_24, th_icon_qr_24, th_icon_trash_24, th_icon_link_24, th_icon_power_24,
    th_icon_download_24, th_icon_pass_24, th_icon_moon_24, th_icon_sun_24, th_icon_palette_24,
    th_icon_contrast_24, th_icon_close_24, th_icon_info_24, th_icon_hub_24, th_icon_flag_24, th_icon_pause_24,
    th_icon_play_24, th_icon_more_24, th_icon_plus_24, th_icon_refresh_24, th_icon_lock_24, th_icon_check_24,
    th_icon_users_24, th_icon_minus_24;
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

// The same mix as a plain 0xRRGGBB value (before any inversion).
uint32_t mixRgbHex(uint32_t a, uint32_t b, uint8_t t) {
  auto ch = [&](int shift) { return (((a >> shift & 0xFF) * t + (b >> shift & 0xFF) * (255 - t)) / 255) << shift; };
  return ch(16) | ch(8) | ch(0);
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

// A design-system icon (an A8 mask) tinted to the given colour.
lv_obj_t *icon(lv_obj_t *parent, const lv_image_dsc_t *src, int16_t x, int16_t y, uint32_t color) {
  lv_obj_t *img = lv_image_create(parent);
  lv_obj_remove_style_all(img);
  lv_image_set_src(img, src);
  lv_obj_set_pos(img, x, y);
  lv_obj_set_style_image_recolor(img, rgb(color), 0);
  lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  return img;
}

// The icon the app shows for each action, or nullptr for none.
const lv_image_dsc_t *actionIcon(TouchAction a) {
  switch (a) {
    case TouchAction::OpenHome: return &th_icon_home_24;
    case TouchAction::OpenGame: return &th_icon_timer_24;
    case TouchAction::OpenOverview: return &th_icon_table_24;
    case TouchAction::OpenSettings: return &th_icon_sliders_24;
    case TouchAction::OpenSetup: return &th_icon_sparkle_24;
    case TouchAction::Pair: case TouchAction::SetupPair: return &th_icon_sigil_24;
    case TouchAction::OpenQr: return &th_icon_qr_24;
    case TouchAction::ClearLobby: return &th_icon_trash_24;
    case TouchAction::UnpairSigils: return &th_icon_link_24;
    case TouchAction::FactoryResetAtlas: return &th_icon_power_24;
    case TouchAction::UsbDrive: return &th_icon_download_24;
    case TouchAction::MasterPass: case TouchAction::PassTurn: return &th_icon_pass_24;
    case TouchAction::ThemeGraphite: case TouchAction::SleepAtlas: return &th_icon_moon_24;
    case TouchAction::ThemeDaylight: return &th_icon_sun_24;
    case TouchAction::ThemeBrass: case TouchAction::OpenThemes: return &th_icon_palette_24;
    case TouchAction::ThemeContrast: case TouchAction::InvertDisplay: return &th_icon_contrast_24;
    case TouchAction::MoveEarlier: return &th_icon_chevron_up_24;
    case TouchAction::MoveLater: return &th_icon_chevron_down_24;
    case TouchAction::RemoveSeat: case TouchAction::PairReject: return &th_icon_close_24;
    case TouchAction::OpenInfo: return &th_icon_info_24;
    case TouchAction::OpenDevice: return &th_icon_hub_24;
    case TouchAction::EndMatch: case TouchAction::Concede: case TouchAction::ConfirmConcede: return &th_icon_flag_24;
    case TouchAction::Pause: return &th_icon_pause_24;
    case TouchAction::Resume: case TouchAction::StartGame: return &th_icon_play_24;
    case TouchAction::OpenTable: case TouchAction::OpenMenu: return &th_icon_more_24;
    case TouchAction::OpenSeat: case TouchAction::NewPlayer: return &th_icon_plus_24;
    case TouchAction::Rematch: case TouchAction::ResetTable: return &th_icon_refresh_24;
    case TouchAction::PairConfirm: case TouchAction::SetupDone: case TouchAction::NameDone: return &th_icon_check_24;
    case TouchAction::CloseScreen: case TouchAction::CancelConcede: case TouchAction::CancelCode:
    case TouchAction::CancelStart: return &th_icon_chevron_left_24;
    case TouchAction::SeatMore: return &th_icon_users_24;
    default: return nullptr;
  }
}

bool tabAction(const TouchButton &b) {
  return b.y >= ATLAS_SCREEN_HEIGHT - 48 && (b.action == TouchAction::OpenHome || b.action == TouchAction::OpenGame ||
      b.action == TouchAction::OpenOverview || b.action == TouchAction::OpenSettings);
}

bool keyAction(TouchAction a) {
  return a == TouchAction::NameKey || a == TouchAction::PinKey || a == TouchAction::PinBack || a == TouchAction::PinDone;
}

// Full-width buttons in the body are rows of an inset grouped list, as on the
// app's Settings and Players screens; the bottom row keeps real buttons.
bool listRow(const AtlasScreen &s, const TouchButton &b) {
  return !keyAction(b.action) && !tabAction(b) && b.w >= W - 24 && b.y < BUTTON_ROW_Y &&
      s.kind != ScreenKind::Player;
}

// The tint of a row's leading icon tile, like the coloured squares in a
// phone's Settings.
uint32_t rowTint(TouchAction a) {
  if (dangerAction(a)) return pal().critical;
  switch (a) {
    case TouchAction::OpenGame: case TouchAction::MasterPass: return pal().turn;
    case TouchAction::Pair: case TouchAction::OpenSetup: case TouchAction::ThemeBrass: return pal().accent;
    case TouchAction::OpenQr: case TouchAction::UsbDrive: case TouchAction::OpenOverview: case TouchAction::ThemeDaylight:
      return pal().info;
    case TouchAction::MoveEarlier: case TouchAction::MoveLater: return pal().good;
    default: return pal().textTertiary;
  }
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

// The title and detail line, and in a game the app's turn ring at the right:
// the clock in the middle of a ring that drains with the countdown (or a full
// ring with no countdown).
void drawHero(lv_obj_t *root, const AtlasScreen &s) {
  const bool clock = s.clock[0] != '\0';
  constexpr int16_t ring = 62;
  const int16_t x = 14;
  const int16_t w = W - x - (clock ? ring + 24 : 14);
  text(root, s.title, pal().ornament ? &th_cinzel_32 : &th_bold_28, pal().text, x, SCREEN_HERO_Y + 6, w);
  const bool notice = s.notice[0] != '\0';
  // A notice is marked in words by its leading arrow as well as its colour.
  char line[56];
  snprintf(line, sizeof(line), notice ? "> %s" : "%s", notice ? s.notice : s.detail);
  text(root, line, &th_semi_16, notice ? pal().accent : pal().textSecondary, x, SCREEN_HERO_Y + 42, w);
  if (!clock) return;
  const uint32_t ink = s.timerWarning ? pal().warning : pal().turn;
  lv_obj_t *arc = lv_arc_create(root);
  lv_obj_remove_style_all(arc);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(arc, ring, ring);
  lv_obj_set_pos(arc, W - 14 - ring, SCREEN_HERO_Y + 2);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_range(arc, 0, 1000);
  lv_arc_set_value(arc, s.timerPermille >= 0 ? s.timerPermille : 1000);
  lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, rgb(pal().fillStrong), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, rgb(ink), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  // A soft halo of the turn colour inside the ring, as behind the app's.
  lv_obj_t *halo = box(root, W - 14 - ring + 7, SCREEN_HERO_Y + 9, ring - 14, ring - 14);
  lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(halo, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(halo, rgb(s.timerWarning ? pal().surface3 : pal().turnSoft), 0);
  const lv_font_t *font = textWidth(s.clock, &th_semi_20) <= ring - 18 ? &th_semi_20 : &th_semi_16;
  text(root, s.clock, font, s.timerWarning ? pal().warning : pal().text, W - 14 - ring,
       SCREEN_HERO_Y + 2 + (ring - lv_font_get_line_height(font)) / 2, ring, LV_TEXT_ALIGN_CENTER);
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

// The app's bottom navigation bar: an icon over each label, the chosen tab's
// icon on a soft accent pill, a hairline along the top.
void drawTabBar(lv_obj_t *root, const AtlasScreen &s) {
  bool any = false;
  for (uint8_t i = 0; i < s.buttonCount; ++i) any = any || tabAction(s.buttons[i]);
  if (!any) return;
  lv_obj_t *bar = box(root, 0, H - 48, W, 48);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(bar, rgb(pal().surface1), 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_color(bar, rgb(pal().separator), 0);
  for (uint8_t i = 0; i < s.buttonCount; ++i) {
    const TouchButton &b = s.buttons[i];
    if (!tabAction(b)) continue;
    const bool pressed = s.pressed == b.action && s.pressedArg == b.arg;
    const uint32_t ink = b.selected ? pal().accent : pal().textSecondary;
    const int16_t pillW = 52, pillX = b.x + (b.w - pillW) / 2;
    if (b.selected || pressed) {
      lv_obj_t *indicator = box(bar, pillX, 3, pillW, 26);
      lv_obj_set_style_radius(indicator, 13, 0);
      lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(indicator, rgb(pressed ? pal().fillStrong : pal().accentSoft), 0);
    }
    if (const lv_image_dsc_t *glyph = actionIcon(b.action)) icon(bar, glyph, pillX + (pillW - 24) / 2, 4, ink);
    text(bar, b.label, &th_eyebrow_13, ink, b.x, 30, b.w, LV_TEXT_ALIGN_CENTER);
  }
}

// What a row says under its label, on the Home list.
const char *rowSubtitle(TouchAction a) {
  switch (a) {
    case TouchAction::OpenGame: return "Play the match";
    case TouchAction::OpenOverview: return "Everyone at a glance";
    case TouchAction::OpenSettings: return "Sigils, codes, theme";
    default: return "";
  }
}

// One grouped-list row: a tinted icon tile (or the player's initial), the
// label, and at the right its value, "Hold", a check for the shown choice or
// a chevron. top/bottom are the row's slice of the group card.
void drawListRow(lv_obj_t *group, const AtlasScreen &s, const TouchButton &b, int16_t top, int16_t bottom, bool first) {
  const bool pressed = s.pressed == b.action && s.pressedArg == b.arg;
  const int16_t w = b.w;
  if (pressed) {
    lv_obj_t *shade = box(group, 0, top, w, bottom - top);
    lv_obj_set_style_bg_opa(shade, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(shade, rgb(pal().fillStrong), 0);
  }
  if (!first) {
    lv_obj_t *rule = box(group, 56, top, w - 56, 1);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(rule, rgb(pal().separator), 0);
  }
  // "USB drive: on" reads as the label "USB drive" and the value "On";
  // "Casey (PIN)" as "Casey" with a lock.
  char label[32], value[12] = {};
  snprintf(label, sizeof(label), "%s", b.label);
  bool locked = false;
  if (char *colon = strstr(label, ": ")) {
    snprintf(value, sizeof(value), "%s", colon + 2);
    if (value[0] >= 'a' && value[0] <= 'z') value[0] -= 'a' - 'A';
    *colon = 0;
  }
  if (char *pin = strstr(label, " (PIN)")) {
    *pin = 0;
    locked = true;
  }
  const int16_t mid = b.y - (b.y - top) + (bottom - top) / 2;
  if (b.action == TouchAction::SeatProfile) {
    ScreenPlayer p;
    p.number = static_cast<uint8_t>(label[0] + b.arg);
    snprintf(p.name, sizeof(p.name), "%s", label);
    seatToken(group, p, 12, mid - 16, 32, false);
  } else if (const lv_image_dsc_t *glyph = actionIcon(b.action)) {
    lv_obj_t *tile = box(group, 12, mid - 16, 32, 32);
    lv_obj_set_style_radius(tile, 9, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    sheen(tile, rowTint(b.action));
    icon(tile, glyph, 4, 4, pal().bg);
  }
  const uint32_t ink = dangerAction(b.action) ? pal().critical : primaryAction(b.action) ? pal().accent : pal().text;
  const char *subtitle = rowSubtitle(b.action);
  int16_t right = w - 12;
  // Trailing mark.
  if (b.selected) {
    icon(group, &th_icon_check_24, right - 24, mid - 12, pal().accent);
    right -= 30;
  } else if (b.hold() && !pressed) {
    const int16_t tw = static_cast<int16_t>(textWidth("Hold", &th_semi_16));
    text(group, "Hold", &th_semi_16, pal().textSecondary, right - tw, mid - 10, tw);
    right -= tw + 8;
  } else if (!b.hold()) {
    icon(group, &th_icon_chevron_right_24, right - 22, mid - 12, pal().textTertiary);
    right -= 28;
  }
  if (value[0]) {
    const int16_t tw = static_cast<int16_t>(textWidth(value, &th_semi_16));
    text(group, value, &th_semi_16, pal().textSecondary, right - tw, mid - 10, tw);
    right -= tw + 8;
  }
  if (locked) {
    icon(group, &th_icon_lock_24, right - 24, mid - 12, pal().textSecondary);
    right -= 28;
  }
  char shown[32];
  if (pressed && b.hold() && s.holdSecondsLeft > 0) {
    snprintf(shown, sizeof(shown), "Hold %u s", static_cast<unsigned>(s.holdSecondsLeft));
  } else {
    snprintf(shown, sizeof(shown), "%s", label);
  }
  if (subtitle[0]) {
    text(group, shown, &th_semi_20, ink, 56, mid - 22, right - 56);
    text(group, subtitle, &th_semi_16, pal().textSecondary, 56, mid + 2, right - 56);
  } else {
    text(group, shown, &th_semi_20, ink, 56, mid - 12, right - 56);
  }
  if (pressed && b.hold()) {
    lv_obj_t *fill = box(group, 0, bottom - 4, w * s.holdPermille / 1000, 4);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, rgb(pal().focus), 0);
  }
}

// Runs of adjacent list rows become one rounded group card.
void drawLists(lv_obj_t *root, const AtlasScreen &s) {
  for (uint8_t i = 0; i < s.buttonCount;) {
    if (!listRow(s, s.buttons[i])) { ++i; continue; }
    uint8_t j = i;
    while (j + 1 < s.buttonCount && listRow(s, s.buttons[j + 1]) &&
           s.buttons[j + 1].y - (s.buttons[j].y + s.buttons[j].h) <= 12) ++j;
    const TouchButton &a = s.buttons[i], &z = s.buttons[j];
    lv_obj_t *group = box(root, a.x, a.y, a.w, z.y + z.h - a.y);
    card(group, pal().light ? pal().surface1 : pal().surface2, pal().separator, 16);
    lv_obj_set_style_clip_corner(group, true, 0);
    lift(group);
    for (uint8_t k = i; k <= j; ++k) {
      const TouchButton &b = s.buttons[k];
      const int16_t top = k == i ? 0 : (s.buttons[k - 1].y + s.buttons[k - 1].h + b.y) / 2 - a.y;
      const int16_t bottom = k == j ? z.y + z.h - a.y : (b.y + b.h + s.buttons[k + 1].y) / 2 - a.y;
      TouchButton local = b;
      local.y = b.y - a.y;
      drawListRow(group, s, local, top, bottom, k == i);
    }
    i = j + 1;
  }
}

// A real button (the bottom row, keys and the player screen's steps): the
// app's filled primary, the turn colour for Hold to pass, tonal accent for
// the rest, and the critical colour for anything that ends or erases.
void drawButton(lv_obj_t *root, const AtlasScreen &s, const TouchButton &b) {
  const bool pressed = s.pressed == b.action && s.pressedArg == b.arg;
  // Keyboard keys stay flat (no shadow): thirty of them redraw per key press.
  const bool key = keyAction(b.action);
  const bool primary = primaryAction(b.action);
  const bool danger = dangerAction(b.action);
  // Hold to pass wears the turn colour, as in the app.
  const bool turn = b.action == TouchAction::PassTurn;
  const bool tonal = !key && !primary && !turn && !danger;
  lv_obj_t *o = box(root, b.x, b.y, b.w, b.h);
  const uint32_t base = turn ? (pressed ? pal().turnSoft : pal().turn)
      : pressed ? (primary ? pal().accentPressed : pal().fillStrong)
      : primary ? pal().accent : key ? pal().surface3 : tonal ? pal().accentSoft : pal().surface2;
  const uint32_t frame = b.selected || pressed ? pal().focus : danger ? pal().critical
      : tonal ? mixRgbHex(pal().accent, pal().accentSoft, 110) : pal().separatorStrong;
  card(o, base, frame, key ? 12 : 16);
  lv_obj_set_style_border_width(o, (primary || turn) && !pressed ? 0 : 1, 0);
  if (!pressed) {
    sheen(o, base);
    if (!key) lift(o);
  }
  // Pressed and the shown choice are also marked by a heavier frame.
  if (pressed || b.selected) lv_obj_set_style_border_width(o, 3, 0);
  const uint32_t ink = turn ? (pressed ? pal().text : pal().onTurn) : primary ? pal().onAccent
      : danger ? pal().critical : tonal ? pal().accent : pal().text;
  char label[32];
  if (pressed && b.hold() && s.holdSecondsLeft > 0) {
    snprintf(label, sizeof(label), "Hold %u s", static_cast<unsigned>(s.holdSecondsLeft));
  } else {
    snprintf(label, sizeof(label), "%s", b.label);
  }
  // "hold" under a hold button, unless its label already says so.
  const bool saysHold = strncmp(b.label, "Hold", 4) == 0;
  const char *caption = b.selected && !pressed ? "shown" : (b.hold() && !pressed && !saysHold ? "hold" : "");
  // The largest type that fits on one line; two lines of the small one
  // otherwise. An icon leads the label when there is room for both.
  const lv_image_dsc_t *glyph = key ? nullptr : actionIcon(b.action);
  int16_t room = b.w - 16;
  const lv_font_t *font = textWidth(label, &th_semi_20) <= room ? &th_semi_20 : &th_semi_16;
  if (glyph && textWidth(label, font) + 30 > room) font = &th_semi_16;
  if (glyph && textWidth(label, font) + 30 > room) glyph = nullptr;
  if (!glyph && textWidth(label, &th_semi_20) <= room) font = &th_semi_20;
  if (glyph) room -= 30;
  lv_obj_t *col = box(o, 0, 0, b.w, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *line = box(col, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(line, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(line, 6, 0);
  if (glyph) icon(line, glyph, 0, 0, ink);
  lv_obj_t *l = lv_label_create(line);
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

// PowerPoint-style turn change: a card in the turn colour pushes in from the
// right with the new player's name, rests, and slides off to the left. It
// lives on LVGL's top layer, so the per-second redraws underneath leave it
// alone. The hero title says the same thing, so the banner is never the only
// place the turn shows.
uint8_t bannerTurn = 0;

void slideX(void *obj, int32_t v) { lv_obj_set_x(static_cast<lv_obj_t *>(obj), v); }

void showTurnBanner(const ScreenPlayer &p, uint16_t round) {
  lv_obj_t *layer = lv_layer_top();
  lv_obj_clean(layer);
  constexpr int16_t bw = W - 24, bh = 96;
  lv_obj_t *banner = box(layer, W, (H - bh) / 2 - 20, bw, bh);
  card(banner, pal().turn, pal().turn, 20);
  sheen(banner, pal().turn);
  lift(banner);
  lv_obj_set_style_shadow_width(banner, 28, 0);
  seatToken(banner, p, 16, (bh - 48) / 2, 48, false);
  lv_obj_set_style_border_width(lv_obj_get_child(banner, 0), 2, 0);
  lv_obj_set_style_border_color(lv_obj_get_child(banner, 0), rgb(pal().onTurn), 0);
  char line[40];
  snprintf(line, sizeof(line), "%s's turn", p.name);
  text(banner, line, &th_bold_28, pal().onTurn, 76, 20, bw - 88);
  if (round > 0) snprintf(line, sizeof(line), "ROUND %u", static_cast<unsigned>(round));
  else snprintf(line, sizeof(line), "YOUR MOVE");
  text(banner, line, &th_eyebrow_13, pal().onTurn, 77, 58, bw - 88);
  lv_anim_t in;
  lv_anim_init(&in);
  lv_anim_set_var(&in, banner);
  lv_anim_set_exec_cb(&in, slideX);
  lv_anim_set_values(&in, W, 12);
  lv_anim_set_duration(&in, 320);
  lv_anim_set_path_cb(&in, lv_anim_path_ease_out);
  lv_anim_start(&in);
  lv_anim_t out;
  lv_anim_init(&out);
  lv_anim_set_var(&out, banner);
  lv_anim_set_exec_cb(&out, slideX);
  lv_anim_set_values(&out, 12, -W);
  lv_anim_set_delay(&out, 320 + 1100);
  lv_anim_set_duration(&out, 280);
  lv_anim_set_path_cb(&out, lv_anim_path_ease_in);
  lv_anim_set_completed_cb(&out, lv_obj_delete_anim_completed_cb);
  lv_anim_start(&out);
}

void noteTurn(const AtlasScreen &s) {
  if (!s.showLife || s.kind == ScreenKind::Overview) return;
  for (uint8_t i = 0; i < s.playerCount; ++i) {
    if (!(s.players[i].flags & CHIP_ACTIVE)) continue;
    const uint8_t turn = s.players[i].number;
    if (bannerTurn != 0 && turn != bannerTurn) showTurnBanner(s.players[i], s.round);
    bannerTurn = turn;
    return;
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
  drawLists(root, s);
  for (uint8_t i = 0; i < s.buttonCount; ++i) {
    const TouchButton &b = s.buttons[i];
    if (!tabAction(b) && !listRow(s, b)) drawButton(root, s, b);
  }
  drawTabBar(root, s);
  // A new match starts without a banner; only a pass shows one.
  if (!s.showLife) bannerTurn = 0;
  noteTurn(s);
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
