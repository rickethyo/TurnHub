#pragma once

#include <cstdio>

#include "protocol.h"

namespace TurnHubSigil {

// Commander damage stays off the game screen until this player has taken
// some (test feedback, 2026-09-28). Atlas sends only sources with damage.
inline bool commanderDamageShown(const TurnHubProtocol::GameDisplayPacket &s) {
  return s.commander && (s.sourceCount || s.omittedSources);
}

// "7", "7/3", or "3 (C2)" when only commander 2 has hit (keeps slot identity).
inline void formatCommanderDamage(const TurnHubProtocol::DisplayCommanderSource &entry,
    char *out, size_t size) {
  if (!entry.damage[0] && entry.damage[1]) {
    snprintf(out, size, "%ld (C2)", static_cast<long>(entry.damage[1]));
  } else if (entry.damage[1]) {
    snprintf(out, size, "%ld/%ld", static_cast<long>(entry.damage[0]),
        static_cast<long>(entry.damage[1]));
  } else {
    snprintf(out, size, "%ld", static_cast<long>(entry.damage[0]));
  }
}

}  // namespace TurnHubSigil
