#pragma once

// turnhub-core: the one set of game rules for TurnHub. The Android app's
// on-device game (StandaloneGame.kt) is the reference; this is its port, so
// Atlas, the app (through the NDK) and later iOS run the same rules. See
// core/README.md and plans/atlas-one-game-path.md.
//
// A Game is a value. Every rule takes the current game and returns the next
// one, or the same one (revision unchanged) when it doesn't apply. Time is
// passed in (wall clock ms) and so are random draws (Random), which keeps the
// rules deterministic and replayable from the shared scenarios in
// core/scenarios/.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "turnhub_json.h"

namespace turnhub {

enum class Profile : uint8_t {
  Generic, Mtg, MtgCommander, MtgStarCommander, MtgBagOfDoom, MtgEmperor, MtgCommanderEmperor, Yugioh, Pokemon,
  Lorcana, DeathMayDie,
};

enum class TableState : uint8_t { Lobby, Starting, Running, Paused, GameOver };
enum class DayNight : uint8_t { Neither, Day, Night };
enum class TimeoutAction : uint8_t { Overtime, Pass, Demerit, PassAndDemerit };
enum class TimerPhase : uint8_t { Normal, Warning, Expired, LongTurn };

struct ProfileRules {
  static bool commander(Profile p) {
    return p == Profile::MtgCommander || p == Profile::MtgStarCommander || p == Profile::MtgBagOfDoom ||
        p == Profile::MtgCommanderEmperor;
  }
  static bool magic(Profile p) { return p == Profile::Mtg || p == Profile::MtgEmperor || commander(p); }
  static bool emperor(Profile p) { return p == Profile::MtgEmperor || p == Profile::MtgCommanderEmperor; }
  static bool ownSeating(Profile p) { return p == Profile::MtgStarCommander || p == Profile::MtgBagOfDoom || emperor(p); }
  static bool cooperative(Profile p) { return p == Profile::DeathMayDie; }
  static bool countsLife(Profile p) { return p != Profile::Pokemon && p != Profile::Lorcana && p != Profile::DeathMayDie; }
};

// Random draws come from the caller: the app and Atlas use a real generator,
// the scenarios a fixed list. nextInt(n) returns 0..n-1.
class Random {
 public:
  virtual ~Random() = default;
  virtual int nextInt(int bound) = 0;
};

struct Counters {
  int poison = 0, energy = 0, experience = 0;
  std::array<int, 2> commanderCasts{{0, 0}};
  bool cityBlessing = false;
  int treasure = 0, rad = 0;
  bool operator==(const Counters &o) const;
};

struct Player {
  std::string name;
  std::string profileId;  // Empty: none.
  std::string localId;
  int life = 0;
  bool eliminated = false;
  bool conceded = false;
  std::optional<int> eliminatedBy;
  bool selfEliminated = false;
  bool eliminationPending = false;
  Counters counters;
  std::optional<int> outOrder;
  int turnsCompleted = 0;
  int64_t turnMs = 0, fastestTurnMs = 0, longestTurnMs = 0;
  // Commander damage received, by source player index: one total per commander.
  std::map<int, std::array<int, 2>> commanderDamage;
  int demerits = 0, extensionsEarned = 0, extensionsUsed = 0;
  std::string commander, commanderArt;
  bool cardOnly = false;
  bool operator==(const Player &o) const;
};

struct TimerRules {
  TimeoutAction timeout = TimeoutAction::Overtime;
  bool earnExtensions = false;
  int earnBeforePercent = 75;
  int maxEarnedPerGame = 2;
  bool operator==(const TimerRules &o) const;
};

struct DoomBag {
  uint32_t atRisk = 0, drawn = 0;
  int boundary = 0, warmup = 0;
  int64_t cycle = 0;
  bool pending = false;
  int pendingTurn = 0, selected = 0;
  bool sealed = false;
  int lastDraw = 0;
  int64_t lastCheck = 0;
  bool contains(int player) const { return player >= 1 && player <= 16 && (atRisk & (1u << (player - 1))) != 0; }
  bool operator==(const DoomBag &o) const;
};

struct TurnTimer {
  TimerPhase phase = TimerPhase::Normal;
  std::optional<int64_t> remainingMs;
};

class Game {
 public:
  static constexpr int MAX_PLAYERS = 10;
  static constexpr int MAX_NAME = 32;
  static constexpr int MAX_LIFE = 99999;
  static constexpr int64_t UNDO_START_MS = 3 * 60000;
  static constexpr int64_t WARNING_MS = 10000;
  static constexpr int64_t LONG_TURN_MS = 300000;

