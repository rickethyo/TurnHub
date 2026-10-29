// The LVGL Sigil screen (sigil_display_lvgl.h). Presentation only: every
// screen is rebuilt from the latest snapshot Atlas sent, and the on-screen
// keys are labelled from the menu view; pressing them is main.cpp's job.

#if defined(TURNHUB_SIGIL_LVGL) || defined(TURNHUB_SCREEN_PREVIEW)

#include "sigil_display_lvgl.h"

#include <math.h>
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
extern const lv_image_dsc_t th_icon_home_24, th_icon_timer_24, th_icon_table_24, th_icon_sliders_24,
    th_icon_chevron_right_24, th_icon_chevron_left_24, th_icon_pause_24, th_icon_play_24, th_icon_pass_24,
    th_icon_more_24, th_icon_hub_24, th_icon_palette_24, th_icon_crown_24, th_icon_flag_24, th_icon_check_24,
    th_icon_close_24, th_icon_minus_24, th_icon_plus_24, th_icon_sigil_24, th_icon_link_24, th_icon_power_24,
    th_icon_users_24, th_icon_contrast_24, th_icon_refresh_24;
}

namespace TurnHubSigil {
namespace {

constexpr int16_t W = SIGIL_SCREEN_W;
constexpr int16_t M = 10;  // Side margin.
constexpr int16_t HEADER_H = 44;

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


lv_color_t mixRgb(uint32_t a, uint32_t b, uint8_t t) { return lv_color_mix(rgb(b), rgb(a), t); }

// A short top sheen, the glossy touch the Atlas buttons have too.
void sheen(lv_obj_t *o, uint32_t base) {
  lv_obj_set_style_bg_color(o, mixRgb(base, 0xFFFFFF, pal().light ? 18 : 26), 0);
  lv_obj_set_style_bg_grad_color(o, rgb(base), 0);
  lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
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

// The app's icon for a key's label, or nullptr. Labels come from the menu
// model, so this matches by text.
const lv_image_dsc_t *labelIcon(const char *label) {
  static const struct { const char *label; const lv_image_dsc_t *icon; } ICONS[] = {
      {"Pause", &th_icon_pause_24}, {"Resume", &th_icon_play_24}, {"Pass turn", &th_icon_pass_24},
      {"Pass", &th_icon_pass_24}, {"More", &th_icon_more_24}, {"Device", &th_icon_hub_24},
      {"Theme", &th_icon_palette_24}, {"Claim win", &th_icon_crown_24}, {"I'm out", &th_icon_flag_24},
      {"Concede", &th_icon_flag_24}, {"Approve", &th_icon_check_24}, {"Join", &th_icon_check_24},
      {"Join game", &th_icon_play_24}, {"Start game", &th_icon_play_24}, {"Apply", &th_icon_check_24},
      {"Deny", &th_icon_close_24}, {"Cancel", &th_icon_close_24}, {"Back", &th_icon_chevron_left_24},
      {"Previous", &th_icon_chevron_left_24}, {"Prev", &th_icon_chevron_left_24},
      {"Next", &th_icon_chevron_right_24}, {"Pair", &th_icon_sigil_24}, {"Unpair", &th_icon_link_24},
      {"Factory reset", &th_icon_power_24}, {"Invert", &th_icon_contrast_24}, {"Rematch", &th_icon_refresh_24},
      {"Add seat B", &th_icon_users_24}, {"Close", &th_icon_close_24}, {"Undo", &th_icon_refresh_24},
  };
  for (const auto &entry : ICONS)
    if (strcmp(entry.label, label) == 0) return entry.icon;
  return nullptr;
}

// Seat and avatar hues, as the app's Palette.avatarColor and the Atlas tiles.
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
  const bool contrast = styleTheme == TurnHubTheme::Id::Contrast;
  const float sat = contrast ? 0.0f : pal().ornament ? 0.42f : 0.5f;
  const float light = contrast ? 0.18f : pal().ornament ? 0.34f : pal().light ? 0.44f : 0.38f;
  return hsl(hues[(number + 7) % 8], sat, light);
}

// A filled disc in the seat's hue holding the name's initial.
lv_obj_t *token(lv_obj_t *parent, const char *name, uint8_t number, int16_t x, int16_t y, int16_t side) {
  lv_obj_t *disc = box(parent, x, y, side, side);
  lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(disc, rgb(seatColor(number)), 0);
  char initial[2] = {name && name[0] ? name[0] : '?', 0};
  lv_obj_t *l = lv_label_create(disc);
  lv_obj_remove_style_all(l);
  lv_label_set_text(l, initial);
  lv_obj_set_style_text_font(l, side >= 36 ? &th_semi_20 : &th_semi_16, 0);
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_obj_center(l);
  return disc;
}

// PowerPoint-style "Your turn": a turn-coloured card pushes in from the
// right, rests and slides off left, on LVGL's top layer so redraws under it
// leave it alone. The YOUR TURN pill says the same, so it is never the only
// sign.
lv_obj_t *banner = nullptr;

void slideX(void *obj, int32_t v) { lv_obj_set_x(static_cast<lv_obj_t *>(obj), v); }

void showTurnBanner(const char *name, uint8_t number) {
  if (banner) lv_obj_delete(banner);
  constexpr int16_t bw = W - 20, bh = 80;
  banner = box(lv_layer_top(), W, 96, bw, bh);
  lv_obj_add_event_cb(banner, [](lv_event_t *) { banner = nullptr; }, LV_EVENT_DELETE, nullptr);
  card(banner, pal().turn, pal().turn, 18);
  sheen(banner, pal().turn);
  lv_obj_t *disc = token(banner, name, number, 14, (bh - 40) / 2, 40);
  lv_obj_set_style_border_width(disc, 2, 0);
  lv_obj_set_style_border_color(disc, rgb(pal().onTurn), 0);
  text(banner, "Your turn", &th_bold_28, pal().onTurn, 64, 14, bw - 72);
  text(banner, name, &th_semi_16, pal().onTurn, 65, 48, bw - 72);
  lv_anim_t in;
  lv_anim_init(&in);
  lv_anim_set_var(&in, banner);
  lv_anim_set_exec_cb(&in, slideX);
  lv_anim_set_values(&in, W, 10);
  lv_anim_set_duration(&in, 300);
  lv_anim_set_path_cb(&in, lv_anim_path_ease_out);
  lv_anim_start(&in);
  lv_anim_t out;
  lv_anim_init(&out);
  lv_anim_set_var(&out, banner);
  lv_anim_set_exec_cb(&out, slideX);
  lv_anim_set_values(&out, 10, -W);
  lv_anim_set_delay(&out, 300 + 1000);
  lv_anim_set_duration(&out, 260);
  lv_anim_set_path_cb(&out, lv_anim_path_ease_in);
  lv_anim_set_completed_cb(&out, lv_obj_delete_anim_completed_cb);
  lv_anim_start(&out);
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

// Each frame owns its visible touch regions; blank space has no action.
lv_obj_t *LvglSigilDisplay::frame(const char *title, uint8_t, uint8_t) {
  styleTheme = theme_; styleInverted = inverted_;
  touchMap_ = TouchMap();
  for (auto &key : keys_) key = nullptr;
  lv_obj_t *screen = lv_screen_active();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, rgb(pal().bg), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  // The app's top bar: the screen's colour with a hairline under the title.
  lv_obj_t *bar = box(screen, 0, 0, W, HEADER_H);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_color(bar, rgb(pal().separator), 0);
  // Large while it clears the header's More key; the smaller title otherwise.
  const bool large = textWidth(title, &th_bold_28) <= W - 2 * M - 80;
  text(bar, title, large ? &th_bold_28 : &th_semi_20, pal().text, M, large ? 6 : 11, W - 2 * M);
  return screen;
}

void LvglSigilDisplay::button(const char *label, const char *note, PadRect r,
    uint8_t target, bool accent, uint32_t meaning) {
  if (!label || !label[0]) return;
  lv_obj_t *b = box(lv_screen_active(), r.x, r.y, r.w, r.h);
  // Names on the profile picker are list rows (the app's join list): the
  // player's initial, the name and its note, and a chevron.
  const bool row = layout_ == Layout::Picker && r.w >= W - 2 * M && target < KEY_COUNT &&
      target != static_cast<uint8_t>(Key::Select) && target != static_cast<uint8_t>(Key::Left);
  if (row) {
    card(b, pal().light ? pal().surface1 : pal().surface2, pal().separator, 14);
    uint8_t hue = 0;
    for (const char *c = label; *c; ++c) hue = static_cast<uint8_t>(hue * 31 + *c);
    token(b, label, hue, 10, (r.h - 32) / 2, 32);
    const int16_t textW = r.w - 52 - 30;
    if (note) {
      text(b, label, &th_semi_20, pal().text, 52, (r.h - 40) / 2, textW);
      text(b, note, &th_eyebrow_13, pal().textSecondary, 52, (r.h - 40) / 2 + 24, textW);
    } else {
      text(b, label, &th_semi_20, pal().text, 52, (r.h - 24) / 2, textW);
    }
    icon(b, &th_icon_chevron_right_24, r.w - 30, (r.h - 24) / 2, pal().textTertiary);
  } else {
    // The app's filled primary for the main action; tonal accent for the rest.
    const uint32_t base = accent ? pal().accent : pal().accentSoft;
    card(b, base, base, 14);
    if (accent) lv_obj_set_style_border_width(b, 0, 0);
    else lv_obj_set_style_border_color(b, mixRgb(pal().accentSoft, pal().accent, 110), 0);
    sheen(b, base);
    const uint32_t ink = accent ? pal().onAccent : pal().accent;
    const lv_image_dsc_t *glyph = labelIcon(label);
    int16_t room = r.w - 16;
    const lv_font_t *font = textWidth(label, &th_semi_20) <= room ? &th_semi_20 : &th_semi_16;
    if (glyph && textWidth(label, font) + 28 > room) glyph = nullptr;
    if (glyph) room -= 28;
    const int16_t lh = lv_font_get_line_height(font);
    const bool wrap = textWidth(label, font) > room;
    const int16_t height = (wrap ? 2 : 1) * lh;
    const int16_t y = (r.h - height - (note ? 15 : 0)) / 2;
    if (glyph) {
      const int16_t tw = static_cast<int16_t>(textWidth(label, font));
      const int16_t x = (r.w - tw - 28) / 2;
      icon(b, glyph, x, y + (lh - 24) / 2, ink);
      text(b, label, font, ink, x + 28, y, tw + 2);
    } else {
      auto *l = text(b, label, font, ink, 8, y, r.w - 16, LV_TEXT_ALIGN_CENTER);
      if (wrap) { lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); lv_obj_set_height(l, height); }
    }
    if (note) text(b, note, &th_eyebrow_13, accent ? pal().onAccent : pal().textSecondary,
        4, y + height + 1, r.w - 8, LV_TEXT_ALIGN_CENTER);
  }
  // A newly labeled control cannot inherit a contact from the old action.
  for (const char *c = label; *c; ++c) meaning = (meaning ^ static_cast<uint8_t>(*c)) * 16777619u;
  touchMap_.add(r, target, meaning);
  if (target < KEY_COUNT) keys_[target] = b;
}

// The app's bottom navigation bar: an icon over each label, the chosen tab's
// icon on a soft accent pill, a hairline along the top.
void LvglSigilDisplay::navigation(TurnHubUi::Page selected) {
  static const lv_image_dsc_t *const ICONS[] = {&th_icon_home_24, &th_icon_timer_24, &th_icon_table_24,
      &th_icon_sliders_24};
  auto *bar = box(lv_screen_active(), 0, NAV_Y, W, NAV_H);
  card(bar, pal().surface1, pal().surface1, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(bar, rgb(pal().separator), 0);
  // Settings, the longest label, gets the widest tab.
  static const int16_t WIDTHS[] = {56, 56, 56, 72};
  int16_t x = 0;
  for (uint8_t i = 0; i < TurnHubUi::PAGE_COUNT; ++i, x += WIDTHS[i - 1]) {
    const auto page = static_cast<TurnHubUi::Page>(i);
    const bool chosen = selected == page;
    const int16_t w = WIDTHS[i];
    const uint32_t ink = chosen ? pal().accent : pal().textSecondary;
    const int16_t pillW = 48, pillX = x + (w - pillW) / 2;
    if (chosen) {
      auto *indicator = box(bar, pillX, 3, pillW, 26);
      card(indicator, pal().accentSoft, pal().accentSoft, 13);
    }
    icon(bar, ICONS[i], pillX + (pillW - 24) / 2, 4, ink);
    text(bar, TurnHubUi::pageTitle(page), &th_eyebrow_13, ink, x, 30, w, LV_TEXT_ALIGN_CENTER);
    touchMap_.add({x, NAV_Y, w, NAV_H}, TOUCH_NAV + i, TOUCH_NAV + i);
  }
}

void LvglSigilDisplay::pad(const PadLabels &labels) {
  touchMap_.menuRevision = menu_.revision;
  if (layout_ == Layout::Hidden) { present(); return; }
  const uint8_t up=0, down=1, left=2, right=3, select=4;
  PadRect rects[KEY_COUNT] = {};
  if (layout_ == Layout::Menu) {
    rects[select]={10,54,106,72}; rects[up]={124,54,106,72};
    rects[right]={10,134,106,72}; rects[down]={124,134,106,72}; rects[left]={10,214,220,48};
  } else if (layout_ == Layout::Picker) {
    rects[up]={10,54,220,50}; rects[right]={10,110,220,50}; rects[down]={10,166,220,50};
    rects[left]={10,222,106,44}; rects[select]={124,222,106,44};
  } else if (layout_ == Layout::Game) {
    rects[up]={164,0,76,44}; rects[left]={10,112,52,56}; rects[right]={178,112,52,56};
    rects[down]={10,170,220,44}; rects[select]={10,218,220,48};
  } else if (layout_ == Layout::Commander) {
    rects[up]={164,0,76,44}; rects[left]={10,154,106,52}; rects[right]={124,154,106,52};
    rects[down]={10,164,220,44}; rects[select]={10,216,220,48};
  } else {
    rects[up]={164,0,76,44}; rects[left]={10,164,106,44}; rects[right]={124,164,106,44};
    rects[down]={10,112,220,44}; rects[select]={10,216,220,48};
  }
  for (uint8_t i=0; i<KEY_COUNT; ++i) {
    const uint32_t meaning = (context_ * 65536u + menu_.revision * 256u +
        (menu_.compass[i] == MENU_NONE ? i : menu_.compass[i])) ^ (touchMap_.actor * 2654435761u) ^
        (static_cast<uint32_t>(touchMap_.lifeRequest) << 16);
    button(labels.text[i], labels.note[i], rects[i], i, labels.accent[i], meaning);
  }
  const uint8_t progress = menu_.pairPercent ? menu_.pairPercent : menu_.holdPercent;
  if (progress) {
    auto *track=box(lv_screen_active(),10,266,220,4);
    card(track,pal().fillStrong,pal().fillStrong,2);
    auto *fill=box(track,0,0,220*progress/100,4); card(fill,pal().accent,pal().accent,2);
  }
  navigation(page_);
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

// Current action labels for the touch buttons.
PadLabels LvglSigilDisplay::menuLabels() const {
  PadLabels l;
  if (!menu_.active) return l;
  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    const uint8_t action = menu_.compass[i];
    if (action == MENU_NONE) continue;
    l.text[i] = action == MENU_LOCAL_DEVICE_MENU ? "More" : sigilActionLabel(static_cast<TurnHubProtocol::SigilAction>(action));
    if (menuActionNeedsHold(action)) l.note[i] = "HOLD";
  }
  const uint8_t left = static_cast<uint8_t>(Key::Left), right = static_cast<uint8_t>(Key::Right);
  if (menu_.pairPercent) {
    l.note[static_cast<uint8_t>(Key::Select)] = "HOLD TO PAIR";
  }
  if (lifeRequestShown()) {
    l.text[left] = "Deny";
    l.text[right] = "Approve";
    l.note[left] = l.note[right] = nullptr;
    l.accent[right] = true;
  } else if (lifeKeysShown()) {
    l.text[left] = life_.startingLife >= 1000 ? "-100" : "-1";
    l.text[right] = life_.startingLife >= 1000 ? "+100" : "+1";
    l.note[left] = l.note[right] = nullptr;
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
  text(s, title, &th_bold_28, pal().text, M, 58, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  if (line1) text(s, line1, &th_semi_16, pal().textSecondary, M, 94, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  if (line2) text(s, line2, &th_semi_16, pal().textSecondary, M, 118, W - 2 * M, LV_TEXT_ALIGN_CENTER);
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
  layout_ = Layout::Menu;
  frame(menu_.recovery ? "Device settings" : "Player controls", sigilId);
  pad(menuLabels());
  present();
  return true;
}

void LvglSigilDisplay::showBooting() { layout_=Layout::Hidden; statusScreen("Starting", "Looking for Atlas", nullptr, 0xFF, PadLabels()); }

void LvglSigilDisplay::showUnpaired() {
  page_=TurnHubUi::Page::Game; layout_=Layout::Actions; context_=1;
  if (drawDeviceMenu(0xFF)) return;
  PadLabels l = menuLabels();
  // Hold the on-screen Pair button to open pairing while unpaired.
  l.text[static_cast<uint8_t>(Key::Select)] = menu_.pairing ? nullptr : "Pair";
  l.note[static_cast<uint8_t>(Key::Select)] = "HOLD";
  l.accent[static_cast<uint8_t>(Key::Select)] = true;
  statusScreen(menu_.pairing ? "Pairing" : "Unpaired",
      menu_.pairing ? "Searching for Atlas" : "Hold Pair, then confirm", "the code on Atlas", 0xFF, l);
}

void LvglSigilDisplay::showReady(uint8_t sigilId) {
  page_=TurnHubUi::Page::Game; layout_=Layout::Actions; context_=1;
  if (drawDeviceMenu(sigilId)) return;
  statusScreen("Ready", "Welcome to TurnHub!", "Choose Join game below", sigilId, menuLabels());
}

void LvglSigilDisplay::showAtlasLost(uint8_t sigilId) {
  page_=TurnHubUi::Page::Game; layout_=Layout::Actions; context_=1;
  if (drawDeviceMenu(sigilId)) return;
  statusScreen("Atlas lost", "Searching for Atlas", nullptr, sigilId, menuLabels());
}

void LvglSigilDisplay::showPairingCode(uint16_t code) {
  layout_=Layout::Hidden;
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
  layout_=Layout::Hidden;
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

bool LvglSigilDisplay::setSeatName(uint8_t slot, const char *name) {
  if (slot > 1) return false;
  char next[sizeof(seatName_[0])];
  snprintf(next, sizeof(next), "%s", name ? name : "");
  if (strcmp(next, seatName_[slot]) == 0) return false;
  memcpy(seatName_[slot], next, sizeof(next));
  return true;
}

void LvglSigilDisplay::showGame(const TurnHubProtocol::GameDisplayPacket &g) {
  page_=TurnHubUi::Page::Game; layout_=Layout::Game; context_=2;
  if (drawDeviceMenu(g.sigilId)) return;
  using namespace TurnHubProtocol;
  const bool active=hasDisplayFlag(g.state,DISPLAY_FLAG_ACTIVE);
  const bool shared=displaySecondaryPlayer(g.state)!=0;
  const bool seatB=hasDisplayFlag(g.state,DISPLAY_FLAG_PRIMARY_B);
  auto *s=frame("My player",g.sigilId);
  touchMap_.actor=displayPrimaryPlayer(g.state);
  touchMap_.lifeRequest=static_cast<uint16_t>(life_.request.target << 8) | life_.request.tag;
  if (lifeRequestShown()) {
    layout_=Layout::Actions;
    frame("Life request",g.sigilId);
    touchMap_.actor=displayPrimaryPlayer(g.state);
    touchMap_.lifeRequest=static_cast<uint16_t>(life_.request.target << 8) | life_.request.tag;
    char request[48];
    snprintf(request,sizeof(request),"Player %u requests",life_.request.requester);
    text(s,request,&th_semi_20,pal().text,10,62,220,LV_TEXT_ALIGN_CENTER);
    snprintf(request,sizeof(request),"%+ld life",static_cast<long>(life_.request.delta));
    text(s,request,&th_bold_28,pal().warning,10,94,220,LV_TEXT_ALIGN_CENTER);
    snprintf(request,sizeof(request),"Current life: %ld",static_cast<long>(g.primary.life));
    text(s,request,&th_semi_16,pal().textSecondary,10,132,220,LV_TEXT_ALIGN_CENTER);
    auto labels=menuLabels();
    labels.text[static_cast<uint8_t>(Key::Select)]=nullptr;
    labels.text[static_cast<uint8_t>(Key::Down)]=nullptr;
    finish(labels);
    return;
  }
  // The player's panel, as the app's seat panels: the turn glows.
  static bool wasActive = false;
  auto *panel=box(s,M,48,W-2*M,122);
  card(panel,active ? pal().turnSoft : pal().surface1,active ? pal().turn : pal().separator,16);
  if (active) lv_obj_set_style_border_width(panel,2,0);
  if (active && !wasActive && !life_.passPending) showTurnBanner(g.primary.name,displayPrimaryPlayer(g.state));
  wasActive=active;
  text(s,g.primary.name,nameFont(g.primary.name,200),pal().text,20,52,200,LV_TEXT_ALIGN_CENTER);
  pill(s,life_.passPending ? "PASSING" : active ? "YOUR TURN" : "WAITING",
      active ? pal().turn : pal().fill,active ? pal().onTurn : pal().textSecondary,84);
  char digits[14];
  const int32_t preview=g.primary.life + life_.pending;
  snprintf(digits,sizeof(digits),"%ld",static_cast<long>(preview));
  const lv_font_t *const fonts[]={&th_num_64,&th_num_40,&th_bold_28,&th_semi_20};
  const lv_font_t *font=fitted(digits,108,fonts,4);
  text(s,digits,font,active ? pal().turn : pal().text,64,110,112,LV_TEXT_ALIGN_CENTER);
  PadLabels labels=menuLabels();
  char other[40];
  if (shared && labels.text[static_cast<uint8_t>(Key::Down)]) {
    snprintf(other,sizeof(other),"%s / %ld",g.secondary.name,static_cast<long>(g.secondary.life));
    labels.text[static_cast<uint8_t>(Key::Down)]=other;
  }
  if (lifeRequestShown()) {
    char request[40]; snprintf(request,sizeof(request),"P%u requests %+ld life",life_.request.requester,
        static_cast<long>(life_.request.delta));
    text(s,request,&th_semi_16,pal().warning,10,182,220,LV_TEXT_ALIGN_CENTER);
  } else if (!shared) {
    char detail[48];
    if (life_.pending) snprintf(detail,sizeof(detail),"%+ld life pending",static_cast<long>(life_.pending));
    else if (g.sourceCount) {
      char damage[24]; formatCommanderDamage(g.sources[0],damage,sizeof(damage));
      snprintf(detail,sizeof(detail),"Cmd / %.12s %s",g.sources[0].name,damage);
    } else snprintf(detail,sizeof(detail),"%s / Round %u",g.commander ? "Commander" : "Life",displayTurnNumber(g.state));
    text(s,detail,&th_eyebrow_13,pal().textSecondary,10,184,220,LV_TEXT_ALIGN_CENTER);
  }
  finish(labels);
}

void LvglSigilDisplay::showState(uint8_t sigilId, TurnHubProtocol::DisplayMode mode, uint8_t primaryPlayer,
    uint8_t secondaryPlayer, uint8_t turnNumber, uint8_t flags) {
  using namespace TurnHubProtocol;
  page_=TurnHubUi::Page::Game; layout_=Layout::Actions; context_=3;
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
    seat("SEAT A", seatName_[0], 50, 48, lit && !focusB);
    seat("SEAT B", seatName_[1], 104, 48, lit && focusB);
  } else {
    char tag[16] = "READY";
    if (primaryPlayer) snprintf(tag, sizeof(tag), "PLAYER %u", static_cast<unsigned>(primaryPlayer));
    seat(tag, seatName_[0], 50, 88, lit);
  }
  if (lit) pill(s, status, mode == DisplayMode::GameOver ? pal().accent : pal().turn,
      mode == DisplayMode::GameOver ? pal().onAccent : pal().onTurn, 140);
  else pill(s, status, pal().fill, pal().textSecondary, 140);
  finish(menuLabels());
}

void LvglSigilDisplay::showCommander(const TurnHubProtocol::CommanderFlowPacket &page) {
  using TurnHubProtocol::CommanderStage;
  page_=TurnHubUi::Page::Game; layout_=Layout::Commander; context_=16 + static_cast<uint8_t>(page.stage) + page.revision * 32u;
  const CommanderFlowView v = commanderFlowView(page);
  lv_obj_t *s = frame("Cmd damage", page.sigilId);
  text(s, v.title, &th_eyebrow_13, pal().accent, M, 50, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  text(s, v.lines[0], &th_semi_16, pal().textSecondary, M, 72, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  text(s, v.lines[1], &th_semi_16, pal().textSecondary, M, 94, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  text(s, v.lines[2], &th_semi_20, pal().text, M, 116, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  if (v.lines[3][0] && page.stage >= CommanderStage::Confirm)
    text(s, v.lines[3], &th_semi_20, pal().text, M, 138, W - 2 * M, LV_TEXT_ALIGN_CENTER);
  PadLabels l;
  const uint8_t up = 0, down = 1, left = 2, right = 3, select = 4;
  static_assert(static_cast<uint8_t>(Key::Select) == 4 && static_cast<uint8_t>(Key::Left) == 2, "key order");
  switch (page.stage) {
    case CommanderStage::Source:
      l.text[left] = "Prev"; l.text[right] = "Next"; l.text[select] = "Choose";
      l.text[up] = "Cancel";
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

void LvglSigilDisplay::showHome(uint8_t sigilId, bool connected, const char *name, uint8_t mode) {
  page_=TurnHubUi::Page::Home; layout_=Layout::Actions;
  auto *s=frame("TurnHub",sigilId);
  text(s,"YOUR SIGIL",&th_eyebrow_13,pal().accent,10,56,220);
  text(s,name && name[0] ? name : "Welcome",&th_bold_28,pal().text,10,78,220);
  text(s,sigilId==0xFF ? "Pair with an Atlas to play" : connected ? "Connected to Atlas" : "Atlas lost / reconnecting",
      &th_semi_16,connected ? pal().good : pal().warning,10,114,220);
  const char *status=mode==static_cast<uint8_t>(TurnHubProtocol::DisplayMode::Running) ? "Your player controls" :
      mode==static_cast<uint8_t>(TurnHubProtocol::DisplayMode::Paused) ? "Game paused" : "Lobby and player controls";
  // The app's grouped list: one card, two rows, a hairline between them.
  auto *group=box(s,10,150,220,110);
  card(group,pal().light ? pal().surface1 : pal().surface2,pal().separator,16);
  auto *rule=box(group,52,55,168,1); card(rule,pal().separator,pal().separator,0);
  const struct { const char *label, *detail; const lv_image_dsc_t *glyph; uint32_t tint; uint8_t target; } rows[]={
      {"Game",status,&th_icon_timer_24,pal().turn,TOUCH_NAV+1},
      {"Table","Everyone at the table",&th_icon_table_24,pal().info,TOUCH_NAV+2}};
  for (uint8_t i=0;i<2;++i) {
    const int16_t y=i*55;
    auto *tile=box(group,10,y+11,32,32);
    card(tile,rows[i].tint,rows[i].tint,9); sheen(tile,rows[i].tint);
    icon(tile,rows[i].glyph,4,4,pal().bg);
    text(group,rows[i].label,&th_semi_20,pal().text,52,y+6,140);
    text(group,rows[i].detail,&th_eyebrow_13,pal().textSecondary,52,y+31,140);
    icon(group,&th_icon_chevron_right_24,188,y+15,pal().textTertiary);
    touchMap_.add({10,static_cast<int16_t>(150+y),220,55},rows[i].target,rows[i].target);
  }
  navigation(page_); present();
}

void LvglSigilDisplay::showSettings(uint8_t sigilId) {
  page_=TurnHubUi::Page::Settings; layout_=Layout::Menu; context_=4;
  frame("Sigil settings",sigilId);
  pad(menuLabels()); present();
}

void LvglSigilDisplay::showTable(uint8_t sigilId, const TableSnapshot &table, uint8_t page, bool connected) {
  page_=TurnHubUi::Page::Table;
  auto *s=frame("Table / read only",sigilId);
  if (!table.valid) {
    text(s,"Waiting for Atlas",&th_semi_20,pal().text,10,100,220,LV_TEXT_ALIGN_CENTER);
    text(s,"The roster appears here",&th_semi_16,pal().textSecondary,10,130,220,LV_TEXT_ALIGN_CENTER);
  } else if (!table.total) {
    text(s,"No players yet",&th_semi_20,pal().text,10,100,220,LV_TEXT_ALIGN_CENTER);
  } else {
    const uint8_t pages=(table.total+2)/3;
    page=page<pages ? page : 0;
    char caption[40]; snprintf(caption,sizeof(caption),"%u players / %u/%u%s",table.total,page+1,pages,connected ? "" : " / offline");
    text(s,caption,&th_eyebrow_13,connected ? pal().textSecondary : pal().warning,10,50,220);
    for(uint8_t i=0;i<3 && page*3+i<table.total;++i) {
      const auto &row=table.rows[page*3+i];
      auto *c=box(s,10,72+i*49,220,44);
      const bool turn=row.flags&TurnHubProtocol::TABLE_ACTIVE;
      card(c,turn ? pal().turnSoft : pal().surface1,turn ? pal().turn : pal().separator,12);
      if (turn) lv_obj_set_style_border_width(c,2,0);
      token(c,row.name,page*3+i+1,8,8,28);
      text(c,row.name,&th_semi_16,pal().text,44,4,84);
      const char *tag=row.flags&TurnHubProtocol::TABLE_WINNER ? "WINNER" : row.flags&TurnHubProtocol::TABLE_OUT ? "OUT" :
          row.flags&TurnHubProtocol::TABLE_ACTIVE ? "TURN" : row.flags&TurnHubProtocol::TABLE_STARTER ? "STARTS" : "";
      text(c,tag,&th_eyebrow_13,pal().turn,44,24,84);
      char life[14]; snprintf(life,sizeof(life),"%ld",static_cast<long>(row.life));
      const lv_font_t *const sizes[]={&th_semi_20,&th_semi_16};
      text(c,life,fitted(life,84,sizes,2),pal().text,128,10,84,LV_TEXT_ALIGN_RIGHT);
    }
    if(pages>1) {
      if(page) button("Previous",nullptr,{10,222,106,44},TOUCH_PREV,false,TOUCH_PREV);
      if(page+1<pages) button("Next",nullptr,{124,222,106,44},TOUCH_NEXT,false,TOUCH_NEXT);
    }
  }
  navigation(page_); present();
}

// Atlas's profile picker: Up, Right and Down are the names on the page.
void LvglSigilDisplay::showPicker(const TurnHubProtocol::ProfilePickerPacket &page, uint8_t) {
  using namespace TurnHubProtocol;
  const bool confirm = page.mode == PickerMode::Confirm;
  page_=TurnHubUi::Page::Game; layout_=confirm ? Layout::Actions : Layout::Picker; context_=128 + page.revision;
  const char *notice = nullptr;
  switch (page.notice) {
    case PickerNotice::NeedsPhone: notice = "Sign in on your phone"; break;
    case PickerNotice::Unavailable: notice = "Not available"; break;
    case PickerNotice::TableFull: notice = "The table is full"; break;
    case PickerNotice::Failed: notice = "Try again"; break;
    default: break;
  }
  lv_obj_t *s = frame(notice ? notice : confirm ? "Join" : "Who plays?", page.sigilId);
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
    if (page.pageCount > 1) l.text[select] = "More";
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
  finish(l);
}

}  // namespace TurnHubSigil

#endif
