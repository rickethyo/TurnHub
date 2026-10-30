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
  out.dayNight = DayNight::Neither;
  out.reversed = false;
  out.radDue = 0;
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
  if (s.dayNight >= DayNight::Count || (s.dayNight != DayNight::Neither && !magicRules(s.settings.profile))) return false;
  if (!s.count) return !s.over && !s.winner && !s.winners && !s.reversed && !s.radDue &&
      s.dayNight == DayNight::Neither && sameDoomBag(s.doom, DoomBag{});
  if (s.count < 16 && (s.radDue >> s.count)) return false;
  for (uint8_t i = 0; i < s.count; ++i)
    if ((s.radDue & (1u << i)) && (s.eliminated[i] || s.counters[i].rad <= 0 || !magicRules(s.settings.profile))) return false;
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
  out.count = playerCount_;
  out.active = static_cast<uint8_t>(core_.active ? *core_.active : 0);
  out.starter = starterPlayerNumber();
  out.monarch = monarchPlayerNumber();
  out.initiative = initiativePlayerNumber();
  out.dayNight = dayNight(); out.reversed = turnOrderReversed();
  for (int index : core_.radDue) out.radDue |= static_cast<uint16_t>(1u << index);
  out.winner = winnerPlayerNumber(); out.winners = winnerMask(); out.paused = paused(); out.over = gameOver();
  out.gameElapsed = gameElapsedMs(nowMs); out.turnElapsed = currentTurnElapsedMs(nowMs);
  out.nextRequestId = nextLifeRequestId_;
  for (uint8_t i = 0; i < playerCount_; ++i) {
    const turnhub::Player &p = core_.players[i];
    out.players[i] = players_[i]; out.stats[i] = stats_[i];
    out.conceded[i] = p.conceded;
    out.selfEliminated[i] = p.selfEliminated; out.eliminationPending[i] = p.eliminationPending;
    out.eliminatedBy[i] = numberOf(p.eliminatedBy);
    out.counters[i] = counters_[i];
    out.eliminated[i] = p.eliminated; out.life[i] = p.life;
    for (uint8_t j = 0; j < playerCount_; ++j)
      for (uint8_t c = 0; c < 2; ++c) out.damage[i][j][c] = commanderDamage(i + 1, j + 1, c + 1);
  }
}

