#pragma once

// Atlas's authoritative game rules: turn order and timing, pause, life and
// Commander damage, elimination, win claims and per-turn statistics. Player
// numbers are 1-based table positions; 0 means "none". All mutators validate
// and return false without changing anything when the request is invalid.

#include <Arduino.h>

#include "turnhub_types.h"
#include "game_profile.h"
#include "doom_bag.h"
#include "turnhub_core.h"

namespace TurnHub {

static_assert(MAX_PLAYERS <= 16, "the winner set is a 16-bit mask over player numbers");

// A requested change to another player's life is applied automatically if
// the target neither accepts nor rejects it within this window.
// The default life-change approval window. A recipient may choose a longer
// one (accessibility, LIFE_APPROVAL_OPTIONS_MS); each request keeps its own.
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
  uint8_t doomOwner = 0;
  uint32_t doomCycle = 0;
  uint32_t windowMs = LIFE_APPROVAL_MS;  // The recipient's approval window.
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

  // The seat's display name in the match record (profile or Sigil name).
  void nameSeat(uint8_t playerNumber, const char *name);
  const GameSettings &settings() const { return settings_; }
  uint8_t monarchPlayerNumber() const { return numberOf(core_.monarch); }
  bool claimMonarch(uint8_t playerNumber);
  uint8_t initiativePlayerNumber() const { return numberOf(core_.initiative); }
  const PlayerCounters *countersForPlayer(uint8_t playerNumber) const;
  // Also the table-wide kinds (RadResolve, DayNight, TurnOrder, Everyone):
  // playerNumber is the living player asking (and, for RadResolve, answering).
  bool changePlayerCounter(uint8_t playerNumber, CounterKind kind, int32_t value, uint8_t commander = 1);
  // Magic: the table's day or night, and whether turns go counterclockwise.
  // Both reset with each new match.
  DayNight dayNight() const { return static_cast<DayNight>(core_.dayNight); }
  bool turnOrderReversed() const { return core_.turnOrderReversed; }
  // Magic: the player's rad counters triggered this turn and they haven't
  // yet said how many nonlands they milled.
  bool radDue(uint8_t playerNumber) const;
  bool setEliminationSettings(const GameSettings &settings);
  void setAutoEliminateAtZeroLife(bool enabled) { settings_.autoEliminateAtZeroLife = core_.autoEliminateAtZeroLife = enabled; }
  bool resolveEliminationRules(uint32_t nowMs);
  // The seats (bit per index) the last resolveEliminationRules() reported out.
  uint16_t lastAutoEliminations() const { return lastAutoOut_; }
  bool hasConceded(uint8_t player) const;
  const DoomBag &doomBag() const { return doom_; }
  bool doomCommander() const { return settings_.profile == GameProfile::DoomCommander; }
  // The bag is in play while more than its last two survivors remain (the app's DoomRules).
  bool doomActive() const { return doomCommander() && livingPlayerCount() > 2; }
  bool setDoomEligibility(uint8_t player, bool inBag, uint32_t cycle);
  bool creditDoomElimination(uint8_t victim, uint8_t source, uint32_t cycle);
  bool prepareDoomDraw(uint32_t cycle, uint8_t pick);
  bool resolveDoomDraw(uint32_t nowMs);
  bool hasPendingLifeChanges() const;
  bool validEliminationCredit(uint8_t victim, uint8_t source) const;
  uint8_t eliminatedBy(uint8_t player) const;
  bool selfEliminated(uint8_t player) const;
  bool eliminationPending(uint8_t player) const;
  bool hasPendingElimination() const;
  bool resolveElimination(uint8_t victim, uint8_t source, bool self, uint32_t nowMs);
  uint8_t eliminationsBy(uint8_t player) const;

  // --- Two-Headed Giant teams (game_profile.h) ---
  // Teams are neighbours in turn order: players 1+2 are team 1, 3+4 team 2.
  // Teammates share one life total, one turn and one elimination; the turn
  // belongs to the team and either teammate may pass it.
  bool twoHeadedGiant() const { return settings_.twoHeadedGiant && playerCount_ > 0; }
  // 1-based team, or 0 outside Two-Headed Giant or for an unknown player.
  uint8_t teamOf(uint8_t playerNumber) const;
  // The other player on the team, or 0.
  uint8_t teammateOf(uint8_t playerNumber) const;
  // The same player, or teammates.
  bool sameTeam(uint8_t a, uint8_t b) const;
  // The active player, or (Two-Headed Giant) their teammate.
  bool hasTurn(uint8_t playerNumber) const;
  // The winner, (Two-Headed Giant) the winner's teammate, or (Star
  // Commander) any of the shared winners.
  bool isWinner(uint8_t playerNumber) const;
  // Every winner's player number in ascending order (Two-Headed Giant: the
  // recorded winner only, not the teammate); returns the count.
  uint8_t winnerPlayers(uint8_t *out, uint8_t capacity) const;
  // Bit (playerNumber - 1) per winner; 0 for a draw or an unfinished match.
  uint16_t winnerMask() const;

