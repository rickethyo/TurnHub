#pragma once

// Whether the paired Atlas is still answering. Atlas acknowledges every Hello
// (sent every 2 s) and streams state, so silence past LINK_TIMEOUT_MS means
// Atlas is off, restarting or out of range. The Sigil then shows "Searching
// for Atlas" on its screen and a scanning status light instead of the last
// state it was sent, which is stale. Pure logic, host-tested.

#include <stdint.h>

#include "protocol.h"

namespace TurnHubSigil {

enum class LinkChange : uint8_t { None, Lost, Restored };

class AtlasLink {
 public:
  // Paired with a saved Atlas (boot or pairing). The timeout counts from now,
  // so a Sigil that boots with Atlas off shows the lost state too.
  void start(uint32_t nowMs) {
    active_ = true;
    lost_ = false;
    lastHeardMs_ = nowMs;
  }
  // Unpaired or forgotten: no Atlas to lose.
  void stop() {
    active_ = false;
    lost_ = false;
  }
  // Any valid packet from the paired Atlas.
  LinkChange heard(uint32_t nowMs) {
    lastHeardMs_ = nowMs;
    if (!active_ || !lost_) return LinkChange::None;
    lost_ = false;
    return LinkChange::Restored;
  }
  LinkChange update(uint32_t nowMs) {
    if (!active_ || lost_ || nowMs - lastHeardMs_ < TurnHubProtocol::LINK_TIMEOUT_MS) {
      return LinkChange::None;
    }
    lost_ = true;
    return LinkChange::Lost;
  }
  bool lost() const { return active_ && lost_; }

 private:
  bool active_ = false;
  bool lost_ = false;
  uint32_t lastHeardMs_ = 0;
};

}  // namespace TurnHubSigil
