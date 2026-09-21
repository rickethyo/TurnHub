#include "game_engine.h"
#include "profile_statistics.h"
#include "profile_store.h"
#include "sigil_bus.h"
#include "turnhub_types.h"

namespace {

String resolveProfileId(const TurnHub::PlayerSeat &seat) {
  // Current ESP migration builds only have physical Sigils in the game engine.
  // Keep resolution behind this adapter so a future virtual-seat registry can
  // return its durable profile ID here without changing the statistics engine.
  if (seat.moduleId >= TurnHub::MAX_PHYSICAL_SIGILS) {
    return String();
  }

  TurnHub::SigilBus *bus = TurnHub::SigilBus::activeInstance();
  const TurnHub::SigilRecord *record =
      bus != nullptr ? bus->record(seat.moduleId) : nullptr;
  if (record == nullptr) {
    return String();
  }

  return TurnHubProfiles::profileIdForSeat(record->mac, seat.slot);
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
