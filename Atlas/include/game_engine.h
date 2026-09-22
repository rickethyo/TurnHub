#pragma once

#include <Arduino.h>

#include "turnhub_types.h"
#include "game_profile.h"

namespace TurnHub {

enum class WarningPhase : uint8_t {
  Normal,
  Caution,
  Warning,
  OffGreen,
};

class GameEngine {
 public:
  using GameCompletedCallback = void (*)(const GameEngine &game);

  GameEngine();

  static void setGameCompletedCallback(GameCompletedCallback callback);

  bool start(
      const PlayerSeat *players,
      uint8_t playerCount,
      const PlayerSeat &starter,
      uint32_t warningMs,
      uint32_t nowMs,
      const GameSettings &settings = GameSettings{});

  const GameSettings &settings() const { return settings_; }
  int32_t lifeTotal(uint8_t playerNumber) const;
  bool changeLife(uint8_t playerNumber, int32_t delta);

  bool passTurn(uint8_t controllerId, uint32_t nextWarningMs, uint32_t nowMs);
  bool pause(uint32_t nowMs);
  bool resume(uint32_t nowMs);

  bool eliminatePlayer(
      uint8_t playerNumber,
      uint32_t nextWarningMs,
      uint32_t nowMs,
      bool &gameFinished);

  bool beginWinClaim(
      uint8_t playerNumber,
      bool restoreRunning,
      uint32_t nowMs);
  bool confirmWinClaim(
      uint8_t playerNumber,
      uint32_t nowMs,
      bool &gameFinished);
  bool denyWinClaim(uint8_t playerNumber, uint32_t nowMs);
  bool cancelWinClaim(uint8_t claimantPlayer, uint32_t nowMs);

  void reset();

  bool running() const;
  bool paused() const;
  bool gameOver() const;
  bool hasPlayers() const;
  bool hasWinClaim() const;

  uint8_t playerCount() const;
  uint8_t livingPlayerCount() const;
  const PlayerSeat *playerAt(uint8_t index) const;
  const PlayerSeat *playerByNumber(uint8_t playerNumber) const;
  const PlayerSeat *activePlayer() const;
  uint8_t activePlayerNumber() const;
  uint8_t activeController() const;
  uint8_t starterPlayerNumber() const;
  uint8_t winnerPlayerNumber() const;
  uint8_t winClaimPlayerNumber() const;
  uint8_t nextWinConfirmationPlayerNumber() const;

  bool controllerInGame(uint8_t controllerId) const;
  bool isEliminated(uint8_t playerNumber) const;
  uint8_t playersForController(uint8_t controllerId, PlayerSeat *out, uint8_t capacity) const;
  uint8_t livingPlayersForController(
      uint8_t controllerId,
      PlayerSeat *out,
      uint8_t capacity) const;

  uint32_t currentTurnElapsedMs(uint32_t nowMs) const;
  uint32_t gameElapsedMs(uint32_t nowMs) const;
  WarningPhase warningPhase(uint32_t nowMs) const;

  uint32_t warningMs() const;
  const PlayerStats *statsForPlayer(uint8_t playerNumber) const;

 private:
  int indexForSeat(const PlayerSeat &seat) const;
  int indexForPlayerNumber(uint8_t playerNumber) const;
  int nextLivingIndex(uint8_t startIndex) const;

  void clearWinClaim();
  void finishGame(uint8_t winnerPlayer, uint32_t nowMs);

  static GameCompletedCallback gameCompletedCallback_;

  PlayerSeat players_[MAX_PLAYERS];
  GameSettings settings_{};
  int32_t life_[MAX_PLAYERS] = {};
  PlayerStats stats_[MAX_PLAYERS];
  bool eliminated_[MAX_PLAYERS] = {};
  uint8_t playerCount_ = 0;
  uint8_t activeIndex_ = 0;
  uint8_t starterPlayer_ = 0;

  bool running_ = false;
  bool paused_ = false;
  bool gameOver_ = false;

  uint32_t gameStartedAtMs_ = 0;
  uint32_t gameEndedAtMs_ = 0;
  uint32_t turnStartedAtMs_ = 0;
  uint32_t pauseStartedAtMs_ = 0;
  uint32_t totalPausedMs_ = 0;
  uint32_t currentWarningMs_ = 0;

  uint8_t winnerPlayer_ = 0;

  bool winClaimActive_ = false;
  uint8_t winClaimPlayer_ = 0;
  uint8_t winRequired_[MAX_PLAYERS] = {};
  bool winConfirmed_[MAX_PLAYERS] = {};
  uint8_t winRequiredCount_ = 0;
  bool winRestoreRunning_ = false;
};

}  // namespace TurnHub
