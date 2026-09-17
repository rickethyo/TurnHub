#include "game_engine.h"

namespace TurnHub {

namespace {
constexpr uint32_t WARNING_OFF = 0;
constexpr uint32_t WARNING_OFF_GREEN_MS = 300000;
constexpr float WARNING_CAUTION_FRACTION = 0.75f;
}

GameEngine::GameEngine() {
  reset();
}

void GameEngine::reset() {
  playerCount_ = 0;
  activeIndex_ = 0;
  starterPlayer_ = 0;
  running_ = false;
  paused_ = false;
  gameStartedAtMs_ = 0;
  turnStartedAtMs_ = 0;
  pauseStartedAtMs_ = 0;
  totalPausedMs_ = 0;
  currentWarningMs_ = WARNING_OFF;

  for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
    players_[i] = PlayerSeat{};
    stats_[i] = PlayerStats{};
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
  gameStartedAtMs_ = nowMs;
  turnStartedAtMs_ = nowMs;
  currentWarningMs_ = warningMs;
  return true;
}

bool GameEngine::passTurn(
    uint8_t moduleId,
    uint32_t nextWarningMs,
    uint32_t nowMs) {
  if (!running_ || paused_ || playerCount_ < 2) {
    return false;
  }

  const PlayerSeat *active = activePlayer();
  if (active == nullptr || active->moduleId != moduleId) {
    return false;
  }

  const uint32_t elapsed = nowMs - turnStartedAtMs_;
  PlayerStats &stats = stats_[activeIndex_];
  ++stats.turnsCompleted;
  stats.totalTurnMs += elapsed;

  activeIndex_ = static_cast<uint8_t>((activeIndex_ + 1) % playerCount_);
  turnStartedAtMs_ = nowMs;
  currentWarningMs_ = nextWarningMs;
  return true;
}

bool GameEngine::pause(uint32_t nowMs) {
  if (!running_ || paused_) {
    return false;
  }

  paused_ = true;
  pauseStartedAtMs_ = nowMs;
  return true;
}

bool GameEngine::resume(uint32_t nowMs) {
  if (!running_ || !paused_) {
    return false;
  }

  const uint32_t pauseDuration = nowMs - pauseStartedAtMs_;
  totalPausedMs_ += pauseDuration;
  turnStartedAtMs_ += pauseDuration;
  pauseStartedAtMs_ = 0;
  paused_ = false;
  return true;
}

bool GameEngine::running() const {
  return running_;
}

bool GameEngine::paused() const {
  return paused_;
}

bool GameEngine::hasPlayers() const {
  return playerCount_ > 0;
}

uint8_t GameEngine::playerCount() const {
  return playerCount_;
}

const PlayerSeat *GameEngine::playerAt(uint8_t index) const {
  return index < playerCount_ ? &players_[index] : nullptr;
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

bool GameEngine::moduleInGame(uint8_t moduleId) const {
  for (uint8_t i = 0; i < playerCount_; ++i) {
    if (players_[i].moduleId == moduleId) {
      return true;
    }
  }
  return false;
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

uint32_t GameEngine::currentTurnElapsedMs(uint32_t nowMs) const {
  if (!running_) {
    return 0;
  }

  const uint32_t end = paused_ ? pauseStartedAtMs_ : nowMs;
  return end - turnStartedAtMs_;
}

uint32_t GameEngine::gameElapsedMs(uint32_t nowMs) const {
  if (!running_) {
    return 0;
  }

  uint32_t end = nowMs;
  uint32_t pausedTotal = totalPausedMs_;
  if (paused_) {
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
  for (uint8_t i = 0; i < playerCount_; ++i) {
    if (players_[i].playerNumber == playerNumber) {
      return &stats_[i];
    }
  }
  return nullptr;
}

}  // namespace TurnHub
