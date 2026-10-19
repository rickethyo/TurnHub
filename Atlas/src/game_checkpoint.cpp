#include "game_checkpoint.h"
#include "game_engine.h"
#include "identity.h"
#include <cstring>

namespace TurnHub {
void clearCheckpoint(GameCheckpoint &out) {
  out.settings = GameSettings{};
  out.doom = DoomBag{};
  out.monarch = out.initiative = 0;
  out.count = out.active = out.starter = out.winner = 0;
  out.winners = 0;
  out.paused = out.over = false;
  out.gameElapsed = out.turnElapsed = out.nextRequestId = 0;
  for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
    out.players[i] = PlayerSeat{};
    out.stats[i] = PlayerStats{};
    out.eliminated[i] = false;
    out.conceded[i] = false;
    out.selfEliminated[i] = out.eliminationPending[i] = false;
    out.eliminatedBy[i] = 0;
    out.counters[i] = PlayerCounters{};
    out.life[i] = 0;
    for (auto &source : out.damage[i]) for (auto &value : source) value = 0;
  }
}

bool validCheckpoint(const GameCheckpoint &s) {
  if (!validGameSettings(s.settings) || s.count > MAX_PLAYERS) return false;
  if (s.initiative && (s.initiative > s.count || s.eliminated[s.initiative-1] || !magicRules(s.settings.profile))) return false;
  if (s.monarch && (s.monarch > s.count || s.eliminated[s.monarch-1] || !magicRules(s.settings.profile))) return false;
  if (!s.count) return !s.over && !s.winner && !s.winners && sameDoomBag(s.doom, DoomBag{});
  if (s.count < 2 || s.active >= s.count || !s.starter || s.starter > s.count ||
      s.winner > s.count || (!s.over && s.winner)) return false;
  if (!validTeamTable(s.settings, s.count) || !validStarTable(s.settings, s.count)) return false;
  // The winner set: the first winner is its lowest bit; only Star Commander
  // shares a victory, and every winner was still in the game.
  if (!s.winner != !s.winners || (s.count < 16 && (s.winners >> s.count))) return false;
  if (s.winner) {
    if ((s.winners & ((1u << (s.winner - 1)) - 1u)) || !(s.winners & (1u << (s.winner - 1)))) return false;
    if (s.settings.profile != GameProfile::StarCommander && s.winners != (1u << (s.winner - 1))) return false;
    for (uint8_t i = 0; i < s.count; ++i)
      if ((s.winners & (1u << i)) && s.eliminated[i]) return false;
  }
  // Two-Headed Giant teammates share one life total and leave together.
  if (s.settings.twoHeadedGiant)
    for (uint8_t i = 0; i + 1 < s.count; i += TEAM_SIZE)
      if (s.life[i] != s.life[i+1] || s.eliminated[i] != s.eliminated[i+1] || s.eliminatedBy[i] != s.eliminatedBy[i+1] || s.counters[i].poison != s.counters[i+1].poison) return false;
  uint8_t living = 0;
  bool pendingAttribution = false;
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
    // A shared Sigil stays adjacent, in either A/B or B/A order.
    if (p.slot == 2 && !((i > 0 && s.players[i-1].controllerId == p.controllerId &&
        s.players[i-1].slot == 1) || (i + 1 < s.count &&
        s.players[i+1].controllerId == p.controllerId && s.players[i+1].slot == 1))) return false;
    for (uint8_t j = 0; j < i; ++j) {
      if (p.sameSeat(s.players[j]) ||
          (p.participantId && p.participantId == s.players[j].participantId) ||
          (p.profileId[0] && !strcmp(p.profileId, s.players[j].profileId))) return false;
    }
    if (!validCounters(s.counters[i]) ||
        (!magicRules(s.settings.profile) && !sameCounters(s.counters[i], PlayerCounters{})) ||
        (!commanderRules(s.settings.profile) && (s.counters[i].commanderCasts[0] || s.counters[i].commanderCasts[1]))) return false;
    if (s.conceded[i] && !s.eliminated[i]) return false;
    if ((s.selfEliminated[i] || s.eliminationPending[i]) &&
        (!s.eliminated[i] || s.conceded[i] || s.eliminatedBy[i] || (s.doom.drawn & doomBit(i + 1)))) return false;
    if (s.eliminationPending[i] && (!s.settings.askEliminationSource || !s.paused || s.over || s.selfEliminated[i])) return false;
    if (s.settings.twoHeadedGiant && (s.selfEliminated[i] != s.selfEliminated[i ^ 1] || s.eliminationPending[i] != s.eliminationPending[i ^ 1])) return false;
    pendingAttribution |= s.eliminationPending[i];
    if ((s.doom.drawn & doomBit(i + 1)) && (s.conceded[i] || s.eliminatedBy[i])) return false;
    if (s.eliminatedBy[i] && (!s.eliminated[i] || s.conceded[i] ||
        s.eliminatedBy[i] > s.count || s.eliminatedBy[i] == i + 1 ||
        (s.settings.twoHeadedGiant && (s.eliminatedBy[i] - 1) / 2 == i / 2))) return false;
    if (!s.eliminated[i]) ++living;
    for (uint8_t j = 0; j < s.count; ++j) for (uint8_t c = 0; c < 2; ++c)
      if (s.damage[i][j][c] < 0 || s.damage[i][j][c] > LIFE_LIMIT ||
          (!commanderRules(s.settings.profile) && s.damage[i][j][c]) ||
          // Star Commander allies never deal each other Commander damage.
          (s.settings.profile == GameProfile::StarCommander && s.damage[i][j][c] && starAllied(i + 1, j + 1))) return false;
  }
  uint16_t livingMask = 0;
  for (uint8_t i = 0; i < s.count; ++i) if (!s.eliminated[i]) livingMask |= doomBit(i + 1);
  if (!validDoomBag(s.doom, s.settings.profile == GameProfile::DoomCommander, livingMask, s.count, s.paused)) return false;
  // A finished match without a winner is a draw (GameEngine::endInDraw).
  return (s.over ? (!s.winner || !s.eliminated[s.winner-1]) :
      ((living >= 2 || pendingAttribution) && (!living || !s.eliminated[s.active])));
}

