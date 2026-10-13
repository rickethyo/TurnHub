#include "ui.hpp"
#include "fonts.hpp"
#include "icons.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

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
void set_ty(void* o, int32_t v) { lv_obj_set_style_translate_y(as_obj(o), v, 0); }
void set_tx(void* o, int32_t v) { lv_obj_set_style_translate_x(as_obj(o), v, 0); }
void set_shadow(void* o, int32_t v) { lv_obj_set_style_shadow_opa(as_obj(o), static_cast<lv_opa_t>(v), 0); }
void set_border_opa(void* o, int32_t v) { lv_obj_set_style_border_opa(as_obj(o), static_cast<lv_opa_t>(v), 0); }
void set_bg_opa(void* o, int32_t v) { lv_obj_set_style_bg_opa(as_obj(o), static_cast<lv_opa_t>(v), 0); }
void set_width(void* o, int32_t v) { lv_obj_set_width(as_obj(o), v); }
// The turn ring and the bright head riding its moving end (as in the app).
void set_ring(void* o, int32_t v) {
    auto* arc = as_obj(o);
    lv_arc_set_value(arc, v);
    auto* head = static_cast<lv_obj_t*>(lv_obj_get_user_data(arc));
    if(!head) return;
    if(v <= 0) { lv_obj_add_flag(head, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(head, LV_OBJ_FLAG_HIDDEN);
    const int32_t side = lv_obj_get_style_width(arc, 0), width = lv_obj_get_style_arc_width(arc, LV_PART_INDICATOR);
    const int32_t r = side / 2 - width / 2, dot = lv_obj_get_style_width(head, 0);
    const auto angle = static_cast<int16_t>((270 + v * 360 / 1000) % 360);
    lv_obj_set_pos(head, lv_obj_get_style_x(arc, 0) + side / 2 + r * lv_trigo_cos(angle) / 32768 - dot / 2,
                   lv_obj_get_style_y(arc, 0) + side / 2 + r * lv_trigo_sin(angle) / 32768 - dot / 2);
}
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
    if(lv_anim_get(obj, cb)) return; // Already breathing; restarting would stutter.
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, obj); lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, low, high); lv_anim_set_duration(&a, period); lv_anim_set_reverse_duration(&a, period);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}
int depth(Screen s) { return s == Screen::Gameplay ? 0 : s == Screen::Settings ? 1 : 2; }
uint32_t hsl(float h, float s, float l) {
    const float c = (1 - std::fabs(2 * l - 1)) * s, hp = h / 60, x = c * (1 - std::fabs(std::fmod(hp, 2.0f) - 1)), m = l - c / 2;
    float r = 0, g = 0, b = 0;
    if(hp < 1) { r = c; g = x; } else if(hp < 2) { r = x; g = c; } else if(hp < 3) { g = c; b = x; }
    else if(hp < 4) { g = x; b = c; } else if(hp < 5) { r = x; b = c; } else { r = c; b = x; }
    auto to = [&](float v) { return static_cast<uint32_t>(std::lround((v + m) * 255)); };
    return to(r) << 16 | to(g) << 8 | to(b);
}
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
                              : (large ? &th_bold_28 : &th_semi_20);
}
// Seat hues and their panel washes follow the app (Palette.avatarColor).
uint32_t Ui::seat_color(unsigned seat) const {
    static const float hues[] = {38, 168, 12, 205, 95, 280, 330, 60};
    const auto& pal = palette();
    const bool contrast = theme_ == TurnHubTheme::Id::Contrast;
    const float s = contrast ? 0.0f : pal.ornament ? 0.42f : 0.5f;
    const float l = contrast ? 0.18f : pal.ornament ? 0.34f : pal.light ? 0.44f : 0.38f;
    return hsl(hues[seat % 8], s, l);
}
uint32_t Ui::seat_tint(unsigned seat) const {
    const auto mixed = lv_color_mix(lv_color_hex(seat_color(seat)), lv_color_hex(palette().surface1), palette().light ? 41 : 56);
    return lv_color_to_u32(mixed) & 0xFFFFFF;
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
    lv_obj_set_style_radius(obj, 20, 0);
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
lv_obj_t* Ui::token(lv_obj_t* parent, int x, int y, int side, uint8_t avatar, unsigned seat, lv_obj_t** image) {
    // A filled disc in the seat's hue with a light glyph, like the app's avatars.
    auto* circle = box(parent, x, y, side, side);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(seat_color(seat)), 0);
    lv_obj_set_style_border_color(circle, lv_color_hex(palette().turn), 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    auto* glyph = icon(circle, assets_.avatar(avatar), palette().ornament ? 0xFFF6E4 : 0xFFFFFF);
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
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_pad_hor(obj, 8, 0); lv_obj_set_style_pad_ver(obj, 3, 0);
    auto* caption = label(obj, text, 0, 0, 0, &th_eyebrow_13, fg);
    if(text_out) *text_out = caption;
    return obj;
}
void Ui::bind(lv_obj_t* obj, const std::string& name, bool focusable, bool holdable) {
    controls_[name] = obj;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    // Owned by the object, so a screen sliding out keeps valid callbacks.
    auto* binding = new Binding{this, name};
    if(holdable) {
        // Tap for one step; hold for big steps that repeat while held.
        lv_obj_add_event_cb(obj, click, LV_EVENT_SHORT_CLICKED, binding);
        lv_obj_add_event_cb(obj, click, LV_EVENT_LONG_PRESSED, binding);
        lv_obj_add_event_cb(obj, click, LV_EVENT_LONG_PRESSED_REPEAT, binding);
    } else lv_obj_add_event_cb(obj, click, LV_EVENT_CLICKED, binding);
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
    const uint32_t fill = look == Look::Accent ? pal.accent : look == Look::Turn ? pal.turn : look == Look::Quiet ? pal.surface1 : pal.surface3;
    const uint32_t ink = look == Look::Accent ? pal.onAccent : look == Look::Turn ? pal.onTurn : pal.text;
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(look == Look::Accent ? pal.accentPressed : look == Look::Turn ? pal.turnSoft : pal.fillStrong), LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj, 14, 0);
    lv_obj_set_style_border_width(obj, look == Look::Accent || look == Look::Turn ? 0 : 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(pal.separatorStrong), 0);
    lv_obj_set_style_transform_width(obj, -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(obj, -3, LV_STATE_PRESSED);
    if(!reduced_motion_) {
        lv_obj_set_style_transition(obj, &transition, 0);
        lv_obj_set_style_transition(obj, &transition, LV_STATE_PRESSED);
    }
    // The Sigil's joystick moves a visible focus ring; touch never shows it.
    lv_obj_set_style_outline_width(obj, 3, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(obj, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(obj, lv_color_hex(pal.focus), LV_STATE_FOCUS_KEY);
    if(text || glyph) {
        lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(obj, 8, 0);
        if(glyph) icon(obj, glyph, ink);
        if(text) label(obj, text, 0, 0, 0, font ? font : &th_semi_16, ink);
    }
    bind(obj, name);
    return obj;
}
lv_obj_t* Ui::hold_to_pass(lv_obj_t* parent, const std::string& name, int x, int y, int w, int h) {
    // Hold to pass, so a brushed panel never ends a turn (as in the app).
    const auto& pal = palette();
    auto* obj = box(parent, x, y, w, h);
    lv_obj_set_style_radius(obj, 14, 0);
    lv_obj_set_style_clip_corner(obj, true, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(pal.turnSoft), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(pal.turn), 0);
    lv_obj_set_style_outline_width(obj, 3, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(obj, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(obj, lv_color_hex(pal.focus), LV_STATE_FOCUS_KEY);
    auto* fill = box(obj, 0, 0, 0, h);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, lv_color_hex(pal.turn), 0);
    auto* caption = label(obj, "Hold to pass", 0, 0, 0, &th_semi_16, pal.text);
    lv_obj_center(caption);
    controls_[name] = obj;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    auto* binding = new Binding{this, name};
    for(auto code : {LV_EVENT_PRESSED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
        lv_obj_add_event_cb(obj, pass_event, code, binding);
    lv_obj_add_event_cb(obj, free_binding, LV_EVENT_DELETE, nullptr);
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
    const int w = profile_.width;
    auto* brand = box(parent, 10, 2, LV_SIZE_CONTENT, 28);
    lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brand, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(brand, 6, 0);
    icon(brand, TH_ICON(sigil, 24), pal.accent);
    label(brand, "Sigil", 0, 0, 0, &th_semi_20, pal.text);
    auto* demo = label(brand, "DEMO", 0, 0, 0, &th_eyebrow_13, pal.textTertiary);
    lv_obj_set_style_margin_left(demo, 4, 0);
    // Battery is not measured in the simulator: show the gauge outline and "--", never a guess.
    auto* bat = icon(parent, TH_ICON(battery, 24), pal.textSecondary);
    lv_obj_set_pos(bat, w - 58, 4);
    label(parent, "--", w - 30, 7, 0, &th_semi_16, pal.textSecondary);
}
void Ui::build_header(const char* title, const char* back) {
    label(root_, title, 64, profile_.compact() ? 14 : 12, profile_.width - 76, title_font(!profile_.compact()), palette().text);
    button(root_, back, nullptr, TH_ICON(chevron_left, 24), 10, 8, 44, 40, Look::Quiet);
}
void Ui::build_gameplay_atlas() {
    // The app's table layout: four seat panels around a centre hub. Atlas is
    // touch-first, so every action is on screen; there is no focus ring.
    const auto& pal = palette();
    const auto& s = source_.snapshot();
    constexpr int PW = 234, PH = 154, HUB = 136;
    for(unsigned i = 0; i < 4; ++i) {
        const auto& p = s.players[i];
        const bool left = i % 2 == 0, top = i < 2;
        auto& seat = seats_[i];
        seat.panel = panel(root_, left ? 4 : 242, top ? 4 : 162, PW, PH);
        lv_obj_set_style_bg_color(seat.panel, lv_color_hex(seat_tint(i)), 0);
        lv_obj_set_style_shadow_color(seat.panel, lv_color_hex(pal.turn), 0);
        lv_obj_set_style_shadow_width(seat.panel, 24, 0);
        lv_obj_set_style_shadow_opa(seat.panel, LV_OPA_TRANSP, 0);
        // Tap halves: top raises, bottom lowers; holding repeats by five.
        seat.up = box(seat.panel, 0, 0, PW, PH / 2);
        seat.down = box(seat.panel, 0, PH / 2, PW, PH - PH / 2);
        for(auto* half : {seat.up, seat.down}) {
            lv_obj_set_style_bg_color(half, lv_color_hex(pal.text), 0);
            lv_obj_set_style_bg_opa(half, LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_opa(half, 18, LV_STATE_PRESSED);
            lv_obj_set_style_radius(half, 20, 0);
        }
        if(p.active) {
            bind(seat.up, "plus" + std::to_string(i), false, true);
            bind(seat.down, "minus" + std::to_string(i), false, true);
        }
        const int outer = left ? 14 : PW - 38;
        auto* plus = icon(seat.panel, TH_ICON(plus, 24), pal.textTertiary);
        lv_obj_set_pos(plus, outer, top ? 46 : 60);
        auto* minus = icon(seat.panel, TH_ICON(minus, 24), pal.textTertiary);
        lv_obj_set_pos(minus, outer, top ? 112 : 120);
        // Header hugs the outer edge, clear of the hub.
        auto* head = box(seat.panel, 12, 10, PW - 24, 32);
        lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(head, left ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(head, 8, 0);
        seat.token = token(head, 0, 0, 30, p.avatar, i, &seat.glyph);
        auto* name = label(head, p.name.c_str(), 0, 0, 0, &th_semi_20, pal.text);
        lv_obj_set_style_max_width(name, 104, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        seat.tag = pill(head, 0, 0, "", pal.turn, pal.onTurn, &seat.tag_text);
        // Life, biased to the outer side so the hub never covers it.
        const int cx = left ? 104 : PW - 104;
        seat.life = label(seat.panel, "", cx - 70, top ? 50 : 62, 140, &th_num_64, pal.text);
        lv_obj_set_style_text_align(seat.life, LV_TEXT_ALIGN_CENTER, 0);
        seat.badge = label(seat.panel, "", left ? cx + 52 : cx - 104, top ? 62 : 74, 52, &th_semi_20, pal.good);
        lv_obj_set_style_text_align(seat.badge, left ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_opa(seat.badge, LV_OPA_TRANSP, 0);
        seat.pass = hold_to_pass(seat.panel, "pass" + std::to_string(i), left ? 46 : 64, 112, 124, 32);
        lv_obj_add_flag(seat.pass, LV_OBJ_FLAG_HIDDEN);
    }
    // Centre hub: the turn ring, the clock, and the table menu on tap.
    hub_ = box(root_, 240 - HUB / 2, 160 - HUB / 2, HUB, HUB);
    lv_obj_set_style_radius(hub_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(hub_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(hub_, lv_color_hex(pal.surface2), 0);
    lv_obj_set_style_bg_color(hub_, lv_color_hex(pal.surface3), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(hub_, 4, 0);
    lv_obj_set_style_border_color(hub_, lv_color_hex(pal.bg), 0);
    lv_obj_set_style_shadow_width(hub_, 30, 0);
    lv_obj_set_style_shadow_color(hub_, lv_color_hex(pal.turn), 0);
    lv_obj_set_style_shadow_opa(hub_, LV_OPA_TRANSP, 0);
    bind(hub_, "hub", false);
    clock_arc_ = lv_arc_create(hub_);
    lv_obj_remove_style_all(clock_arc_);
    lv_obj_set_size(clock_arc_, HUB - 16, HUB - 16);
    lv_obj_set_pos(clock_arc_, 4, 4);
    lv_obj_remove_flag(clock_arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(clock_arc_, 270);
    lv_arc_set_bg_angles(clock_arc_, 0, 360);
    lv_arc_set_range(clock_arc_, 0, 1000);
    lv_obj_set_style_arc_width(clock_arc_, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(clock_arc_, lv_color_hex(pal.fillStrong), LV_PART_MAIN);
    lv_obj_set_style_arc_width(clock_arc_, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(clock_arc_, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(clock_arc_, lv_color_hex(pal.turn), LV_PART_INDICATOR);
    clock_head_ = box(hub_, 0, 0, 4, 4);
    lv_obj_set_style_radius(clock_head_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(clock_head_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(clock_head_, lv_color_hex(pal.bg), 0);
    lv_obj_set_user_data(clock_arc_, clock_head_);
    status_ = label(hub_, "", 0, 0, 0, &th_eyebrow_13, pal.textSecondary);
    lv_obj_align(status_, LV_ALIGN_CENTER, 0, -30);
    timer_ = label(hub_, "", 0, 0, 0, &th_bold_28, pal.text);
    lv_obj_align(timer_, LV_ALIGN_CENTER, 0, -4);
    name_ = label(hub_, "", 0, 0, 84, &th_semi_16, pal.textSecondary);
    lv_obj_set_style_text_align(name_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(name_, LV_ALIGN_CENTER, 0, 24);
}
void Ui::build_gameplay_sigil() {
    const auto& pal = palette();
    build_status_bar(root_);
    life_card_ = panel(root_, 10, 128, 220, 136);
    // The header token and life card are recolored per seat in update().
    seats_[0].token = token(root_, 12, 34, 44, 1, selected_, &seats_[0].glyph);
    name_ = label(root_, "", 66, 33, 162, &th_semi_20, pal.text);
    status_pill_ = pill(root_, 66, 60, "", pal.turn, pal.onTurn, &status_);
    label(root_, "TURN CLOCK", 12, 94, 0, &th_eyebrow_13, pal.textTertiary);
    timer_ = label(root_, "", 128, 88, 100, &th_semi_20, pal.text);
    lv_obj_set_style_text_align(timer_, LV_TEXT_ALIGN_RIGHT, 0);
    clock_bar_ = lv_bar_create(root_);
    lv_obj_remove_style_all(clock_bar_);
    lv_obj_set_pos(clock_bar_, 12, 114); lv_obj_set_size(clock_bar_, 216, 6);
    lv_bar_set_range(clock_bar_, 0, 1000);
    lv_obj_set_style_bg_opa(clock_bar_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(clock_bar_, lv_color_hex(pal.fillStrong), LV_PART_MAIN);
    lv_obj_set_style_radius(clock_bar_, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(clock_bar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(clock_bar_, lv_color_hex(pal.turn), LV_PART_INDICATOR);
    lv_obj_set_style_radius(clock_bar_, 3, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(clock_bar_, 950, LV_PART_MAIN);
    // Touch halves on the life card; the joystick's left/right do the same.
    auto zone = [&](const char* name, int x, const lv_image_dsc_t* glyph, lv_align_t corner) {
        auto* z = box(root_, x, 128, 110, 136);
        lv_obj_set_style_radius(z, 20, 0);
        lv_obj_set_style_bg_color(z, lv_color_hex(pal.text), 0);
        lv_obj_set_style_bg_opa(z, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(z, 18, LV_STATE_PRESSED);
        bind(z, name, false, true);
        auto* g = icon(z, glyph, pal.textTertiary);
        lv_obj_align(g, corner, corner == LV_ALIGN_BOTTOM_LEFT ? 12 : -12, -10);
    };
    zone("minus", 10, TH_ICON(minus, 24), LV_ALIGN_BOTTOM_LEFT);
    zone("plus", 120, TH_ICON(plus, 24), LV_ALIGN_BOTTOM_RIGHT);
    label(root_, "LIFE", 24, 140, 0, &th_eyebrow_13, pal.textTertiary);
    life_ = label(root_, "", 12, 160, 216, &th_num_96, pal.text);
    lv_obj_set_style_text_align(life_, LV_TEXT_ALIGN_CENTER, 0);
    badge_ = label(root_, "", 150, 138, 68, &th_semi_20, pal.good);
    lv_obj_set_style_text_align(badge_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_opa(badge_, LV_OPA_TRANSP, 0);
    button(root_, "menu", nullptr, TH_ICON(sliders, 24), 12, 272, 52, 44);
    bottom_seat_ = button(root_, "player", "Seat", TH_ICON(users, 24), 72, 272, 104, 44);
    bottom_pass_ = hold_to_pass(root_, "pass", 72, 272, 104, 44);
    lv_group_add_obj(group_, bottom_pass_); // Hidden objects are skipped by focus.
    button(root_, "scenario", nullptr, TH_ICON(dice, 24), 184, 272, 44, 44);
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
            label(row, it.title, 46, 5, 140, &th_semi_20, pal.text);
            label(row, it.detail, 46, 29, 140, &th_inter_16, pal.textSecondary);
            auto* c = icon(row, TH_ICON(chevron_right, 24), pal.textTertiary); lv_obj_align(c, LV_ALIGN_RIGHT_MID, -8, 0);
        } else {
            auto* tile = button(root_, it.name, nullptr, nullptr, 12 + static_cast<int>(i) * 156, 72, 144, 176);
            auto* g = icon(tile, it.big, pal.accent); lv_obj_align(g, LV_ALIGN_TOP_MID, 0, 28);
            auto* t = label(tile, it.title, 0, 0, 0, title_font(), pal.text); lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 88);
            auto* d = label(tile, it.detail, 0, 0, 128, &th_inter_16, pal.textSecondary);
            lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 120);
        }
    }
    auto* note = label(root_, "Changes here stay on this demo; nothing is saved.", 12, profile_.compact() ? 250 : 268,
                       profile_.width - 24, &th_inter_16, pal.textTertiary);
    lv_obj_set_height(note, LV_SIZE_CONTENT); lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
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
        const lv_font_t* font = look.ornament ? &th_cinzel_20 : &th_semi_20;
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
        lv_obj_set_style_bg_color(row, lv_color_hex(seat_tint(i)), 0);
        if(i == selected_) { lv_obj_set_style_border_color(row, lv_color_hex(pal.accent), 0); lv_obj_set_style_border_width(row, 2, 0); }
        const int side = compact ? 36 : 52;
        token(row, 8, (h - side) / 2, side, p.avatar, i);
        label(row, p.name.c_str(), side + 18, compact ? 6 : 14, 100, compact ? &th_semi_20 : title_font(), p.active ? pal.text : pal.textTertiary);
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
    pill(root_, 12, compact ? 60 : 64, "MOCK  /  NO RADIO", pal.fillStrong, pal.warning);
    label(root_, "PAIRING CODE", 12, compact ? 96 : 106, 0, &th_eyebrow_13, pal.textTertiary);
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
        auto* l = label(c, digit, 0, 0, 0, compact ? &th_bold_28 : &th_num_40, pal.text);
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
    if(!compact) lv_obj_set_style_text_align(label(root_, "SEARCHING", 348, 146, 96, &th_eyebrow_13, pal.textTertiary), LV_TEXT_ALIGN_CENTER, 0);
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
    timer_ = life_ = name_ = status_ = status_pill_ = hub_ = clock_arc_ = clock_head_ = clock_bar_ = badge_ = nullptr;
    turn_frame_ = life_card_ = bottom_seat_ = bottom_pass_ = card_scrim_ = card_ = nullptr;
    card_kind_ = Card::None; card_names_.clear();
    seats_.fill(Seat{});
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
        lv_screen_load_anim(screen_obj_, type, 260, 0, true);
    }
    revision_ = 0;
    fresh_ = true;
    update();
}

// ---- Atlas overlay cards (menu, paused, game over) -------------------------
void Ui::open_card(Card kind) {
    close_card();
    const auto& pal = palette();
    const auto& s = source_.snapshot();
    const auto before = controls_;
    card_kind_ = kind;
    card_scrim_ = box(root_, 0, 0, profile_.width, profile_.height);
    lv_obj_set_style_bg_color(card_scrim_, lv_color_hex(pal.scrim), 0);
    animate(card_scrim_, set_bg_opa, 0, kind == Card::Menu ? LV_OPA_50 : LV_OPA_70, ms(200));
    bind(card_scrim_, "card_scrim", false);
    const int h = kind == Card::Menu ? 210 : 190;
    card_ = panel(root_, 80, (profile_.height - h) / 2, 320, h);
    lv_obj_set_style_bg_color(card_, lv_color_hex(pal.surface2), 0);
    lv_obj_set_style_shadow_width(card_, 40, 0);
    lv_obj_set_style_shadow_offset_y(card_, 12, 0);
    lv_obj_set_style_shadow_color(card_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card_, static_cast<lv_opa_t>(pal.light ? LV_OPA_20 : LV_OPA_60), 0);
    lv_obj_add_flag(card_, LV_OBJ_FLAG_CLICKABLE); // Swallow taps between buttons.
    const auto& holder = s.players[s.turn_holder];
    const lv_image_dsc_t* glyph = kind == Card::Menu ? TH_ICON(hub, 40) : kind == Card::Paused ? TH_ICON(pause, 40) : TH_ICON(crown, 40);
    auto* g = icon(card_, glyph, kind == Card::Paused ? pal.warning : pal.accent);
    lv_obj_set_pos(g, 20, 18);
    char title[48];
    std::snprintf(title, sizeof title, kind == Card::Menu ? "Table" : kind == Card::Paused ? "Paused" : "%s wins", holder.name.c_str());
    label(card_, title, 72, 16, 228, title_font(true), pal.text);
    const char* text = kind == Card::Menu ? "Battery --   /   demo fixtures"
                     : kind == Card::Paused ? "The clock is stopped for everyone." : "Game over. The next sample deals a new table.";
    auto* t = label(card_, text, 72, 52, 230, &th_inter_16, pal.textSecondary);
    lv_obj_set_height(t, LV_SIZE_CONTENT); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    if(kind == Card::Menu) {
        const bool paused = s.phase == Phase::Paused;
        button(card_, "pause", paused ? "Resume" : "Pause", paused ? TH_ICON(play, 24) : TH_ICON(pause, 24), 20, 92, 136, 48);
        button(card_, "scenario", "Sample", TH_ICON(dice, 24), 164, 92, 136, 48);
        button(card_, "menu", "Settings", TH_ICON(sliders, 24), 20, 148, 136, 48);
        button(card_, "card_close", "Close", TH_ICON(close, 24), 164, 148, 136, 48, Look::Quiet);
    } else if(kind == Card::Paused) {
        button(card_, "resume", "Resume", TH_ICON(play, 24), 20, 120, 136, 52, Look::Accent);
        button(card_, "scenario", "Sample", TH_ICON(dice, 24), 164, 120, 136, 52);
    } else {
        button(card_, "scenario", "Next sample", TH_ICON(dice, 24), 20, 120, 280, 52, Look::Accent);
    }
    for(const auto& entry : controls_) if(!before.count(entry.first)) card_names_.push_back(entry.first);
    if(!reduced_motion_) {
        animate(card_, set_opa, 0, 255, 200);
        animate(card_, set_ty, 24, 0, 300, 0, lv_anim_path_overshoot);
    }
}
void Ui::close_card() {
    if(!card_) return;
    for(const auto& name : card_names_) {
        auto it = controls_.find(name);
        if(it != controls_.end()) { lv_group_remove_obj(it->second); controls_.erase(it); }
    }
    card_names_.clear();
    lv_obj_remove_flag(card_scrim_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(card_, LV_OBJ_FLAG_CLICKABLE);
    if(reduced_motion_) { lv_obj_delete(card_); lv_obj_delete(card_scrim_); }
    else {
        // Children stay deaf to touch while the card fades away.
        for(uint32_t i = 0; i < lv_obj_get_child_count(card_); ++i)
            lv_obj_remove_flag(lv_obj_get_child(card_, static_cast<int32_t>(i)), LV_OBJ_FLAG_CLICKABLE);
        animate(card_, set_opa, 255, 0, 160, 0, lv_anim_path_ease_in, delete_var);
        animate(card_scrim_, set_bg_opa, lv_obj_get_style_bg_opa(card_scrim_, 0), 0, 180, 0, lv_anim_path_linear, delete_var);
    }
    card_ = card_scrim_ = nullptr;
    card_kind_ = Card::None;
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
        animate(clock_arc_, set_ring, lv_arc_get_value(clock_arc_), progress, fresh_ ? 0 : ms(ticking ? 980 : 420), 0,
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
        if(fresh_ || selected_ != last_selected_) {
            lv_obj_set_style_bg_color(seats_[0].token, lv_color_hex(seat_color(selected_)), 0);
            lv_image_set_src(seats_[0].glyph, assets_.avatar(player.avatar));
            lv_obj_set_style_bg_color(life_card_, lv_color_hex(seat_tint(selected_)), 0);
            if(!fresh_ && !reduced_motion_) { animate(name_, set_tx, 16, 0, 220); animate(life_, set_opa, 0, 255, 220); }
        }
        lv_label_set_text(name_, player.name.c_str());
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
        lv_obj_set_style_border_width(seats_[0].token, your_turn ? 3 : 0, 0);
        char text[16]; std::snprintf(text, sizeof text, "%d", player.life);
        lv_label_set_text(life_, text);
        // Four-character totals (-999) step down so they stay inside the card.
        lv_obj_set_style_text_font(life_, std::strlen(text) > 3 ? &th_num_64 : &th_num_96, 0);
        lv_obj_set_y(life_, std::strlen(text) > 3 ? 172 : 160);
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
        // Your turn swaps the seat button for hold-to-pass.
        lv_obj_set_flag(bottom_pass_, LV_OBJ_FLAG_HIDDEN, !your_turn);
        lv_obj_set_flag(bottom_seat_, LV_OBJ_FLAG_HIDDEN, your_turn);
    } else {
        lv_label_set_text(name_, holder.name.c_str());
        lv_label_set_text(status_, phase_name(s.phase));
        lv_obj_set_style_text_color(status_, lv_color_hex(running ? pal.turn : s.phase == Phase::Paused ? pal.warning : pal.accent), 0);
        if(turn_moved && !reduced_motion_) { animate(name_, set_opa, 0, 255, 260); pop(timer_, 280); }
        lv_obj_set_style_shadow_color(hub_, lv_color_hex(urgency), 0);
        if(running && !reduced_motion_) pulse(hub_, set_shadow, 50, 150, 1600);
        else { lv_anim_delete(hub_, set_shadow); set_shadow(hub_, 0); }
        for(unsigned i = 0; i < 4; ++i) {
            const auto& p = s.players[i];
            auto& seat = seats_[i];
            char text[16]; std::snprintf(text, sizeof text, "%d", p.life);
            lv_label_set_text(seat.life, text);
            lv_obj_set_style_text_font(seat.life, std::strlen(text) > 3 ? &th_num_40 : &th_num_64, 0);
            const bool turn = i == s.turn_holder && p.active;
            const bool won = turn && s.phase == Phase::GameOver;
            const char* tag = !p.active ? "OUT" : won ? "WINNER" : turn ? "TURN" : "";
            lv_label_set_text(seat.tag_text, tag);
            lv_obj_set_flag(seat.tag, LV_OBJ_FLAG_HIDDEN, !*tag);
            lv_obj_set_style_bg_color(seat.tag, lv_color_hex(!p.active ? pal.critical : won ? pal.accent : pal.turn), 0);
            lv_obj_set_style_text_color(seat.tag_text, lv_color_hex(!p.active ? pal.bg : won ? pal.onAccent : pal.onTurn), 0);
            lv_obj_set_style_border_color(seat.panel, lv_color_hex(turn ? (won ? pal.accent : pal.turn) : pal.separatorStrong), 0);
            lv_obj_set_style_border_width(seat.panel, turn ? 3 : (pal.ornament ? 2 : 1), 0);
            lv_obj_set_style_border_width(seat.token, turn ? 2 : 0, 0);
            lv_obj_set_style_opa(seat.panel, static_cast<lv_opa_t>(p.active ? LV_OPA_COVER : 115), 0);
            if(turn && running && !reduced_motion_) pulse(seat.panel, set_shadow, 30, 130, 1200);
            else { lv_anim_delete(seat.panel, set_shadow); set_shadow(seat.panel, 0); }
            const bool can_pass = turn && running;
            if(can_pass && lv_obj_has_flag(seat.pass, LV_OBJ_FLAG_HIDDEN) && !fresh_) animate(seat.pass, set_opa, 0, 255, ms(240));
            lv_obj_set_flag(seat.pass, LV_OBJ_FLAG_HIDDEN, !can_pass);
            if(turn_moved && turn) pop(seat.token, 340);
            if(change[i]) { pop(seat.life); show_badge(seat.badge, delta_[i]); }
        }
        // State cards over the whole table; the menu yields to them.
        const Card wanted = s.phase == Phase::Paused ? Card::Paused : s.phase == Phase::GameOver ? Card::Over
                          : card_kind_ == Card::Menu ? Card::Menu : Card::None;
        if(wanted != card_kind_) { if(wanted == Card::None) close_card(); else open_card(wanted); }
    }
    for(unsigned i = 0; i < 4; ++i) last_life_[i] = s.players[i].life;
    last_turn_ = s.turn_holder;
    last_seconds_ = s.remaining_seconds;
    last_selected_ = selected_;
    fresh_ = false;
}
void Ui::update() {
    const auto& s = source_.snapshot();
    if(screen_ != Screen::Gameplay || revision_ == s.revision) return;
    revision_ = s.revision;
    update_gameplay(s);
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
void Ui::activate(const std::string& raw) {
    const auto& s = source_.snapshot();
    const bool hold = raw.size() > 5 && raw.compare(raw.size() - 5, 5, ":hold") == 0;
    const std::string name = hold ? raw.substr(0, raw.size() - 5) : raw;
    auto seat_of = [&](size_t prefix) { return name.size() > prefix ? static_cast<unsigned>(name[prefix] - '0') : selected_; };
    if(name == "settings") show(Screen::Settings);
    else if(name == "menu") { close_card(); show(Screen::Settings); }
    else if(name == "back") show(Screen::Gameplay);
    else if(name == "pairing") show(Screen::Pairing);
    else if(name == "themes") show(Screen::Themes);
    else if(name == "players") show(Screen::Players);
    else if(name.rfind("theme", 0) == 0) set_theme(static_cast<TurnHubTheme::Id>(name.back() - '0'));
    else if(name.rfind("choose", 0) == 0) { selected_ = static_cast<unsigned>(name.back() - '0'); show(Screen::Players); }
    else if(name == "hub") open_card(Card::Menu);
    else if(name == "card_close" || name == "card_scrim") { if(card_kind_ == Card::Menu) close_card(); }
    else if(name == "player") { selected_ = (selected_ + 1) % s.players.size(); revision_ = 0; }
    else if(name.rfind("plus", 0) == 0 || name.rfind("minus", 0) == 0) {
        const bool up = name[0] == 'p';
        const unsigned seat = seat_of(up ? 4 : 5);
        sink_.request({ActionKind::AdjustLife, s.players[seat].participant_id, (up ? 1 : -1) * (hold ? 5 : 1)});
    }
    else if(name.rfind("pass", 0) == 0) sink_.request({ActionKind::PassTurn, s.players[seat_of(4)].participant_id});
    else if(name == "pause" || name == "resume") { if(card_kind_ == Card::Menu) close_card(); sink_.request({ActionKind::RequestPause}); }
    else if(name == "scenario") { if(card_kind_ == Card::Menu) close_card(); sink_.request({ActionKind::DemoNextScenario}); }
    else if(name == "pair" || name == "unpair") { sink_.request({name == "pair" ? ActionKind::MockPair : ActionKind::MockUnpair}); show(Screen::Settings); }
    update();
}
void Ui::click(lv_event_t* e) {
    auto* binding = static_cast<Binding*>(lv_event_get_user_data(e));
    Ui* ui = binding->ui;
    // A screen still sliding out must not act.
    if(lv_obj_get_screen(static_cast<lv_obj_t*>(lv_event_get_current_target(e))) != ui->screen_obj_) return;
    const auto code = lv_event_get_code(e);
    // Copy: a navigation action can delete the object and its binding.
    const std::string name = binding->name + (code == LV_EVENT_LONG_PRESSED || code == LV_EVENT_LONG_PRESSED_REPEAT ? ":hold" : "");
    ui->activate(name);
}
void Ui::pass_event(lv_event_t* e) {
    auto* binding = static_cast<Binding*>(lv_event_get_user_data(e));
    auto* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* fill = lv_obj_get_child(obj, 0);
    if(lv_event_get_code(e) == LV_EVENT_PRESSED) {
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, fill); lv_anim_set_exec_cb(&a, set_width);
        lv_anim_set_values(&a, 0, lv_obj_get_width(obj)); lv_anim_set_duration(&a, PASS_HOLD_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_linear);
        lv_anim_set_user_data(&a, binding);
        lv_anim_set_completed_cb(&a, [](lv_anim_t* done) {
            auto* b = static_cast<Binding*>(lv_anim_get_user_data(done));
            set_width(done->var, 0);
            b->ui->activate(b->name);
        });
        lv_anim_start(&a);
    } else {
        lv_anim_delete(fill, set_width);
        set_width(fill, 0);
    }
}
void Ui::free_binding(lv_event_t* e) {
    auto* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    std::set<Binding*> owned;
    const auto count = lv_obj_get_event_count(obj);
    for(uint32_t i = 0; i < count; ++i) {
        auto* dsc = lv_obj_get_event_dsc(obj, i);
        const auto cb = lv_event_dsc_get_cb(dsc);
        if(cb == click || cb == pass_event) owned.insert(static_cast<Binding*>(lv_event_dsc_get_user_data(dsc)));
    }
    for(auto* b : owned) delete b;
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
    // With an Atlas card open, the keys move between its buttons.
    const bool game = ui->screen_ == Screen::Gameplay && !ui->card_;
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
