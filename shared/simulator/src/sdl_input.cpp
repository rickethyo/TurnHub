#include "sdl_input.hpp"
namespace th {
SdlInput::SdlInput(Ui& ui, ActionSink& sink) : ui_(ui), sink_(sink) { SDL_AddEventWatch(watch, this); }
SdlInput::~SdlInput() { SDL_DelEventWatch(watch, this); }
int SdlInput::watch(void* userdata, SDL_Event* e) {
    if(e->type != SDL_KEYDOWN && e->type != SDL_KEYUP && e->type != SDL_QUIT) return 1;
    if(e->type == SDL_KEYDOWN && e->key.repeat) return 1;
    auto* input = static_cast<SdlInput*>(userdata);
    std::lock_guard<std::mutex> lock(input->mutex_);
    if(input->events_.size() < 128) input->events_.push_back(*e);
    return 1;
}
void SdlInput::drain() {
    std::deque<SDL_Event> events;
    { std::lock_guard<std::mutex> lock(mutex_); events.swap(events_); }
    for(const auto& e : events) {
        if(e.type == SDL_QUIT) { quit_ = true; continue; }
        const bool pressed = e.type == SDL_KEYDOWN;
        switch(e.key.keysym.sym) {
        case SDLK_UP: ui_.navigation({NavKey::Up, pressed}); break;
        case SDLK_DOWN: ui_.navigation({NavKey::Down, pressed}); break;
        case SDLK_LEFT: ui_.navigation({NavKey::Left, pressed}); break;
        case SDLK_RIGHT: ui_.navigation({NavKey::Right, pressed}); break;
        case SDLK_RETURN: ui_.navigation({NavKey::Select, pressed}); break;
        default:
            if(!pressed) break;
            switch(e.key.keysym.sym) {
            case SDLK_m: ui_.show(Screen::Settings); break;
            case SDLK_t: ui_.set_theme(TurnHubTheme::next(ui_.theme())); break;
            case SDLK_n: sink_.request({ActionKind::DemoNextScenario}); break;
            case SDLK_SPACE: sink_.request({ActionKind::RequestPause}); break;
            case SDLK_ESCAPE: ui_.show(Screen::Gameplay); break;
            default: break;
            }
        }
    }
}
} // namespace th