  // --- Star Commander (game_profile.h) ---
  // A five-player Star Commander match. Relationships come from the seat's
  // player number and stay fixed for the match, also after elimination.
  bool starCommander() const;
  // Distinct allies in a Star Commander match (false otherwise).
  bool starAllies(uint8_t a, uint8_t b) const;
  // A living Star Commander player whose two enemies are both eliminated.
  bool starVictoryPending(uint8_t playerNumber) const;
  // How many of the player's two enemies are out (0 outside Star Commander).
  uint8_t starEnemiesEliminated(uint8_t playerNumber) const;

  // --- Life and Commander damage (running or paused, no win claim) ---
  int32_t lifeTotal(uint8_t playerNumber) const;
  // Applies delta to a living player; totals stay within +/-LIFE_LIMIT.
  bool changeLife(uint8_t playerNumber, int32_t delta);
  // Asks target to approve a change to their life (see LIFE_APPROVAL_MS).
  bool requestLifeChange(uint8_t actor, uint8_t target, int32_t delta, uint32_t nowMs,
      uint32_t windowMs = LIFE_APPROVAL_MS);
  // The target answers its pending request. A late answer settles it as
  // Automatic and returns false.
  bool respondLifeChange(uint8_t recipient, uint32_t requestId, bool accept, uint32_t nowMs);
  void expireLifeChanges(uint32_t nowMs);
  // Cancels pending requests involving the player, or all when 0.
  void cancelLifeChanges(uint8_t involvedPlayer = 0);
  const LifeChangeRequest *lifeChangeFor(uint8_t target) const;
  // Damage recipient has taken from source's commander (1 or 2).
  int32_t commanderDamage(uint8_t recipient, uint8_t source, uint8_t commander) const;
  // Commander profiles only. Adjusts damage and the recipient's life together.
  // Star Commander refuses damage between allies, Two-Headed Giant between
  // teammates.
  bool changeCommanderDamage(uint8_t recipient, uint8_t source, uint8_t commander, int32_t delta);

  // --- Turns ---
  // Ends the active turn if controllerId owns it; records its duration.
  bool passTurn(uint8_t controllerId, uint32_t nowMs);
  // Paused time is excluded from turn and game clocks.
  bool pause(uint32_t nowMs);
  bool resume(uint32_t nowMs);

  // --- Leaving and winning ---
  // Paused games only. Sets gameFinished when one living player (or team)
  // remains, or (Star Commander) when evaluateStarVictory ends the match.
  // In Two-Headed Giant the teammate is eliminated too.
  bool eliminatePlayer(
      uint8_t playerNumber,
      uint32_t nowMs,
      bool &gameFinished, bool conceded = false, uint8_t eliminatedBy = 0, bool self = false, bool attributionKnown = true);

  // Pauses and asks every other living player (not the claimant's teammate),
  // in controller order after the claimant's, to confirm. A denial resumes play when restoreRunning is set.
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
  // A finished match without a winner: endInDraw(), or an elimination batch
  // that leaves nobody (and, in Star Commander, no winner). A shared Star
  // Commander victory is never a draw.
  bool endedInDraw() const { return gameOver() && core_.winners.empty(); }
  uint8_t winClaimPlayerNumber() const;
  uint8_t nextWinConfirmationPlayerNumber() const;

  bool controllerInGame(uint8_t controllerId) const;
  // Moves every seat of oldController to newController (a Sigil taking over a
  // phone or tablet player mid-game). False if old has no seat or new has one.
  bool replaceController(uint8_t oldController, uint8_t newController);
  // The controller of the active seat or (Two-Headed Giant) of a living teammate.
  bool controllerHasTurn(uint8_t controllerId) const;
  // That controller's living seat holding the turn: the active seat, else
  // (Two-Headed Giant) the teammate's; nullptr when it has no turn.
  const PlayerSeat *turnSeatForController(uint8_t controllerId) const;
  bool isEliminated(uint8_t playerNumber) const;
  // Copies the controller's seats in turn order into out; returns the count.
  uint8_t playersForController(uint8_t controllerId, PlayerSeat *out, uint8_t capacity) const;
  uint8_t livingPlayersForController(
      uint8_t controllerId,
      PlayerSeat *out,
      uint8_t capacity) const;

