#include "game_engine.h"

// Atlas's game runs on the shared rules core (core/README.md), the app's own
// rules. This class keeps the interface the rest of Atlas reads (seats by
// player number and controller) and the few things only Atlas has: its seats'
// controllers, life-change approvals between players, Commander hit receipts
// and partners. Every rule decision is the core's.

namespace TurnHub {

namespace {

// Core draws (a random first player, the Bag of Doom) from the hardware RNG.
class AtlasRandom : public turnhub::Random {
 public:
  int nextInt(int bound) override { return bound > 0 ? static_cast<int>(esp_random() % static_cast<uint32_t>(bound)) : 0; }
};

turnhub::Profile coreProfile(GameProfile profile) {
  switch (profile) {
    case GameProfile::Magic: return turnhub::Profile::Mtg;
    case GameProfile::Commander: return turnhub::Profile::MtgCommander;
    case GameProfile::Yugioh: return turnhub::Profile::Yugioh;
    case GameProfile::StarCommander: return turnhub::Profile::MtgStarCommander;
    case GameProfile::DoomCommander: return turnhub::Profile::MtgBagOfDoom;
    default: return turnhub::Profile::Generic;
  }
}

const char *counterName(CounterKind kind) {
  switch (kind) {
    case CounterKind::Poison: return "poison";
    case CounterKind::Energy: return "energy";
    case CounterKind::Experience: return "experience";
    case CounterKind::CommanderCast: return "commander-casts";
    case CounterKind::CityBlessing: return "city-blessing";
    case CounterKind::Initiative: return "initiative";
    case CounterKind::Treasure: return "treasure";
    case CounterKind::Rad: return "rad";
    case CounterKind::RadResolve: return "rad-resolve";
    default: return nullptr;
  }
}

uint32_t clampMs(int64_t ms) { return ms <= 0 ? 0 : ms >= 0xFFFFFFFFLL ? 0xFFFFFFFFUL : static_cast<uint32_t>(ms); }

bool contains(const std::vector<int> &list, int value) {
  for (int item : list) if (item == value) return true;
  return false;
}

}  // namespace

GameEngine::GameCompletedCallback GameEngine::gameCompletedCallback_ = nullptr;

GameEngine::GameEngine() {
  reset();
}

void GameEngine::setGameCompletedCallback(GameCompletedCallback callback) {
  gameCompletedCallback_ = callback;
}

void GameEngine::reset() {
  ++matchGeneration_;
  core_ = turnhub::Game();
  autoOut_ = lastAutoOut_ = 0;
  winRestoreRunning_ = false;
  settings_ = GameSettings{};
  playerCount_ = 0;
  for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
    players_[i] = PlayerSeat{};
    lastHits_[i] = CommanderHit{};
    partner_[i] = false;
    lifeChanges_[i] = LifeChangeRequest{};
  }
  mirror();
}

bool GameEngine::commitRules(const turnhub::Game &next) {
  uint16_t out = 0;
  for (uint8_t i = 0; i < playerCount_ && i < next.players.size(); ++i)
    if (next.players[i].eliminated && !core_.players[i].eliminated) out |= static_cast<uint16_t>(1u << i);
  if (!commit(next)) return false;
  autoOut_ |= out;
  return true;
}

bool GameEngine::commit(const turnhub::Game &next) {
  if (next.revision == core_.revision) return false;
  const bool finished = core_.state != turnhub::TableState::GameOver && next.state == turnhub::TableState::GameOver;
  core_ = next;
  mirror();
  if (finished) {
    cancelLifeChanges();
    if (gameCompletedCallback_ != nullptr) gameCompletedCallback_(*this);
  }
  return true;
}

void GameEngine::mirror() {
  const auto &b = core_.doomBag;
  doom_.atRisk = static_cast<uint16_t>(b.atRisk);
  doom_.drawn = static_cast<uint16_t>(b.drawn);
  doom_.boundary = static_cast<uint8_t>(b.boundary);
  doom_.warmup = static_cast<uint8_t>(b.warmup);
  doom_.pendingTurn = static_cast<uint8_t>(b.pendingTurn);
  doom_.selected = static_cast<uint8_t>(b.selected);
  doom_.lastDraw = static_cast<uint8_t>(b.lastDraw);
  doom_.cycle = static_cast<uint32_t>(b.cycle);
  doom_.lastCheck = static_cast<uint32_t>(b.lastCheck);
  doom_.pending = b.pending;
  for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
    counters_[i] = PlayerCounters{};
    stats_[i] = PlayerStats{};
    if (i >= core_.players.size()) continue;
    const turnhub::Player &p = core_.players[i];
    PlayerCounters &c = counters_[i];
    c.poison = p.counters.poison; c.energy = p.counters.energy; c.experience = p.counters.experience;
    c.commanderCasts[0] = p.counters.commanderCasts[0]; c.commanderCasts[1] = p.counters.commanderCasts[1];
    c.cityBlessing = p.counters.cityBlessing; c.treasure = p.counters.treasure; c.rad = p.counters.rad;
    stats_[i].turnsCompleted = static_cast<uint32_t>(p.turnsCompleted);
    stats_[i].totalTurnMs = clampMs(p.turnMs);
    stats_[i].fastestTurnMs = clampMs(p.fastestTurnMs);
    stats_[i].longestTurnMs = clampMs(p.longestTurnMs);
  }
}

