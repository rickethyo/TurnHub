#pragma once
#include "assets.hpp"
#include "device_theme.h"
#include "presentation.hpp"
#include <array>
#include <deque>
#include <map>
#include <string>
#include <vector>
namespace th {
enum class Screen { Gameplay, Settings, Pairing, Themes, Players, Home, Effects };
// How the table announces a new turn; runtime only, like the theme.
enum class TurnEffect : uint8_t { Bubble, Flip, Push, Cube, Count };
class Ui {
public:
    Ui(DisplayProfile profile, SnapshotSource& source, ActionSink& sink, const AssetProvider& assets);
    ~Ui();
    void update();
    void show(Screen screen);
    void set_theme(TurnHubTheme::Id theme);
    // Reduced motion keeps every state change but skips movement, scaling and
    // pulses; tests and captures use it so geometry is never mid-animation.
    void set_reduced_motion(bool reduced) { reduced_motion_ = reduced; }
    void navigation(NavEvent event);
    Screen screen() const { return screen_; }
    TurnHubTheme::Id theme() const { return theme_; }
    unsigned selected_player() const { return selected_; }
    TurnEffect turn_effect() const { return turn_effect_; }
    void set_turn_effect(TurnEffect effect) { if(effect < TurnEffect::Count) turn_effect_ = effect; }
    lv_obj_t* control(const std::string& name) const;
    lv_group_t* group() const { return group_; }
    lv_indev_t* keypad() const { return keypad_; }
    const DisplayProfile& display_profile() const { return profile_; }
    bool bounds_valid();
    std::string visible_text(const std::string& role) const;
    static constexpr uint32_t PASS_HOLD_MS = 700;
private:
    struct Binding { Ui* ui; std::string name; };
    enum class Look { Normal, Accent, Quiet, Turn };
    enum class Motion { None, Forward, Back, Fade, ZoomIn, ZoomOut };
    enum class Card { None, Menu, Paused, Over };
    struct Seat {
        lv_obj_t *panel = nullptr, *token = nullptr, *glyph = nullptr, *life = nullptr, *badge = nullptr;
        lv_obj_t *tag = nullptr, *tag_text = nullptr, *pass = nullptr, *up = nullptr, *down = nullptr;
    };
    DisplayProfile profile_;
    SnapshotSource& source_;
    ActionSink& sink_;
    const AssetProvider& assets_;
    Screen screen_ = Screen::Gameplay;
    TurnHubTheme::Id theme_ = TurnHubTheme::Id::Graphite;
    unsigned selected_ = 0;
    TurnEffect turn_effect_ = TurnEffect::Bubble;
    Screen return_to_ = Screen::Gameplay;
    uint32_t revision_ = 0;
    bool reduced_motion_ = false;
    bool fresh_ = true;
    lv_group_t* group_ = nullptr;
    lv_indev_t* keypad_ = nullptr;
    lv_obj_t* base_screen_ = nullptr;
    lv_obj_t* screen_obj_ = nullptr;
    lv_obj_t* root_ = nullptr;
    // Gameplay widgets; null on other screens.
    lv_obj_t *timer_ = nullptr, *life_ = nullptr, *name_ = nullptr, *status_ = nullptr, *status_pill_ = nullptr;
    lv_obj_t *hub_ = nullptr, *clock_arc_ = nullptr, *clock_head_ = nullptr, *clock_bar_ = nullptr, *badge_ = nullptr;
    lv_obj_t *turn_frame_ = nullptr, *life_card_ = nullptr, *bottom_seat_ = nullptr, *bottom_pass_ = nullptr;
    lv_obj_t *card_scrim_ = nullptr, *card_ = nullptr;
    lv_obj_t *home_title_ = nullptr, *home_detail_ = nullptr, *home_token_ = nullptr, *home_glyph_ = nullptr;
    lv_obj_t *home_pause_ = nullptr, *home_pause_text_ = nullptr, *home_badge_ = nullptr;
    Card card_kind_ = Card::None;
    std::vector<std::string> card_names_;
    std::array<Seat, 4> seats_{};
    std::array<int, 4> last_life_{}, delta_{};
    std::array<uint32_t, 4> delta_at_{};
    unsigned last_turn_ = 0;
    unsigned last_seconds_ = 0;
    unsigned last_selected_ = 0;
    std::map<std::string, lv_obj_t*> controls_;
    std::deque<NavEvent> keys_;
    uint32_t last_key_ = LV_KEY_ENTER;
    lv_indev_state_t key_state_ = LV_INDEV_STATE_RELEASED;
    void render(Motion motion = Motion::None);
    void build_status_bar(lv_obj_t* parent);
    void build_header(const char* title, const char* back);
    void build_gameplay_atlas();
    void build_gameplay_sigil();
    void build_settings();
    void build_themes();
    void build_players();
    void build_pairing();
    void build_home();
    void build_effects();
    lv_obj_t* app_icon(const std::string& name, const char* title, const lv_image_dsc_t* glyph, uint32_t color, int cx, int y, unsigned order);
    void update_home(const Snapshot& s);
    lv_obj_t* freeze(lv_obj_t* target);
    void play_turn_effect(lv_obj_t* before);
    void banner(const char* title, const char* detail, uint8_t avatar, unsigned seat, uint32_t delay);
    void gloss(lv_obj_t* obj, uint32_t base, int w, int h, int radius);
    void open_card(Card kind);
    void close_card();
    void update_gameplay(const Snapshot& s);
    int accumulate(unsigned seat, int delta);
    void show_badge(lv_obj_t* badge, int total);
    void activate(const std::string& name);
    void gameplay_key(uint32_t key);
    void bind(lv_obj_t* obj, const std::string& name, bool focusable = true, bool holdable = false);
    lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h);
    lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h);
    lv_obj_t* label(lv_obj_t* parent, const char* text, int x, int y, int width, const lv_font_t* font, uint32_t color);
    lv_obj_t* icon(lv_obj_t* parent, const lv_image_dsc_t* src, uint32_t color);
    lv_obj_t* token(lv_obj_t* parent, int x, int y, int side, uint8_t avatar, unsigned seat, lv_obj_t** image = nullptr);
    lv_obj_t* pill(lv_obj_t* parent, int x, int y, const char* text, uint32_t bg, uint32_t fg, lv_obj_t** text_out = nullptr);
    lv_obj_t* button(lv_obj_t* parent, const std::string& name, const char* text, const lv_image_dsc_t* glyph,
                     int x, int y, int w, int h, Look look = Look::Normal, const lv_font_t* font = nullptr);
    lv_obj_t* hold_to_pass(lv_obj_t* parent, const std::string& name, int x, int y, int w, int h);
    const TurnHubTheme::Palette& palette() const { return TurnHubTheme::palette(theme_); }
    const lv_font_t* title_font(bool large = false) const;
    uint32_t seat_color(unsigned seat) const;
    uint32_t seat_tint(unsigned seat) const;
    uint32_t ms(uint32_t duration) const { return reduced_motion_ ? 0 : duration; }
    void pop(lv_obj_t* obj, int32_t peak = 300);
    static void click(lv_event_t* event);
    static void pass_event(lv_event_t* event);
    static void free_binding(lv_event_t* event);
    static void key_event(lv_event_t* event);
    static void read_keypad(lv_indev_t* indev, lv_indev_data_t* data);
};
} // namespace th
