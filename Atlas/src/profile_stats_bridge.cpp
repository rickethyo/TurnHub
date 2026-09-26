#include "profile_stats_bridge.h"

#include "game_engine.h"
#include "profile_statistics.h"
#include "profile_store.h"
#include "turnhub_types.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace {

String resolveProfileId(const TurnHub::PlayerSeat &seat) {
  // Participation captures its profile at game start. Controller discovery,
  // reauthentication and later device bindings cannot redirect match results.
  return String(seat.profileId);
}

}  // namespace

void TurnHubProfileStats::persistCompletedGame(const TurnHub::GameEngine &game) {
  const uint8_t updated =
      TurnHubProfileStats::recordCompletedGame(game, resolveProfileId);
  serialLog.print("ATLAS|PROFILE_STATS|GAME_RECORDED|");
  serialLog.println(updated);
}

namespace {

struct ProfileStatsBridgeRegistration {
  ProfileStatsBridgeRegistration() {
    TurnHub::GameEngine::setGameCompletedCallback(TurnHubProfileStats::persistCompletedGame);
  }
};

ProfileStatsBridgeRegistration registration;

}  // namespace
