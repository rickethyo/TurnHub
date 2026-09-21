#include "game_engine.h"
#include "profile_statistics.h"
#include "profile_store.h"
#include "turnhub_types.h"

namespace {

String resolveProfileId(const TurnHub::PlayerSeat &seat) {
  // Participation captures its profile at game start. Controller discovery,
  // reauthentication and later device bindings cannot redirect match results.
  return String(seat.profileId);
}

void persistCompletedGame(const TurnHub::GameEngine &game) {
  const uint8_t updated =
      TurnHubProfileStats::recordCompletedGame(game, resolveProfileId);
  Serial.print("ATLAS|PROFILE_STATS|GAME_RECORDED|");
  Serial.println(updated);
}

struct ProfileStatsBridgeRegistration {
  ProfileStatsBridgeRegistration() {
    TurnHub::GameEngine::setGameCompletedCallback(persistCompletedGame);
  }
};

ProfileStatsBridgeRegistration registration;

}  // namespace
