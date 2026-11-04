#pragma once
#include "ui.hpp"
#include <SDL.h>
#include <deque>
#include <mutex>
namespace th {
// SDL is contained here; future PCA9554 adapter only needs Ui::navigation(NavEvent).
class SdlInput {
public:
    SdlInput(Ui& ui, ActionSink& sink);
    ~SdlInput();
    void drain();
    bool quit_requested() const { return quit_; }
private:
    Ui& ui_;
    ActionSink& sink_;
    std::deque<SDL_Event> events_;
    std::mutex mutex_;
    bool quit_ = false;
    static int watch(void* userdata, SDL_Event* event);
};
} // namespace th
