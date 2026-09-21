#include "game_engine.h"

namespace TurnHub {

namespace {
constexpr uint32_t WARNING_OFF = 0;
constexpr uint32_t WARNING_OFF_GREEN_MS = 300000;
constexpr float WARNING_CAUTION_FRACTION = 0.75f;
}

GameEngine::GameCompletedCallback GameEngine::gameCompletedCallback_ = nullptr;

GameEngine::GameEngine() {
  reset();
}

void GameEngine::setGameCompletedCallback(GameCompletedCallback callback) {
  gameCompletedCallback_ = callback;
}

void GameEngine::reset() {
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
  currentWarningMs_ = WARNING_OFF;
  winnerPlayer_ = 0;
  clearWinClaim();

  for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
    players_[i] = PlayerSeat{};
    stats_[i] = PlayerStats{};
    eliminated_[i] = false;
  }
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
    uint32_t warningMs,
    uint32_t nowMs) {
  if (players == nullptr || playerCount < 2 || playerCount > MAX_PLAYERS) {
    return false;
  }

  reset();
  playerCount_ = playerCount;

  for (uint8_t i = 0; i < playerCount_; ++i) {
    players_[i] = players[i];
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
  currentWarningMs_ = warningMs;
  return true;
}

bool GameEngine::passTurn(
    uint8_t moduleId,
    uint32_t nextWarningMs,
    uint32_t nowMs) {
  if (!running_ || paused_ || gameOver_ || playerCount_ < 2 || winClaimActive_) {
    return false;
  }

  const PlayerSeat *active = activePlayer();
  if (active == nullptr || active->moduleId != moduleId || eliminated_[activeIndex_]) {
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
  currentWarningMs_ = nextWarningMs;
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
    uint32_t nextWarningMs,
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

  if (wasActive) {
    const int next = nextLivingIndex(activeIndex_);
    if (next >= 0) {
      activeIndex_ = static_cast<uint8_t>(next);
    }

    turnStartedAtMs_ = pauseStartedAtMs_ != 0 ? pauseStartedAtMs_ : nowMs;
    currentWarningMs_ = nextWarningMs;
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
  winClaimPlayer_ = playerNumber;
  winRestoreRunning_ = restoreRunning;

  const uint8_t claimantModule = players_[claimantIndex].moduleId;

  uint8_t moduleOrder[MAX_PHYSICAL_SIGILS] = {};
  uint8_t moduleCount = 0;
  for (uint8_t i = 0; i < playerCount_; ++i) {
    const uint8_t moduleId = players_[i].moduleId;
    bool alreadyAdded = false;
    for (uint8_t m = 0; m < moduleCount; ++m) {
      if (moduleOrder[m] == moduleId) {
        alreadyAdded = true;
        break;
      }
    }
    if (!alreadyAdded && moduleCount < MAX_PHYSICAL_SIGILS) {
      moduleOrder[moduleCount++] = moduleId;
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
    const uint8_t moduleId = moduleOrder[moduleIndex];

    for (uint8_t i = 0; i < playerCount_; ++i) {
      if (players_[i].moduleId != moduleId || eliminated_[i]) {
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

uint8_t GameEngine::activeModule() const {
  const PlayerSeat *active = activePlayer();
  return active != nullptr ? active->moduleId : INVALID_ID;
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

bool GameEngine::moduleInGame(uint8_t moduleId) const {
  for (uint8_t i = 0; i < playerCount_; ++i) {
    if (players_[i].moduleId == moduleId) {
      return true;
    }
  }
  return false;
}

bool GameEngine::isEliminated(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  return index >= 0 ? eliminated_[index] : false;
}

uint8_t GameEngine::playersForModule(
    uint8_t moduleId,
    PlayerSeat *out,
    uint8_t capacity) const {
  if (out == nullptr || capacity == 0) {
    return 0;
  }

  uint8_t count = 0;
  for (uint8_t i = 0; i < playerCount_ && count < capacity; ++i) {
    if (players_[i].moduleId == moduleId) {
      out[count++] = players_[i];
    }
  }
  return count;
}

uint8_t GameEngine::livingPlayersForModule(
    uint8_t moduleId,
    PlayerSeat *out,
    uint8_t capacity) const {
  if (out == nullptr || capacity == 0) {
    return 0;
  }

  uint8_t count = 0;
  for (uint8_t i = 0; i < playerCount_ && count < capacity; ++i) {
    if (players_[i].moduleId == moduleId && !eliminated_[i]) {
      out[count++] = players_[i];
    }
  }
  return count;
}

uint32_t GameEngine::currentTurnElapsedMs(uint32_t nowMs) const {
  if (!hasPlayers() || gameStartedAtMs_ == 0) {
    return 0;
  }

  uint32_t end = nowMs;
  if (gameOver_) {
    end = gameEndedAtMs_;
  } else if (paused_) {
    end = pauseStartedAtMs_;
  }

  return end - turnStartedAtMs_;
}

uint32_t GameEngine::gameElapsedMs(uint32_t nowMs) const {
  if (!hasPlayers() || gameStartedAtMs_ == 0) {
    return 0;
  }

  const uint32_t end = gameOver_ ? gameEndedAtMs_ : nowMs;
  uint32_t pausedTotal = totalPausedMs_;
  if (!gameOver_ && paused_) {
    pausedTotal += nowMs - pauseStartedAtMs_;
  }

  return end - gameStartedAtMs_ - pausedTotal;
}

WarningPhase GameEngine::warningPhase(uint32_t nowMs) const {
  const uint32_t elapsed = currentTurnElapsedMs(nowMs);

  if (currentWarningMs_ == WARNING_OFF) {
    return elapsed >= WARNING_OFF_GREEN_MS
        ? WarningPhase::OffGreen
        : WarningPhase::Normal;
  }

  if (elapsed >= currentWarningMs_) {
    return WarningPhase::Warning;
  }

  const uint32_t caution = static_cast<uint32_t>(
      static_cast<float>(currentWarningMs_) * WARNING_CAUTION_FRACTION);

  return elapsed >= caution
      ? WarningPhase::Caution
      : WarningPhase::Normal;
}

uint32_t GameEngine::warningMs() const {
  return currentWarningMs_;
}

const PlayerStats *GameEngine::statsForPlayer(uint8_t playerNumber) const {
  const int index = indexForPlayerNumber(playerNumber);
  return index >= 0 ? &stats_[index] : nullptr;
}

}  // namespace TurnHub
