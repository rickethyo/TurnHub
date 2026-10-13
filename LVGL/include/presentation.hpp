#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace th {
enum class Device { Atlas, Sigil };
struct DisplayProfile {
    Device device;
    int width, height;
    bool compact() const { return device == Device::Sigil; }
};
inline DisplayProfile profile(Device device) {
    return device == Device::Atlas ? DisplayProfile{device, 480, 320}
                                   : DisplayProfile{device, 240, 320};
}
enum class Phase { Running, Paused, GameOver };
struct PlayerView {
    uint32_t participant_id;
    std::string name;
    int life;
    bool active;
    uint8_t avatar;
};
struct Snapshot {
    std::array<PlayerView, 4> players;
    unsigned turn_holder = 0;
    unsigned remaining_seconds = 87;
    unsigned turn_seconds = 120; // Full turn allowance, for clock progress only.
    Phase phase = Phase::Running;
    uint32_t revision = 1;
};
enum class ActionKind { AdjustLife, RequestPause, DemoNextScenario, MockPair, MockUnpair };
struct Action { ActionKind kind; uint32_t participant_id = 0; int amount = 0; };
class SnapshotSource {
public:
    virtual ~SnapshotSource() = default;
    virtual const Snapshot& snapshot() const = 0;
};
class ActionSink {
public:
    virtual ~ActionSink() = default;
    virtual void request(Action action) = 0;
};
enum class NavKey { Up, Down, Left, Right, Select };
struct NavEvent { NavKey key; bool pressed; };
} // namespace th
