#pragma once

#include <cstdint>

namespace TurnHubClock {
enum class Source : uint8_t { Unknown, Phone, Internet };

/** UTC bookkeeping only. Game timing and authorization use their own monotonic clocks. */
class WallClock {
 public:
  bool observe(uint64_t utcMs, uint32_t uptimeMs, Source source = Source::Phone) {
    if (utcMs < 1577836800000ULL || utcMs > 4102444800000ULL) return false;
    // A recent NTP result wins over a phone's potentially inaccurate clock.
    if (source == Source::Phone && source_ == Source::Internet && uptimeMs - networkAt_ < 21600000UL) return false;
    utcMs_ = utcMs; sampledAt_ = uptimeMs; source_ = source;
    if (source == Source::Internet) networkAt_ = uptimeMs;
    return true;
  }
  uint64_t now(uint32_t uptimeMs) {
    if (source_ == Source::Unknown) return 0;
    utcMs_ += static_cast<uint32_t>(uptimeMs - sampledAt_);
    sampledAt_ = uptimeMs;
    return utcMs_;
  }
  Source source() const { return source_; }
 private:
  uint64_t utcMs_ = 0;
  uint32_t sampledAt_ = 0, networkAt_ = 0;
  Source source_ = Source::Unknown;
};

inline WallClock clock;
}
