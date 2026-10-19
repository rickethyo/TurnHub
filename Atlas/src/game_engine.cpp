#include "game_engine.h"

#include <initializer_list>

namespace TurnHub {

namespace {
// Callers often sample millis() once per loop pass and then run handlers that
// stamp state with a fresh, slightly later millis() (e.g. the countdown
// starting a game, or a deferred pass committing). A clock read from before
// the stamp is not a 49-day-old turn: treat it as zero elapsed instead of
// letting the unsigned difference wrap. Genuine millis() rollover still works
// because real spans stay far below 2^31 ms (~24.8 days).
uint32_t elapsedBetween(uint32_t startMs, uint32_t endMs) {
  const uint32_t elapsed = endMs - startMs;
  return elapsed > 0x7FFFFFFFUL ? 0 : elapsed;
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
  for (auto &hit : lastHits_) hit = CommanderHit{};
  settings_ = GameSettings{};
  batchElimination_ = false;
  playerCount_ = 0;
  doom_ = DoomBag{};
  activeIndex_ = 0;
  starterPlayer_ = 0;
  monarchPlayer_ = initiativePlayer_ = 0;
  running_ = false;
  paused_ = false;
  gameOver_ = false;
  gameStartedAtMs_ = 0;
  gameEndedAtMs_ = 0;
  turnStartedAtMs_ = 0;
  pauseStartedAtMs_ = 0;
  totalPausedMs_ = 0;
  winnerPlayer_ = 0;
  winnerMask_ = 0;
  clearWinClaim();

  for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
    players_[i] = PlayerSeat{};
    stats_[i] = PlayerStats{};
    eliminated_[i] = false;
    conceded_[i] = false;
    selfEliminated_[i] = eliminationPending_[i] = false;
    eliminatedBy_[i] = 0;
    counters_[i] = PlayerCounters{};
    life_[i] = 0;
    lifeChanges_[i] = LifeChangeRequest{};
    partner_[i] = false;
    for (uint8_t source = 0; source < MAX_PLAYERS; ++source)
      for (uint8_t commander = 0; commander < COMMANDERS_PER_PLAYER; ++commander)
        commanderDamage_[i][source][commander] = 0;
  }
}

int32_t GameEngine::lifeTotal(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  return index < 0 ? 0 : life_[index];
}

bool GameEngine::canChangeLife(uint8_t playerNumber, int32_t delta) const {
  const int index = indexForPlayerNumber(playerNumber);
  if (index < 0 || gameOver_ || (!running_ && !paused_) || winClaimActive_ || eliminated_[index] || doom_.selected) return false;
  const int64_t total = static_cast<int64_t>(life_[index]) + delta;
  return delta != 0 && delta >= -LIFE_LIMIT && delta <= LIFE_LIMIT &&
      total >= -LIFE_LIMIT && total <= LIFE_LIMIT;
}

bool GameEngine::changeLife(uint8_t playerNumber, int32_t delta) {
  return changeLifeForTurn(playerNumber, delta, doomTurnPlayer());
}

bool GameEngine::changeLifeForTurn(uint8_t playerNumber, int32_t delta, uint8_t owner) {
  if (!canChangeLife(playerNumber, delta)) return false;
  const int index = indexForPlayerNumber(playerNumber);
  life_[index] += delta;
  doomLifeLoss(playerNumber, delta, owner);
  const int mate = teammateIndex(index);
  if (mate >= 0) life_[mate] = life_[index];  // One shared team total.
  return true;
}

int GameEngine::teammateIndex(int index) const {
  if (!settings_.twoHeadedGiant || index < 0 || index >= playerCount_) return -1;
  const int mate = index ^ 1;
  return mate < playerCount_ ? mate : -1;
}

uint8_t GameEngine::teamOf(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  return settings_.twoHeadedGiant && index >= 0 ? static_cast<uint8_t>(index / TEAM_SIZE + 1) : 0;
}

uint8_t GameEngine::teammateOf(uint8_t playerNumber) const {
  const int mate = teammateIndex(indexForPlayerNumber(playerNumber));
  return mate >= 0 ? players_[mate].playerNumber : 0;
}

bool GameEngine::sameTeam(uint8_t a, uint8_t b) const {
  return a != 0 && (a == b || teammateOf(a) == b);
}

bool GameEngine::hasTurn(uint8_t playerNumber) const {
  return playerCount_ > 0 && sameTeam(activePlayerNumber(), playerNumber);
}

bool GameEngine::isWinner(uint8_t playerNumber) const {
  if (!gameOver_ || winnerPlayer_ == 0 || playerNumber == 0) return false;
  // Star Commander can have several winners; Two-Headed Giant shares one with the teammate.
  if (playerNumber <= MAX_PLAYERS && (winnerMask_ & winnerBit(playerNumber))) return true;
  return sameTeam(winnerPlayer_, playerNumber);
}

uint8_t GameEngine::winnerPlayers(uint8_t *out, uint8_t capacity) const {
  uint8_t count = 0;
  for (uint8_t number = 1; number <= MAX_PLAYERS; ++number)
    if ((winnerMask_ & winnerBit(number)) && out && count < capacity) out[count++] = number;
  return count;
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

// The Star Commander victory policy, evaluated once a whole elimination batch
// has been applied: every living player whose two enemies are out wins
// (shared), whoever eliminated them. No living player and no winner is a draw.
bool GameEngine::evaluateStarVictory(uint32_t nowMs) {
  if (!starCommander() || gameOver_) return false;
  uint16_t winners = 0;
  uint8_t living = 0;
  for (uint8_t i = 0; i < playerCount_; ++i) {
    if (eliminated_[i]) continue;
    ++living;
    if (starVictoryPending(players_[i].playerNumber)) winners |= winnerBit(players_[i].playerNumber);
  }
  if (winners) {
    finishGameWithWinners(winners, nowMs);
    return true;
  }
  if (living == 0) {
    finishGame(0, nowMs);
    return true;
  }
  return false;
}

uint8_t GameEngine::livingSideCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < playerCount_; ++i) {
    const int mate = teammateIndex(i);
    // A team counts once: at its first living member.
    if (!eliminated_[i] && !(mate >= 0 && mate < i && !eliminated_[mate])) ++count;
  }
  return count;
}

