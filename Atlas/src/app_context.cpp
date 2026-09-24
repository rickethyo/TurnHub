// Shared application helpers: seat resolution, audio target masks and the
// Intent builders every adapter uses. Nothing here mutates canonical state.

#include "atlas_app.h"
#include "controller_profiles.h"

namespace TurnHubAtlas {

const char *intentOriginName(IntentOrigin origin) {
  switch (origin) {
    case IntentOrigin::PhysicalSigil: return "SIGIL";
    case IntentOrigin::Browser: return "BROWSER";
    case IntentOrigin::AndroidApp: return "ANDROID";
    case IntentOrigin::AtlasHardware: return "ATLAS";
    case IntentOrigin::Simulator: return "SIMULATOR";
    case IntentOrigin::System: return "SYSTEM";
    case IntentOrigin::Unknown:
    default:
      return "UNKNOWN";
  }
}

uint16_t lobbyAudioMask() {
  uint16_t mask = 0;
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if (lobby.isJoined(id)) mask |= AudioController::maskForSigil(id);
  }
  return mask;
}

uint16_t gameAudioMask() {
  uint16_t mask = 0;
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if (game.controllerInGame(id)) mask |= AudioController::maskForSigil(id);
  }
  return mask;
}

bool seatForModuleSlot(uint8_t controllerId, uint8_t slot, PlayerSeat &seat) {
  PlayerSeat local[2];
  const uint8_t count = game.hasPlayers()
      ? game.playersForController(controllerId, local, 2)
      : lobby.playersForController(controllerId, local, 2);
  for (uint8_t i = 0; i < count; ++i) {
    if (local[i].slot == slot) {
      seat = local[i];
      return true;
    }
  }
  return false;
}

bool firstLivingSeatForModule(uint8_t controllerId, PlayerSeat &seat) {
  PlayerSeat local[2];
  if (game.livingPlayersForController(controllerId, local, 2) == 0) return false;
  seat = local[0];
  return true;
}

const PlayerSeat *seatForIntentActor(const Intent &intent) {
  if (intent.actor.playerNumber == 0) return nullptr;
  const PlayerSeat *seat = game.playerByNumber(intent.actor.playerNumber);
  if (seat == nullptr ||
      seat->controllerId != intent.actor.controllerId ||
      seat->slot != intent.actor.slot) {
    return nullptr;
  }
  return seat;
}

bool resolveProfileParticipant(const String &profileId, uint8_t &controllerId, uint8_t &slot) {
  if (profileId.length() == 0) return false;
  PlayerSeat seats[MAX_PLAYERS];
  uint8_t count = 0;
  if (game.hasPlayers()) {
    count = game.playerCount();
    for (uint8_t i = 0; i < count; ++i) seats[i] = *game.playerAt(i);
  } else {
    count = lobby.buildPlayers(seats, MAX_PLAYERS);
  }
  for (uint8_t i = 0; i < count; ++i) {
    // A started game carries the captured profile; the lobby asks the
    // controller registry for the current assignment.
    const String owner = seats[i].profileId[0] ? String(seats[i].profileId) :
        TurnHubControllers::profileForSeat(seats[i].controllerId, seats[i].slot);
    if (owner == profileId) {
      controllerId = seats[i].controllerId;
      slot = seats[i].slot;
      return true;
    }
  }
  return false;
}

IntentResult dispatchSeatIntent(IntentType type, IntentOrigin origin,
    const PlayerSeat &seat, uint32_t flags) {
  Intent intent;
  intent.type = type;
  intent.payload.flags = flags;
  intent.actor.origin = origin;
  intent.actor.controllerId = seat.controllerId;
  intent.actor.slot = seat.slot;
  intent.actor.playerNumber = seat.playerNumber;
  return intents.dispatch(intent);
}

IntentResult dispatchModuleIntent(IntentType type, uint8_t controllerId,
    uint8_t slot, int32_t value) {
  Intent intent;
  intent.type = type;
  intent.actor.origin = IntentOrigin::PhysicalSigil;
  intent.actor.controllerId = controllerId;
  intent.actor.slot = slot;
  intent.payload.value = value;
  return intents.dispatch(intent);
}

IntentResult dispatchSystemIntent(IntentType type) {
  Intent intent;
  intent.type = type;
  intent.actor.origin = IntentOrigin::System;
  return intents.dispatch(intent);
}

}  // namespace TurnHubAtlas