bool GameEngine::start(
    const PlayerSeat *players,
    uint8_t playerCount,
    const PlayerSeat &starter,
    uint32_t nowMs, const GameSettings &settings) {
  if (players == nullptr || playerCount < 2 || playerCount > MAX_PLAYERS || !validGameSettings(settings) ||
      !validTeamTable(settings, playerCount) || !validStarTable(settings, playerCount) ||
      !followsOfficialRules(settings) || !validOfficialTable(settings, playerCount)) {
    return false;
  }
  int starterIndex = -1;
  for (uint8_t i = 0; i < playerCount; ++i)
    if (players[i].sameSeat(starter)) starterIndex = i;
  if (starterIndex < 0) return false;

  reset();
  // Atlas validated the table's settings form; the lobby carries them over as is.
  turnhub::Game lobby;
  lobby.profile = coreProfile(settings.profile);
  lobby.startingLife = settings.startingLife;
  lobby.twoHeadedGiant = settings.twoHeadedGiant;
  lobby.houseRules = settings.houseRules;
  lobby.turnTimerMs = settings.turnTimerMs;
  lobby.poisonLimit = settings.poisonLimit;
  lobby.autoEliminateAtZeroLife = settings.autoEliminateAtZeroLife;
  lobby.autoEliminateAtPoisonLimit = settings.autoEliminateAtPoisonLimit;
  lobby.autoEliminateAtCommanderDamage = settings.autoEliminateAtCommanderDamage;
  lobby.askEliminationSource = settings.askEliminationSource;
  for (uint8_t i = 0; i < playerCount; ++i) {
    turnhub::Player seat;
    seat.name = "Player " + std::to_string(i + 1);
    seat.profileId = players[i].profileId;
    lobby.players.push_back(seat);
  }
  lobby.starter = starterIndex;
  // A match ID unique across boots: one random nonce per boot, then a counter.
  static uint32_t bootNonce = 0;
  static uint32_t matches = 0;
  if (!bootNonce) bootNonce = esp_random() | 1u;
  char id[32];
  snprintf(id, sizeof(id), "atlas-%08lx-%lu", static_cast<unsigned long>(bootNonce), static_cast<unsigned long>(++matches));
  // The core counts 64-bit milliseconds from this match's start, so a turn
  // may cross millis()'s 32-bit wrap.
  anchor32_ = nowMs;
  anchor64_ = int64_t(1) << 32;
  AtlasRandom random;
  const turnhub::Game started = lobby.start(at(nowMs), id, random);
  if (started.state != turnhub::TableState::Running) return false;

  settings_ = settings;
  settings_.askEliminationSource = started.askEliminationSource;
  playerCount_ = playerCount;
  for (uint8_t i = 0; i < playerCount; ++i) players_[i] = players[i];
  core_ = started;
  mirror();
  return true;
}

void GameEngine::nameSeat(uint8_t playerNumber, const char *name) {
  const int index = indexForPlayerNumber(playerNumber);
  if (index >= 0 && name && name[0]) core_.players[index].name = name;
}

// --- reading the game -------------------------------------------------------

int GameEngine::indexForPlayerNumber(uint8_t playerNumber) const {
  return playerNumber >= 1 && playerNumber <= playerCount_ ? playerNumber - 1 : -1;
}

int GameEngine::indexForSeat(const PlayerSeat &seat) const {
  for (uint8_t i = 0; i < playerCount_; ++i)
    if (players_[i].sameSeat(seat)) return i;
  return -1;
}

bool GameEngine::playing() const {
  return playerCount_ > 0 &&
      (core_.state == turnhub::TableState::Running || core_.state == turnhub::TableState::Paused);
}

bool GameEngine::running() const { return playing(); }
bool GameEngine::paused() const { return playerCount_ > 0 && core_.state == turnhub::TableState::Paused; }
bool GameEngine::gameOver() const { return playerCount_ > 0 && core_.state == turnhub::TableState::GameOver; }
bool GameEngine::hasPlayers() const { return playerCount_ > 0; }
uint8_t GameEngine::playerCount() const { return playerCount_; }

uint8_t GameEngine::livingPlayerCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < playerCount_; ++i) if (!core_.players[i].eliminated) ++count;
  return count;
}

