#pragma once

// Atlas's authoritative game rules: turn order and timing, pause, life and
// Commander damage, elimination, win claims and per-turn statistics. Player
// numbers are 1-based table positions; 0 means "none". All mutators validate
// and return false without changing anything when the request is invalid.

#include <Arduino.h>

#include "turnhub_types.h"
#include "game_profile.h"

namespace TurnHub {

// A requested change to another player's life is applied automatically if
// the target neither accepts nor rejects it within this window.
constexpr uint32_t LIFE_APPROVAL_MS = 15000;
constexpr uint8_t COMMANDERS_PER_PLAYER = 2;

// Automatic: accepted by timeout. Failed: accepted but could not be applied
// (limits, or the requester was eliminated). Canceled: a table decision or
// the end of the game superseded it.
enum class LifeChangeState : uint8_t { None, Pending, Accepted, Rejected, Automatic, Cancelled, Failed };

// The latest request aimed at one target player; one may be pending at a time.
struct LifeChangeRequest {
  uint32_t id = 0;
  uint32_t requestedAtMs = 0;
  uint8_t actor = 0;
  uint8_t target = 0;
  int32_t delta = 0;
  LifeChangeState state = LifeChangeState::None;
};

// Derived from the turn anchor and the captured turnTimerMs; never stored.
// Normal: nothing to show. Warning: TURN_TIMER_WARNING_MS or less remaining.
// Expired: the countdown reached zero; the turn continues (no automatic pass).
// LongTurn: timer OFF and the turn has reached TURN_TIMER_LONG_TURN_MS.
enum class TurnTimerPhase : uint8_t {
  Normal,
  Warning,
  Expired,
  LongTurn,
};

inline const char *turnTimerPhaseName(TurnTimerPhase phase) {
  switch (phase) {
    case TurnTimerPhase::Warning: return "WARNING";
    case TurnTimerPhase::Expired: return "EXPIRED";
    case TurnTimerPhase::LongTurn: return "LONG_TURN";
    default: return "NORMAL";
  }
}

struct GameCheckpoint;

class GameEngine {
 public:
  using GameCompletedCallback = void (*)(const GameEngine &game);

  GameEngine();

  // Called exactly once per finished match (statistics commit). Never called
  // by restoreCheckpoint().
  static void setGameCompletedCallback(GameCompletedCallback callback);

  // Starts a running match. Needs 2..MAX_PLAYERS seats and a starter among them.
  bool start(
      const PlayerSeat *players,
      uint8_t playerCount,
      const PlayerSeat &starter,
      uint32_t nowMs,
      const GameSettings &settings = GameSettings{});

  const GameSettings &settings() const { return settings_; }

  // --- Life and Commander damage (running or paused, no win claim) ---
  int32_t lifeTotal(uint8_t playerNumber) const;
  // Applies delta to a living player; totals stay within +/-LIFE_LIMIT.
  bool changeLife(uint8_t playerNumber, int32_t delta);
  // Asks target to approve a change to their life (see LIFE_APPROVAL_MS).
  bool requestLifeChange(uint8_t actor, uint8_t target, int32_t delta, uint32_t nowMs);
  // The target answers its pending request. A late answer settles it as
  // Automatic and returns false.
  bool respondLifeChange(uint8_t recipient, uint32_t requestId, bool accept, uint32_t nowMs);
  void expireLifeChanges(uint32_t nowMs);
  // Cancels pending requests involving the player, or all when 0.
  void cancelLifeChanges(uint8_t involvedPlayer = 0);
  const LifeChangeRequest *lifeChangeFor(uint8_t target) const;
  // Damage recipient has taken from source's commander (1 or 2).
  int32_t commanderDamage(uint8_t recipient, uint8_t source, uint8_t commander) const;
  // Commander profile only. Adjusts damage and the recipient's life together.
  bool changeCommanderDamage(uint8_t recipient, uint8_t source, uint8_t commander, int32_t delta);

  // --- Turns ---
  // Ends the active turn if controllerId owns it; records its duration.
  bool passTurn(uint8_t controllerId, uint32_t nowMs);
  // Paused time is excluded from turn and game clocks.
  bool pause(uint32_t nowMs);
  bool resume(uint32_t nowMs);

