#include "ui.hpp"
#include "fonts.hpp"
#include <cstdio>

namespace th {
static const char* phase_name(Phase p) {
    switch(p) { case Phase::Running: return "PLAYING"; case Phase::Paused: return "PAUSED"; case Phase::GameOver: return "GAME OVER"; }
    return "UNKNOWN";
}
Ui::Ui(DisplayProfile p, SnapshotSource& s, ActionSink& sink, const AssetProvider& a)
    : profile_(p), source_(s), sink_(sink), assets_(a) {
    group_ = lv_group_create();
    lv_group_set_wrap(group_, true);
    keypad_ = lv_indev_create();
    lv_indev_set_type(keypad_, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_user_data(keypad_, this);
    lv_indev_set_read_cb(keypad_, read_keypad);
    lv_indev_set_group(keypad_, group_);
    render();
}
Ui::~Ui() {
    lv_indev_delete(keypad_);
    lv_group_delete(group_);
    if(root_) lv_obj_delete(root_);
}
const lv_font_t* Ui::title_font(bool large) const {
    return palette().ornament ? (large ? &th_cinzel_32 : &th_cinzel_20)
                              : (large ? &th_inter_32 : &th_inter_20);
}
lv_obj_t* Ui::panel(lv_obj_t* parent, int x, int y, int w, int h) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(palette().surface1), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_border_width(obj, palette().ornament ? 2 : 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(palette().separatorStrong), 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}
lv_obj_t* Ui::label(lv_obj_t* parent, const char* text, int x, int y, int width, const lv_font_t* font, uint32_t color) {
    auto* obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y); lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    lv_label_set_text(obj, text);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}
lv_obj_t* Ui::button(const std::string& name, const char* text, int x, int y, int w, int h, bool accent) {
    auto* obj = lv_button_create(root_);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(accent ? palette().accent : palette().surface3), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(palette().accentPressed), LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(palette().separatorStrong), 0);
    lv_obj_set_style_outline_width(obj, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(obj, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(obj, lv_color_hex(palette().focus), LV_STATE_FOCUSED);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    auto* caption = label(obj, text, 0, 0, w - 8, &th_inter_16, accent ? palette().onAccent : palette().text);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(caption);
    controls_[name] = obj;
    bindings_.push_back({this, name});
    lv_obj_add_event_cb(obj, click, LV_EVENT_CLICKED, &bindings_.back());
    lv_group_add_obj(group_, obj);
    return obj;
}
void Ui::render() {
    lv_group_remove_all_objs(group_);
    if(root_) lv_obj_delete(root_);
    controls_.clear(); bindings_.clear();
    timer_ = life_ = name_ = status_ = avatar_ = nullptr;
    card_life_.fill(nullptr); card_tag_.fill(nullptr); cards_.fill(nullptr);
    root_ = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, profile_.width, profile_.height);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(palette().bg), 0);
    lv_obj_set_style_text_font(root_, &th_inter_16, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    const bool compact = profile_.compact();
    const int w = profile_.width;
    label(root_, compact ? "SIGIL / DEMO" : "TURNHUB / ATLAS DEMO", 12, 8, w - 90, &th_inter_16, palette().textSecondary);
    label(root_, "BAT --", w - 72, 8, 62, &th_inter_16, palette().textSecondary);
    if(screen_ == Screen::Gameplay) {
        if(compact) {
            avatar_ = lv_image_create(root_);
            lv_obj_set_pos(avatar_, 14, 40);
            lv_obj_set_style_image_recolor(avatar_, lv_color_hex(palette().accent), 0);
            lv_obj_set_style_image_recolor_opa(avatar_, LV_OPA_COVER, 0);
            name_ = label(root_, "", 88, 42, 140, title_font(), palette().text);
            status_ = label(root_, "", 88, 71, 140, &th_inter_16, palette().turn);
            life_ = label(root_, "", 12, 111, 216, &th_inter_72, palette().text);
            lv_obj_set_style_text_align(life_, LV_TEXT_ALIGN_CENTER, 0);
            timer_ = label(root_, "", 12, 196, 216, &th_inter_20, palette().textSecondary);
            lv_obj_set_style_text_align(timer_, LV_TEXT_ALIGN_CENTER, 0);
            button("minus", "-1", 12, 225, 64, 44);
            button("menu", "Menu", 84, 225, 72, 44);
            button("plus", "+1", 164, 225, 64, 44, true);
            button("scenario", "Sample", 12, 276, 103, 44);
            button("player", "Player", 123, 276, 105, 44);
        } else {
            auto* hero = panel(root_, 12, 36, 456, 104);
            name_ = label(hero, "", 12, 8, 247, title_font(true), palette().text);
            status_ = label(hero, "", 12, 55, 247, &th_inter_16, palette().turn);
            timer_ = label(hero, "", 270, 10, 174, &th_inter_56, palette().text);
            label(hero, "TURN CLOCK", 272, 77, 165, &th_inter_16, palette().textSecondary);
            for(unsigned i = 0; i < 4; ++i) {
                cards_[i] = panel(root_, 12 + static_cast<int>(i) * 116, 148, 108, 108);
                label(cards_[i], source_.snapshot().players[i].name.c_str(), 8, 6, 92, title_font(), palette().text);
                card_life_[i] = label(cards_[i], "", 8, 35, 92, &th_inter_32, palette().text);
                card_tag_[i] = label(cards_[i], "", 8, 79, 92, &th_inter_16, palette().textSecondary);
                const std::string name = "seat" + std::to_string(i);
                controls_[name] = cards_[i];
                bindings_.push_back({this, name});
                lv_obj_add_flag(cards_[i], LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(cards_[i], click, LV_EVENT_CLICKED, &bindings_.back());
                lv_obj_set_style_outline_width(cards_[i], 2, LV_STATE_FOCUSED);
                lv_obj_set_style_outline_color(cards_[i], lv_color_hex(palette().focus), LV_STATE_FOCUSED);
                lv_group_add_obj(group_, cards_[i]);
            }
            button("pause", "Pause / Resume", 12, 272, 158, 44);
            button("scenario", "Next sample", 180, 272, 139, 44);
            button("menu", "Settings", 329, 272, 139, 44);
        }
        // A focus target for gameplay joystick semantics, independent of touch buttons.
        lv_obj_add_event_cb(root_, key_event, LV_EVENT_KEY, this);
        lv_group_add_obj(group_, root_);
        lv_group_focus_obj(root_);
    } else {
        const char* heading = screen_ == Screen::Settings ? "Settings" : screen_ == Screen::Themes ? "Theme" : screen_ == Screen::Players ? "Player" : "Pairing";
        label(root_, heading, 12, 39, w - 24, title_font(true), palette().text);
        if(screen_ == Screen::Settings) {
            button("pairing", "Pair / unpair", 12, 87, w - 24, 44);
            button("themes", "Theme", 12, 139, w - 24, 44);
            button("players", "Select player", 12, 191, w - 24, 44);
            button("back", "Back to game", 12, 266, w - 24, 44);
        } else if(screen_ == Screen::Themes) {
            for(unsigned i = 0; i < 4; ++i) {
                auto id = static_cast<TurnHubTheme::Id>(i);
                button("theme" + std::to_string(i), TurnHubTheme::palette(id).label, 12, 83 + static_cast<int>(i) * 45, w - 24, 44, id == theme_);
            }
            button("settings", "Back", 12, 266, w - 24, 44);
        } else if(screen_ == Screen::Players) {
            for(unsigned i = 0; i < 4; ++i) {
                button("choose" + std::to_string(i), source_.snapshot().players[i].name.c_str(), 12, 83 + static_cast<int>(i) * 45, w - 24, 44, i == selected_);
            }
            button("settings", "Back", 12, 266, w - 24, 44);
        } else {
            label(root_, "MOCK / no radio", 12, 84, w - 24, &th_inter_16, palette().warning);
            label(root_, "Code 314159", 12, 117, w - 24, title_font(), palette().text);
            label(root_, "Compare on both screens", 12, 148, w - 24, &th_inter_16, palette().textSecondary);
            button("pair", "Codes match", 12, 182, w - 24, 44, true);
            button("unpair", "Unpair (mock)", 12, 228, w - 24, 44);
            button("settings", "Back", 12, 276, w - 24, 44);
        }
    }
    revision_ = 0;
    update();
}
void Ui::update() {
    const auto& s = source_.snapshot();
    if(screen_ != Screen::Gameplay || revision_ == s.revision) return;
    revision_ = s.revision;
    char clock[24];
    const auto& player = s.players[profile_.compact() ? selected_ : s.turn_holder];
    lv_label_set_text(name_, player.name.c_str());
    const char* tag = !player.active ? "OUT" : s.phase == Phase::GameOver ? (player.participant_id == s.players[s.turn_holder].participant_id ? "WINNER" : "GAME OVER")
                      : s.phase == Phase::Paused ? "PAUSED" : player.participant_id == s.players[s.turn_holder].participant_id ? "YOUR TURN" : "WAITING";
    lv_label_set_text(status_, profile_.compact() ? tag : phase_name(s.phase));
    std::snprintf(clock, sizeof clock, "%02u:%02u", s.remaining_seconds / 60, s.remaining_seconds % 60);
    if(profile_.compact()) {
        char text[48]; std::snprintf(text, sizeof text, "Turn clock %s", clock);
        lv_label_set_text(timer_, text);
        std::snprintf(text, sizeof text, "%d", player.life); lv_label_set_text(life_, text);
        lv_image_set_src(avatar_, assets_.avatar(player.avatar));
    } else {
        lv_label_set_text(timer_, clock);
        for(unsigned i = 0; i < 4; ++i) {
            char text[24]; std::snprintf(text, sizeof text, "%d", s.players[i].life);
            lv_label_set_text(card_life_[i], text);
            lv_label_set_text(card_tag_[i], !s.players[i].active ? "OUT" : i == s.turn_holder ? (s.phase == Phase::GameOver ? "WINNER" : "TURN") : "SEATED");
            lv_obj_set_style_border_color(cards_[i], lv_color_hex(i == s.turn_holder ? palette().turn : palette().separatorStrong), 0);
            lv_obj_set_style_border_width(cards_[i], i == s.turn_holder ? 2 : 1, 0);
        }
    }
}
void Ui::show(Screen screen) { screen_ = screen; render(); }
void Ui::set_theme(TurnHubTheme::Id theme) {
    if(!TurnHubTheme::valid(static_cast<uint8_t>(theme))) return;
    theme_ = theme; render();
}
void Ui::activate(const std::string& name) {
    if(name == "menu" || name == "settings") show(Screen::Settings);
    else if(name == "back") show(Screen::Gameplay);
    else if(name == "pairing") show(Screen::Pairing);
    else if(name == "themes") show(Screen::Themes);
    else if(name == "players") show(Screen::Players);
    else if(name.rfind("theme", 0) == 0) set_theme(static_cast<TurnHubTheme::Id>(name.back() - '0'));
    else if(name.rfind("choose", 0) == 0 || name.rfind("seat", 0) == 0) { selected_ = static_cast<unsigned>(name.back() - '0'); show(Screen::Players); }
    else if(name == "player") { selected_ = (selected_ + 1) % source_.snapshot().players.size(); revision_ = 0; }
    else if(name == "plus" || name == "minus") sink_.request({ActionKind::AdjustLife, source_.snapshot().players[selected_].participant_id, name == "plus" ? 1 : -1});
    else if(name == "pause") sink_.request({ActionKind::RequestPause});
    else if(name == "scenario") sink_.request({ActionKind::DemoNextScenario});
    else if(name == "pair" || name == "unpair") { sink_.request({name == "pair" ? ActionKind::MockPair : ActionKind::MockUnpair}); show(Screen::Settings); }
    update();
}
void Ui::click(lv_event_t* e) {
    auto* binding = static_cast<Binding*>(lv_event_get_user_data(e));
    // Copy: a navigation action can delete the object and all old bindings.
    Ui* ui = binding->ui; const std::string name = binding->name;
    ui->activate(name);
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
    const bool game = ui->screen_ == Screen::Gameplay;
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
    lv_obj_update_layout(root_);
    for(const auto& entry : controls_) {
        lv_area_t a; lv_obj_get_coords(entry.second, &a);
        if(a.x1 < 0 || a.y1 < 0 || a.x2 >= profile_.width || a.y2 >= profile_.height) return false;
    }
    return true;
}
} // namespace th