const PlayerSeat *GameEngine::playerAt(uint8_t index) const {
  return index < playerCount_ ? &players_[index] : nullptr;
}

const PlayerSeat *GameEngine::playerByNumber(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  return index >= 0 ? &players_[index] : nullptr;
}

const PlayerSeat *GameEngine::activePlayer() const {
  if (playerCount_ == 0) return nullptr;
  const int index = core_.active ? *core_.active : 0;
  return &players_[index < playerCount_ ? index : 0];
}

uint8_t GameEngine::activePlayerNumber() const {
  const PlayerSeat *active = activePlayer();
  return active != nullptr ? active->playerNumber : 0;
}

uint8_t GameEngine::activeController() const {
  const PlayerSeat *active = activePlayer();
  return active != nullptr ? active->controllerId : INVALID_ID;
}

uint8_t GameEngine::starterPlayerNumber() const { return playerCount_ ? numberOf(core_.starter) : 0; }
uint8_t GameEngine::winnerPlayerNumber() const { return gameOver() ? numberOf(core_.winner) : 0; }

uint16_t GameEngine::winnerMask() const {
  uint16_t mask = 0;
  if (!gameOver()) return 0;
  for (int index : core_.winners) mask |= static_cast<uint16_t>(1u << index);
  return mask;
}

uint8_t GameEngine::winnerPlayers(uint8_t *out, uint8_t capacity) const {
  uint8_t count = 0;
  const uint16_t mask = winnerMask();
  for (uint8_t number = 1; number <= MAX_PLAYERS; ++number)
    if ((mask & (1u << (number - 1))) && out && count < capacity) out[count++] = number;
  return count;
}

bool GameEngine::isWinner(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  if (!gameOver() || index < 0 || core_.winners.empty()) return false;
  // Two-Headed Giant names the team by its first player; the teammate shares it.
  return contains(core_.winners, index) || sameTeam(winnerPlayerNumber(), playerNumber);
}

int32_t GameEngine::lifeTotal(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  return index < 0 ? 0 : core_.players[index].life;
}

bool GameEngine::isEliminated(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  return index >= 0 && core_.players[index].eliminated;
}

bool GameEngine::hasConceded(uint8_t player) const {
  const int index = indexForPlayerNumber(player);
  return index >= 0 && core_.players[index].conceded;
}

uint8_t GameEngine::eliminatedBy(uint8_t player) const {
  const int index = indexForPlayerNumber(player);
  return index >= 0 ? numberOf(core_.players[index].eliminatedBy) : 0;
}

bool GameEngine::selfEliminated(uint8_t player) const {
  const int index = indexForPlayerNumber(player);
  return index >= 0 && core_.players[index].selfEliminated;
}

bool GameEngine::eliminationPending(uint8_t player) const {
  const int index = indexForPlayerNumber(player);
  return index >= 0 && core_.players[index].eliminationPending;
}

bool GameEngine::hasPendingElimination() const {
  for (uint8_t i = 0; i < playerCount_; ++i) if (core_.players[i].eliminationPending) return true;
  return false;
}

uint8_t GameEngine::eliminationsBy(uint8_t player) const {
  const int source = indexForPlayerNumber(player);
  if (source < 0) return 0;
  uint8_t count = 0;
  for (uint8_t i = 0; i < playerCount_; ++i) {
    const turnhub::Player &p = core_.players[i];
    if (p.eliminated && !p.conceded && p.eliminatedBy == source) ++count;
  }
  return count;
}

bool GameEngine::validEliminationCredit(uint8_t victim, uint8_t source) const {
  return indexForPlayerNumber(victim) >= 0 &&
      (!source || (indexForPlayerNumber(source) >= 0 && !sameTeam(source, victim)));
}

uint8_t GameEngine::teamOf(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  return settings_.twoHeadedGiant && index >= 0 ? static_cast<uint8_t>(index / TEAM_SIZE + 1) : 0;
}

uint8_t GameEngine::teammateOf(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  if (!settings_.twoHeadedGiant || index < 0) return 0;
  const int mate = index ^ 1;
  return mate < playerCount_ ? players_[mate].playerNumber : 0;
}

bool GameEngine::sameTeam(uint8_t a, uint8_t b) const {
  return a != 0 && (a == b || teammateOf(a) == b);
}

bool GameEngine::hasTurn(uint8_t playerNumber) const {
  return playerCount_ > 0 && sameTeam(activePlayerNumber(), playerNumber);
}

bool GameEngine::starCommander() const {
  return settings_.profile == GameProfile::StarCommander && playerCount_ == STAR_PLAYERS;
}

bool GameEngine::starAllies(uint8_t a, uint8_t b) const {
  return starCommander() && a != b && starAllied(a, b);
}