void GameEngine::checkpoint(GameCheckpoint &out, uint32_t nowMs) const {
  clearCheckpoint(out);
  if (!hasPlayers()) return;
  out.settings = settings_;
  out.doom = doom_;
  out.count = playerCount_; out.active = activeIndex_; out.starter = starterPlayer_;
  out.monarch = monarchPlayer_;
  out.initiative = initiativePlayer_;
  out.winner = winnerPlayer_; out.winners = winnerMask_; out.paused = paused_; out.over = gameOver_;
  out.gameElapsed = gameElapsedMs(nowMs); out.turnElapsed = currentTurnElapsedMs(nowMs);
  out.nextRequestId = nextLifeRequestId_;
  for (uint8_t i = 0; i < playerCount_; ++i) {
    out.players[i] = players_[i]; out.stats[i] = stats_[i];
    out.conceded[i] = conceded_[i];
    out.selfEliminated[i] = selfEliminated_[i]; out.eliminationPending[i] = eliminationPending_[i];
    out.eliminatedBy[i] = eliminatedBy_[i];
    out.counters[i] = counters_[i];
    out.eliminated[i] = eliminated_[i]; out.life[i] = life_[i];
    for (uint8_t j = 0; j < playerCount_; ++j)
      for (uint8_t c = 0; c < 2; ++c) out.damage[i][j][c] = commanderDamage_[i][j][c];
  }
}

bool GameEngine::restoreCheckpoint(const GameCheckpoint &s, uint32_t nowMs) {
  if (!validCheckpoint(s)) return false;
  reset();
  if (!s.count) return true;
  doom_ = s.doom;
  settings_ = s.settings;
  if (settings_.profile == GameProfile::DoomCommander) settings_.askEliminationSource = true;
  playerCount_ = s.count; activeIndex_ = s.active;
  monarchPlayer_ = s.monarch;
  initiativePlayer_ = s.initiative;
  starterPlayer_ = s.starter; winnerPlayer_ = s.winner; winnerMask_ = s.winners;
  running_ = true; gameOver_ = s.over; paused_ = !s.over;
  nextLifeRequestId_ = s.nextRequestId;
  // Rebase durations into the new boot's millis domain, excluding downtime.
  gameStartedAtMs_ = nowMs - s.gameElapsed;
  turnStartedAtMs_ = nowMs - s.turnElapsed;
  pauseStartedAtMs_ = paused_ ? nowMs : 0;
  gameEndedAtMs_ = s.over ? nowMs : 0;
  for (uint8_t i = 0; i < s.count; ++i) {
    players_[i] = s.players[i]; stats_[i] = s.stats[i];
    conceded_[i] = s.conceded[i];
    selfEliminated_[i] = s.selfEliminated[i]; eliminationPending_[i] = s.eliminationPending[i];
    eliminatedBy_[i] = s.eliminatedBy[i];
    counters_[i] = s.counters[i];
    if (counters_[i].commanderCasts[1]) partner_[i] = true;
    eliminated_[i] = s.eliminated[i]; life_[i] = s.life[i];
    for (uint8_t j = 0; j < s.count; ++j)
      for (uint8_t c = 0; c < 2; ++c) commanderDamage_[i][j][c] = s.damage[i][j][c];
  }
  // Partners are not saved: recorded commander-2 damage brings them back.
  for (uint8_t i = 0; i < s.count; ++i)
    for (uint8_t j = 0; j < s.count; ++j)
      if (s.damage[j][i][1]) partner_[i] = true;
  // Pending requests, claims and confirmations are deliberately canceled.
  // Restoration never invokes the game-completed statistics callback.
  return true;
}
} // namespace TurnHub
