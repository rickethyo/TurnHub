#pragma once

// Finding Atlas on its table channel (protocol.h, 2026-10-30). The owner can
// move Atlas off channel 6 to match a home network, so a Sigil that has lost
// Atlas, or is pairing and has had no answer, steps through channels 1-11,
// CHANNEL_DWELL_MS on each: the caller tunes the radio and sends a
// SecureHello (or pair request) whenever step() moves. The first frame from
// Atlas ends the search on that channel, which the Sigil remembers for its
// next start-up. Pure logic, host-tested.

#include <stdint.h>

#include "protocol.h"

namespace TurnHubSigil {

class ChannelSearch {
 public:
  // The channel to start on (the saved one; the default if it is invalid).
  void begin(uint8_t channel) {
    channel_ = TurnHubProtocol::validTableChannel(channel) ? channel : TurnHubProtocol::DEFAULT_TABLE_CHANNEL;
  }
  uint8_t channel() const { return channel_; }

  // While `searching`, moves to the next channel once the dwell has passed
  // and returns true; the caller retunes and probes. The first call of a
  // search stays put for one dwell, since Atlas is most likely still there.
  bool step(uint32_t nowMs, bool searching) {
    if (!searching) {
      active_ = false;
      return false;
    }
    if (!active_) {
      active_ = true;
      sinceMs_ = nowMs;
      return false;
    }
    if (nowMs - sinceMs_ < TurnHubProtocol::CHANNEL_DWELL_MS) return false;
    sinceMs_ = nowMs;
    channel_ = channel_ >= TurnHubProtocol::MAX_TABLE_CHANNEL ? TurnHubProtocol::MIN_TABLE_CHANNEL : channel_ + 1;
    return true;
  }

 private:
  uint8_t channel_ = TurnHubProtocol::DEFAULT_TABLE_CHANNEL;
  bool active_ = false;
  uint32_t sinceMs_ = 0;
};

}  // namespace TurnHubSigil