bool GameEngine::starVictoryPending(uint8_t playerNumber) const {
  uint8_t enemies[2] = {};
  if (!starCommander() || isEliminated(playerNumber) || !TurnHub::starEnemies(playerNumber, enemies)) return false;
  return isEliminated(enemies[0]) && isEliminated(enemies[1]);
}

uint8_t GameEngine::starEnemiesEliminated(uint8_t playerNumber) const {
  uint8_t enemies[2] = {};
  if (!starCommander() || !TurnHub::starEnemies(playerNumber, enemies)) return 0;
  return static_cast<uint8_t>((isEliminated(enemies[0]) ? 1 : 0) + (isEliminated(enemies[1]) ? 1 : 0));
}

const PlayerCounters *GameEngine::countersForPlayer(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  return index >= 0 ? &counters_[index] : nullptr;
}

const PlayerStats *GameEngine::statsForPlayer(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  return index >= 0 ? &stats_[index] : nullptr;
}

bool GameEngine::radDue(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  return index >= 0 && contains(core_.radDue, index);
}

int32_t GameEngine::commanderDamage(uint8_t recipient, uint8_t source, uint8_t commander) const {
  const int to = indexForPlayerNumber(recipient), from = indexForPlayerNumber(source);
  if (to < 0 || from < 0 || commander < 1 || commander > COMMANDERS_PER_PLAYER) return 0;
  const auto &damage = core_.players[to].commanderDamage;
  const auto found = damage.find(from);
  return found == damage.end() ? 0 : found->second[commander - 1];
}

bool GameEngine::hasWinClaim() const { return playerCount_ > 0 && core_.openWinClaim().has_value(); }
uint8_t GameEngine::winClaimPlayerNumber() const { return playerCount_ ? numberOf(core_.openWinClaim()) : 0; }
uint8_t GameEngine::nextWinConfirmationPlayerNumber() const { return playerCount_ ? numberOf(core_.winConfirmer()) : 0; }

bool GameEngine::controllerInGame(uint8_t controllerId) const {
  for (uint8_t i = 0; i < playerCount_; ++i)
    if (players_[i].controllerId == controllerId) return true;
  return false;
}

bool GameEngine::replaceController(uint8_t oldController, uint8_t newController) {
  if (oldController == newController || !controllerInGame(oldController) || controllerInGame(newController)) return false;
  for (uint8_t i = 0; i < playerCount_; ++i)
    if (players_[i].controllerId == oldController) players_[i].controllerId = newController;
  return true;
}

bool GameEngine::controllerHasTurn(uint8_t controllerId) const {
  return turnSeatForController(controllerId) != nullptr;
}

const PlayerSeat *GameEngine::turnSeatForController(uint8_t controllerId) const {
  if (playerCount_ == 0 || !core_.active) return nullptr;
  const int active = *core_.active;
  for (int index : core_.mates(active))
    if (!core_.players[index].eliminated && players_[index].controllerId == controllerId) return &players_[index];
  return nullptr;
}

uint8_t GameEngine::playersForController(uint8_t controllerId, PlayerSeat *out, uint8_t capacity) const {
  if (out == nullptr || capacity == 0) return 0;
  uint8_t count = 0;
  for (uint8_t i = 0; i < playerCount_ && count < capacity; ++i)
    if (players_[i].controllerId == controllerId) out[count++] = players_[i];
  return count;
}

uint8_t GameEngine::livingPlayersForController(uint8_t controllerId, PlayerSeat *out, uint8_t capacity) const {
  if (out == nullptr || capacity == 0) return 0;
  uint8_t count = 0;
  for (uint8_t i = 0; i < playerCount_ && count < capacity; ++i)
    if (players_[i].controllerId == controllerId && !core_.players[i].eliminated) out[count++] = players_[i];
  return count;
}

uint32_t GameEngine::gameElapsedMs(uint32_t nowMs) const {
  return hasPlayers() ? clampMs(core_.gameElapsedMs(at(nowMs))) : 0;
}

uint32_t GameEngine::currentTurnElapsedMs(uint32_t nowMs) const {
  return hasPlayers() ? clampMs(core_.gameElapsedMs(at(nowMs)) - core_.turnStartedAtElapsedMs) : 0;
}

TurnTimerPhase GameEngine::turnTimerPhase(uint32_t nowMs) const {
  if (!hasPlayers()) return TurnTimerPhase::Normal;
  return static_cast<TurnTimerPhase>(core_.turnTimer(at(nowMs)).phase);
}

uint32_t GameEngine::turnRemainingMs(uint32_t nowMs) const {
  if (!hasPlayers()) return 0;
  const auto remaining = core_.turnTimer(at(nowMs)).remainingMs;
  return remaining ? clampMs(*remaining) : 0;
}