  std::string gameId;
  TableState state = TableState::Lobby;
  Profile profile = Profile::MtgCommander;
  int startingLife = 40;
  bool autoEliminateAtZeroLife = true;
  bool askEliminationSource = false;
  bool autoEliminateAtPoisonLimit = true;
  bool autoEliminateAtCommanderDamage = true;
  int poisonLimit = 10;
  int64_t turnTimerMs = 0;
  TimerRules timerRules;
  int64_t turnExtensionMs = 0;
  bool timeoutHandled = false;
  bool twoHeadedGiant = false;
  bool houseRules = false;
  std::vector<Player> players;
  std::optional<int> active, starter, winner;
  std::vector<int> winners;
  std::optional<int> monarch, initiative;
  DayNight dayNight = DayNight::Neither;
  bool turnOrderReversed = false;
  std::optional<int> winClaim;
  std::set<int> winConfirmed;
  int64_t startedAtMs = 0, elapsedMs = 0;
  std::optional<int64_t> runningSinceMs;
  int64_t turnStartedAtElapsedMs = 0;
  DoomBag doomBag;
  std::vector<int> radDue;
  int64_t revision = 0;

  bool operator==(const Game &o) const;
  bool operator!=(const Game &o) const { return !(*this == o); }

  // --- derived -------------------------------------------------------------
  int64_t gameElapsedMs(int64_t nowMs) const;
  std::vector<int> mates(int index) const;
  bool canStart() const;
  TurnTimer turnTimer(int64_t nowMs) const;
  std::optional<int> openWinClaim() const;
  std::optional<int> winConfirmer() const;
  static int defaultLife(Profile p, bool twoHeadedGiant);

  // --- the lobby -----------------------------------------------------------
  Game addPlayer(const std::string &name, const std::string &profileId = "", const std::string &localId = "") const;
  Game removePlayer(int index) const;
  Game clearPlayers() const;
  Game movePlayer(int index, int by) const { return swapPlayers(index, index + by); }
  Game swapPlayers(int a, int b) const;
  Game chooseStarter(std::optional<int> index) const;
  Game setFormat(Profile p, std::optional<int> life = std::nullopt) const;
  Game setHouseRules(bool on) const;
  Game setTwoHeadedGiant(bool on) const;
  Game setStartingLife(int life) const;
  Game setTurnTimer(int64_t ms) const;
  Game setTimerRules(const TimerRules &rules) const;
  Game start(int64_t nowMs, const std::string &id, Random &random) const;
  Game reset() const;
  Game backToSetup(int64_t nowMs) const;

