#pragma once
#include "presentation.hpp"
namespace th {
// Synthetic presentation data only. Never linked into production firmware.
class Fixtures final : public SnapshotSource, public ActionSink {
public:
    Fixtures();
    const Snapshot& snapshot() const override { return state_; }
    void request(Action action) override;
    void advance_ms(uint32_t ms);
    bool paired() const { return paired_; }
private:
    Snapshot state_;
    unsigned scenario_ = 0;
    uint32_t elapsed_ = 0;
    bool paired_ = false;
    void load();
};
} // namespace th