uint16_t GameEngine::currentRound() const {
  if (playerCount_ == 0) return 0;
  uint32_t most = 0, active = 0;
  const int activeIndex = core_.active ? *core_.active : -1;
  for (uint8_t i = 0; i < playerCount_; ++i) {
    if (core_.players[i].eliminated) continue;
    const uint32_t done = stats_[i].turnsCompleted;
    if (done > most) most = done;
    if (i == activeIndex) active = done;
  }
  const uint32_t round = active == most ? most + 1 : most;
  return static_cast<uint16_t>(round > 9999 ? 9999 : round);
}

// --- the table's actions ------------------------------------------------------

bool GameEngine::passTurn(uint8_t controllerId, uint32_t nowMs) {
  const PlayerSeat *seat = turnSeatForController(controllerId);
  if (seat == nullptr) return false;
  return commit(core_.pass(indexForSeat(*seat), at(nowMs)));
}

bool GameEngine::pause(uint32_t nowMs) {
  return core_.state == turnhub::TableState::Running && commit(core_.togglePause(at(nowMs)));
}

bool GameEngine::resume(uint32_t nowMs) {
  return core_.state == turnhub::TableState::Paused && commit(core_.togglePause(at(nowMs)));
}

bool GameEngine::endInDraw(uint32_t nowMs) {
  return playing() && commit(core_.endWithoutWinner(at(nowMs)));
}

bool GameEngine::canChangeLife(uint8_t playerNumber, int32_t delta) const {
  const int index = indexForPlayerNumber(playerNumber);
  if (index < 0 || !playing() || hasWinClaim() || core_.players[index].eliminated || core_.doomBag.selected) return false;
  const int64_t total = static_cast<int64_t>(core_.players[index].life) + delta;
  return delta != 0 && delta >= -turnhub::Game::MAX_LIFE && delta <= turnhub::Game::MAX_LIFE &&
      total >= -turnhub::Game::MAX_LIFE && total <= turnhub::Game::MAX_LIFE;
}

bool GameEngine::changeLife(uint8_t playerNumber, int32_t delta) {
  const int index = indexForPlayerNumber(playerNumber);
  return index >= 0 && canChangeLife(playerNumber, delta) && commitRules(core_.changeLife(index, delta, at(millis())));
}

bool GameEngine::changeCommanderDamage(uint8_t recipient, uint8_t source, uint8_t commander, int32_t delta) {
  const int to = indexForPlayerNumber(recipient), from = indexForPlayerNumber(source);
  if (to < 0 || from < 0 || commander < 1 || commander > COMMANDERS_PER_PLAYER || hasWinClaim() || core_.doomBag.selected) return false;
  // Two-Headed Giant: a teammate is never a source of Commander damage.
  if (twoHeadedGiant() && recipient != source && sameTeam(recipient, source)) return false;
  // Commander damage costs the same life, which stays within its bounds.
  if (delta < -turnhub::Game::MAX_LIFE || delta > turnhub::Game::MAX_LIFE || !canChangeLife(recipient, -delta)) return false;
  if (!commitRules(core_.commanderDamage(to, from, commander, delta, at(millis())))) return false;
  // A correction to the same cell invalidates the previous undo receipt.
  if (lastHits_[to].source == source && lastHits_[to].commander == commander) lastHits_[to] = CommanderHit{};
  // Damage entered for a second commander (phone or app) means partners.
  if (commander == 2 && commanderDamage(recipient, source, 2) > 0) partner_[from] = true;
  return true;
}

bool GameEngine::hasPartner(uint8_t player) const {
  const int index = indexForPlayerNumber(player);
  return index >= 0 && partner_[index];
}

bool GameEngine::setPartner(uint8_t player, bool on) {
  const int from = indexForPlayerNumber(player);
  if (!commanderRules(settings_.profile) || from < 0) return false;
  if (!on) {
    if (counters_[from].commanderCasts[1]) return false;
    for (uint8_t to = 0; to < playerCount_; ++to)
      if (commanderDamage(players_[to].playerNumber, player, 2)) return false;
  }
  partner_[from] = on;
  return true;
}

const GameEngine::CommanderHit *GameEngine::lastCommanderHit(uint8_t recipient) const {
  const int to = indexForPlayerNumber(recipient);
  return to >= 0 && lastHits_[to].amount > 0 ? &lastHits_[to] : nullptr;
}

bool GameEngine::recordCommanderHit(uint8_t recipient, uint8_t source, uint8_t commander, int32_t amount) {
  if (amount < 1 || amount > 9999 || (commander == 2 && !hasPartner(source)) ||
      !changeCommanderDamage(recipient, source, commander, amount)) return false;
  auto &hit = lastHits_[indexForPlayerNumber(recipient)];
  hit.source = source; hit.commander = commander; hit.amount = amount;
  return true;
}

