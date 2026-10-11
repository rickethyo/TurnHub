#pragma once
#include "assets.hpp"
#include "device_theme.h"
#include "presentation.hpp"
#include <array>
#include <deque>
#include <map>
#include <string>
namespace th {
enum class Screen { Gameplay, Settings, Pairing, Themes, Players };
class Ui {
public:
    Ui(DisplayProfile profile, SnapshotSource& source, ActionSink& sink, const AssetProvider& assets);
    ~Ui();
    void update();
    void show(Screen screen);
    void set_theme(TurnHubTheme::Id theme);
    void navigation(NavEvent event);
    Screen screen() const { return screen_; }
    TurnHubTheme::Id theme() const { return theme_; }
    unsigned selected_player() const { return selected_; }
    lv_obj_t* control(const std::string& name) const;
    lv_group_t* group() const { return group_; }
    lv_indev_t* keypad() const { return keypad_; }
    const DisplayProfile& display_profile() const { return profile_; }
    bool bounds_valid();
    std::string visible_text(const std::string& role) const;
private:
    struct Binding { Ui* ui; std::string name; };
    DisplayProfile profile_;
    SnapshotSource& source_;
    ActionSink& sink_;
    const AssetProvider& assets_;
    Screen screen_ = Screen::Gameplay;
    TurnHubTheme::Id theme_ = TurnHubTheme::Id::Graphite;
    unsigned selected_ = 0;
    uint32_t revision_ = 0;
    lv_group_t* group_ = nullptr;
    lv_indev_t* keypad_ = nullptr;
    lv_obj_t* root_ = nullptr;
    lv_obj_t *timer_ = nullptr, *life_ = nullptr, *name_ = nullptr, *status_ = nullptr, *avatar_ = nullptr;
    std::array<lv_obj_t*, 4> card_life_{};
    std::array<lv_obj_t*, 4> card_tag_{};
    std::array<lv_obj_t*, 4> cards_{};
    std::map<std::string, lv_obj_t*> controls_;
    std::deque<Binding> bindings_;
    std::deque<NavEvent> keys_;
    uint32_t last_key_ = LV_KEY_ENTER;
    lv_indev_state_t key_state_ = LV_INDEV_STATE_RELEASED;
    void render();
    void activate(const std::string& name);
    void gameplay_key(uint32_t key);
    lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h);
    lv_obj_t* label(lv_obj_t* parent, const char* text, int x, int y, int width, const lv_font_t* font, uint32_t color);
    lv_obj_t* button(const std::string& name, const char* text, int x, int y, int w, int h, bool accent = false);
    const TurnHubTheme::Palette& palette() const { return TurnHubTheme::palette(theme_); }
    const lv_font_t* title_font(bool large = false) const;
    static void click(lv_event_t* event);
    static void key_event(lv_event_t* event);
    static void read_keypad(lv_indev_t* indev, lv_indev_data_t* data);
};
} // namespace th