bool GameEngine::restoreCheckpoint(const GameCheckpoint &s, uint32_t nowMs) {
  if (!validCheckpoint(s)) return false;
  reset();
  if (!s.count) return true;
  settings_ = s.settings;
  if (settings_.profile == GameProfile::DoomCommander) settings_.askEliminationSource = true;
  playerCount_ = s.count;
  nextLifeRequestId_ = s.nextRequestId;
  // Rebuild the core's game: restored matches are paused (or over), with
  // durations rebased into this boot's clock, excluding downtime.
  turnhub::Game g;
  switch (s.settings.profile) {
    case GameProfile::Magic: g.profile = turnhub::Profile::Mtg; break;
    case GameProfile::Commander: g.profile = turnhub::Profile::MtgCommander; break;
    case GameProfile::Yugioh: g.profile = turnhub::Profile::Yugioh; break;
    case GameProfile::StarCommander: g.profile = turnhub::Profile::MtgStarCommander; break;
    case GameProfile::DoomCommander: g.profile = turnhub::Profile::MtgBagOfDoom; break;
    default: g.profile = turnhub::Profile::Generic; break;
  }
  g.gameId = "atlas-restored-" + std::to_string(matchGeneration_);
  g.state = s.over ? turnhub::TableState::GameOver : turnhub::TableState::Paused;
  g.startingLife = s.settings.startingLife;
  g.twoHeadedGiant = s.settings.twoHeadedGiant;
  g.houseRules = s.settings.houseRules;
  g.turnTimerMs = s.settings.turnTimerMs;
  g.poisonLimit = s.settings.poisonLimit;
  g.autoEliminateAtZeroLife = s.settings.autoEliminateAtZeroLife;
  g.autoEliminateAtPoisonLimit = s.settings.autoEliminateAtPoisonLimit;
  g.autoEliminateAtCommanderDamage = s.settings.autoEliminateAtCommanderDamage;
  g.askEliminationSource = settings_.askEliminationSource;
  g.active = s.active;
  g.starter = s.starter - 1;
  if (s.monarch) g.monarch = s.monarch - 1;
  if (s.initiative) g.initiative = s.initiative - 1;
  g.dayNight = static_cast<turnhub::DayNight>(s.dayNight);
  g.turnOrderReversed = s.reversed;
  for (uint8_t i = 0; i < s.count; ++i) if (s.radDue & (1u << i)) g.radDue.push_back(i);
  for (uint8_t i = 0; i < s.count; ++i) if (s.winners & (1u << i)) g.winners.push_back(i);
  if (s.winner) g.winner = s.winner - 1;
  anchor32_ = nowMs;
  anchor64_ = int64_t(1) << 32;
  g.startedAtMs = at(nowMs) - s.gameElapsed;
  g.elapsedMs = s.gameElapsed;
  g.turnStartedAtElapsedMs = static_cast<int64_t>(s.gameElapsed) - s.turnElapsed;
  g.doomBag.atRisk = s.doom.atRisk; g.doomBag.drawn = s.doom.drawn;
  g.doomBag.boundary = s.doom.boundary; g.doomBag.warmup = s.doom.warmup;
  g.doomBag.pendingTurn = s.doom.pendingTurn; g.doomBag.selected = s.doom.selected;
  g.doomBag.lastDraw = s.doom.lastDraw; g.doomBag.cycle = s.doom.cycle;
  g.doomBag.lastCheck = s.doom.lastCheck; g.doomBag.pending = s.doom.pending;
  for (uint8_t i = 0; i < s.count; ++i) {
    players_[i] = s.players[i];
    turnhub::Player p;
    p.name = "Player " + std::to_string(i + 1);
    p.profileId = s.players[i].profileId;
    p.life = s.life[i];
    p.eliminated = s.eliminated[i]; p.conceded = s.conceded[i];
    p.selfEliminated = s.selfEliminated[i]; p.eliminationPending = s.eliminationPending[i];
    if (s.eliminatedBy[i]) p.eliminatedBy = s.eliminatedBy[i] - 1;
    const PlayerCounters &c = s.counters[i];
    p.counters.poison = c.poison; p.counters.energy = c.energy; p.counters.experience = c.experience;
    p.counters.commanderCasts = {{c.commanderCasts[0], c.commanderCasts[1]}};
    p.counters.cityBlessing = c.cityBlessing; p.counters.treasure = c.treasure; p.counters.rad = c.rad;
    p.turnsCompleted = static_cast<int>(s.stats[i].turnsCompleted);
    p.turnMs = s.stats[i].totalTurnMs; p.fastestTurnMs = s.stats[i].fastestTurnMs; p.longestTurnMs = s.stats[i].longestTurnMs;
    for (uint8_t j = 0; j < s.count; ++j)
      if (s.damage[i][j][0] || s.damage[i][j][1]) p.commanderDamage[j] = {{s.damage[i][j][0], s.damage[i][j][1]}};
    if (c.commanderCasts[1]) partner_[i] = true;
    g.players.push_back(p);
  }
  // Partners are not saved: recorded commander-2 damage brings them back.
  for (uint8_t i = 0; i < s.count; ++i)
    for (uint8_t j = 0; j < s.count; ++j)
      if (s.damage[j][i][1]) partner_[i] = true;
  g.revision = core_.revision + 1;
  core_ = g;
  mirror();
  // Pending requests, claims and confirmations are deliberately canceled.
  // Restoration never invokes the game-completed statistics callback.
  return true;
}
} // namespace TurnHub