bool GameEngine::undoCommanderHit(uint8_t recipient) {
  const CommanderHit *last = lastCommanderHit(recipient);
  if (!last) return false;
  const CommanderHit hit = *last;
  if (!changeCommanderDamage(recipient, hit.source, hit.commander, -hit.amount)) return false;
  lastHits_[indexForPlayerNumber(recipient)] = CommanderHit{};
  return true;
}

bool GameEngine::claimMonarch(uint8_t playerNumber) {
  const int index = indexForPlayerNumber(playerNumber);
  // Nothing else changes while the table answers a win claim.
  if (index < 0 || hasWinClaim()) return false;
  // Claiming the crown you hold is a no-op that still succeeds, as before.
  if (core_.monarch == index) return playing() && !core_.players[index].eliminated;
  return commit(core_.claimMonarch(index));
}

bool GameEngine::changePlayerCounter(uint8_t playerNumber, CounterKind kind, int32_t value, uint8_t commander) {
  const int index = indexForPlayerNumber(playerNumber);
  if (index < 0 || !playing() || hasWinClaim() || core_.players[index].eliminated || core_.doomBag.selected) return false;
  const int64_t now = at(millis());
  switch (kind) {
    case CounterKind::DayNight:
      if (value < 0 || value >= static_cast<int32_t>(DayNight::Count)) return false;
      return commit(core_.setDayNight(static_cast<turnhub::DayNight>(value)));
    case CounterKind::TurnOrder:
      if (value != 0 && value != 1) return false;
      return commit(core_.setTurnOrderReversed(value == 1));
    case CounterKind::Everyone: {
      const auto counter = static_cast<CounterKind>(commander);
      if (commander >= static_cast<uint8_t>(CounterKind::Count) || !everyoneCounter(counter)) return false;
      return commitRules(core_.changeCounterForEveryone(counterName(counter), value, now));
    }
    default: {
      const char *name = counterName(kind);
      if (!name) return false;
      if (!commitRules(core_.changeCounter(index, name, value, commander, now))) return false;
      if (kind == CounterKind::CommanderCast && commander == 2 && counters_[index].commanderCasts[1]) partner_[index] = true;
      return true;
    }
  }
}

bool GameEngine::setEliminationSettings(const GameSettings &settings) {
  if (!validGameSettings(settings) || settings.profile != settings_.profile || settings.startingLife != settings_.startingLife ||
      settings.turnTimerMs != settings_.turnTimerMs || settings.twoHeadedGiant != settings_.twoHeadedGiant ||
      settings.houseRules != settings_.houseRules || !followsOfficialRules(settings)) return false;
  const bool ask = settings_.askEliminationSource;
  settings_ = settings;
  settings_.askEliminationSource = ask;  // Live rule toggles do not change the match's attribution policy.
  turnhub::Game next = core_;
  next.autoEliminateAtZeroLife = settings.autoEliminateAtZeroLife;
  next.autoEliminateAtPoisonLimit = settings.autoEliminateAtPoisonLimit;
  next.autoEliminateAtCommanderDamage = settings.autoEliminateAtCommanderDamage;
  next.poisonLimit = settings.poisonLimit;
  ++next.revision;
  commit(next);
  return true;
}

bool GameEngine::resolveEliminationRules(uint32_t nowMs) {
  // The core applies these rules inside each change; this reports (once) the
  // players they took out since the last call, and settles a changed rule.
  if (playing()) commitRules(core_.resolveEliminationRules(at(nowMs)));
  lastAutoOut_ = autoOut_;
  autoOut_ = 0;
  return lastAutoOut_ != 0;
}

bool GameEngine::eliminatePlayer(
    uint8_t playerNumber,
    uint32_t nowMs,
    bool &gameFinished, bool conceded, uint8_t creditedPlayer, bool self, bool attributionKnown) {
  gameFinished = false;
  const int index = indexForPlayerNumber(playerNumber);
  if (index < 0 || (creditedPlayer && indexForPlayerNumber(creditedPlayer) < 0) || hasWinClaim() || core_.doomBag.selected) return false;
  const std::optional<int> by = creditedPlayer ? std::optional<int>(creditedPlayer - 1) : std::nullopt;
  turnhub::Game next = core_.eliminate(index, at(nowMs), conceded, by, self);
  // A Sigil can't name who eliminated the player: in a match that asks, the
  // removal waits paused for the app's answer, as the automatic rules do.
  if (next.revision != core_.revision && core_.askEliminationSource && !attributionKnown && !conceded && !self &&
      !creditedPlayer && next.state != turnhub::TableState::GameOver) {
    for (int mate : next.mates(index)) next.players[mate].eliminationPending = true;
    if (next.state == turnhub::TableState::Running) {
      next.elapsedMs = next.gameElapsedMs(at(nowMs));
      next.state = turnhub::TableState::Paused;
      next.runningSinceMs.reset();
    }
  }
  if (!commit(next)) return false;
  cancelLifeChanges(playerNumber);
  if (const uint8_t mate = teammateOf(playerNumber)) cancelLifeChanges(mate);
  gameFinished = gameOver();
  return true;
}