  uint32_t matchGeneration() const { return matchGeneration_; }
  struct CommanderHit { uint8_t source = 0, commander = 1; int32_t amount = 0; };
  bool recordCommanderHit(uint8_t recipient, uint8_t source, uint8_t commander, int32_t amount);
  bool undoCommanderHit(uint8_t recipient);
  const CommanderHit *lastCommanderHit(uint8_t recipient) const;
  // Partner commanders, per player for this match (off by default). A player
  // with partners deals damage from commander 1 or 2; without, from 1 only.
  // Any recorded commander-2 damage implies partners (also after recovery).
  bool hasPartner(uint8_t player) const;
  // Fails outside Commander, for an unknown player, or turning partners off
  // while commander-2 damage from that player is recorded.
  bool setPartner(uint8_t player, bool on);

  uint32_t currentTurnElapsedMs(uint32_t nowMs) const;
  uint32_t gameElapsedMs(uint32_t nowMs) const;
  TurnTimerPhase turnTimerPhase(uint32_t nowMs) const;
  // Countdown time left in the current turn; 0 when OFF or expired.
  uint32_t turnRemainingMs(uint32_t nowMs) const;
  uint32_t turnTimerMs() const { return settings_.turnTimerMs; }
  const PlayerStats *statsForPlayer(uint8_t playerNumber) const;
  // The table round (1-based; 0 with no players): the most turns any living
  // player has completed, plus one while the active player is on that count.
  // The Atlas header and every Sigil show this same number.
  uint16_t currentRound() const;

  // The match's own rules state, ported from the app (core/README.md). The
  // rest is what only Atlas has: the seats' controllers, its settings form,
  // life-change approvals, Commander hit receipts and partners.
  const turnhub::Game &core() const { return core_; }

 private:
  // Core player index for a player number (players are numbered in seat
  // order from 1), or -1.
  int indexForPlayerNumber(uint8_t playerNumber) const;
  int indexForSeat(const PlayerSeat &seat) const;
  uint8_t numberOf(const std::optional<int> &index) const { return index ? static_cast<uint8_t>(*index + 1) : 0; }
  bool playing() const;
  // A millis() reading on the core's 64-bit clock (anchored at the match's
  // start; readings up to ~24 days either side are exact across the wrap).
  int64_t at(uint32_t nowMs) const { return anchor64_ + static_cast<int32_t>(nowMs - anchor32_); }
  bool canChangeLife(uint8_t player, int32_t delta) const;
  void settleLifeChange(LifeChangeRequest &request, LifeChangeState outcome, uint32_t nowMs);
  // Takes the core's next game: refreshes the mirrors below and calls the
  // completion callback once if the match just finished. False when the core
  // refused (same revision).
  bool commit(const turnhub::Game &next);
  // commit() for a change the automatic elimination rules may follow.
  bool commitRules(const turnhub::Game &next);
  // Takes a game whose claim just ended, resuming play when the claim paused it.
  bool endWinClaim(const turnhub::Game &ended, uint32_t nowMs);
  void mirror();

  static GameCompletedCallback gameCompletedCallback_;
  turnhub::Game core_;
  PlayerSeat players_[MAX_PLAYERS];
  GameSettings settings_{};
  uint8_t playerCount_ = 0;
  uint32_t matchGeneration_ = 0;
  CommanderHit lastHits_[MAX_PLAYERS] = {};
  bool partner_[MAX_PLAYERS] = {};
  LifeChangeRequest lifeChanges_[MAX_PLAYERS] = {};
  uint32_t nextLifeRequestId_ = 0; // Deliberately survives reset/rematch in this boot.
  // Mirrors of the core for the accessors that return references.
  DoomBag doom_{};
  PlayerCounters counters_[MAX_PLAYERS]{};
  PlayerStats stats_[MAX_PLAYERS];
  // Players (bit per seat index) the automatic rules took out since the last
  // resolveEliminationRules(), and those it reported.
  uint16_t autoOut_ = 0, lastAutoOut_ = 0;
  bool winRestoreRunning_ = false;
  uint32_t anchor32_ = 0;
  int64_t anchor64_ = int64_t(1) << 32;
};

}  // namespace TurnHub
