#pragma once

#include <Arduino.h>

#include "game_engine.h"
#include "profile_store.h"
#include "sigil_bus.h"

namespace TurnHubProfileStats {

// Roll one completed game into the persistent profile attached to each seat.
// Returns the number of player profiles successfully updated.
uint8_t recordCompletedGame(
    const TurnHub::GameEngine &game,
    const TurnHub::SigilBus &sigilBus);

const char *resultName(TurnHubProfiles::LastGameResult result);
String formatDuration(uint64_t milliseconds);

// Human-readable export used by the authenticated-player download endpoint.
String buildTextReport(
    const String &profileId,
    const String &displayName,
    const TurnHubProfiles::ProfileStats &stats);

}  // namespace TurnHubProfileStats