  // --- the table -----------------------------------------------------------
  Game applyTurnTimeout(int64_t nowMs) const;
  Game extendTurn(int index, int64_t nowMs) const;
  Game pass(int index, int64_t nowMs) const;
  Game togglePause(int64_t nowMs) const;
  Game changeLife(int index, int delta, int64_t nowMs) const;
  Game commanderDamage(int index, int source, int commander, int delta, int64_t nowMs) const;
  Game setDayNight(DayNight next) const;
  Game setTurnOrderReversed(bool on) const;
  Game claimMonarch(int index) const;
  Game changeCounter(int index, const std::string &kind, int value, int commander, int64_t nowMs) const;
  Game changeCounterForEveryone(const std::string &kind, int delta, int64_t nowMs) const;
  Game setEliminationRule(const std::string &field, int value, int64_t nowMs) const;
  // Applies the automatic elimination rules (life, poison, Commander damage)
  // now. Every rule that changes them already does; a host calls this after
  // changing a rule's field directly.
  Game resolveEliminationRules(int64_t nowMs) const;
  Game concede(int index, int64_t nowMs) const { return eliminate(index, nowMs, true); }
  Game eliminate(int index, int64_t nowMs, bool conceded = false, std::optional<int> by = std::nullopt, bool self = false) const;
  Game resolveElimination(int victim, std::optional<int> source, bool self, int64_t nowMs) const;
  Game requestWin(int index, int64_t nowMs) const;
  Game answerWinClaim(int index, bool accept, int64_t nowMs) const;
  Game claimWin(int index, int64_t nowMs) const;
  Game endWithoutWinner(int64_t nowMs) const;
  Game rematch(int64_t nowMs, const std::string &id, Random &random) const;
  Game seanModeRematch(int64_t nowMs, const std::string &id, Random &random) const;

  // --- Bag of Doom ---------------------------------------------------------
  Game doomEligibility(int player, bool inBag, int64_t cycle) const;
  Game doomCredit(int victim, int source, int64_t cycle) const;
  Game drawDoom(int64_t cycle, int64_t nowMs, Random &random) const;

 private:
  bool playing() const { return state == TableState::Running || state == TableState::Paused; }
  bool magic() const { return ProfileRules::magic(profile); }
  Game bump() const { Game g = *this; ++g.revision; return g; }
  Game officialValues() const;
  Game advanceTurn(int64_t nowMs, bool reward) const;
  Game resolveRad(int index, int nonlands, int64_t nowMs) const;
  Game passStatuses(const std::vector<int> &leaving) const;
  Game finish(const std::vector<int> &winners, int64_t nowMs) const;
  std::optional<std::vector<int>> decided() const;
  std::optional<int> nextIn(int index) const;
  std::optional<int> nextAfter(const std::vector<int> &team) const;
  std::optional<int> confirmerFor(int claimant) const;
  uint32_t doomLiving() const;
  Game doomLifeLoss(int target, int applied) const;
  Game doomPassed(int player, int64_t nowMs) const;
  Game doomLeft(int credited = 0) const;
};

// --- JSON: the state every client renders and the actions they send --------
Json toJson(const Game &game);
Game gameFromJson(const Json &json);
// The finished game as a match record (protocol/game-record.schema.json): the
// one format the app and Atlas keep, import and show. Null until the game is over.
Json gameRecord(const Game &game);
// Match records as CSV, one row per player per game (protocol/http-v1.md,
// "Match records"): the header line, then recordCsvRows for each record. The app
// and Atlas produce exactly the same text.
extern const char *const RECORD_CSV_HEADER;
std::string recordCsvRows(const Json &record);
// Applies one action ({"do": "changeLife", "index": 0, "delta": -1, "nowMs": ...});
// the scenarios and the transports use this one door.
Game applyAction(const Game &game, const Json &action, Random &random);

// A player's own action from their seat: a Sigil, a guest phone or the Atlas
// screen. The same door as the app's Play together guests
// (StandaloneTable.applyLanAction): the transport authenticates the seat,
// [match] and [revision] must name the current game, and only that player's
// controls are reachable. player counts from 1.
struct SeatAction {
  std::string match;
  int64_t revision = 0;
  int player = 0;
  std::string kind;
  int value = 0, slot = 0, source = 0;
};
Game applySeatAction(const Game &game, const SeatAction &action, int64_t nowMs);

const char *profileName(Profile p);
bool profileFromName(const std::string &name, Profile &out);
const char *stateName(TableState s);
const char *phaseName(TimerPhase p);

}  // namespace turnhub
