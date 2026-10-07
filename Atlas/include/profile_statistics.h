#pragma once

#include <Arduino.h>

#include "game_engine.h"
#include "profile_store.h"

namespace TurnHubProfileStats {

// One player's part in a finished game, as it is rolled into their profile.
struct GameResult {
  uint8_t gameProfile = 0;
  uint32_t durationMs = 0;
  TurnHubProfiles::LastGameResult result = TurnHubProfiles::LastGameResult::Loss;
  bool started = false;
  uint32_t turns = 0;
  uint32_t turnMs = 0;
  uint32_t fastestTurnMs = 0;
  uint32_t longestTurnMs = 0;
};

// Adds one finished game to a profile's statistics (live games and imported
// standalone games alike).
void applyGameResult(TurnHubProfiles::ProfileStats &stats, const GameResult &result);

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
