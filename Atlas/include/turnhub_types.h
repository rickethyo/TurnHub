#pragma once

#include <Arduino.h>

namespace TurnHub {

constexpr uint8_t MAX_PHYSICAL_SIGILS = 8;
constexpr uint8_t MAX_PLAYERS = MAX_PHYSICAL_SIGILS * 2;
// Physical controller handles occupy the radio-compatible prefix; browser
// registrations have their own range and never create SigilBus records.
constexpr uint8_t MAX_CONTROLLERS = MAX_PHYSICAL_SIGILS + MAX_PLAYERS;
constexpr uint8_t INVALID_ID = 0xFF;

enum class HubState : uint8_t {
  Lobby,
  Starting,
  Running,
  Paused,
  GameOver,
};

inline const char *stateName(HubState state) {
  switch (state) {
    case HubState::Lobby: return "LOBBY";
    case HubState::Starting: return "STARTING";
    case HubState::Running: return "RUNNING";
    case HubState::Paused: return "PAUSED";
    case HubState::GameOver: return "GAME_OVER";
    default: return "UNKNOWN";
  }
}

struct PlayerSeat {
  PlayerSeat() = default;

  PlayerSeat(
      uint8_t playerNumberValue,
      uint8_t controllerIdValue,
      uint8_t slotValue = 1)
      : playerNumber(playerNumberValue),
        controllerId(controllerIdValue),
        slot(slotValue) {}

  uint8_t playerNumber = 0;
  uint8_t controllerId = INVALID_ID;
  uint8_t slot = 1;
  uint32_t participantId = 0;  // Atlas-local identity, stable while at this table.
  char profileId[9] = {};      // Captured at game start, independent of controller.

  bool valid() const {
    return playerNumber != 0 && controllerId != INVALID_ID;
  }

  char slotName() const {
    return slot == 1 ? 'A' : 'B';
  }

  bool sameSeat(const PlayerSeat &other) const {
    return controllerId == other.controllerId && slot == other.slot;
  }
};

struct PlayerStats {
  uint32_t turnsCompleted = 0;
  uint32_t totalTurnMs = 0;
  uint32_t fastestTurnMs = 0;
  uint32_t longestTurnMs = 0;
};

}  // namespace TurnHub
