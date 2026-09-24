#include "game_engine.h"

namespace TurnHub {

GameEngine::GameCompletedCallback GameEngine::gameCompletedCallback_ = nullptr;

GameEngine::GameEngine() {
  reset();
}

void GameEngine::setGameCompletedCallback(GameCompletedCallback callback) {
  gameCompletedCallback_ = callback;
}

void GameEngine::reset() {
  settings_ = GameSettings{};
  playerCount_ = 0;
  activeIndex_ = 0;
  starterPlayer_ = 0;
  running_ = false;
  paused_ = false;
  gameOver_ = false;
  gameStartedAtMs_ = 0;
  gameEndedAtMs_ = 0;
  turnStartedAtMs_ = 0;
  pauseStartedAtMs_ = 0;
  totalPausedMs_ = 0;
  winnerPlayer_ = 0;
  clearWinClaim();

  for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
    players_[i] = PlayerSeat{};
    stats_[i] = PlayerStats{};
    eliminated_[i] = false;
    life_[i] = 0;
    lifeChanges_[i] = LifeChangeRequest{};
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
  if (index < 0 || gameOver_ || (!running_ && !paused_) || winClaimActive_ || eliminated_[index]) return false;
  const int64_t total = static_cast<int64_t>(life_[index]) + delta;
  return delta != 0 && delta >= -LIFE_LIMIT && delta <= LIFE_LIMIT &&
      total >= -LIFE_LIMIT && total <= LIFE_LIMIT;
}

bool GameEngine::changeLife(uint8_t playerNumber, int32_t delta) {
  if (!canChangeLife(playerNumber, delta)) return false;
  life_[indexForPlayerNumber(playerNumber)] += delta;
  return true;
}

const LifeChangeRequest *GameEngine::lifeChangeFor(uint8_t target) const {
  const int index = indexForPlayerNumber(target);
  return index < 0 ? nullptr : &lifeChanges_[index];
}

bool GameEngine::requestLifeChange(uint8_t actor, uint8_t target, int32_t delta, uint32_t nowMs) {
  const int from = indexForPlayerNumber(actor), to = indexForPlayerNumber(target);
  if (from < 0 || to < 0 || actor == target || eliminated_[from] ||
      !canChangeLife(target, delta) || lifeChanges_[to].state == LifeChangeState::Pending ||
      nextLifeRequestId_ == UINT32_MAX) return false;
  auto &request = lifeChanges_[to];
  request = LifeChangeRequest{};
  request.id = ++nextLifeRequestId_;
  request.requestedAtMs = nowMs;
  request.actor = actor;
  request.target = target;
  request.delta = delta;
  request.state = LifeChangeState::Pending;
  return true;
}

void GameEngine::settleLifeChange(LifeChangeRequest &request, LifeChangeState outcome) {
  if (outcome == LifeChangeState::Rejected) { request.state = outcome; return; }
  const int from = indexForPlayerNumber(request.actor);
  request.state = from >= 0 && !eliminated_[from] && changeLife(request.target, request.delta)
      ? outcome : LifeChangeState::Failed;
}

bool GameEngine::respondLifeChange(uint8_t recipient, uint32_t requestId, bool accept, uint32_t nowMs) {
  const int index = indexForPlayerNumber(recipient);
  if (index < 0) return false;
  auto &request = lifeChanges_[index];
  if (!requestId || request.id != requestId || request.state != LifeChangeState::Pending) return false;
  if (nowMs - request.requestedAtMs >= LIFE_APPROVAL_MS) {
    settleLifeChange(request, LifeChangeState::Automatic);
    return false;
  }
  settleLifeChange(request, accept ? LifeChangeState::Accepted : LifeChangeState::Rejected);
  return request.state != LifeChangeState::Failed;
}

void GameEngine::expireLifeChanges(uint32_t nowMs) {
  for (uint8_t i = 0; i < playerCount_; ++i) {
    auto &request = lifeChanges_[i];
    if (request.state == LifeChangeState::Pending && nowMs - request.requestedAtMs >= LIFE_APPROVAL_MS)
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
  if (settings_.profile != GameProfile::Commander || to < 0 || from < 0 ||
      commander < 1 || commander > COMMANDERS_PER_PLAYER || delta == 0 ||
      delta < -LIFE_LIMIT || delta > LIFE_LIMIT) return false;
  const int64_t total = static_cast<int64_t>(commanderDamage_[to][from][commander - 1]) + delta;
  if (total < 0 || total > LIFE_LIMIT || !canChangeLife(recipient, -delta)) return false;
  life_[to] -= delta;
  commanderDamage_[to][from][commander - 1] = static_cast<int32_t>(total);
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

  for (uint8_t offset = 1; offset <= playerCount_; ++offset) {
    const uint8_t index = static_cast<uint8_t>(
        (startIndex + offset) % playerCount_);
    if (!eliminated_[index]) {
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
  if (players == nullptr || playerCount < 2 || playerCount > MAX_PLAYERS || !validGameSettings(settings)) {
    return false;
  }

  reset();
  settings_ = settings;
  playerCount_ = playerCount;

  for (uint8_t i = 0; i < playerCount_; ++i) {
    players_[i] = players[i];
    life_[i] = settings_.startingLife;
    eliminated_[i] = false;
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
  if (active == nullptr || active->controllerId != controllerId || eliminated_[activeIndex_]) {
    return false;
  }

  const uint32_t elapsed = nowMs - turnStartedAtMs_;
  PlayerStats &stats = stats_[activeIndex_];
  ++stats.turnsCompleted;
  stats.totalTurnMs += elapsed;
  if (stats.fastestTurnMs == 0 || elapsed < stats.fastestTurnMs) {
    stats.fastestTurnMs = elapsed;
  }
  if (elapsed > stats.longestTurnMs) {
    stats.longestTurnMs = elapsed;
  }

  const int next = nextLivingIndex(activeIndex_);
  if (next < 0) {
    return false;
  }

  activeIndex_ = static_cast<uint8_t>(next);
  turnStartedAtMs_ = nowMs;
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
  if (!running_ || !paused_ || gameOver_ || winClaimActive_) {
    return false;
  }

  const uint32_t pauseDuration = nowMs - pauseStartedAtMs_;
  totalPausedMs_ += pauseDuration;
  turnStartedAtMs_ += pauseDuration;
  pauseStartedAtMs_ = 0;
  paused_ = false;
  return true;
}

bool GameEngine::eliminatePlayer(
    uint8_t playerNumber,
    uint32_t nowMs,
    bool &gameFinished) {
  gameFinished = false;

  if (!running_ || !paused_ || gameOver_ || winClaimActive_) {
    return false;
  }

  const int index = indexForPlayerNumber(playerNumber);
  if (index < 0 || eliminated_[index]) {
    return false;
  }

  if (livingPlayerCount() <= 1) {
    return false;
  }

  const bool wasActive = static_cast<uint8_t>(index) == activeIndex_;
  eliminated_[index] = true;
  cancelLifeChanges(playerNumber);

  if (wasActive) {
    const int next = nextLivingIndex(activeIndex_);
    if (next >= 0) {
      activeIndex_ = static_cast<uint8_t>(next);
    }

    turnStartedAtMs_ = pauseStartedAtMs_ != 0 ? pauseStartedAtMs_ : nowMs;
  }

  if (livingPlayerCount() == 1) {
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

void GameEngine::finishGame(uint8_t winnerPlayer, uint32_t nowMs) {
  if (gameOver_) {
    return;
  }

  winnerPlayer_ = winnerPlayer;
  gameEndedAtMs_ = nowMs;

  if (paused_ && pauseStartedAtMs_ != 0) {
    const uint32_t pauseDuration = nowMs - pauseStartedAtMs_;
    totalPausedMs_ += pauseDuration;
    turnStartedAtMs_ += pauseDuration;
  }

  pauseStartedAtMs_ = 0;
  paused_ = false;
  gameOver_ = true;
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
      if (players_[i].playerNumber == playerNumber) {
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