const LifeChangeRequest *GameEngine::lifeChangeFor(uint8_t target) const {
  const int index = indexForPlayerNumber(target);
  return index < 0 ? nullptr : &lifeChanges_[index];
}

bool GameEngine::requestLifeChange(uint8_t actor, uint8_t target, int32_t delta, uint32_t nowMs,
    uint32_t windowMs) {
  const int from = indexForPlayerNumber(actor), to = indexForPlayerNumber(target);
  const int mate = teammateIndex(to);
  // A teammate's life is the player's own: they change it directly.
  if (from < 0 || to < 0 || sameTeam(actor, target) || eliminated_[from] ||
      !canChangeLife(target, delta) || lifeChanges_[to].state == LifeChangeState::Pending ||
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
  request.doomOwner = doomTurnPlayer();
  request.doomCycle = doom_.cycle;
  request.windowMs = windowMs;
  return true;
}

void GameEngine::settleLifeChange(LifeChangeRequest &request, LifeChangeState outcome) {
  if (outcome == LifeChangeState::Rejected) { request.state = outcome; return; }
  const int from = indexForPlayerNumber(request.actor);
  request.state = from >= 0 && !eliminated_[from] && changeLifeForTurn(request.target, request.delta, request.doomCycle == doom_.cycle ? request.doomOwner : 0)
      ? outcome : LifeChangeState::Failed;
}

bool GameEngine::respondLifeChange(uint8_t recipient, uint32_t requestId, bool accept, uint32_t nowMs) {
  const int index = indexForPlayerNumber(recipient);
  if (index < 0) return false;
  // Either teammate may answer a request aimed at their shared life.
  const int mate = teammateIndex(index);
  auto &request = mate >= 0 && lifeChanges_[mate].id == requestId && lifeChanges_[index].id != requestId
      ? lifeChanges_[mate] : lifeChanges_[index];
  if (!requestId || request.id != requestId || request.state != LifeChangeState::Pending) return false;
  if (nowMs - request.requestedAtMs >= request.windowMs) {
    settleLifeChange(request, LifeChangeState::Automatic);
    return false;
  }
  settleLifeChange(request, accept ? LifeChangeState::Accepted : LifeChangeState::Rejected);
  return request.state != LifeChangeState::Failed;
}

void GameEngine::expireLifeChanges(uint32_t nowMs) {
  for (uint8_t i = 0; i < playerCount_; ++i) {
    auto &request = lifeChanges_[i];
    if (request.state == LifeChangeState::Pending && nowMs - request.requestedAtMs >= request.windowMs)
      settleLifeChange(request, LifeChangeState::Automatic);
  }
}

void GameEngine::cancelLifeChanges(uint8_t involvedPlayer) {
  for (auto &request : lifeChanges_)
    if (request.state == LifeChangeState::Pending &&
        (!involvedPlayer || request.actor == involvedPlayer || request.target == involvedPlayer))
      request.state = LifeChangeState::Cancelled;
}

int32_t GameEngine::commanderDamage(uint8_t recipient, uint8_t source, uint8_t commander) const {
  const int to = indexForPlayerNumber(recipient), from = indexForPlayerNumber(source);
  if (to < 0 || from < 0 || commander < 1 || commander > COMMANDERS_PER_PLAYER) return 0;
  return commanderDamage_[to][from][commander - 1];
}

bool GameEngine::changeCommanderDamage(uint8_t recipient, uint8_t source, uint8_t commander, int32_t delta) {
  const int to = indexForPlayerNumber(recipient), from = indexForPlayerNumber(source);
  if (!commanderRules(settings_.profile) || to < 0 || from < 0 ||
      // Star Commander: an ally is never a source of Commander damage.
      starAllies(players_[to].playerNumber, players_[from].playerNumber) ||
      // Two-Headed Giant: a teammate is never a source of Commander damage.
      (twoHeadedGiant() && to != from && teammateIndex(to) == from) ||
      commander < 1 || commander > COMMANDERS_PER_PLAYER || delta == 0 ||
      delta < -LIFE_LIMIT || delta > LIFE_LIMIT) return false;
  const int64_t total = static_cast<int64_t>(commanderDamage_[to][from][commander - 1]) + delta;
  if (total < 0 || total > LIFE_LIMIT || !canChangeLife(recipient, -delta)) return false;
  // A correction to the same cell invalidates the previous undo receipt.
  if (lastHits_[to].source == source && lastHits_[to].commander == commander)
    lastHits_[to] = CommanderHit{};
  life_[to] -= delta;
  doomLifeLoss(recipient, -delta, doomTurnPlayer());
  const int mate = teammateIndex(to);
  if (mate >= 0) life_[mate] = life_[to];  // Per-player damage, team life.
  commanderDamage_[to][from][commander - 1] = static_cast<int32_t>(total);
  // Damage entered for a second commander (phone or app) means partners.
  if (commander == 2 && total > 0) partner_[from] = true;
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
      if (commanderDamage_[to][from][1]) return false;
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
      !changeCommanderDamage(recipient,source,commander,amount)) return false;
  auto &hit = lastHits_[indexForPlayerNumber(recipient)];
  hit.source = source; hit.commander = commander; hit.amount = amount;
  return true;
}
bool GameEngine::undoCommanderHit(uint8_t recipient) {
  const CommanderHit *last = lastCommanderHit(recipient);
  if (!last) return false;
  const CommanderHit hit = *last;
  if (!changeCommanderDamage(recipient,hit.source,hit.commander,-hit.amount)) return false;
  lastHits_[indexForPlayerNumber(recipient)] = CommanderHit{};
  return true;
}

