#include "ui.hpp"
#include "fonts.hpp"
#include "icons.hpp"
#include <algorithm>
#include <cstdio>

#define TH_ICON(name, size) (&th_icon_##name##_##size)

namespace th {
namespace {
const char* phase_name(Phase p) {
    switch(p) { case Phase::Running: return "PLAYING"; case Phase::Paused: return "PAUSED"; case Phase::GameOver: return "GAME OVER"; }
    return "UNKNOWN";
}
lv_obj_t* as_obj(void* v) { return static_cast<lv_obj_t*>(v); }
void set_opa(void* o, int32_t v) { lv_obj_set_style_opa(as_obj(o), static_cast<lv_opa_t>(v), 0); }
void set_scale(void* o, int32_t v) { lv_obj_set_style_transform_scale(as_obj(o), v, 0); }
void set_tx(void* o, int32_t v) { lv_obj_set_style_translate_x(as_obj(o), v, 0); }
void set_ty(void* o, int32_t v) { lv_obj_set_style_translate_y(as_obj(o), v, 0); }
void set_shadow(void* o, int32_t v) { lv_obj_set_style_shadow_opa(as_obj(o), static_cast<lv_opa_t>(v), 0); }
void set_border_opa(void* o, int32_t v) { lv_obj_set_style_border_opa(as_obj(o), static_cast<lv_opa_t>(v), 0); }
void set_bg_opa(void* o, int32_t v) { lv_obj_set_style_bg_opa(as_obj(o), static_cast<lv_opa_t>(v), 0); }
void set_arc(void* o, int32_t v) { lv_arc_set_value(as_obj(o), v); }
void delete_var(lv_anim_t* a) { lv_obj_delete(as_obj(a->var)); }

// Replaces any running animation of the same property. A zero-length,
// undelayed animation applies its end value at once (reduced motion).
void animate(lv_obj_t* obj, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t time,
             uint32_t delay = 0, lv_anim_path_cb_t path = lv_anim_path_ease_out, lv_anim_completed_cb_t done = nullptr) {
    lv_anim_delete(obj, cb);
    if(time == 0 && delay == 0 && !done) { cb(obj, to); return; }
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, obj); lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to); lv_anim_set_duration(&a, time); lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, path);
    if(done) lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
}
void pulse(lv_obj_t* obj, lv_anim_exec_xcb_t cb, int32_t low, int32_t high, uint32_t period) {
    lv_anim_delete(obj, cb);
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, obj); lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, low, high); lv_anim_set_duration(&a, period); lv_anim_set_reverse_duration(&a, period);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}
int depth(Screen s) { return s == Screen::Gameplay ? 0 : s == Screen::Settings ? 1 : 2; }
} // namespace

