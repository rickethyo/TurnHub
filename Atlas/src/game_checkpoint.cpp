#include "game_checkpoint.h"
#include "game_engine.h"
#include "identity.h"
#include <cstring>

namespace TurnHub {
bool validCheckpoint(const GameCheckpoint &s) {
  if (!validGameSettings(s.settings) || s.count > MAX_PLAYERS) return false;
  if (!s.count) return !s.over && !s.winner;
  if (s.count < 2 || s.active >= s.count || !s.starter || s.starter > s.count ||
      s.winner > s.count || (!s.over && s.winner)) return false;
  uint8_t living = 0;
  for (uint8_t i = 0; i < s.count; ++i) {
    const auto &p = s.players[i];
    if (p.playerNumber != i + 1 || p.controllerId >= MAX_CONTROLLERS ||
        (p.slot != 1 && p.slot != 2) ||
        (p.controllerId >= MAX_PHYSICAL_SIGILS && p.slot != 1) ||
        p.participantId == UINT32_MAX || p.profileId[8] != '\0' ||
        s.life[i] < -LIFE_LIMIT || s.life[i] > LIFE_LIMIT) return false;
    TurnHubIdentity::ProfileId profile;
    if (p.profileId[0] && !TurnHubIdentity::ProfileId::parse(p.profileId, profile)) return false;
    // Browser participants must be able to reauthenticate after a reboot.
    if (p.controllerId >= MAX_PHYSICAL_SIGILS && !p.profileId[0]) return false;
    if (p.slot == 2 && (i == 0 || s.players[i-1].controllerId != p.controllerId ||
        s.players[i-1].slot != 1)) return false;
    for (uint8_t j = 0; j < i; ++j) {
      if (p.sameSeat(s.players[j]) ||
          (p.slot == 1 && p.controllerId == s.players[j].controllerId) ||
          (p.participantId && p.participantId == s.players[j].participantId) ||
          (p.profileId[0] && !strcmp(p.profileId, s.players[j].profileId))) return false;
    }
    if (!s.eliminated[i]) ++living;
    for (uint8_t j = 0; j < s.count; ++j) for (uint8_t c = 0; c < 2; ++c)
      if (s.damage[i][j][c] < 0 || s.damage[i][j][c] > LIFE_LIMIT ||
          (s.settings.profile != GameProfile::Commander && s.damage[i][j][c])) return false;
  }
  // A finished match without a winner is a draw (GameEngine::endInDraw).
  return living && (s.over ? (!s.winner || !s.eliminated[s.winner-1]) :
      (living >= 2 && !s.eliminated[s.active]));
}

void GameEngine::checkpoint(GameCheckpoint &out, uint32_t nowMs) const {
  out = GameCheckpoint{};
  if (!hasPlayers()) return;
  out.settings = settings_;
  out.count = playerCount_; out.active = activeIndex_; out.starter = starterPlayer_;
  out.winner = winnerPlayer_; out.paused = paused_; out.over = gameOver_;
  out.gameElapsed = gameElapsedMs(nowMs); out.turnElapsed = currentTurnElapsedMs(nowMs);
  out.nextRequestId = nextLifeRequestId_;
  for (uint8_t i = 0; i < playerCount_; ++i) {
    out.players[i] = players_[i]; out.stats[i] = stats_[i];
    out.eliminated[i] = eliminated_[i]; out.life[i] = life_[i];
    for (uint8_t j = 0; j < playerCount_; ++j)
      for (uint8_t c = 0; c < 2; ++c) out.damage[i][j][c] = commanderDamage_[i][j][c];
  }
}

bool GameEngine::restoreCheckpoint(const GameCheckpoint &s, uint32_t nowMs) {
  if (!validCheckpoint(s)) return false;
  reset();
  if (!s.count) return true;
  settings_ = s.settings; playerCount_ = s.count; activeIndex_ = s.active;
  starterPlayer_ = s.starter; winnerPlayer_ = s.winner;
  running_ = true; gameOver_ = s.over; paused_ = !s.over;
  nextLifeRequestId_ = s.nextRequestId;
  // Rebase durations into the new boot's millis domain, excluding downtime.
  gameStartedAtMs_ = nowMs - s.gameElapsed;
  turnStartedAtMs_ = nowMs - s.turnElapsed;
  pauseStartedAtMs_ = paused_ ? nowMs : 0;
  gameEndedAtMs_ = s.over ? nowMs : 0;
  for (uint8_t i = 0; i < s.count; ++i) {
    players_[i] = s.players[i]; stats_[i] = s.stats[i];
    eliminated_[i] = s.eliminated[i]; life_[i] = s.life[i];
    for (uint8_t j = 0; j < s.count; ++j)
      for (uint8_t c = 0; c < 2; ++c) commanderDamage_[i][j][c] = s.damage[i][j][c];
  }
  // Pending requests, claims and confirmations are deliberately cancelled.
  // Restoration never invokes the game-completed statistics callback.
  return true;
}
} // namespace TurnHub