int GameEngine::indexForSeat(const PlayerSeat &seat) const {
  for (uint8_t i = 0; i < playerCount_; ++i) {
    if (players_[i].sameSeat(seat)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int GameEngine::indexForPlayerNumber(uint8_t playerNumber) const {
  for (uint8_t i = 0; i < playerCount_; ++i) {
    if (players_[i].playerNumber == playerNumber) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int GameEngine::nextLivingIndex(uint8_t startIndex) const {
  if (playerCount_ == 0) {
    return -1;
  }

  const int mate = teammateIndex(startIndex);
  for (uint8_t offset = 1; offset <= playerCount_; ++offset) {
    const uint8_t index = static_cast<uint8_t>(
        (startIndex + offset) % playerCount_);
    // Two-Headed Giant: the turn passes to the next team, not the teammate.
    if (!eliminated_[index] && static_cast<int>(index) != mate) {
      return static_cast<int>(index);
    }
  }

  return -1;
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

  reset();
  settings_ = settings;
  settings_.askEliminationSource = settings.askEliminationSource || settings.profile == GameProfile::DoomCommander;
  playerCount_ = playerCount;

  for (uint8_t i = 0; i < playerCount_; ++i) {
    players_[i] = players[i];
    life_[i] = settings_.startingLife;
    eliminated_[i] = false;
    conceded_[i] = false;
    selfEliminated_[i] = eliminationPending_[i] = false;
    eliminatedBy_[i] = 0;
  }

  const int starterIndex = indexForSeat(starter);
  if (starterIndex < 0) {
    reset();
    return false;
  }

  activeIndex_ = static_cast<uint8_t>(starterIndex);
  starterPlayer_ = players_[activeIndex_].playerNumber;
  running_ = true;
  paused_ = false;
  gameOver_ = false;
  if (doomCommander()) {
    doom_.atRisk = doomLiving(); doom_.boundary = doomPrevious(starterPlayer_, doom_.atRisk, playerCount_);
    doom_.warmup = 4; doom_.cycle = 1;
  }
  gameStartedAtMs_ = nowMs;
  turnStartedAtMs_ = nowMs;
  return true;
}

bool GameEngine::passTurn(
    uint8_t controllerId,
    uint32_t nowMs) {
  if (!running_ || paused_ || gameOver_ || playerCount_ < 2 || winClaimActive_) {
    return false;
  }

  const PlayerSeat *active = activePlayer();
  if (active == nullptr || !controllerHasTurn(controllerId) || eliminated_[activeIndex_]) {
    return false;
  }

  const int next = nextLivingIndex(activeIndex_);
  if (next < 0) {
    return false;
  }

  const uint32_t elapsed = elapsedBetween(turnStartedAtMs_, nowMs);
  // A team turn counts for both teammates.
  const int mate = teammateIndex(activeIndex_);
  for (const int index : {static_cast<int>(activeIndex_), mate}) {
    if (index < 0) continue;
    PlayerStats &stats = stats_[index];
    ++stats.turnsCompleted;
    stats.totalTurnMs += elapsed;
    if (stats.fastestTurnMs == 0 || elapsed < stats.fastestTurnMs) {
      stats.fastestTurnMs = elapsed;
    }
    if (elapsed > stats.longestTurnMs) {
      stats.longestTurnMs = elapsed;
    }
  }

  const uint8_t passed = active->playerNumber;
  activeIndex_ = static_cast<uint8_t>(next);
  turnStartedAtMs_ = nowMs;
  doomPassed(passed, nowMs);
  return true;
}

bool GameEngine::pause(uint32_t nowMs) {
  if (!running_ || paused_ || gameOver_ || winClaimActive_) {
    return false;
  }

  paused_ = true;
  pauseStartedAtMs_ = nowMs;
  return true;
}

bool GameEngine::resume(uint32_t nowMs) {
  if (doom_.pending || hasPendingElimination()) return false;
  if (!running_ || !paused_ || gameOver_ || winClaimActive_ || doom_.selected) {
    return false;
  }

  const uint32_t pauseDuration = elapsedBetween(pauseStartedAtMs_, nowMs);
  totalPausedMs_ += pauseDuration;
  turnStartedAtMs_ += pauseDuration;
  pauseStartedAtMs_ = 0;
  paused_ = false;
  return true;
}

bool GameEngine::eliminatePlayer(
    uint8_t playerNumber,
    uint32_t nowMs,
    bool &gameFinished, bool conceded, uint8_t creditedPlayer, bool self, bool attributionKnown) {
  gameFinished = false;
  if (self && (conceded || creditedPlayer)) return false;

  if (!running_ || !paused_ || gameOver_ || winClaimActive_ || doom_.selected) {
    return false;
  }

  const int index = indexForPlayerNumber(playerNumber);
  if ((conceded && creditedPlayer) || !validEliminationCredit(playerNumber, creditedPlayer)) return false;
  if (index < 0 || eliminated_[index]) {
    return false;
  }

  if (!batchElimination_ && livingSideCount() <= 1) {
    return false;
  }

  const int mate = teammateIndex(index);
  const bool wasActive = static_cast<uint8_t>(index) == activeIndex_ ||
      (mate >= 0 && static_cast<uint8_t>(mate) == activeIndex_);
  const bool passMonarch = sameTeam(monarchPlayer_, playerNumber);
  const bool passInitiative = sameTeam(initiativePlayer_, playerNumber);
  eliminated_[index] = true;
  conceded_[index] = conceded;
  eliminatedBy_[index] = creditedPlayer;
  selfEliminated_[index] = self;
  eliminationPending_[index] = settings_.askEliminationSource && !attributionKnown && !conceded && !self && !creditedPlayer;
  cancelLifeChanges(playerNumber);
  // Two-Headed Giant: the team leaves together.
  if (mate >= 0 && !eliminated_[mate]) {
    eliminated_[mate] = true;
    conceded_[mate] = conceded;
    eliminatedBy_[mate] = creditedPlayer;
    selfEliminated_[mate] = self;
    eliminationPending_[mate] = eliminationPending_[index];
    cancelLifeChanges(players_[mate].playerNumber);
  }

  doomLeft(creditedPlayer);

  if (wasActive) {
    const int next = nextLivingIndex(activeIndex_);
    if (next >= 0) {
      activeIndex_ = static_cast<uint8_t>(next);
    }

    turnStartedAtMs_ = pauseStartedAtMs_ != 0 ? pauseStartedAtMs_ : nowMs;
  }

  // CR 724.4 / 725.4: the active player takes over a leaving holder's
  // monarch or initiative; when the active player is leaving, the next in
  // turn order (already active here). Nobody, when no one is left.
  const uint8_t heir = eliminated_[activeIndex_] ? 0 : players_[activeIndex_].playerNumber;
  if (passMonarch) monarchPlayer_ = heir;
  if (passInitiative) initiativePlayer_ = heir;

  if (!batchElimination_ && !hasPendingElimination() && starCommander()) {
    // Star Commander never uses last-player-standing: its own policy decides.
    gameFinished = evaluateStarVictory(nowMs);
  } else if (!batchElimination_ && !hasPendingElimination() && livingSideCount() == 1) {
    for (uint8_t i = 0; i < playerCount_; ++i) {
      if (!eliminated_[i]) {
        finishGame(players_[i].playerNumber, nowMs);
        gameFinished = true;
        break;
      }
    }
  }

  return true;
}

bool GameEngine::claimMonarch(uint8_t playerNumber) {
  const int index = indexForPlayerNumber(playerNumber);
  if (index < 0 || eliminated_[index] || gameOver_ || !running_ || winClaimActive_ ||
      !magicRules(settings_.profile)) return false;
  monarchPlayer_ = playerNumber;
  return true;
}

const PlayerCounters *GameEngine::countersForPlayer(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  return index >= 0 ? &counters_[index] : nullptr;
}

bool GameEngine::changePlayerCounter(uint8_t playerNumber, CounterKind kind, int32_t value, uint8_t commander) {
  const int index = indexForPlayerNumber(playerNumber);
  if (index < 0 || eliminated_[index] || !running_ || gameOver_ || winClaimActive_ || doom_.selected || !magicRules(settings_.profile)) return false;
  auto &c = counters_[index];
  if (kind == CounterKind::Initiative) {
    if (value != 0 && value != 1) return false;
    if (value) initiativePlayer_ = playerNumber;
    else if (initiativePlayer_ == playerNumber) initiativePlayer_ = 0;
    else return false;
    return true;
  }
  if (kind == CounterKind::CityBlessing) {
    if (value != 0 && value != 1) return false;
    c.cityBlessing = value == 1;
    return true;
  }
  int32_t *target = nullptr;
  switch (kind) {
    case CounterKind::Poison: target = &c.poison; break;
    case CounterKind::Energy: target = &c.energy; break;
    case CounterKind::Experience: target = &c.experience; break;
    case CounterKind::CommanderCast:
      if (!commanderRules(settings_.profile) || commander < 1 || commander > 2) return false;
      target = &c.commanderCasts[commander-1]; break;
    default: return false;
  }
  if (!value || value < -LIFE_LIMIT || value > LIFE_LIMIT) return false;
  const int64_t next = int64_t(*target) + value;
  if (next < 0 || next > LIFE_LIMIT) return false;
  *target = static_cast<int32_t>(next);
  if (kind == CounterKind::Poison) {
    const int mate = teammateIndex(index);
    if (mate >= 0) counters_[mate].poison = c.poison;
  }
  if (kind == CounterKind::CommanderCast && commander == 2 && *target) partner_[index] = true;
  return true;
}

bool GameEngine::setEliminationSettings(const GameSettings &settings) {
  if (!validGameSettings(settings) || settings.profile != settings_.profile || settings.startingLife != settings_.startingLife ||
      settings.turnTimerMs != settings_.turnTimerMs || settings.twoHeadedGiant != settings_.twoHeadedGiant ||
      settings.houseRules != settings_.houseRules || !followsOfficialRules(settings)) return false;
  settings_ = settings;
  return true;
}

bool GameEngine::hasConceded(uint8_t player) const {
  const int index = indexForPlayerNumber(player);
  return index >= 0 && conceded_[index];
}

uint16_t GameEngine::doomLiving() const {
  uint16_t mask = 0;
  for (uint8_t i = 0; i < playerCount_; ++i) if (!eliminated_[i]) mask |= doomBit(players_[i].playerNumber);
  return mask;
}
uint8_t GameEngine::doomTurnPlayer() const { return doom_.pending ? doom_.pendingTurn : activePlayerNumber(); }
void GameEngine::doomLifeLoss(uint8_t target, int32_t delta, uint8_t owner) {
  if (doomActive() && delta < 0 && owner != target && (doomLiving() & doomBit(owner)))
    doom_.atRisk &= ~doomBit(owner);
}
void GameEngine::doomPassed(uint8_t player, uint32_t nowMs) {
  if (!doomActive() || doom_.boundary != player) return;
  if (doom_.warmup) --doom_.warmup;
  if (!doom_.warmup) { doom_.pending = true; doom_.pendingTurn = player; pause(nowMs); }
}
void GameEngine::doomLeft(uint8_t credited) {
  if (!doomCommander()) return;
  const uint16_t living = doomLiving();
  if (!doomActive()) {
    doom_.atRisk = 0; doom_.boundary = doom_.warmup = doom_.pendingTurn = doom_.selected = 0; doom_.pending = false;
    return;
  }
  if (credited) {
    doom_.atRisk = living & ~doomBit(credited); doom_.boundary = doomPrevious(credited, living, playerCount_);
    doom_.warmup = doom_.pendingTurn = doom_.selected = 0; doom_.pending = false; ++doom_.cycle;
  } else {
    doom_.atRisk &= living;
    if (!(living & doomBit(doom_.boundary))) doom_.boundary = doomPrevious(doom_.boundary, living, playerCount_);
  }
}
bool GameEngine::hasPendingLifeChanges() const {
  for (uint8_t i = 0; i < playerCount_; ++i) if (lifeChanges_[i].state == LifeChangeState::Pending) return true;
  return false;
}
bool GameEngine::setDoomEligibility(uint8_t player, bool inBag, uint32_t cycle) {
  if (!running_ || gameOver_ || winClaimActive_ || !doomActive() || doom_.selected ||
      cycle != doom_.cycle || !(doomLiving() & doomBit(player))) return false;
  if (inBag) doom_.atRisk |= doomBit(player); else doom_.atRisk &= ~doomBit(player);
  return true;
}
bool GameEngine::creditDoomElimination(uint8_t victim, uint8_t source, uint32_t cycle) {
  const int i = indexForPlayerNumber(victim);
  if (!running_ || gameOver_ || winClaimActive_ || !doomCommander() || doom_.selected || cycle != doom_.cycle ||
      i < 0 || !eliminated_[i] || conceded_[i] || selfEliminated_[i] || eliminationPending_[i] || eliminatedBy_[i] || !source ||
      !validEliminationCredit(victim, source) || (doom_.drawn & doomBit(victim))) return false;
  eliminatedBy_[i] = source; doomLeft(source); return true;
}
bool GameEngine::selfEliminated(uint8_t player) const {
  const int i = indexForPlayerNumber(player); return i >= 0 && selfEliminated_[i];
}
bool GameEngine::eliminationPending(uint8_t player) const {
  const int i = indexForPlayerNumber(player); return i >= 0 && eliminationPending_[i];
}
bool GameEngine::hasPendingElimination() const {
  for (uint8_t i = 0; i < playerCount_; ++i) if (eliminationPending_[i]) return true;
  return false;
}
bool GameEngine::resolveElimination(uint8_t victim, uint8_t source, bool self, uint32_t nowMs) {
  const int i = indexForPlayerNumber(victim);
  if (!running_ || gameOver_ || !paused_ || winClaimActive_ || doom_.selected || i < 0 ||
      !eliminationPending_[i] || (self && source) || !validEliminationCredit(victim, source)) return false;
  eliminationPending_[i] = false; selfEliminated_[i] = self; eliminatedBy_[i] = source;
  const int mate = teammateIndex(i);
  if (mate >= 0) { eliminationPending_[mate] = false; selfEliminated_[mate] = self; eliminatedBy_[mate] = source; }
  if (source) doomLeft(source);
  if (hasPendingElimination()) return true;
  if (starCommander()) evaluateStarVictory(nowMs);
  else if (!livingSideCount()) finishGame(0, nowMs);
  else if (livingSideCount() == 1) {
    for (uint8_t j = 0; j < playerCount_; ++j) if (!eliminated_[j]) { finishGame(players_[j].playerNumber, nowMs); break; }
  }
  return true;
}

bool GameEngine::prepareDoomDraw(uint32_t cycle, uint8_t pick) {
  if (!paused_ || gameOver_ || winClaimActive_ || !doomActive() || !doom_.pending ||
      cycle != doom_.cycle || hasPendingLifeChanges() || hasPendingElimination()) return false;
  if (doom_.selected) return true;  // Failed/uncertain saves never choose again.
  if (!doom_.atRisk) return true;
  if (pick >= doomCount(doom_.atRisk)) return false;
  for (uint8_t p = 1; p <= playerCount_; ++p) if (doom_.atRisk & doomBit(p)) {
    if (!pick--) { doom_.selected = p; return true; }
  }
  return false;
}
bool GameEngine::resolveDoomDraw(uint32_t nowMs) {
  if (!doomActive() || !paused_ || !doom_.pending || hasPendingLifeChanges() ||
      (doom_.atRisk && !doom_.selected)) return false;
  const uint8_t victim = doom_.selected;
  const uint32_t check = doom_.cycle;
  doom_.selected = 0;
  if (victim) { bool finished = false; if (!eliminatePlayer(victim, nowMs, finished)) return false; }
  doom_.drawn |= doomBit(victim);
  doom_.lastDraw = victim; doom_.lastCheck = check; doom_.cycle = check + 1;
  doom_.atRisk = doomActive() ? doomLiving() : 0;
  doom_.warmup = doom_.pendingTurn = 0; doom_.pending = false;
  return true;
}

bool GameEngine::validEliminationCredit(uint8_t victim, uint8_t source) const {
  return indexForPlayerNumber(victim) >= 0 &&
      (!source || (indexForPlayerNumber(source) >= 0 && !sameTeam(source, victim)));
}

uint8_t GameEngine::eliminatedBy(uint8_t player) const {
  const int i = indexForPlayerNumber(player);
  return i >= 0 ? eliminatedBy_[i] : 0;
}

uint8_t GameEngine::eliminationsBy(uint8_t player) const {
  if (indexForPlayerNumber(player) < 0) return 0;
  uint8_t count = 0;
  for (uint8_t i = 0; i < playerCount_; ++i)
    if (eliminated_[i] && !conceded_[i] && eliminatedBy_[i] == player) ++count;
  return count;
}

bool GameEngine::resolveEliminationRules(uint32_t nowMs) {
  if (!running_ || gameOver_ || winClaimActive_) return false;
  bool changed = false;
  const bool restoreRunning = !paused_;
  for (uint8_t i = 0; i < playerCount_; ++i) {
    if (eliminated_[i]) continue;
    bool lethal = settings_.autoEliminateAtZeroLife && life_[i] <= 0;
    if (magicRules(settings_.profile) && settings_.autoEliminateAtPoisonLimit && counters_[i].poison >= settings_.poisonLimit) lethal = true;
    if (commanderRules(settings_.profile) && settings_.autoEliminateAtCommanderDamage)
      for (uint8_t source = 0; source < playerCount_; ++source)
        for (uint8_t c = 0; c < 2; ++c)
          if (commanderDamage_[i][source][c] >= COMMANDER_DAMAGE_LIMIT) lethal = true;
    if (!lethal) continue;
    if (!changed && restoreRunning) pause(nowMs);
    batchElimination_ = true;
    bool finished = false;
    const bool removed = eliminatePlayer(players_[i].playerNumber, nowMs, finished);
    changed |= removed;
    if (removed && settings_.askEliminationSource) {
      eliminationPending_[i] = true;
      const int mate = teammateIndex(i);
      if (mate >= 0) eliminationPending_[mate] = true;
    }
  }
  batchElimination_ = false;
  if (!changed) return false;
  // Attribution must be saved before completion/statistics, including the last removal.
  if (hasPendingElimination()) return true;
  // The whole batch is applied; only now is the outcome decided.
  if (starCommander()) {
    if (!evaluateStarVictory(nowMs) && restoreRunning) resume(nowMs);
    return true;
  }
  const uint8_t sides = livingSideCount();
  if (sides == 0) finishGame(0, nowMs);
  else if (sides == 1) {
    for (uint8_t i = 0; i < playerCount_; ++i)
      if (!eliminated_[i]) { finishGame(players_[i].playerNumber, nowMs); break; }
  } else if (restoreRunning) resume(nowMs);
  return true;
}

void GameEngine::clearWinClaim() {
  winClaimActive_ = false;
  winClaimPlayer_ = 0;
  winRequiredCount_ = 0;
  winRestoreRunning_ = false;
  for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
    winRequired_[i] = 0;
    winConfirmed_[i] = false;
  }
}

bool GameEngine::endInDraw(uint32_t nowMs) {
  if (!running_ || gameOver_ || playerCount_ == 0) {
    return false;
  }
  finishGame(0, nowMs);
  return true;
}

void GameEngine::finishGame(uint8_t winnerPlayer, uint32_t nowMs) {
  finishGameWithWinners(winnerPlayer >= 1 && winnerPlayer <= MAX_PLAYERS ? winnerBit(winnerPlayer) : 0, nowMs);
}

void GameEngine::finishGameWithWinners(uint16_t winners, uint32_t nowMs) {
  if (gameOver_) {
    return;
  }

  winnerMask_ = winners;
  winnerPlayer_ = 0;
  for (uint8_t number = 1; number <= MAX_PLAYERS && !winnerPlayer_; ++number)
    if (winners & winnerBit(number)) winnerPlayer_ = number;
  gameEndedAtMs_ = nowMs;

  if (paused_ && pauseStartedAtMs_ != 0) {
    const uint32_t pauseDuration = elapsedBetween(pauseStartedAtMs_, nowMs);
    totalPausedMs_ += pauseDuration;
    turnStartedAtMs_ += pauseDuration;
  }

  pauseStartedAtMs_ = 0;
  paused_ = false;
  gameOver_ = true;
  doom_.pending = false; doom_.selected = doom_.pendingTurn = 0;
  for (uint8_t i = 0; i < playerCount_; ++i) eliminationPending_[i] = false;
  cancelLifeChanges();
  clearWinClaim();

  if (gameCompletedCallback_ != nullptr) {
    gameCompletedCallback_(*this);
  }
}

bool GameEngine::beginWinClaim(
    uint8_t playerNumber,
    bool restoreRunning,
    uint32_t nowMs) {
  if (doom_.pending || hasPendingElimination()) return false;
  if (!running_ || gameOver_ || winClaimActive_) {
    return false;
  }

  const int claimantIndex = indexForPlayerNumber(playerNumber);
  if (claimantIndex < 0 || eliminated_[claimantIndex]) {
    return false;
  }

  if (!paused_ && !pause(nowMs)) {
    return false;
  }

  clearWinClaim();
  winClaimActive_ = true;
  cancelLifeChanges();
  winClaimPlayer_ = playerNumber;
  winRestoreRunning_ = restoreRunning;

  const uint8_t claimantModule = players_[claimantIndex].controllerId;

  uint8_t moduleOrder[MAX_CONTROLLERS] = {};
  uint8_t moduleCount = 0;
  for (uint8_t i = 0; i < playerCount_; ++i) {
    const uint8_t controllerId = players_[i].controllerId;
    bool alreadyAdded = false;
    for (uint8_t m = 0; m < moduleCount; ++m) {
      if (moduleOrder[m] == controllerId) {
        alreadyAdded = true;
        break;
      }
    }
    if (!alreadyAdded && moduleCount < MAX_CONTROLLERS) {
      moduleOrder[moduleCount++] = controllerId;
    }
  }

  int claimantModuleIndex = -1;
  for (uint8_t i = 0; i < moduleCount; ++i) {
    if (moduleOrder[i] == claimantModule) {
      claimantModuleIndex = static_cast<int>(i);
      break;
    }
  }

  if (claimantModuleIndex < 0) {
    clearWinClaim();
    return false;
  }

  for (uint8_t moduleOffset = 1; moduleOffset <= moduleCount; ++moduleOffset) {
    const uint8_t moduleIndex = static_cast<uint8_t>(
        (claimantModuleIndex + moduleOffset) % moduleCount);
    const uint8_t controllerId = moduleOrder[moduleIndex];

    for (uint8_t i = 0; i < playerCount_; ++i) {
      if (players_[i].controllerId != controllerId || eliminated_[i]) {
        continue;
      }
      if (sameTeam(playerNumber, players_[i].playerNumber)) {
        continue;
      }
      if (winRequiredCount_ < MAX_PLAYERS) {
        winRequired_[winRequiredCount_++] = players_[i].playerNumber;
      }
    }
  }

  if (winRequiredCount_ == 0) {
    finishGame(playerNumber, nowMs);
  }

  return true;
}

bool GameEngine::confirmWinClaim(
    uint8_t playerNumber,
    uint32_t nowMs,
    bool &gameFinished) {
  gameFinished = false;

  if (!winClaimActive_ || gameOver_) {
    return false;
  }

  int requiredIndex = -1;
  for (uint8_t i = 0; i < winRequiredCount_; ++i) {
    if (winRequired_[i] == playerNumber) {
      requiredIndex = static_cast<int>(i);
      break;
    }
  }

  if (requiredIndex < 0 || winConfirmed_[requiredIndex] || isEliminated(playerNumber)) {
    return false;
  }

  winConfirmed_[requiredIndex] = true;

  for (uint8_t i = 0; i < winRequiredCount_; ++i) {
    if (!winConfirmed_[i]) {
      return true;
    }
  }

  const uint8_t winner = winClaimPlayer_;
  finishGame(winner, nowMs);
  gameFinished = true;
  return true;
}

bool GameEngine::denyWinClaim(uint8_t playerNumber, uint32_t nowMs) {
  if (!winClaimActive_ || gameOver_) {
    return false;
  }

  bool required = false;
  for (uint8_t i = 0; i < winRequiredCount_; ++i) {
    if (winRequired_[i] == playerNumber && !winConfirmed_[i]) {
      required = true;
      break;
    }
  }
  if (!required || isEliminated(playerNumber)) {
    return false;
  }

  const bool restoreRunning = winRestoreRunning_;
  clearWinClaim();

  if (restoreRunning) {
    return resume(nowMs);
  }

  return true;
}

bool GameEngine::cancelWinClaim(uint8_t claimantPlayer, uint32_t nowMs) {
  if (!winClaimActive_ || claimantPlayer != winClaimPlayer_ || gameOver_) {
    return false;
  }

  const bool restoreRunning = winRestoreRunning_;
  clearWinClaim();

  if (restoreRunning) {
    return resume(nowMs);
  }

  return true;
}

bool GameEngine::running() const {
  return running_ && !gameOver_;
}

bool GameEngine::paused() const {
  return paused_;
}

bool GameEngine::gameOver() const {
  return gameOver_;
}

bool GameEngine::hasPlayers() const {
  return playerCount_ > 0;
}

bool GameEngine::hasWinClaim() const {
  return winClaimActive_;
}

uint8_t GameEngine::playerCount() const {
  return playerCount_;
}

uint8_t GameEngine::livingPlayerCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < playerCount_; ++i) {
    if (!eliminated_[i]) {
      ++count;
    }
  }
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
  return playerCount_ > 0 ? &players_[activeIndex_] : nullptr;
}

uint8_t GameEngine::activePlayerNumber() const {
  const PlayerSeat *active = activePlayer();
  return active != nullptr ? active->playerNumber : 0;
}

uint8_t GameEngine::activeController() const {
  const PlayerSeat *active = activePlayer();
  return active != nullptr ? active->controllerId : INVALID_ID;
}

uint8_t GameEngine::starterPlayerNumber() const {
  return starterPlayer_;
}

uint8_t GameEngine::winnerPlayerNumber() const {
  return winnerPlayer_;
}

uint8_t GameEngine::winClaimPlayerNumber() const {
  return winClaimPlayer_;
}

uint8_t GameEngine::nextWinConfirmationPlayerNumber() const {
  if (!winClaimActive_) {
    return 0;
  }

  for (uint8_t i = 0; i < winRequiredCount_; ++i) {
    if (!winConfirmed_[i]) {
      return winRequired_[i];
    }
  }
  return 0;
}

bool GameEngine::controllerInGame(uint8_t controllerId) const {
  for (uint8_t i = 0; i < playerCount_; ++i) {
    if (players_[i].controllerId == controllerId) {
      return true;
    }
  }
  return false;
}

bool GameEngine::replaceController(uint8_t oldController, uint8_t newController) {
  if (oldController == newController || !controllerInGame(oldController) || controllerInGame(newController)) {
    return false;
  }
  for (uint8_t i = 0; i < playerCount_; ++i) {
    if (players_[i].controllerId == oldController) players_[i].controllerId = newController;
  }
  return true;
}

bool GameEngine::controllerHasTurn(uint8_t controllerId) const {
  return turnSeatForController(controllerId) != nullptr;
}

const PlayerSeat *GameEngine::turnSeatForController(uint8_t controllerId) const {
  if (playerCount_ == 0 || eliminated_[activeIndex_]) return nullptr;
  if (players_[activeIndex_].controllerId == controllerId) return &players_[activeIndex_];
  const int mate = teammateIndex(activeIndex_);
  return mate >= 0 && !eliminated_[mate] && players_[mate].controllerId == controllerId
      ? &players_[mate] : nullptr;
}

uint16_t GameEngine::currentRound() const {
  if (playerCount_ == 0) return 0;
  uint32_t most = 0;
  uint32_t active = 0;
  for (uint8_t i = 0; i < playerCount_; ++i) {
    if (eliminated_[i]) continue;
    const PlayerStats *stats = statsForPlayer(players_[i].playerNumber);
    const uint32_t done = stats != nullptr ? stats->turnsCompleted : 0;
    if (done > most) most = done;
    if (i == activeIndex_) active = done;
  }
  const uint32_t round = active == most ? most + 1 : most;
  return static_cast<uint16_t>(round > 9999 ? 9999 : round);
}

bool GameEngine::isEliminated(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  return index >= 0 ? eliminated_[index] : false;
}

uint8_t GameEngine::playersForController(
    uint8_t controllerId,
    PlayerSeat *out,
    uint8_t capacity) const {
  if (out == nullptr || capacity == 0) {
    return 0;
  }

  uint8_t count = 0;
  for (uint8_t i = 0; i < playerCount_ && count < capacity; ++i) {
    if (players_[i].controllerId == controllerId) {
      out[count++] = players_[i];
    }
  }
  return count;
}

uint8_t GameEngine::livingPlayersForController(
    uint8_t controllerId,
    PlayerSeat *out,
    uint8_t capacity) const {
  if (out == nullptr || capacity == 0) {
    return 0;
  }

  uint8_t count = 0;
  for (uint8_t i = 0; i < playerCount_ && count < capacity; ++i) {
    if (players_[i].controllerId == controllerId && !eliminated_[i]) {
      out[count++] = players_[i];
    }
  }
  return count;
}


uint32_t GameEngine::currentTurnElapsedMs(uint32_t nowMs) const {
  if (!hasPlayers()) {
    return 0;
  }

  uint32_t end = nowMs;
  if (gameOver_) {
    end = gameEndedAtMs_;
  } else if (paused_) {
    end = pauseStartedAtMs_;
  }

  return elapsedBetween(turnStartedAtMs_, end);
}

uint32_t GameEngine::gameElapsedMs(uint32_t nowMs) const {
  if (!hasPlayers()) {
    return 0;
  }

  const uint32_t end = gameOver_ ? gameEndedAtMs_ : nowMs;
  uint32_t pausedTotal = totalPausedMs_;
  if (!gameOver_ && paused_) {
    pausedTotal += elapsedBetween(pauseStartedAtMs_, nowMs);
  }

  return elapsedBetween(gameStartedAtMs_ + pausedTotal, end);
}

TurnTimerPhase GameEngine::turnTimerPhase(uint32_t nowMs) const {
  if (!hasPlayers() || gameOver_) {
    return TurnTimerPhase::Normal;
  }
  const uint32_t elapsed = currentTurnElapsedMs(nowMs);
  const uint32_t limit = settings_.turnTimerMs;

  if (limit == TURN_TIMER_OFF) {
    return elapsed >= TURN_TIMER_LONG_TURN_MS
        ? TurnTimerPhase::LongTurn
        : TurnTimerPhase::Normal;
  }
  if (elapsed >= limit) {
    return TurnTimerPhase::Expired;
  }
  return limit - elapsed <= TURN_TIMER_WARNING_MS
      ? TurnTimerPhase::Warning
      : TurnTimerPhase::Normal;
}

uint32_t GameEngine::turnRemainingMs(uint32_t nowMs) const {
  const uint32_t limit = settings_.turnTimerMs;
  if (!hasPlayers() || limit == TURN_TIMER_OFF) {
    return 0;
  }
  const uint32_t elapsed = currentTurnElapsedMs(nowMs);
  return elapsed >= limit ? 0 : limit - elapsed;
}

const PlayerStats *GameEngine::statsForPlayer(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  return index >= 0 ? &stats_[index] : nullptr;
}

}  // namespace TurnHub