Ui::Ui(DisplayProfile p, SnapshotSource& s, ActionSink& sink, const AssetProvider& a)
    : profile_(p), source_(s), sink_(sink), assets_(a) {
    group_ = lv_group_create();
    lv_group_set_wrap(group_, true);
    keypad_ = lv_indev_create();
    lv_indev_set_type(keypad_, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_user_data(keypad_, this);
    lv_indev_set_read_cb(keypad_, read_keypad);
    lv_indev_set_group(keypad_, group_);
    base_screen_ = lv_screen_active();
    render();
}
Ui::~Ui() {
    lv_indev_delete(keypad_);
    lv_group_delete(group_);
    if(screen_obj_) { lv_screen_load(base_screen_); lv_obj_delete(screen_obj_); }
}
const lv_font_t* Ui::title_font(bool large) const {
    return palette().ornament ? (large ? &th_cinzel_32 : &th_cinzel_20)
                              : (large ? &th_inter_32 : &th_inter_20);
}

// ---- Widget helpers ----------------------------------------------------
lv_obj_t* Ui::box(lv_obj_t* parent, int x, int y, int w, int h) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}
lv_obj_t* Ui::panel(lv_obj_t* parent, int x, int y, int w, int h) {
    auto* obj = box(parent, x, y, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(palette().surface1), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 14, 0);
    lv_obj_set_style_border_width(obj, palette().ornament ? 2 : 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(palette().separatorStrong), 0);
    return obj;
}
lv_obj_t* Ui::label(lv_obj_t* parent, const char* text, int x, int y, int width, const lv_font_t* font, uint32_t color) {
    auto* obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    if(width > 0) {
        // One line, truncated with an ellipsis; callers opt into wrapping.
        lv_obj_set_size(obj, width, lv_font_get_line_height(font));
        lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    }
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_text(obj, text);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}
lv_obj_t* Ui::icon(lv_obj_t* parent, const lv_image_dsc_t* src, uint32_t color) {
    auto* obj = lv_image_create(parent);
    lv_image_set_src(obj, src);
    lv_obj_set_style_image_recolor(obj, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}
lv_obj_t* Ui::token(lv_obj_t* parent, int x, int y, int side, uint8_t avatar, lv_obj_t** image) {
    auto* circle = box(parent, x, y, side, side);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(palette().accentSoft), 0);
    lv_obj_set_style_border_width(circle, 2, 0);
    lv_obj_set_style_border_color(circle, lv_color_hex(palette().accent), 0);
    auto* glyph = icon(circle, assets_.avatar(avatar), palette().accent);
    // 64 px preset masks, drawn at roughly 60% of the token.
    lv_image_set_scale(glyph, static_cast<uint32_t>(256 * side * 6 / 10 / 64));
    lv_obj_set_size(glyph, side, side);
    lv_image_set_inner_align(glyph, LV_IMAGE_ALIGN_CENTER);
    lv_obj_center(glyph);
    if(image) *image = glyph;
    return circle;
}
lv_obj_t* Ui::pill(lv_obj_t* parent, int x, int y, const char* text, uint32_t bg, uint32_t fg, lv_obj_t** text_out) {
    auto* obj = box(parent, x, y, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_pad_hor(obj, 10, 0); lv_obj_set_style_pad_ver(obj, 3, 0);
    auto* caption = label(obj, text, 0, 0, 0, &th_inter_16, fg);
    if(text_out) *text_out = caption;
    return obj;
}
void Ui::bind(lv_obj_t* obj, const std::string& name, bool focusable) {
    controls_[name] = obj;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    // Owned by the object, so a screen sliding out keeps valid callbacks.
    lv_obj_add_event_cb(obj, click, LV_EVENT_CLICKED, new Binding{this, name});
    lv_obj_add_event_cb(obj, free_binding, LV_EVENT_DELETE, nullptr);
    if(focusable) lv_group_add_obj(group_, obj);
}
lv_obj_t* Ui::button(lv_obj_t* parent, const std::string& name, const char* text, const lv_image_dsc_t* glyph,
                     int x, int y, int w, int h, Look look, const lv_font_t* font) {
    static const lv_style_prop_t props[] = {LV_STYLE_BG_COLOR, LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_TRANSFORM_HEIGHT, LV_STYLE_PROP_INV};
    static lv_style_transition_dsc_t transition;
    static bool ready = false;
    if(!ready) { lv_style_transition_dsc_init(&transition, props, lv_anim_path_ease_out, 140, 0, nullptr); ready = true; }
    const auto& pal = palette();
    auto* obj = box(parent, x, y, w, h);
    const uint32_t fill = look == Look::Accent ? pal.accent : look == Look::Quiet ? pal.surface1 : pal.surface3;
    const uint32_t ink = look == Look::Accent ? pal.onAccent : pal.text;
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(look == Look::Accent ? pal.accentPressed : pal.fillStrong), LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_border_width(obj, look == Look::Accent ? 0 : 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(pal.separatorStrong), 0);
    lv_obj_set_style_transform_width(obj, -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(obj, -3, LV_STATE_PRESSED);
    if(!reduced_motion_) {
        lv_obj_set_style_transition(obj, &transition, 0);
        lv_obj_set_style_transition(obj, &transition, LV_STATE_PRESSED);
    }
    lv_obj_set_style_outline_width(obj, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(obj, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(obj, lv_color_hex(pal.focus), LV_STATE_FOCUS_KEY);
    if(text || glyph) {
        lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(obj, 8, 0);
        if(glyph) icon(obj, glyph, ink);
        if(text) label(obj, text, 0, 0, 0, font ? font : &th_inter_16, ink);
    }
    bind(obj, name);
    return obj;
}
void Ui::pop(lv_obj_t* obj, int32_t peak) {
    if(reduced_motion_ || !obj) return;
    lv_obj_set_style_transform_pivot_x(obj, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(obj, lv_pct(50), 0);
    lv_anim_delete(obj, set_scale);
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, obj); lv_anim_set_exec_cb(&a, set_scale);
    lv_anim_set_values(&a, 256, peak); lv_anim_set_duration(&a, 90); lv_anim_set_reverse_duration(&a, 160);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

// ---- Screens ---------------------------------------------------------------
void Ui::build_status_bar(lv_obj_t* parent) {
    const auto& pal = palette();
    const bool compact = profile_.compact();
    const int w = profile_.width;
    auto* brand = box(parent, 10, 2, LV_SIZE_CONTENT, 28);
    lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brand, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(brand, 6, 0);
    icon(brand, compact ? TH_ICON(sigil, 24) : TH_ICON(hub, 24), pal.accent);
    label(brand, compact ? "Sigil" : "TurnHub", 0, 0, 0, compact ? &th_inter_20 : title_font(), pal.text);
    auto* demo = label(brand, "DEMO", 0, 0, 0, &th_inter_16, pal.textTertiary);
    lv_obj_set_style_margin_left(demo, 4, 0);
    // Battery is not measured in the simulator: show the gauge outline and "--", never a guess.
    auto* bat = icon(parent, TH_ICON(battery, 24), pal.textSecondary);
    lv_obj_set_pos(bat, w - 58, 4);
    label(parent, "--", w - 30, 7, 0, &th_inter_16, pal.textSecondary);
}
void Ui::build_header(const char* title, const char* back) {
    label(root_, title, 64, profile_.compact() ? 14 : 12, profile_.width - 76, title_font(!profile_.compact()), palette().text);
    button(root_, back, nullptr, TH_ICON(chevron_left, 24), 10, 8, 44, 40, Look::Quiet);
}
void Ui::build_gameplay_atlas() {
    const auto& pal = palette();
    const auto& s = source_.snapshot();
    build_status_bar(root_);
    // Hero: the turn clock ring and whose turn it is.
    auto* hero = panel(root_, 12, 36, 184, 228);
    clock_arc_ = lv_arc_create(hero);
    lv_obj_remove_style_all(clock_arc_);
    lv_obj_set_size(clock_arc_, 136, 136);
    lv_obj_set_pos(clock_arc_, 24, 12);
    lv_obj_remove_flag(clock_arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(clock_arc_, 270);
    lv_arc_set_bg_angles(clock_arc_, 0, 360);
    lv_arc_set_range(clock_arc_, 0, 1000);
    lv_arc_set_value(clock_arc_, 1000);
    lv_obj_set_style_arc_width(clock_arc_, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(clock_arc_, lv_color_hex(pal.fillStrong), LV_PART_MAIN);
    lv_obj_set_style_arc_width(clock_arc_, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(clock_arc_, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(clock_arc_, lv_color_hex(pal.turn), LV_PART_INDICATOR);
    auto* clock_icon = icon(hero, TH_ICON(clock, 24), pal.textTertiary);
    lv_obj_set_pos(clock_icon, 80, 40);
    timer_ = label(hero, "", 24, 66, 136, &th_inter_32, pal.text);
    lv_obj_set_style_text_align(timer_, LV_TEXT_ALIGN_CENTER, 0);
    name_ = label(hero, "", 8, 154, 168, title_font(true), pal.text);
    lv_obj_set_style_text_align(name_, LV_TEXT_ALIGN_CENTER, 0);
    status_pill_ = pill(hero, 0, 194, "", pal.turnSoft, pal.turn, &status_);
    lv_obj_align(status_pill_, LV_ALIGN_TOP_MID, 0, 194);
    // Seats: a 2x2 grid of touch cards; tapping one opens its life sheet.
    for(unsigned i = 0; i < 4; ++i) {
        const auto& player = s.players[i];
        auto* card = panel(root_, 204 + static_cast<int>(i % 2) * 136, 36 + static_cast<int>(i / 2) * 118, 128, 110);
        cards_[i] = card;
        lv_obj_set_style_shadow_color(card, lv_color_hex(pal.turn), 0);
        lv_obj_set_style_shadow_width(card, 22, 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(pal.surface2), LV_STATE_PRESSED);
        lv_obj_set_style_outline_color(card, lv_color_hex(pal.focus), 0);
        lv_obj_set_style_outline_pad(card, 3, 0);
        card_token_[i] = token(card, 10, 10, 40, player.avatar, &card_avatar_[i]);
        card_life_[i] = label(card, "", 54, 10, 64, &th_inter_32, pal.text);
        lv_obj_set_style_text_align(card_life_[i], LV_TEXT_ALIGN_RIGHT, 0);
        label(card, player.name.c_str(), 10, 58, 108, &th_inter_20, pal.text);
        card_tag_[i] = label(card, "", 10, 83, 70, &th_inter_16, pal.textSecondary);
        card_badge_[i] = label(card, "", 70, 82, 48, &th_inter_20, pal.good);
        lv_obj_set_style_text_align(card_badge_[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_opa(card_badge_[i], LV_OPA_TRANSP, 0);
        bind(card, "seat" + std::to_string(i));
    }
    button(root_, "pause", "Pause", TH_ICON(pause, 24), 12, 272, 150, 44);
    button(root_, "scenario", "Sample", TH_ICON(dice, 24), 170, 272, 140, 44);
    button(root_, "menu", "Settings", TH_ICON(sliders, 24), 318, 272, 150, 44);
    // Paused / game-over veil over the play area; the action bar stays live.
    overlay_ = box(root_, 0, 30, profile_.width, 238);
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(pal.scrim), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_80, 0);
    overlay_icon_ = icon(overlay_, TH_ICON(pause, 40), pal.warning);
    lv_obj_align(overlay_icon_, LV_ALIGN_CENTER, 0, -42);
    overlay_title_ = label(overlay_, "", 0, 0, 0, title_font(true), pal.text);
    lv_obj_align(overlay_title_, LV_ALIGN_CENTER, 0, 4);
    overlay_text_ = label(overlay_, "", 0, 0, 0, &th_inter_16, pal.textSecondary);
    lv_obj_align(overlay_text_, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    bind(overlay_, "veil", false);
}
void Ui::build_gameplay_sigil() {
    const auto& pal = palette();
    build_status_bar(root_);
    token_ = token(root_, 12, 34, 48, 1, &avatar_);
    name_ = label(root_, "", 70, 34, 158, title_font(), pal.text);
    status_pill_ = pill(root_, 70, 60, "", pal.turn, pal.onTurn, &status_);
    auto* clock_icon = icon(root_, TH_ICON(timer, 24), pal.textSecondary);
    lv_obj_set_pos(clock_icon, 10, 94);
    timer_ = label(root_, "", 40, 95, 90, &th_inter_20, pal.text);
    char allowance[24];
    const auto total = source_.snapshot().turn_seconds;
    std::snprintf(allowance, sizeof allowance, "of %u:%02u", total / 60, total % 60);
    auto* of = label(root_, allowance, 128, 98, 100, &th_inter_16, pal.textTertiary);
    lv_obj_set_style_text_align(of, LV_TEXT_ALIGN_RIGHT, 0);
    clock_bar_ = lv_bar_create(root_);
    lv_obj_remove_style_all(clock_bar_);
    lv_obj_set_pos(clock_bar_, 12, 124); lv_obj_set_size(clock_bar_, 216, 8);
    lv_bar_set_range(clock_bar_, 0, 1000);
    lv_obj_set_style_bg_opa(clock_bar_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(clock_bar_, lv_color_hex(pal.fillStrong), LV_PART_MAIN);
    lv_obj_set_style_radius(clock_bar_, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(clock_bar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(clock_bar_, lv_color_hex(pal.turn), LV_PART_INDICATOR);
    lv_obj_set_style_radius(clock_bar_, 4, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(clock_bar_, 950, LV_PART_MAIN);
    // Touch-first life: the left half lowers, the right half raises.
    // One card, two invisible halves that light up while pressed.
    panel(root_, 10, 142, 220, 122);
    auto zone = [&](const char* name, int x, const lv_image_dsc_t* glyph, lv_align_t corner) {
        auto* z = button(root_, name, nullptr, nullptr, x, 142, 110, 122, Look::Quiet);
        lv_obj_set_style_radius(z, 14, 0);
        lv_obj_set_style_bg_opa(z, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(z, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(z, 0, 0);
        auto* g = icon(z, glyph, pal.textSecondary);
        lv_obj_align(g, corner, corner == LV_ALIGN_BOTTOM_LEFT ? 10 : -10, -8);
    };
    zone("minus", 10, TH_ICON(minus, 24), LV_ALIGN_BOTTOM_LEFT);
    zone("plus", 120, TH_ICON(plus, 24), LV_ALIGN_BOTTOM_RIGHT);
    auto* heart = icon(root_, TH_ICON(heart, 24), pal.critical);
    lv_obj_set_pos(heart, 108, 150);
    life_ = label(root_, "", 12, 166, 216, &th_inter_72, pal.text);
    lv_obj_set_style_text_align(life_, LV_TEXT_ALIGN_CENTER, 0);
    badge_ = label(root_, "", 140, 150, 80, &th_inter_20, pal.good);
    lv_obj_set_style_text_align(badge_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_opa(badge_, LV_OPA_TRANSP, 0);
    button(root_, "menu", nullptr, TH_ICON(sliders, 24), 12, 272, 64, 44);
    button(root_, "player", nullptr, TH_ICON(users, 24), 88, 272, 64, 44);
    button(root_, "scenario", nullptr, TH_ICON(dice, 24), 164, 272, 64, 44);
    // Your-turn frame around the whole panel; never intercepts touches.
    turn_frame_ = box(root_, 0, 0, profile_.width, profile_.height);
    lv_obj_set_style_border_width(turn_frame_, 3, 0);
    lv_obj_set_style_border_color(turn_frame_, lv_color_hex(pal.turn), 0);
    lv_obj_set_style_radius(turn_frame_, 6, 0);
    lv_obj_add_flag(turn_frame_, LV_OBJ_FLAG_HIDDEN);
}
void Ui::build_settings() {
    const auto& pal = palette();
    const auto& s = source_.snapshot();
    struct Item { const char* name; const char* title; const char* detail; const lv_image_dsc_t* small; const lv_image_dsc_t* big; };
    const Item items[] = {
        {"pairing", "Pairing", "Mock, no radio", TH_ICON(link, 24), TH_ICON(link, 40)},
        {"themes", "Theme", pal.label, TH_ICON(palette, 24), TH_ICON(palette, 40)},
        {"players", "Player", s.players[selected_].name.c_str(), TH_ICON(users, 24), TH_ICON(users, 40)},
    };
    for(unsigned i = 0; i < 3; ++i) {
        const auto& it = items[i];
        if(profile_.compact()) {
            auto* row = button(root_, it.name, nullptr, nullptr, 12, 64 + static_cast<int>(i) * 60, 216, 52);
            auto* g = icon(row, it.small, pal.accent); lv_obj_align(g, LV_ALIGN_LEFT_MID, 12, 0);
            label(row, it.title, 46, 5, 140, &th_inter_20, pal.text);
            label(row, it.detail, 46, 28, 140, &th_inter_16, pal.textSecondary);
            auto* c = icon(row, TH_ICON(chevron_right, 24), pal.textTertiary); lv_obj_align(c, LV_ALIGN_RIGHT_MID, -8, 0);
        } else {
            auto* tile = button(root_, it.name, nullptr, nullptr, 12 + static_cast<int>(i) * 156, 72, 144, 176);
            auto* g = icon(tile, it.big, pal.accent); lv_obj_align(g, LV_ALIGN_TOP_MID, 0, 28);
            auto* t = label(tile, it.title, 0, 0, 0, title_font(), pal.text); lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 88);
            auto* d = label(tile, it.detail, 0, 0, 128, &th_inter_16, pal.textSecondary);
            lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 120);
        }
    }
    label(root_, "Changes here stay on this demo; nothing is saved.", 12, profile_.compact() ? 250 : 268,
          profile_.width - 24, &th_inter_16, pal.textTertiary);
    if(profile_.compact()) { auto* note = lv_obj_get_child(root_, -1); lv_obj_set_height(note, LV_SIZE_CONTENT); lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP); }
    build_header("Settings", "back");
}
void Ui::build_themes() {
    const bool compact = profile_.compact();
    for(unsigned i = 0; i < 4; ++i) {
        const auto id = static_cast<TurnHubTheme::Id>(i);
        const auto& look = TurnHubTheme::palette(id);
        const int x = compact ? 12 : 12 + static_cast<int>(i % 2) * 234;
        const int y = compact ? 64 + static_cast<int>(i) * 58 : 64 + static_cast<int>(i / 2) * 124;
        const int w = compact ? 216 : 222, h = compact ? 50 : 112;
        auto* tile = button(root_, "theme" + std::to_string(i), nullptr, nullptr, x, y, w, h);
        // Each tile previews its own palette, whatever the current theme is.
        lv_obj_set_style_bg_color(tile, lv_color_hex(look.bg), 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(id == theme_ ? palette().accent : look.separatorStrong), 0);
        lv_obj_set_style_border_width(tile, id == theme_ ? 3 : 1, 0);
        auto* sample = box(tile, 10, compact ? 9 : 12, compact ? 52 : 88, compact ? 30 : 56);
        lv_obj_set_style_bg_opa(sample, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(sample, lv_color_hex(look.surface2), 0);
        lv_obj_set_style_radius(sample, 8, 0);
        lv_obj_set_style_border_width(sample, look.ornament ? 2 : 1, 0);
        lv_obj_set_style_border_color(sample, lv_color_hex(look.separatorStrong), 0);
        const uint32_t dots[] = {look.accent, look.turn, look.critical};
        for(unsigned d = 0; d < 3; ++d) {
            auto* dot = box(sample, 6 + static_cast<int>(d) * (compact ? 14 : 26), compact ? 9 : 18, compact ? 10 : 18, compact ? 10 : 18);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(dots[d]), 0);
        }
        const lv_font_t* font = look.ornament ? (compact ? &th_cinzel_20 : &th_cinzel_20) : &th_inter_20;
        if(compact) label(tile, look.label, 72, 13, 110, font, look.text);
        else {
            label(tile, look.label, 108, 18, 104, font, look.text);
            label(tile, look.light ? "Light" : look.ornament ? "Ornate" : "Dark", 108, 46, 104, &th_inter_16, look.textSecondary);
        }
        if(id == theme_) {
            auto* tick = icon(tile, TH_ICON(check, 24), look.accent);
            lv_obj_align(tick, compact ? LV_ALIGN_RIGHT_MID : LV_ALIGN_BOTTOM_RIGHT, -10, compact ? 0 : -10);
        }
    }
    build_header("Theme", "settings");
}
void Ui::build_players() {
    const auto& pal = palette();
    const auto& s = source_.snapshot();
    const bool compact = profile_.compact();
    for(unsigned i = 0; i < 4; ++i) {
        const auto& p = s.players[i];
        const int x = compact ? 12 : 12 + static_cast<int>(i % 2) * 234;
        const int y = compact ? 62 + static_cast<int>(i) * 58 : 72 + static_cast<int>(i / 2) * 96;
        const int w = compact ? 216 : 222, h = compact ? 52 : 84;
        auto* row = button(root_, "choose" + std::to_string(i), nullptr, nullptr, x, y, w, h, i == selected_ ? Look::Normal : Look::Quiet);
        if(i == selected_) { lv_obj_set_style_border_color(row, lv_color_hex(pal.accent), 0); lv_obj_set_style_border_width(row, 2, 0); }
        const int side = compact ? 36 : 52;
        token(row, 8, (h - side) / 2, side, p.avatar);
        label(row, p.name.c_str(), side + 18, compact ? 6 : 14, 100, compact ? &th_inter_20 : title_font(), p.active ? pal.text : pal.textTertiary);
        char line[32]; std::snprintf(line, sizeof line, p.active ? "%d life" : "Out", p.life);
        label(row, line, side + 18, compact ? 29 : 46, 100, &th_inter_16, pal.textSecondary);
        if(i == selected_) { auto* tick = icon(row, TH_ICON(check, 24), pal.accent); lv_obj_align(tick, LV_ALIGN_RIGHT_MID, -10, 0); }
    }
    build_header("Player", "settings");
}
void Ui::build_pairing() {
    const auto& pal = palette();
    const bool compact = profile_.compact();
    const char* code = "314159";
    auto* warn = pill(root_, 12, compact ? 60 : 64, "MOCK  /  no radio", pal.fillStrong, pal.warning);
    (void)warn;
    label(root_, "Pairing code", 12, compact ? 94 : 104, 0, &th_inter_16, pal.textSecondary);
    const int cell = compact ? 30 : 40, tall = compact ? 44 : 60, gap = compact ? 4 : 8;
    for(int d = 0; d < 6; ++d) {
        const int x = 12 + d * (cell + gap) + (d >= 3 ? (compact ? 8 : 16) : 0);
        auto* c = box(root_, x, compact ? 116 : 128, cell, tall);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(pal.surface2), 0);
        lv_obj_set_style_radius(c, 8, 0);
        lv_obj_set_style_border_width(c, 1, 0);
        lv_obj_set_style_border_color(c, lv_color_hex(pal.separatorStrong), 0);
        const char digit[2] = {code[d], 0};
        auto* l = label(c, digit, 0, 0, 0, compact ? &th_inter_20 : &th_inter_32, pal.text);
        lv_obj_center(l);
        if(!reduced_motion_) { lv_obj_set_style_opa(c, LV_OPA_TRANSP, 0); animate(c, set_opa, 0, 255, 220, 60 * static_cast<uint32_t>(d)); }
    }
    label(root_, compact ? "Match it on Atlas." : "Compare with the code on Atlas.", 12, compact ? 168 : 200, compact ? 216 : 290, &th_inter_16, pal.textSecondary);
    const int sx = compact ? 196 : 360, sy = compact ? 58 : 66;
    const int side = compact ? 32 : 72;
    if(reduced_motion_) {
        auto* g = icon(root_, compact ? TH_ICON(signal, 24) : TH_ICON(signal, 40), pal.accent);
        lv_obj_set_pos(g, sx + (side - (compact ? 24 : 40)) / 2, sy + (side - (compact ? 24 : 40)) / 2);
    } else {
        auto* spin = lv_spinner_create(root_);
        lv_obj_remove_style_all(spin);
        lv_obj_set_pos(spin, sx, sy); lv_obj_set_size(spin, side, side);
        lv_obj_remove_flag(spin, LV_OBJ_FLAG_CLICKABLE);
        lv_spinner_set_anim_params(spin, 1200, 240);
        lv_obj_set_style_arc_width(spin, compact ? 4 : 6, LV_PART_MAIN);
        lv_obj_set_style_arc_color(spin, lv_color_hex(pal.fillStrong), LV_PART_MAIN);
        lv_obj_set_style_arc_width(spin, compact ? 4 : 6, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(spin, lv_color_hex(pal.accent), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(spin, true, LV_PART_INDICATOR);
        if(!compact) {
            auto* g = icon(root_, TH_ICON(signal, 24), pal.accent);
            lv_obj_set_pos(g, sx + 24, sy + 24);
        }
    }
    if(!compact) lv_obj_set_style_text_align(label(root_, "Searching", 348, 146, 96, &th_inter_16, pal.textTertiary), LV_TEXT_ALIGN_CENTER, 0);
    if(compact) {
        button(root_, "pair", "Codes match", TH_ICON(check, 24), 12, 196, 216, 48, Look::Accent);
        button(root_, "unpair", "Unpair (mock)", TH_ICON(close, 24), 12, 252, 216, 44);
    } else {
        button(root_, "pair", "Codes match", TH_ICON(check, 24), 12, 236, 222, 52, Look::Accent);
        button(root_, "unpair", "Unpair (mock)", TH_ICON(close, 24), 246, 236, 222, 52);
    }
    build_header("Pairing", "settings");
}
void Ui::render(Motion motion) {
    lv_group_remove_all_objs(group_);
    controls_.clear();
    timer_ = life_ = name_ = status_ = status_pill_ = avatar_ = token_ = clock_arc_ = clock_bar_ = badge_ = nullptr;
    overlay_ = overlay_icon_ = overlay_title_ = overlay_text_ = turn_frame_ = pause_icon_ = pause_text_ = nullptr;
    sheet_ = sheet_scrim_ = sheet_life_ = sheet_badge_ = nullptr;
    card_life_.fill(nullptr); card_tag_.fill(nullptr); cards_.fill(nullptr);
    card_token_.fill(nullptr); card_avatar_.fill(nullptr); card_badge_.fill(nullptr);
    auto* old = screen_obj_;
    screen_obj_ = lv_obj_create(nullptr);
    lv_obj_remove_style_all(screen_obj_);
    lv_obj_set_style_bg_opa(screen_obj_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen_obj_, lv_color_hex(palette().bg), 0);
    lv_obj_remove_flag(screen_obj_, LV_OBJ_FLAG_SCROLLABLE);
    root_ = box(screen_obj_, 0, 0, profile_.width, profile_.height);
    lv_obj_set_style_text_font(root_, &th_inter_16, 0);
    switch(screen_) {
    case Screen::Gameplay:
        if(profile_.compact()) build_gameplay_sigil(); else build_gameplay_atlas();
        // A focus target for gameplay joystick semantics, independent of touch buttons.
        lv_obj_add_event_cb(root_, key_event, LV_EVENT_KEY, this);
        lv_group_add_obj(group_, root_);
        lv_group_focus_obj(root_);
        break;
    case Screen::Settings: build_settings(); break;
    case Screen::Themes: build_themes(); break;
    case Screen::Players: build_players(); break;
    case Screen::Pairing: build_pairing(); break;
    }
    if(!old) lv_screen_load(screen_obj_);
    else if(motion == Motion::None || reduced_motion_) { lv_screen_load(screen_obj_); lv_obj_delete(old); }
    else {
        const auto type = motion == Motion::Forward ? LV_SCR_LOAD_ANIM_MOVE_LEFT
                        : motion == Motion::Back ? LV_SCR_LOAD_ANIM_MOVE_RIGHT : LV_SCR_LOAD_ANIM_FADE_IN;
        lv_screen_load_anim(screen_obj_, type, motion == Motion::Fade ? 260 : 240, 0, true);
    }
    revision_ = 0;
    fresh_ = true;
    update();
}

// ---- Live state ----------------------------------------------------------
int Ui::accumulate(unsigned seat, int delta) {
    // Consecutive taps merge into one running "+3" until the player pauses.
    if(lv_tick_elaps(delta_at_[seat]) > 1800) delta_[seat] = 0;
    delta_[seat] += delta;
    delta_at_[seat] = lv_tick_get();
    return delta_[seat];
}
void Ui::show_badge(lv_obj_t* badge, int total) {
    if(!badge) return;
    char text[16]; std::snprintf(text, sizeof text, "%+d", total);
    lv_label_set_text(badge, total ? text : "");
    lv_obj_set_style_text_color(badge, lv_color_hex(total >= 0 ? palette().good : palette().critical), 0);
    lv_anim_delete(badge, set_opa);
    set_opa(badge, LV_OPA_COVER);
    animate(badge, set_ty, 10, 0, ms(180));
    animate(badge, set_opa, 255, 0, ms(380), 1500);
}
void Ui::update_gameplay(const Snapshot& s) {
    const auto& pal = palette();
    const bool compact = profile_.compact();
    const bool running = s.phase == Phase::Running;
    char clock[24];
    std::snprintf(clock, sizeof clock, "%02u:%02u", s.remaining_seconds / 60, s.remaining_seconds % 60);
    lv_label_set_text(timer_, clock);
    const unsigned total = std::max(1u, s.turn_seconds);
    const int32_t progress = static_cast<int32_t>(std::min(1000u, s.remaining_seconds * 1000u / total));
    const bool low = running && s.remaining_seconds <= 10;
    const uint32_t urgency = !running ? pal.textTertiary : low ? pal.critical : s.remaining_seconds <= 30 ? pal.warning : pal.turn;
    const bool ticking = !fresh_ && running && s.remaining_seconds + 1 == last_seconds_;
    lv_obj_set_style_text_color(timer_, lv_color_hex(low ? pal.critical : pal.text), 0);
    if(clock_arc_) {
        lv_obj_set_style_arc_color(clock_arc_, lv_color_hex(urgency), LV_PART_INDICATOR);
        // A steady sweep between whole-second updates; jumps settle quickly.
        animate(clock_arc_, set_arc, lv_arc_get_value(clock_arc_), progress, fresh_ ? 0 : ms(ticking ? 980 : 320), 0,
                ticking ? lv_anim_path_linear : lv_anim_path_ease_out);
    }
    if(clock_bar_) {
        lv_obj_set_style_bg_color(clock_bar_, lv_color_hex(urgency), LV_PART_INDICATOR);
        lv_bar_set_value(clock_bar_, progress, fresh_ || reduced_motion_ ? LV_ANIM_OFF : LV_ANIM_ON);
    }
    if(low && ticking) pop(timer_, 290);

    std::array<int, 4> change{};
    for(unsigned i = 0; i < 4; ++i) {
        change[i] = fresh_ ? 0 : s.players[i].life - last_life_[i];
        if(change[i]) accumulate(i, change[i]);
    }
    const auto& holder = s.players[s.turn_holder];
    const bool turn_moved = !fresh_ && s.turn_holder != last_turn_;
    if(compact) {
        const auto& player = s.players[selected_];
        const bool mine = player.participant_id == holder.participant_id;
        lv_label_set_text(name_, player.name.c_str());
        lv_image_set_src(avatar_, assets_.avatar(player.avatar));
        const char* tag = !player.active ? "OUT" : s.phase == Phase::GameOver ? (mine ? "WINNER" : "GAME OVER")
                          : s.phase == Phase::Paused ? "PAUSED" : mine ? "YOUR TURN" : "WAITING";
        lv_label_set_text(status_, tag);
        const bool your_turn = running && mine && player.active;
        uint32_t bg = pal.fillStrong, fg = pal.textSecondary;
        if(your_turn) { bg = pal.turn; fg = pal.onTurn; }
        else if(s.phase == Phase::GameOver && mine) { bg = pal.accent; fg = pal.onAccent; }
        else if(s.phase == Phase::Paused) fg = pal.warning;
        else if(!player.active) fg = pal.critical;
        lv_obj_set_style_bg_color(status_pill_, lv_color_hex(bg), 0);
        lv_obj_set_style_text_color(status_, lv_color_hex(fg), 0);
        lv_obj_set_style_border_color(token_, lv_color_hex(your_turn ? pal.turn : pal.accent), 0);
        lv_obj_set_style_image_recolor(avatar_, lv_color_hex(your_turn ? pal.turn : pal.accent), 0);
        lv_obj_set_style_bg_color(token_, lv_color_hex(your_turn ? pal.turnSoft : pal.accentSoft), 0);
        char text[16]; std::snprintf(text, sizeof text, "%d", player.life);
        lv_label_set_text(life_, text);
        lv_obj_set_style_opa(life_, static_cast<lv_opa_t>(running ? LV_OPA_COVER : LV_OPA_60), 0);
        if(change[selected_]) { pop(life_); show_badge(badge_, delta_[selected_]); }
        if(your_turn) {
            lv_obj_remove_flag(turn_frame_, LV_OBJ_FLAG_HIDDEN);
            if(!reduced_motion_) pulse(turn_frame_, set_border_opa, 90, 255, 900);
            if(turn_moved) pop(status_pill_, 300);
        } else {
            lv_anim_delete(turn_frame_, set_border_opa);
            lv_obj_add_flag(turn_frame_, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_label_set_text(name_, holder.name.c_str());
        lv_label_set_text(status_, phase_name(s.phase));
        const uint32_t chip_bg = running ? pal.turnSoft : s.phase == Phase::Paused ? pal.fillStrong : pal.accentSoft;
        const uint32_t chip_fg = running ? pal.turn : s.phase == Phase::Paused ? pal.warning : pal.accent;
        lv_obj_set_style_bg_color(status_pill_, lv_color_hex(chip_bg), 0);
        lv_obj_set_style_text_color(status_, lv_color_hex(chip_fg), 0);
        if(turn_moved && !reduced_motion_) { animate(name_, set_tx, 28, 0, 260); animate(name_, set_opa, 0, 255, 220); }
        for(unsigned i = 0; i < 4; ++i) {
            const auto& p = s.players[i];
            char text[16]; std::snprintf(text, sizeof text, "%d", p.life);
            lv_label_set_text(card_life_[i], text);
            const bool turn = i == s.turn_holder && p.active;
            const char* tag = !p.active ? "OUT" : turn ? (s.phase == Phase::GameOver ? "WINNER" : "TURN") : "SEATED";
            lv_label_set_text(card_tag_[i], tag);
            lv_obj_set_style_text_color(card_tag_[i], lv_color_hex(!p.active ? pal.critical : turn ? (s.phase == Phase::GameOver ? pal.accent : pal.turn) : pal.textSecondary), 0);
            lv_obj_set_style_border_color(cards_[i], lv_color_hex(turn ? pal.turn : pal.separatorStrong), 0);
            lv_obj_set_style_border_width(cards_[i], turn ? 2 : (pal.ornament ? 2 : 1), 0);
            lv_obj_set_style_outline_width(cards_[i], i == selected_ ? 2 : 0, 0);
            lv_obj_set_style_opa(cards_[i], static_cast<lv_opa_t>(p.active ? LV_OPA_COVER : LV_OPA_50), 0);
            lv_obj_set_style_border_color(card_token_[i], lv_color_hex(turn ? pal.turn : pal.accent), 0);
            lv_obj_set_style_bg_color(card_token_[i], lv_color_hex(turn ? pal.turnSoft : pal.accentSoft), 0);
            lv_obj_set_style_image_recolor(card_avatar_[i], lv_color_hex(turn ? pal.turn : pal.accent), 0);
            if(turn && running && !reduced_motion_) pulse(cards_[i], set_shadow, 40, 150, 1000);
            else { lv_anim_delete(cards_[i], set_shadow); set_shadow(cards_[i], turn ? 90 : 0); }
            if(change[i]) { pop(card_life_[i]); show_badge(card_badge_[i], delta_[i]); }
        }
        if(sheet_) {
            char text[16]; std::snprintf(text, sizeof text, "%d", s.players[selected_].life);
            lv_label_set_text(sheet_life_, text);
            if(change[selected_]) { pop(sheet_life_); show_badge(sheet_badge_, delta_[selected_]); }
        }
        // Pause / game-over veil.
        auto* pause = control("pause");
        if(pause) {
            auto* glyph = lv_obj_get_child(pause, 0);
            auto* caption = lv_obj_get_child(pause, 1);
            lv_image_set_src(glyph, s.phase == Phase::Paused ? TH_ICON(play, 24) : TH_ICON(pause, 24));
            lv_label_set_text(caption, s.phase == Phase::Paused ? "Resume" : "Pause");
            lv_obj_set_style_opa(pause, static_cast<lv_opa_t>(s.phase == Phase::GameOver ? LV_OPA_40 : LV_OPA_COVER), 0);
        }
        if(running) lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
        else {
            const bool over = s.phase == Phase::GameOver;
            lv_image_set_src(overlay_icon_, over ? TH_ICON(crown, 40) : TH_ICON(pause, 40));
            lv_obj_set_style_image_recolor(overlay_icon_, lv_color_hex(over ? pal.accent : pal.warning), 0);
            char title[48]; std::snprintf(title, sizeof title, over ? "%s wins" : "Paused", holder.name.c_str());
            lv_label_set_text(overlay_title_, title);
            lv_label_set_text(overlay_text_, over ? "GAME OVER  /  tap Sample for a new table" : "Tap anywhere or Resume to continue");
            const bool appearing = lv_obj_has_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
            if(appearing && !fresh_) {
                animate(overlay_, set_opa, 0, 255, ms(220));
                if(!reduced_motion_) { lv_obj_set_style_transform_pivot_x(overlay_icon_, lv_pct(50), 0); lv_obj_set_style_transform_pivot_y(overlay_icon_, lv_pct(50), 0); animate(overlay_icon_, set_scale, 160, 256, 320, 0, lv_anim_path_overshoot); }
            } else set_opa(overlay_, LV_OPA_COVER);
        }
    }
    for(unsigned i = 0; i < 4; ++i) last_life_[i] = s.players[i].life;
    last_turn_ = s.turn_holder;
    last_seconds_ = s.remaining_seconds;
    last_phase_ = s.phase;
    fresh_ = false;
}
void Ui::update() {
    const auto& s = source_.snapshot();
    if(screen_ != Screen::Gameplay || revision_ == s.revision) return;
    revision_ = s.revision;
    update_gameplay(s);
}

// ---- Atlas life sheet -----------------------------------------------------
void Ui::open_sheet(unsigned seat) {
    if(sheet_ || profile_.compact()) return;
    const auto& pal = palette();
    const auto& p = source_.snapshot().players[seat];
    selected_ = seat;
    sheet_scrim_ = box(root_, 0, 0, profile_.width, profile_.height);
    lv_obj_set_style_bg_color(sheet_scrim_, lv_color_hex(pal.scrim), 0);
    animate(sheet_scrim_, set_bg_opa, 0, LV_OPA_60, ms(200));
    bind(sheet_scrim_, "sheet_scrim", false);
    sheet_ = panel(root_, 12, 92, 456, 220);
    lv_obj_set_style_bg_color(sheet_, lv_color_hex(pal.surface2), 0);
    lv_obj_set_style_shadow_width(sheet_, 30, 0);
    lv_obj_set_style_shadow_color(sheet_, lv_color_hex(pal.scrim), 0);
    lv_obj_set_style_shadow_opa(sheet_, LV_OPA_70, 0);
    lv_obj_add_flag(sheet_, LV_OBJ_FLAG_CLICKABLE); // Swallow taps between buttons.
    token(sheet_, 16, 14, 56, p.avatar);
    label(sheet_, p.name.c_str(), 86, 14, 200, title_font(true), pal.text);
    char seat_text[32]; std::snprintf(seat_text, sizeof seat_text, "Seat %u  /  life total", seat + 1);
    label(sheet_, seat_text, 86, 52, 200, &th_inter_16, pal.textSecondary);
    sheet_life_ = label(sheet_, "", 280, 6, 160, &th_inter_56, pal.text);
    lv_obj_set_style_text_align(sheet_life_, LV_TEXT_ALIGN_RIGHT, 0);
    char life[16]; std::snprintf(life, sizeof life, "%d", p.life); lv_label_set_text(sheet_life_, life);
    sheet_badge_ = label(sheet_, "", 300, 76, 140, &th_inter_20, pal.good);
    lv_obj_set_style_text_align(sheet_badge_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_opa(sheet_badge_, LV_OPA_TRANSP, 0);
    const char* names[] = {"sheet_m5", "sheet_m1", "sheet_p1", "sheet_p5"};
    const char* texts[] = {"-5", "-1", "+1", "+5"};
    for(int i = 0; i < 4; ++i)
        button(sheet_, names[i], texts[i], nullptr, 16 + i * 108, 104, 100, 56, i == 2 ? Look::Accent : Look::Normal, &th_inter_32);
    button(sheet_, "sheet_close", "Done", TH_ICON(chevron_down, 24), 16, 168, 424, 40, Look::Quiet);
    lv_group_focus_obj(control("sheet_p1"));
    if(!reduced_motion_) animate(sheet_, set_ty, 240, 0, 280, 0, lv_anim_path_ease_out);
}
void Ui::close_sheet() {
    if(!sheet_) return;
    for(auto it = controls_.begin(); it != controls_.end();)
        if(it->first.rfind("sheet_", 0) == 0) { lv_group_remove_obj(it->second); it = controls_.erase(it); } else ++it;
    lv_obj_remove_flag(sheet_scrim_, LV_OBJ_FLAG_CLICKABLE);
    if(reduced_motion_) { lv_obj_delete(sheet_); lv_obj_delete(sheet_scrim_); }
    else {
        animate(sheet_, set_ty, 0, 240, 220, 0, lv_anim_path_ease_in, delete_var);
        animate(sheet_scrim_, set_bg_opa, LV_OPA_60, 0, 220, 0, lv_anim_path_linear, delete_var);
    }
    sheet_ = sheet_scrim_ = sheet_life_ = sheet_badge_ = nullptr;
    lv_group_focus_obj(root_);
}

// ---- Input -----------------------------------------------------------------
void Ui::show(Screen screen) {
    const auto motion = screen == screen_ ? Motion::None : depth(screen) >= depth(screen_) ? Motion::Forward : Motion::Back;
    screen_ = screen; render(motion);
}
void Ui::set_theme(TurnHubTheme::Id theme) {
    if(!TurnHubTheme::valid(static_cast<uint8_t>(theme))) return;
    theme_ = theme; render(Motion::Fade);
}
void Ui::activate(const std::string& name) {
    const auto& s = source_.snapshot();
    if(name == "menu" || name == "settings") show(Screen::Settings);
    else if(name == "back") show(Screen::Gameplay);
    else if(name == "pairing") show(Screen::Pairing);
    else if(name == "themes") show(Screen::Themes);
    else if(name == "players") show(Screen::Players);
    else if(name.rfind("theme", 0) == 0) set_theme(static_cast<TurnHubTheme::Id>(name.back() - '0'));
    else if(name.rfind("choose", 0) == 0) { selected_ = static_cast<unsigned>(name.back() - '0'); show(Screen::Players); }
    else if(name.rfind("seat", 0) == 0) { selected_ = static_cast<unsigned>(name.back() - '0'); revision_ = 0; update(); open_sheet(selected_); }
    else if(name == "sheet_close" || name == "sheet_scrim") close_sheet();
    else if(name.rfind("sheet_", 0) == 0) {
        const int amount = name == "sheet_m5" ? -5 : name == "sheet_m1" ? -1 : name == "sheet_p1" ? 1 : 5;
        sink_.request({ActionKind::AdjustLife, s.players[selected_].participant_id, amount});
    }
    else if(name == "veil") { if(s.phase == Phase::Paused) sink_.request({ActionKind::RequestPause}); }
    else if(name == "player") { selected_ = (selected_ + 1) % s.players.size(); revision_ = 0; fresh_ = true; }
    else if(name == "plus" || name == "minus") sink_.request({ActionKind::AdjustLife, s.players[selected_].participant_id, name == "plus" ? 1 : -1});
    else if(name == "pause") sink_.request({ActionKind::RequestPause});
    else if(name == "scenario") sink_.request({ActionKind::DemoNextScenario});
    else if(name == "pair" || name == "unpair") { sink_.request({name == "pair" ? ActionKind::MockPair : ActionKind::MockUnpair}); show(Screen::Settings); }
    update();
}
void Ui::click(lv_event_t* e) {
    auto* binding = static_cast<Binding*>(lv_event_get_user_data(e));
    Ui* ui = binding->ui;
    // A screen still sliding out must not act.
    if(lv_obj_get_screen(static_cast<lv_obj_t*>(lv_event_get_current_target(e))) != ui->screen_obj_) return;
    // Copy: a navigation action can delete the object and its binding.
    const std::string name = binding->name;
    ui->activate(name);
}
void Ui::free_binding(lv_event_t* e) {
    auto* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    const auto count = lv_obj_get_event_count(obj);
    for(uint32_t i = 0; i < count; ++i) {
        auto* dsc = lv_obj_get_event_dsc(obj, i);
        if(lv_event_dsc_get_cb(dsc) == click) delete static_cast<Binding*>(lv_event_dsc_get_user_data(dsc));
    }
}
void Ui::gameplay_key(uint32_t key) {
    if(key == LV_KEY_UP) show(Screen::Settings);
    else if(key == LV_KEY_DOWN) activate("player");
    else if(key == LV_KEY_LEFT) activate("minus");
    else if(key == LV_KEY_RIGHT) activate("plus");
    else if(key == LV_KEY_ENTER) sink_.request({ActionKind::DemoNextScenario});
    update();
}
void Ui::key_event(lv_event_t* e) {
    auto* ui = static_cast<Ui*>(lv_event_get_user_data(e));
    ui->gameplay_key(lv_event_get_key(e));
}
void Ui::navigation(NavEvent e) {
    if(keys_.size() < 32) keys_.push_back(e);
}
void Ui::read_keypad(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* ui = static_cast<Ui*>(lv_indev_get_user_data(indev));
    data->key = ui->last_key_; data->state = ui->key_state_;
    if(ui->keys_.empty()) return;
    const auto e = ui->keys_.front(); ui->keys_.pop_front();
    if(!e.pressed) { ui->key_state_ = LV_INDEV_STATE_RELEASED; data->key = ui->last_key_; data->state = ui->key_state_; data->continue_reading = !ui->keys_.empty(); return; }
    // With the Atlas life sheet open, the five-way moves between its buttons.
    const bool game = ui->screen_ == Screen::Gameplay && !ui->sheet_;
    if(game) lv_group_focus_obj(ui->root_);
    switch(e.key) {
    case NavKey::Up: data->key = game ? LV_KEY_UP : LV_KEY_PREV; break;
    case NavKey::Down: data->key = game ? LV_KEY_DOWN : LV_KEY_NEXT; break;
    case NavKey::Left: data->key = game ? LV_KEY_LEFT : LV_KEY_PREV; break;
    case NavKey::Right: data->key = game ? LV_KEY_RIGHT : LV_KEY_NEXT; break;
    case NavKey::Select: data->key = LV_KEY_ENTER; break;
    }
    ui->last_key_ = data->key;
    ui->key_state_ = LV_INDEV_STATE_PRESSED;
    data->state = ui->key_state_;
    data->continue_reading = !ui->keys_.empty();
}
lv_obj_t* Ui::control(const std::string& name) const {
    auto it = controls_.find(name); return it == controls_.end() ? nullptr : it->second;
}
std::string Ui::visible_text(const std::string& role) const {
    auto* obj = role == "name" ? name_ : role == "life" ? life_ : role == "status" ? status_ : role == "timer" ? timer_ : nullptr;
    return obj ? lv_label_get_text(obj) : "";
}
bool Ui::bounds_valid() {
    lv_obj_update_layout(screen_obj_);
    for(const auto& entry : controls_) {
        lv_area_t a; lv_obj_get_coords(entry.second, &a);
        if(a.x1 < 0 || a.y1 < 0 || a.x2 >= profile_.width || a.y2 >= profile_.height) return false;
    }
    return true;
}
} // namespace th
