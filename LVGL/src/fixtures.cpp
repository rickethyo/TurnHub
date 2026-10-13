#include "fixtures.hpp"
#include <algorithm>
namespace th {
Fixtures::Fixtures() { load(); }
void Fixtures::load() {
    const auto revision = state_.revision + 1;
    state_ = {{{{101, "Rowan", 40, true, 1}, {102, "Mira", 27, true, 2},
                {103, "Ash", 13, true, 3}, {104, "Vale", 0, false, 4}}},
              scenario_ == 0 ? 0u : 1u, 87, 120, Phase::Running, revision};
    if(scenario_ == 1) state_.remaining_seconds = 42;
    if(scenario_ == 2) state_.phase = Phase::Paused;
    if(scenario_ == 3) { state_.phase = Phase::GameOver; state_.remaining_seconds = 0; }
    elapsed_ = 0;
}
void Fixtures::request(Action action) {
    switch(action.kind) {
    case ActionKind::AdjustLife:
        // Visual fixture editing only: deliberately no elimination, team, outcome or statistics rules.
        for(auto& p : state_.players) if(p.participant_id == action.participant_id)
            p.life = static_cast<int>(std::clamp(static_cast<long long>(p.life) + action.amount, -999LL, 999LL));
        break;
    case ActionKind::RequestPause:
        if(state_.phase != Phase::GameOver)
            state_.phase = state_.phase == Phase::Paused ? Phase::Running : Phase::Paused;
        elapsed_ = 0;
        break;
    case ActionKind::DemoNextScenario: scenario_ = (scenario_ + 1) % 4; load(); break;
    case ActionKind::MockPair: paired_ = true; break;
    case ActionKind::MockUnpair: paired_ = false; break;
    }
    ++state_.revision;
}
void Fixtures::advance_ms(uint32_t ms) {
    if(state_.phase != Phase::Running) return;
    elapsed_ += ms;
    while(elapsed_ >= 1000) {
        elapsed_ -= 1000;
        if(state_.remaining_seconds) { --state_.remaining_seconds; ++state_.revision; }
        // Countdown ending does not decide a turn or outcome.
    }
}
} // namespace th
