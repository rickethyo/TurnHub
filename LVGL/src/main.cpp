#define SDL_MAIN_HANDLED
#include "fixtures.hpp"
#include "sdl_input.hpp"
#include "ui.hpp"
#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
struct Options {
    th::Device device = th::Device::Sigil;
    TurnHubTheme::Id theme = TurnHubTheme::Id::Graphite;
    th::Screen screen = th::Screen::Gameplay;
    unsigned scenario = 0;
    unsigned frames = 0;
    float zoom = 1;
    bool self_test = false;
    bool reduced_motion = false;
    std::string capture;
};
void require(bool ok, const std::string& message) { if(!ok) throw std::runtime_error(message); }
Options options(int argc, char** argv) {
    Options o;
    for(int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        auto value = [&]() -> std::string { require(i + 1 < argc, "Missing value for " + flag); return argv[++i]; };
        if(flag == "--device") {
            const auto v = value(); require(v == "atlas" || v == "sigil", "Device must be atlas or sigil");
            o.device = v == "atlas" ? th::Device::Atlas : th::Device::Sigil;
        } else if(flag == "--theme") {
            const auto v = value(); bool found = false;
            for(unsigned t = 0; t < 4; ++t) if(v == TurnHubTheme::palette(static_cast<TurnHubTheme::Id>(t)).key) { o.theme = static_cast<TurnHubTheme::Id>(t); found = true; }
            require(found, "Unknown theme: " + v);
        } else if(flag == "--screen") {
            const auto v = value();
            if(v == "game") o.screen = th::Screen::Gameplay;
            else if(v == "settings") o.screen = th::Screen::Settings;
            else if(v == "pairing") o.screen = th::Screen::Pairing;
            else if(v == "themes") o.screen = th::Screen::Themes;
            else if(v == "players") o.screen = th::Screen::Players;
            else throw std::runtime_error("Unknown screen: " + v);
        } else if(flag == "--scenario") { o.scenario = static_cast<unsigned>(std::stoul(value())); require(o.scenario < 4, "Scenario must be 0..3"); }
        else if(flag == "--frames") { o.frames = static_cast<unsigned>(std::stoul(value())); require(o.frames > 0, "Frames must be positive"); }
        else if(flag == "--zoom") { o.zoom = std::stof(value()); require(o.zoom >= 1 && o.zoom <= 4, "Zoom must be 1..4"); }
        else if(flag == "--capture") o.capture = value();
        else if(flag == "--self-test") o.self_test = true;
        else if(flag == "--reduced-motion") o.reduced_motion = true;
        else if(flag == "--help") {
            std::cout << "TurnHub LVGL desktop demo\n--device atlas|sigil --theme graphite|daylight|brass|contrast\n"
                         "--screen game|settings|pairing|themes|players --scenario 0..3\n"
                         "--zoom 1..4 --frames N --capture image.bmp --self-test\n"
                         "Mouse: touchscreen. Arrows/Enter: five-way. M menu, T theme, N sample, Space pause, Escape game.\n";
            std::exit(0);
        } else throw std::runtime_error("Unknown option: " + flag);
    }
    return o;
}
void pump(th::Ui& ui, th::SdlInput& input, unsigned ms) {
    // The official SDL backend installs SDL_GetTicks as LVGL's clock callback.
    const uint32_t start = SDL_GetTicks();
    do {
        input.drain(); ui.update(); lv_timer_handler(); SDL_Delay(5);
    } while(SDL_GetTicks() - start < ms);
}
void key(th::Ui& ui, th::SdlInput& input, SDL_Keycode code) {
    SDL_Event e{}; e.type = SDL_KEYDOWN; e.key.keysym.sym = code; e.key.state = SDL_PRESSED;
    require(SDL_PushEvent(&e) == 1, "Could not inject keyboard press"); pump(ui, input, 80);
    e.type = SDL_KEYUP; e.key.state = SDL_RELEASED;
    require(SDL_PushEvent(&e) == 1, "Could not inject keyboard release"); pump(ui, input, 80);
}
void touch(th::Ui& ui, th::SdlInput& input, const std::string& id) {
    auto* obj = ui.control(id); require(obj != nullptr, "Missing control " + id);
    lv_obj_update_layout(lv_screen_active());
    lv_area_t a; lv_obj_get_coords(obj, &a);
    SDL_Event e{}; e.type = SDL_MOUSEMOTION;
    e.motion.x = (a.x1 + a.x2) / 2; e.motion.y = (a.y1 + a.y2) / 2;
    require(SDL_PushEvent(&e) == 1, "Could not inject pointer motion");
    e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT; e.button.state = SDL_PRESSED;
    e.button.x = (a.x1 + a.x2) / 2; e.button.y = (a.y1 + a.y2) / 2;
    require(SDL_PushEvent(&e) == 1, "Could not inject pointer press"); pump(ui, input, 80);
    e.type = SDL_MOUSEBUTTONUP; e.button.state = SDL_RELEASED;
    require(SDL_PushEvent(&e) == 1, "Could not inject pointer release"); pump(ui, input, 80);
}
void self_test(th::Ui& ui, th::Fixtures& fixtures, th::SdlInput& input, lv_display_t* display) {
    require(lv_display_get_horizontal_resolution(display) == ui.display_profile().width, "Wrong display width");
    require(lv_display_get_vertical_resolution(display) == ui.display_profile().height, "Wrong display height");
    require(ui.bounds_valid(), "Gameplay controls outside display");
    require(ui.visible_text("name") == "Rowan", "Initial turn/player name not rendered");
    require(ui.visible_text("timer").find("01:27") != std::string::npos, "Initial timer not rendered");
    const auto initial = fixtures.snapshot().players[0].life;
    key(ui, input, SDLK_RIGHT);
    require(fixtures.snapshot().players[0].life == initial + 1, "Keypad right did not emit life request");
    if(ui.display_profile().compact()) require(ui.visible_text("life") == "41", "Life request not rendered");
    key(ui, input, SDLK_LEFT);
    require(fixtures.snapshot().players[0].life == initial, "Keypad left did not emit life request");
    key(ui, input, SDLK_DOWN); require(ui.selected_player() == 1, "Keypad player selection failed");
    key(ui, input, SDLK_UP); require(ui.screen() == th::Screen::Settings, "Keypad menu failed");
    require(ui.bounds_valid(), "Settings outside display");
    key(ui, input, SDLK_DOWN);
    key(ui, input, SDLK_RETURN);
    require(ui.screen() == th::Screen::Themes, "Keypad menu focus/select failed");
    key(ui, input, SDLK_DOWN); key(ui, input, SDLK_RETURN);
    require(ui.theme() == TurnHubTheme::Id::Daylight, "Keypad theme selection failed");
    for(unsigned t = 0; t < 4; ++t) {
        touch(ui, input, "theme" + std::to_string(t));
        require(ui.theme() == static_cast<TurnHubTheme::Id>(t), "Touch theme selection failed");
        require(ui.bounds_valid(), "Theme controls outside display");
    }
    touch(ui, input, "settings"); touch(ui, input, "pairing");
    require(ui.screen() == th::Screen::Pairing && ui.bounds_valid(), "Pairing screen failed");
    touch(ui, input, "pair"); require(fixtures.paired(), "Mock pairing action not delivered");
    touch(ui, input, "pairing"); touch(ui, input, "unpair"); require(!fixtures.paired(), "Mock unpairing failed");
    touch(ui, input, "players"); touch(ui, input, "choose2");
    require(ui.selected_player() == 2 && ui.bounds_valid(), "Touch player selection failed");
    touch(ui, input, "settings"); touch(ui, input, "back");
    if(ui.display_profile().compact()) {
        const auto before = fixtures.snapshot().players[2].life;
        touch(ui, input, "plus");
        require(fixtures.snapshot().players[2].life == before + 1, "Touch plus request failed");
        touch(ui, input, "minus");
        require(fixtures.snapshot().players[2].life == before, "Touch life adjustment failed");
    }
    const auto seconds = fixtures.snapshot().remaining_seconds;
    fixtures.advance_ms(1000); ui.update();
    require(fixtures.snapshot().remaining_seconds == seconds - 1, "Running fixture timer failed");
    require(ui.visible_text("timer").find("01:26") != std::string::npos, "Running timer text did not update");
    key(ui, input, SDLK_SPACE); require(fixtures.snapshot().phase == th::Phase::Paused, "Pause shortcut failed");
    require(ui.visible_text("status") == "PAUSED", "Paused state not rendered");
    const auto paused = fixtures.snapshot().remaining_seconds;
    fixtures.advance_ms(2000); require(fixtures.snapshot().remaining_seconds == paused, "Paused timer changed");
    key(ui, input, SDLK_SPACE); require(fixtures.snapshot().phase == th::Phase::Running, "Resume failed");
    touch(ui, input, "scenario"); require(fixtures.snapshot().turn_holder == 1, "Alternate turn fixture failed");
    touch(ui, input, "scenario"); require(fixtures.snapshot().phase == th::Phase::Paused, "Paused sample failed");
    touch(ui, input, "scenario"); require(fixtures.snapshot().phase == th::Phase::GameOver, "Game-over sample failed");
    require(ui.visible_text("status") == "GAME OVER", "Game-over state not rendered");
    require(!fixtures.snapshot().players[3].active, "Inactive player missing");
    touch(ui, input, "scenario"); require(fixtures.snapshot().phase == th::Phase::Running, "Sample cycle failed");
    key(ui, input, SDLK_RETURN); require(fixtures.snapshot().turn_holder == 1, "Keypad select sample failed");
    key(ui, input, SDLK_t); require(ui.theme() == TurnHubTheme::Id::Graphite, "Theme shortcut failed");
    // Long and signed life labels, then an invalid theme must fail closed.
    fixtures.request({th::ActionKind::AdjustLife, 103, -9999}); ui.update();
    require(fixtures.snapshot().players[2].life == -999, "Fixture edit bounds failed");
    if(ui.display_profile().compact()) require(ui.visible_text("life") == "-999", "Signed life total not rendered");
    ui.set_theme(static_cast<TurnHubTheme::Id>(99)); require(ui.theme() == TurnHubTheme::Id::Graphite, "Invalid theme accepted");
    require(ui.bounds_valid(), "Final controls outside display");
    std::cout << "PASS: " << (ui.display_profile().compact() ? "Sigil" : "Atlas")
              << " resolution, SDL pointer/keypad, focus, themes, pairing, players, life, timer, pause and fixture states\n";
}
void capture(lv_display_t* display, const std::string& path) {
    auto* renderer = static_cast<SDL_Renderer*>(lv_sdl_window_get_renderer(display));
    int width = 0, height = 0;
    require(SDL_GetRendererOutputSize(renderer, &width, &height) == 0, SDL_GetError());
    auto* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    require(surface != nullptr, SDL_GetError());
    const int read = SDL_RenderReadPixels(renderer, nullptr, surface->format->format, surface->pixels, surface->pitch);
    const int saved = read == 0 ? SDL_SaveBMP(surface, path.c_str()) : -1;
    SDL_FreeSurface(surface); require(saved == 0, "Screenshot failed: " + std::string(SDL_GetError()));
    std::cout << "Captured " << width << 'x' << height << " to " << path << '\n';
}
} // namespace
int main(int argc, char** argv) {
    try {
        const auto o = options(argc, argv);
        SDL_SetMainReady();
        SDL_version runtime{}; SDL_GetVersion(&runtime);
        require(runtime.major == 2 && runtime.minor == 32 && runtime.patch == 4, "SDL runtime must be pinned 2.32.4");
        lv_init();
        const auto p = th::profile(o.device);
        auto* display = lv_sdl_window_create(p.width, p.height);
        require(display != nullptr, "SDL display creation failed");
        lv_sdl_window_set_title(display, p.compact() ? "TurnHub Sigil / LVGL demo" : "TurnHub Atlas / LVGL demo");
        lv_sdl_window_set_resizeable(display, false); lv_sdl_window_set_zoom(display, o.zoom);
        auto* pointer = lv_sdl_mouse_create(); lv_indev_set_display(pointer, display);
        {
            th::Fixtures fixtures;
            for(unsigned i = 0; i < o.scenario; ++i) fixtures.request({th::ActionKind::DemoNextScenario});
            th::PresetAssets assets;
            th::Ui ui(p, fixtures, fixtures, assets);
            // Tests and captures need settled geometry, never a frame mid-transition.
            ui.set_reduced_motion(o.reduced_motion || o.self_test || !o.capture.empty());
            ui.set_theme(o.theme); ui.show(o.screen);
            th::SdlInput input(ui, fixtures);
            pump(ui, input, 100);
            if(o.self_test) self_test(ui, fixtures, input, display);
            else if(!o.capture.empty()) { pump(ui, input, 100); capture(display, o.capture); }
            else {
                std::cout << "Desktop demo: mouse touch, arrows/Enter five-way; M menu, T theme, N sample, Space pause, Escape game\n";
                uint32_t last = SDL_GetTicks();
                for(unsigned frame = 0; !input.quit_requested() && (o.frames == 0 || frame < o.frames); ++frame) {
                    const auto now = SDL_GetTicks(); fixtures.advance_ms(now - last); last = now;
                    input.drain(); ui.update(); lv_timer_handler(); SDL_Delay(5);
                }
            }
        }
        lv_indev_delete(pointer); lv_display_delete(display); lv_deinit();
        return 0;
    } catch(const std::exception& e) { std::cerr << "TurnHub simulator: " << e.what() << '\n'; return 1; }
}