bool GameEngine::resolveElimination(uint8_t victim, uint8_t source, bool self, uint32_t nowMs) {
  if (indexForPlayerNumber(victim) < 0 || (source && indexForPlayerNumber(source) < 0)) return false;
  const std::optional<int> credited = source ? std::optional<int>(source - 1) : std::nullopt;
  return commit(core_.resolveElimination(victim, credited, self, at(nowMs)));
}

bool GameEngine::beginWinClaim(uint8_t playerNumber, bool restoreRunning, uint32_t nowMs) {
  const int index = indexForPlayerNumber(playerNumber);
  if (index < 0) return false;
  // Atlas pauses the table while the others answer, and a claim made during
  // play resumes it when denied or withdrawn.
  turnhub::Game claimed = core_.state == turnhub::TableState::Running ? core_.togglePause(at(nowMs)) : core_;
  claimed = claimed.requestWin(index, at(nowMs));
  if (!claimed.openWinClaim() && claimed.state != turnhub::TableState::GameOver) return false;
  winRestoreRunning_ = restoreRunning;
  cancelLifeChanges();
  return commit(claimed);
}

bool GameEngine::confirmWinClaim(uint8_t playerNumber, uint32_t nowMs, bool &gameFinished) {
  gameFinished = false;
  const int index = indexForPlayerNumber(playerNumber);
  if (index < 0 || !commit(core_.answerWinClaim(index, true, at(nowMs)))) return false;
  gameFinished = gameOver();
  return true;
}

bool GameEngine::endWinClaim(const turnhub::Game &ended, uint32_t nowMs) {
  if (ended.revision == core_.revision) return false;
  const bool resume = winRestoreRunning_ && ended.state == turnhub::TableState::Paused;
  winRestoreRunning_ = false;
  return commit(resume ? ended.togglePause(at(nowMs)) : ended);
}

bool GameEngine::denyWinClaim(uint8_t playerNumber, uint32_t nowMs) {
  const int index = indexForPlayerNumber(playerNumber);
  return index >= 0 && endWinClaim(core_.answerWinClaim(index, false, at(nowMs)), nowMs);
}

bool GameEngine::cancelWinClaim(uint8_t claimantPlayer, uint32_t nowMs) {
  if (!hasWinClaim() || claimantPlayer != winClaimPlayerNumber()) return false;
  // The claimant withdraws: as a denial, the claim simply ends.
  turnhub::Game next = core_;
  next.winClaim.reset();
  next.winConfirmed.clear();
  ++next.revision;
  return endWinClaim(next, nowMs);
}

// --- Bag of Doom ---------------------------------------------------------------

bool GameEngine::setDoomEligibility(uint8_t player, bool inBag, uint32_t cycle) {
  return commit(core_.doomEligibility(player, inBag, cycle));
}

bool GameEngine::creditDoomElimination(uint8_t victim, uint8_t source, uint32_t cycle) {
  return commit(core_.doomCredit(victim, source, cycle));
}

bool GameEngine::prepareDoomDraw(uint32_t cycle, uint8_t pick) {
  const auto &bag = core_.doomBag;
  if (!paused() || !doomActive() || !bag.pending || bag.cycle != cycle || hasPendingLifeChanges() || hasPendingElimination()) return false;
  if (bag.selected) return true;  // Failed/uncertain saves never choose again.
  // The candidates the core draws from: living players still in the bag, in seat order.
  uint8_t remaining = pick;
  bool any = false;
  for (uint8_t i = 0; i < playerCount_; ++i) {
    if (core_.players[i].eliminated || !bag.contains(i + 1)) continue;
    any = true;
    if (remaining-- == 0) {
      turnhub::Game next = core_;
      next.doomBag.selected = i + 1;
      ++next.revision;
      return commit(next);
    }
  }
  return !any;
}

bool GameEngine::resolveDoomDraw(uint32_t nowMs) {
  const auto &bag = core_.doomBag;
  if (!doomActive() || !paused() || !bag.pending || hasPendingLifeChanges()) return false;
  bool candidates = false;
  for (uint8_t i = 0; i < playerCount_; ++i) candidates |= !core_.players[i].eliminated && bag.contains(i + 1);
  if (candidates && !bag.selected) return false;
  AtlasRandom random;
  return commit(core_.drawDoom(bag.cycle, at(nowMs), random));
}

// --- life-change approvals (Atlas only) -------------------------------------------

const LifeChangeRequest *GameEngine::lifeChangeFor(uint8_t target) const {
  const int index = indexForPlayerNumber(target);
  return index < 0 ? nullptr : &lifeChanges_[index];
}

