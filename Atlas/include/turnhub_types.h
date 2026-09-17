#pragma once

#include <Arduino.h>

namespace TurnHub {

constexpr uint8_t MAX_PHYSICAL_SIGILS = 8;
constexpr uint8_t MAX_PLAYERS = MAX_PHYSICAL_SIGILS * 2;
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
  uint8_t playerNumber = 0;
  uint8_t moduleId = INVALID_ID;
  uint8_t slot = 1;

  bool valid() const {
    return playerNumber != 0 && moduleId != INVALID_ID;
  }

  char slotName() const {
    return slot == 1 ? 'A' : 'B';
  }

  bool sameSeat(const PlayerSeat &other) const {
    return moduleId == other.moduleId && slot == other.slot;
  }
};

struct PlayerStats {
  uint32_t turnsCompleted = 0;
  uint32_t totalTurnMs = 0;
};

}  // namespace TurnHub