  // --- Leaving and winning ---
  // Paused games only. Sets gameFinished when one living player remains.
  bool eliminatePlayer(
      uint8_t playerNumber,
      uint32_t nowMs,
      bool &gameFinished);

  // Pauses and asks every other living player, in controller order after the
  // claimant's, to confirm. A denial resumes play when restoreRunning is set.
  bool beginWinClaim(
      uint8_t playerNumber,
      bool restoreRunning,
      uint32_t nowMs);
  // Only nextWinConfirmationPlayerNumber() may answer. The last confirmation
  // finishes the game and sets gameFinished.
  bool confirmWinClaim(
      uint8_t playerNumber,
      uint32_t nowMs,
      bool &gameFinished);
  bool denyWinClaim(uint8_t playerNumber, uint32_t nowMs);
  bool cancelWinClaim(uint8_t claimantPlayer, uint32_t nowMs);
  // Ends a running or paused match with no winner (a draw). Cancels any win
  // claim or life request and fires the game-completed callback once.
  bool endInDraw(uint32_t nowMs);

  // Clears the match (settings return to defaults).
  void reset();
  // Snapshot for interrupted-match recovery (game_recovery.h).
  void checkpoint(GameCheckpoint &out, uint32_t nowMs) const;
  // Validates before changing state. Active matches always recover paused.
  bool restoreCheckpoint(const GameCheckpoint &saved, uint32_t nowMs);

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
  // A finished match without a winner. Only endInDraw() produces one.
  bool endedInDraw() const { return gameOver_ && winnerPlayer_ == 0; }
  uint8_t winClaimPlayerNumber() const;
  uint8_t nextWinConfirmationPlayerNumber() const;

  bool controllerInGame(uint8_t controllerId) const;
  bool isEliminated(uint8_t playerNumber) const;
  // Copies the controller's seats (A then B) into out; returns the count.
  uint8_t playersForController(uint8_t controllerId, PlayerSeat *out, uint8_t capacity) const;
  uint8_t livingPlayersForController(
      uint8_t controllerId,
      PlayerSeat *out,
      uint8_t capacity) const;

  uint32_t currentTurnElapsedMs(uint32_t nowMs) const;
  uint32_t gameElapsedMs(uint32_t nowMs) const;
  TurnTimerPhase turnTimerPhase(uint32_t nowMs) const;
  // Countdown time left in the current turn; 0 when OFF or expired.
  uint32_t turnRemainingMs(uint32_t nowMs) const;
  uint32_t turnTimerMs() const { return settings_.turnTimerMs; }
  const PlayerStats *statsForPlayer(uint8_t playerNumber) const;

 private:
  int indexForSeat(const PlayerSeat &seat) const;
  int indexForPlayerNumber(uint8_t playerNumber) const;
  int nextLivingIndex(uint8_t startIndex) const;
  bool canChangeLife(uint8_t player, int32_t delta) const;
  void settleLifeChange(LifeChangeRequest &request, LifeChangeState outcome);

  void clearWinClaim();
  void finishGame(uint8_t winnerPlayer, uint32_t nowMs);

  static GameCompletedCallback gameCompletedCallback_;

  PlayerSeat players_[MAX_PLAYERS];
  GameSettings settings_{};
  int32_t life_[MAX_PLAYERS] = {};
  int32_t commanderDamage_[MAX_PLAYERS][MAX_PLAYERS][COMMANDERS_PER_PLAYER] = {};
  LifeChangeRequest lifeChanges_[MAX_PLAYERS] = {};
  uint32_t nextLifeRequestId_ = 0; // Deliberately survives reset/rematch in this boot.
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

  uint8_t winnerPlayer_ = 0;

  bool winClaimActive_ = false;
  uint8_t winClaimPlayer_ = 0;
  uint8_t winRequired_[MAX_PLAYERS] = {};
  bool winConfirmed_[MAX_PLAYERS] = {};
  uint8_t winRequiredCount_ = 0;
  bool winRestoreRunning_ = false;
};

}  // namespace TurnHub
