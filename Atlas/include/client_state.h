#pragma once

#include "game_engine.h"
#include "lobby.h"

namespace TurnHub {

// Read model only: no rules, storage, authentication or transport state.
struct ClientPending {
  uint8_t passPlayer = 0;
  uint32_t passStartedMs = 0;
  uint32_t countdownStartedMs = 0;
  uint8_t eliminationTarget = 0;
};

// The /api/v1/state projection (protocol/state-v0.1.schema.json). observe()
// copies the fields clients render and bumps revision() whenever any changed;
// clocks are sampled at json() time and never bump the revision.
class ClientState {
 public:
  void observe(HubState state, const Lobby &lobby, const GameEngine &game,
      const GameSettings &nextSettings, const ClientPending &pending);
  uint32_t revision() const { return revision_; }
  // True when a pending life request has passed LIFE_APPROVAL_MS.
  bool expirationDue(uint32_t nowMs) const;
  String json(const String &atlasId, const char *bootId, const GameEngine &game,
      uint32_t nowMs, uint32_t passGraceMs) const;

 private:
  struct Player {
    uint8_t number = 0, controller = INVALID_ID, slot = 0;
    uint32_t participant = 0, turnsCompleted = 0;
    bool eliminated = false;
    int32_t life = 0;
    int32_t damage[MAX_PLAYERS][COMMANDERS_PER_PLAYER] = {};
    LifeChangeRequest request{};
  };
  Player players_[MAX_PLAYERS]{};
  HubState state_ = HubState::Lobby;
  GameSettings settings_{};
  ClientPending pending_{};
  uint8_t count_ = 0, host_ = INVALID_ID, starter_ = 0, active_ = 0;
  uint8_t winner_ = 0, claimant_ = 0, confirmation_ = 0;
  bool inGame_ = false, initialized_ = false;
  uint32_t revision_ = 0;
};
} // namespace TurnHub
