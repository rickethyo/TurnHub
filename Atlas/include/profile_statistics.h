#pragma once

#include <Arduino.h>

#include "game_engine.h"
#include "profile_store.h"

namespace TurnHubProfileStats {

using ResolveProfileIdCallback = String (*)(const TurnHub::PlayerSeat &seat);

// Roll one completed game into the persistent profile attached to each logical
// player seat. Resolution is deliberately abstracted from physical hardware so
// virtual Sigils can participate without changing the statistics engine.
uint8_t recordCompletedGame(
    const TurnHub::GameEngine &game,
    ResolveProfileIdCallback resolveProfileId);

const char *resultName(TurnHubProfiles::LastGameResult result);
String formatDuration(uint64_t milliseconds);

// Human-readable export used by the authenticated-player download endpoint.
String buildTextReport(
    const String &profileId,
    const String &displayName,
    const TurnHubProfiles::ProfileStats &stats);

}  // namespace TurnHubProfileStats
