// Shared application helpers: seat resolution, audio target masks and the
// Intent builders every adapter uses. Nothing here mutates canonical state.

#include "atlas_app.h"
#include "controller_profiles.h"
#include "profile_store.h"

namespace TurnHubAtlas {

const char *intentOriginName(IntentOrigin origin) {
  switch (origin) {
    case IntentOrigin::PhysicalSigil: return "SIGIL";
    case IntentOrigin::Browser: return "BROWSER";
    case IntentOrigin::AndroidApp: return "ANDROID";
    case IntentOrigin::AtlasHardware: return "ATLAS";
    case IntentOrigin::TableTablet: return "TABLET";
    case IntentOrigin::Simulator: return "SIMULATOR";
    case IntentOrigin::System: return "SYSTEM";
    case IntentOrigin::Unknown:
    default:
      return "UNKNOWN";
  }
}

String profileIdForTableSeat(const PlayerSeat &seat, bool inGame) {
  if (inGame) return String(seat.profileId);
  // Phone participants have no Sigil binding; their seat is their profile.
  if (seat.controllerId >= MAX_PHYSICAL_SIGILS) {
    return TurnHubControllers::profileForSeat(seat.controllerId, seat.slot);
  }
  return TurnHubControllers::existingProfileForSeat(seat.controllerId, seat.slot);
}

String displayNameForTableSeat(const PlayerSeat &seat, bool inGame) {
  const String profileId = profileIdForTableSeat(seat, inGame);
  return profileId.length() ? TurnHubProfiles::nameForProfile(profileId) : String();
}

// --- Venue tables --------------------------------------------------------------

uint8_t sigilTable[MAX_PHYSICAL_SIGILS] = {};

namespace {
bool gameInProgress(HubState state) {
  return state == HubState::Starting || state == HubState::Running || state == HubState::Paused;
}
}  // namespace

uint8_t tableForController(uint8_t controllerId) {
  for (uint8_t t = 0; t < MAX_GAME_TABLES; ++t) {
    if (tables[t].lobby.isJoined(controllerId) || tables[t].game.controllerInGame(controllerId)) return t;
  }
  return controllerId < MAX_PHYSICAL_SIGILS ? sigilTable[controllerId] : 0;
}

bool seatedAnywhere(uint8_t controllerId) {
  for (const GameTable &t : tables) {
    if (t.lobby.isJoined(controllerId) || t.game.controllerInGame(controllerId)) return true;
  }
  return false;
}

int8_t tableForProfile(const String &profileId) {
  for (uint8_t t = 0; t < MAX_GAME_TABLES; ++t) {
    TableScope scope(t);
    uint8_t controllerId = INVALID_ID, slot = 1;
    if (resolveProfileParticipant(profileId, controllerId, slot)) return static_cast<int8_t>(t);
  }
  return -1;
}

uint16_t sigilsAtTable(uint8_t index) {
  uint16_t mask = 0;
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if (tableForController(id) == index) mask |= static_cast<uint16_t>(1u << id);
  }
  return mask;
}

bool allTablesBetweenGames() {
  for (const GameTable &t : tables) {
    if (t.hubState != HubState::Lobby && t.hubState != HubState::GameOver) return false;
  }
  return true;
}

bool atlasSpeakerShared() {
  uint8_t busy = 0;
  for (const GameTable &t : tables) busy += gameInProgress(t.hubState) ? 1 : 0;
  return busy > 1;
}

// Atlas's speaker plays table-wide cues only while at most one game is in
// progress (owner decision 2026-09-29); Sigils and phones still get them.
uint16_t lobbyAudioMask() {
  uint16_t mask = atlasSpeakerShared() ? 0 : AudioController::ATLAS_SPEAKER_MASK;
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if (table().lobby.isJoined(id)) mask |= AudioController::maskForSigil(id);
  }
  return mask;
}

uint16_t gameAudioMask() {
  uint16_t mask = atlasSpeakerShared() ? 0 : AudioController::ATLAS_SPEAKER_MASK;
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if (table().game.controllerInGame(id)) mask |= AudioController::maskForSigil(id);
  }
  return mask;
}

bool seatForModuleSlot(uint8_t controllerId, uint8_t slot, PlayerSeat &seat) {
  PlayerSeat local[2];
  const uint8_t count = table().game.hasPlayers()
      ? table().game.playersForController(controllerId, local, 2)
      : table().lobby.playersForController(controllerId, local, 2);
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
  if (table().game.livingPlayersForController(controllerId, local, 2) == 0) return false;
  seat = local[0];
  return true;
}

const PlayerSeat *seatForIntentActor(const Intent &intent) {
  if (intent.actor.playerNumber == 0) return nullptr;
  const PlayerSeat *seat = table().game.playerByNumber(intent.actor.playerNumber);
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
  if (table().game.hasPlayers()) {
    count = table().game.playerCount();
    for (uint8_t i = 0; i < count; ++i) seats[i] = *table().game.playerAt(i);
  } else {
    count = table().lobby.buildPlayers(seats, MAX_PLAYERS);
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
