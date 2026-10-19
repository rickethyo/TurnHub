#include "client_state.h"

#include <string.h>

#include "json_text.h"

namespace TurnHub {
namespace {
template <typename T> bool update(T &previous, T value) {
  if (previous == value) return false;
  previous = value;
  return true;
}
const char *requestState(LifeChangeState state) {
  switch (state) {
    case LifeChangeState::Pending: return "PENDING";
    case LifeChangeState::Accepted: return "ACCEPTED";
    case LifeChangeState::Rejected: return "REJECTED";
    case LifeChangeState::Automatic: return "AUTOMATIC";
    case LifeChangeState::Cancelled: return "CANCELLED";
    case LifeChangeState::Failed: return "FAILED";
    default: return "NONE";
  }
}
void nullablePlayer(String &out, uint8_t player) {
  if (player) out += String(player); else out += "null";
}
}

uint32_t ClientState::revision_ = 0;

void ClientState::observe(HubState state, const Lobby &lobby, const GameEngine &game,
    const GameSettings &nextSettings, const ClientPending &pending) {
  bool changed = !initialized_;
  changed |= update(state_, state);
  changed |= update(inGame_, game.hasPlayers());
  const auto &settings = inGame_ ? game.settings() : nextSettings;
  changed |= update(settings_.profile, settings.profile);
  changed |= update(settings_.startingLife, settings.startingLife);
  changed |= update(settings_.turnTimerMs, settings.turnTimerMs);
  changed |= update(settings_.twoHeadedGiant, settings.twoHeadedGiant);
  changed |= update(settings_.houseRules, settings.houseRules);
  changed |= update(settings_.autoEliminateAtZeroLife, settings.autoEliminateAtZeroLife);
  // hostModuleId stays null: there is no table host (2026-09-25); the field
  // remains for client-contract compatibility.
  PlayerSeat selected;
  const uint8_t starter = lobby.selectedStarter(selected) ? selected.playerNumber :
      (inGame_ ? game.starterPlayerNumber() : 0);
  changed |= update(starter_, starter);
  const uint8_t active = state == HubState::Running || state == HubState::Paused ?
      game.activePlayerNumber() : 0;
  changed |= update(active_, active);
  changed |= update(winner_, game.winnerPlayerNumber());
  // Every player who shares the win: Star Commander's shared winners and a
  // Two-Headed Giant winner's teammate (GameEngine::isWinner).
  uint16_t winners = 0;
  for (uint8_t i = 0; i < game.playerCount(); ++i) {
    const uint8_t number = game.playerAt(i)->playerNumber;
    if (number >= 1 && number <= MAX_PLAYERS && game.isWinner(number)) winners |= static_cast<uint16_t>(1u << (number - 1));
  }
  changed |= update(winners_, winners);
  changed |= update(monarch_, game.monarchPlayerNumber());
  changed |= update(initiative_, game.initiativePlayerNumber());
  changed |= update(claimant_, game.winClaimPlayerNumber());
  changed |= update(confirmation_, game.nextWinConfirmationPlayerNumber());
  changed |= update(pending_.passPlayer, pending.passPlayer);
  changed |= update(pending_.passStartedMs, pending.passStartedMs);
  changed |= update(pending_.countdownStartedMs, pending.countdownStartedMs);
  changed |= update(pending_.eliminationTarget, pending.eliminationTarget);
  changed |= update(pending_.nudgeSeq, pending.nudgeSeq);
  changed |= update(pending_.nudgeFrom, pending.nudgeFrom);
  changed |= update(pending_.nudgeTo, pending.nudgeTo);
  changed |= update(pending_.nudgeAtMs, pending.nudgeAtMs);
  changed |= update(pending_.shortGameUncounted, pending.shortGameUncounted);
  PlayerSeat seats[MAX_PLAYERS];
  const uint8_t count = inGame_ ? game.playerCount() : lobby.buildPlayers(seats, MAX_PLAYERS);
  changed |= update(count_, count);
  for (uint8_t i = 0; i < count; ++i) {
    const auto &seat = inGame_ ? *game.playerAt(i) : seats[i];
    auto &p = players_[i];
    bool occupant = update(p.number, seat.playerNumber);
    occupant |= update(p.controller, seat.controllerId);
    occupant |= update(p.slot, seat.slot);
    occupant |= update(p.participant, seat.participantId);
    changed |= occupant;
    // Two-Headed Giant teams are neighbours in turn order (also previewed in the lobby).
    changed |= update(p.team, settings.twoHeadedGiant ? static_cast<uint8_t>(i / TEAM_SIZE + 1) : uint8_t(0));
    if (nameLookup_ != nullptr && (occupant || namesStale_ || !initialized_)) {
      char name[NAME_LENGTH + 1] = {};
      const String value = nameLookup_(seat, inGame_);
      strncpy(name, value.c_str(), NAME_LENGTH);
      if (strcmp(name, p.name) != 0) {
        memcpy(p.name, name, sizeof(name));
        changed = true;
      }
    }
    changed |= update(p.eliminated, inGame_ && game.isEliminated(seat.playerNumber));
    changed |= update(p.conceded, inGame_ && game.hasConceded(seat.playerNumber));
    changed |= update(p.eliminatedBy, inGame_ ? game.eliminatedBy(seat.playerNumber) : uint8_t(0));
    const auto *counters = inGame_ ? game.countersForPlayer(seat.playerNumber) : nullptr;
    const PlayerCounters countersValue = counters ? *counters : PlayerCounters{};
    if (!sameCounters(p.counters, countersValue)) { p.counters = countersValue; changed = true; }
    changed |= update(p.life, inGame_ ? game.lifeTotal(seat.playerNumber) : int32_t(0));
    const auto *stats = inGame_ ? game.statsForPlayer(seat.playerNumber) : nullptr;
    changed |= update(p.turnsCompleted, stats ? stats->turnsCompleted : uint32_t(0));
    for (uint8_t j = 0; j < count; ++j) {
      const uint8_t source = inGame_ ? game.playerAt(j)->playerNumber : seats[j].playerNumber;
      for (uint8_t c = 0; c < COMMANDERS_PER_PLAYER; ++c)
        changed |= update(p.damage[j][c], inGame_ ?
            game.commanderDamage(seat.playerNumber, source, c + 1) : int32_t(0));
    }
    const auto *request = inGame_ ? game.lifeChangeFor(seat.playerNumber) : nullptr;
    const LifeChangeRequest value = request ? *request : LifeChangeRequest{};
    changed |= update(p.request.id, value.id);
    changed |= update(p.request.requestedAtMs, value.requestedAtMs);
    changed |= update(p.request.actor, value.actor);
    changed |= update(p.request.target, value.target);
    changed |= update(p.request.delta, value.delta);
    changed |= update(p.request.state, value.state);
    changed |= update(p.request.windowMs, value.windowMs);
  }
  initialized_ = true;
  namesStale_ = false;
  if (changed) ++revision_;
}

bool ClientState::expirationDue(uint32_t nowMs) const {
  for (uint8_t i = 0; i < count_; ++i) {
    const auto &r = players_[i].request;
    if (r.state == LifeChangeState::Pending && nowMs - r.requestedAtMs >= r.windowMs)
      return true;
  }
  return false;
}

String ClientState::json(const String &atlasId, const char *bootId, const GameEngine &game,
    uint32_t nowMs, uint32_t passGraceMs) const {
  String out;
  size_t capacity = 1024 + count_ * 460;
  for (uint8_t i = 0; i < count_; ++i)
    for (uint8_t j = 0; j < count_; ++j)
      if (players_[i].damage[j][0] || players_[i].damage[j][1]) capacity += 64;
  out.reserve(capacity);
  // Atlas identity and boot identity are generated hexadecimal identifiers.
  out = "{\"protocolVersion\":\"0.1\",\"atlasId\":\"";
  out += atlasId; out += "\",\"bootId\":\""; out += bootId;
  out += "\",\"revision\":"; out += String(revision_);
  out += ",\"state\":\""; out += stateName(state_);
  out += "\",\"hostModuleId\":";
  if (host_ == INVALID_ID) out += "null"; else out += String(host_);
  out += ",\"starterPlayer\":"; nullablePlayer(out, starter_);
  out += ",\"activePlayer\":"; nullablePlayer(out, active_);
  out += ",\"winnerPlayer\":"; nullablePlayer(out, winner_);
  // Every winner, ascending: one, both Two-Headed Giant teammates, or two
  // allies sharing a Star Commander victory.
  out += ",\"winnerPlayers\":[";
  bool firstWinner = true;
  for (uint8_t number = 1; number <= MAX_PLAYERS; ++number) {
    if (!(winners_ & (1u << (number - 1)))) continue;
    if (!firstWinner) out += ',';
    firstWinner = false;
    out += String(number);
  }
  out += ']';
  out += ",\"monarchPlayer\":"; nullablePlayer(out, monarch_);
  out += ",\"initiativePlayer\":"; nullablePlayer(out, initiative_);
  out += ",\"settings\":{\"profile\":\""; out += gameProfileKey(settings_.profile);
  out += "\",\"startingLife\":"; out += String(settings_.startingLife);
  out += ",\"turnTimerMs\":"; out += String(settings_.turnTimerMs);
  out += ",\"autoEliminateAtZeroLife\":"; out += settings_.autoEliminateAtZeroLife ? "true" : "false";
  out += ",\"autoEliminateAtPoisonLimit\":"; out += settings_.autoEliminateAtPoisonLimit ? "true" : "false";
  out += ",\"autoEliminateAtCommanderDamage\":"; out += settings_.autoEliminateAtCommanderDamage ? "true" : "false";
  out += ",\"poisonLimit\":"; out += String(settings_.poisonLimit);
  out += ",\"twoHeadedGiant\":"; out += settings_.twoHeadedGiant ? "true" : "false";
  out += ",\"houseRules\":"; out += settings_.houseRules ? "true" : "false";
  out += "},\"sampledAtMs\":"; out += String(nowMs);
  out += ",\"gameElapsedMs\":"; out += String(game.gameElapsedMs(nowMs));
  out += ",\"turnElapsedMs\":"; out += String(game.currentTurnElapsedMs(nowMs));
  // Sampled like the clocks above: it may change without a revision change.
  const bool counting = inGame_ && !game.gameOver() && settings_.turnTimerMs != TURN_TIMER_OFF;
  out += ",\"turnTimer\":{\"phase\":\""; out += turnTimerPhaseName(game.turnTimerPhase(nowMs));
  out += "\",\"remainingMs\":";
  if (counting) out += String(game.turnRemainingMs(nowMs)); else out += "null";
  out += '}';
  out += ",\"pending\":{\"passPlayer\":"; nullablePlayer(out, pending_.passPlayer);
  const uint32_t elapsed = nowMs - pending_.passStartedMs;
  out += ",\"passGraceRemainingMs\":";
  out += String(pending_.passPlayer && elapsed < passGraceMs ? passGraceMs - elapsed : 0);
  out += ",\"winClaimPlayer\":"; nullablePlayer(out, claimant_);
  out += ",\"winConfirmationPlayer\":"; nullablePlayer(out, confirmation_);
  out += ",\"eliminationTargetPlayer\":"; nullablePlayer(out, pending_.eliminationTarget);
  out += "},\"nudge\":";
  if (pending_.nudgeSeq) {
    out += "{\"seq\":"; out += String(pending_.nudgeSeq);
    out += ",\"fromPlayer\":"; out += String(pending_.nudgeFrom);
    out += ",\"toPlayer\":"; out += String(pending_.nudgeTo);
    out += ",\"ageMs\":"; out += String(nowMs - pending_.nudgeAtMs);
    out += '}';
  } else {
    out += "null";
  }
  out += ",\"shortGameUncounted\":"; out += pending_.shortGameUncounted ? "true" : "false";
  out += ",\"players\":[";
  for (uint8_t i = 0; i < count_; ++i) {
    const auto &p = players_[i];
    if (i) out += ',';
    out += "{\"playerNumber\":"; out += String(p.number);
    out += ",\"moduleId\":"; out += String(p.controller);
    out += ",\"slot\":"; out += String(p.slot);
    out += ",\"team\":"; nullablePlayer(out, p.team);
    out += ",\"displayName\":";
    if (p.name[0]) { out += '"'; out += jsonEscape(p.name); out += '"'; } else out += "null";
    out += ",\"participantId\":"; out += String(p.participant);
    out += ",\"turnsCompleted\":"; out += String(p.turnsCompleted);
    out += ",\"counters\":{\"poison\":"; out += String(p.counters.poison);
    out += ",\"energy\":"; out += String(p.counters.energy);
    out += ",\"experience\":"; out += String(p.counters.experience);
    out += ",\"commanderCasts\":["; out += String(p.counters.commanderCasts[0]); out += ','; out += String(p.counters.commanderCasts[1]);
    out += "],\"cityBlessing\":"; out += p.counters.cityBlessing ? "true" : "false"; out += '}';
    out += ",\"conceded\":"; out += p.conceded ? "true" : "false";
    out += ",\"eliminatedBy\":"; nullablePlayer(out, p.eliminatedBy);
    out += ",\"eliminated\":"; out += p.eliminated ? "true" : "false";
    out += ",\"life\":";
    if (inGame_) out += String(p.life); else out += "null";
    out += ",\"commanderDamage\":[";
    bool comma = false;
    for (uint8_t j = 0; j < count_; ++j) {
      if (!p.damage[j][0] && !p.damage[j][1]) continue;
      if (comma) out += ',';
      comma = true;
      out += "{\"sourcePlayer\":"; out += String(players_[j].number);
      out += ",\"damage\":["; out += String(p.damage[j][0]); out += ',';
      out += String(p.damage[j][1]); out += "]}";
    }
    out += "],\"lifeRequest\":";
    const auto &r = p.request;
    if (!r.id) out += "null";
    else {
      out += "{\"id\":"; out += String(r.id);
      out += ",\"actor\":"; out += String(r.actor);
      out += ",\"target\":"; out += String(r.target);
      out += ",\"delta\":"; out += String(r.delta);
      out += ",\"state\":\""; out += requestState(r.state);
      out += "\",\"requestedAtMs\":"; out += String(r.requestedAtMs);
      out += ",\"windowMs\":"; out += String(r.windowMs); out += '}';
    }
    out += '}';
  }
  out += "]}";
  return out;
}
} // namespace TurnHub