bool GameEngine::hasPendingLifeChanges() const {
  for (uint8_t i = 0; i < playerCount_; ++i) if (lifeChanges_[i].state == LifeChangeState::Pending) return true;
  return false;
}

bool GameEngine::requestLifeChange(uint8_t actor, uint8_t target, int32_t delta, uint32_t nowMs, uint32_t windowMs) {
  const int from = indexForPlayerNumber(actor), to = indexForPlayerNumber(target);
  if (from < 0 || to < 0) return false;
  const int mate = settings_.twoHeadedGiant && (to ^ 1) < playerCount_ ? to ^ 1 : -1;
  // A teammate's life is the player's own: they change it directly.
  if (sameTeam(actor, target) || core_.players[from].eliminated || !canChangeLife(target, delta) ||
      lifeChanges_[to].state == LifeChangeState::Pending ||
      (mate >= 0 && lifeChanges_[mate].state == LifeChangeState::Pending) ||
      nextLifeRequestId_ == UINT32_MAX) return false;
  auto &request = lifeChanges_[to];
  request = LifeChangeRequest{};
  request.id = ++nextLifeRequestId_;
  request.requestedAtMs = nowMs;
  request.actor = actor;
  request.target = target;
  request.delta = delta;
  request.state = LifeChangeState::Pending;
  request.doomOwner = core_.doomBag.pending ? static_cast<uint8_t>(core_.doomBag.pendingTurn) : activePlayerNumber();
  request.doomCycle = static_cast<uint32_t>(core_.doomBag.cycle);
  request.windowMs = windowMs;
  return true;
}

void GameEngine::settleLifeChange(LifeChangeRequest &request, LifeChangeState outcome, uint32_t nowMs) {
  if (outcome == LifeChangeState::Rejected) { request.state = outcome; return; }
  const int from = indexForPlayerNumber(request.actor), to = indexForPlayerNumber(request.target);
  const bool applied = from >= 0 && to >= 0 && !core_.players[from].eliminated && canChangeLife(request.target, request.delta);
  // Mark it settled first: a change that ends the match cancels pending requests.
  request.state = applied ? outcome : LifeChangeState::Failed;
  if (!applied) return;
  turnhub::Game next = core_.changeLife(to, request.delta, at(nowMs));
  // Bag of Doom: a delayed approval's life loss belongs to the turn it was
  // asked on, not the turn it was answered on (when nobody left meanwhile).
  bool sameTable = next.state == core_.state;
  for (uint8_t i = 0; i < playerCount_ && sameTable; ++i) sameTable = next.players[i].eliminated == core_.players[i].eliminated;
  if (sameTable && doomActive() && request.delta < 0 && request.doomCycle == core_.doomBag.cycle) {
    uint32_t atRisk = core_.doomBag.atRisk;
    const uint8_t owner = request.doomOwner;
    if (owner && owner != request.target && !isEliminated(owner)) atRisk &= ~(1u << (owner - 1));
    next.doomBag.atRisk = atRisk;
  }
  if (!commitRules(next)) request.state = LifeChangeState::Failed;
}

bool GameEngine::respondLifeChange(uint8_t recipient, uint32_t requestId, bool accept, uint32_t nowMs) {
  const int index = indexForPlayerNumber(recipient);
  if (index < 0) return false;
  // Either teammate may answer a request aimed at their shared life.
  const int mate = settings_.twoHeadedGiant && (index ^ 1) < playerCount_ ? index ^ 1 : -1;
  auto &request = mate >= 0 && lifeChanges_[mate].id == requestId && lifeChanges_[index].id != requestId
      ? lifeChanges_[mate] : lifeChanges_[index];
  if (!requestId || request.id != requestId || request.state != LifeChangeState::Pending) return false;
  if (nowMs - request.requestedAtMs >= request.windowMs) {
    settleLifeChange(request, LifeChangeState::Automatic, nowMs);
    return false;
  }
  settleLifeChange(request, accept ? LifeChangeState::Accepted : LifeChangeState::Rejected, nowMs);
  return request.state != LifeChangeState::Failed;
}

void GameEngine::expireLifeChanges(uint32_t nowMs) {
  for (uint8_t i = 0; i < playerCount_; ++i) {
    auto &request = lifeChanges_[i];
    if (request.state == LifeChangeState::Pending && nowMs - request.requestedAtMs >= request.windowMs)
      settleLifeChange(request, LifeChangeState::Automatic, nowMs);
  }
}

void GameEngine::cancelLifeChanges(uint8_t involvedPlayer) {
  for (auto &request : lifeChanges_)
    if (request.state == LifeChangeState::Pending &&
        (!involvedPlayer || request.actor == involvedPlayer || request.target == involvedPlayer))
      request.state = LifeChangeState::Cancelled;
}

}  // namespace TurnHub
