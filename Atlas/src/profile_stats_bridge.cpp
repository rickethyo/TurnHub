#include "profile_stats_bridge.h"

#include "game_engine.h"
#include "game_recovery.h"
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

uint32_t TurnHubProfileStats::shortGameMinimumMs = TurnHub::SHORT_GAME_MS;

bool TurnHubProfileStats::heldShortGame(const TurnHub::GameEngine &game) {
  // A finished match's clock stops at its end, so the time argument is unused.
  return game.gameOver() && game.gameElapsedMs(0) < shortGameMinimumMs;
}

uint8_t TurnHubProfileStats::countShortGame(const TurnHub::GameEngine &game) {
  if (!heldShortGame(game)) return 0;
  // The finished checkpoint was committed when the match ended.
  const uint8_t updated = TurnHubProfileStats::recordCompletedGame(game, resolveProfileId);
  serialLog.print("ATLAS|PROFILE_STATS|SHORT_GAME_COUNTED|");
  serialLog.println(updated);
  return updated;
}

void TurnHubProfileStats::persistCompletedGame(const TurnHub::GameEngine &game) {
  // The observer runs after this callback. Commit GameOver now so a power cut
  // after a profile write cannot resurrect the same match as unfinished.
  // A failed/uncertain checkpoint must not be followed by aggregate increments.
  const auto status = TurnHub::checkpointCompletedGame(game, millis());
  if (status != TurnHubStorage::Status::Ok) {
    serialLog.print("ATLAS|PROFILE_STATS|SKIPPED_CHECKPOINT|");
    serialLog.println(TurnHub::storageStatusName(status));
    return;
  }
  if (heldShortGame(game)) {
    serialLog.print("ATLAS|PROFILE_STATS|SHORT_GAME_HELD|MS|");
    serialLog.println(game.gameElapsedMs(0));
    return;
  }
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
