#include "client_state.h"

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

void ClientState::observe(HubState state, const Lobby &lobby, const GameEngine &game,
    const GameSettings &nextSettings, const ClientPending &pending) {
  bool changed = !initialized_;
  changed |= update(state_, state);
  changed |= update(inGame_, game.hasPlayers());
  const auto &settings = inGame_ ? game.settings() : nextSettings;
  changed |= update(settings_.profile, settings.profile);
  changed |= update(settings_.startingLife, settings.startingLife);
  changed |= update(settings_.turnTimerMs, settings.turnTimerMs);
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
  changed |= update(claimant_, game.winClaimPlayerNumber());
  changed |= update(confirmation_, game.nextWinConfirmationPlayerNumber());
  changed |= update(pending_.passPlayer, pending.passPlayer);
  changed |= update(pending_.passStartedMs, pending.passStartedMs);
  changed |= update(pending_.countdownStartedMs, pending.countdownStartedMs);
  changed |= update(pending_.eliminationTarget, pending.eliminationTarget);
  PlayerSeat seats[MAX_PLAYERS];
  const uint8_t count = inGame_ ? game.playerCount() : lobby.buildPlayers(seats, MAX_PLAYERS);
  changed |= update(count_, count);
  for (uint8_t i = 0; i < count; ++i) {
    const auto &seat = inGame_ ? *game.playerAt(i) : seats[i];
    auto &p = players_[i];
    changed |= update(p.number, seat.playerNumber);
    changed |= update(p.controller, seat.controllerId);
    changed |= update(p.slot, seat.slot);
    changed |= update(p.participant, seat.participantId);
    changed |= update(p.eliminated, inGame_ && game.isEliminated(seat.playerNumber));
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
  }
  initialized_ = true;
  if (changed) ++revision_;
}

bool ClientState::expirationDue(uint32_t nowMs) const {
  for (uint8_t i = 0; i < count_; ++i) {
    const auto &r = players_[i].request;
    if (r.state == LifeChangeState::Pending && nowMs - r.requestedAtMs >= LIFE_APPROVAL_MS)
      return true;
  }
  return false;
}

String ClientState::json(const String &atlasId, const char *bootId, const GameEngine &game,
    uint32_t nowMs, uint32_t passGraceMs) const {
  String out;
  size_t capacity = 1024 + count_ * 300;
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
  out += ",\"settings\":{\"profile\":\""; out += gameProfileKey(settings_.profile);
  out += "\",\"startingLife\":"; out += String(settings_.startingLife);
  out += ",\"turnTimerMs\":"; out += String(settings_.turnTimerMs);
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
  out += "},\"players\":[";
  for (uint8_t i = 0; i < count_; ++i) {
    const auto &p = players_[i];
    if (i) out += ',';
    out += "{\"playerNumber\":"; out += String(p.number);
    out += ",\"moduleId\":"; out += String(p.controller);
    out += ",\"slot\":"; out += String(p.slot);
    out += ",\"participantId\":"; out += String(p.participant);
    out += ",\"turnsCompleted\":"; out += String(p.turnsCompleted);
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
      out += "\",\"requestedAtMs\":"; out += String(r.requestedAtMs); out += '}';
    }
    out += '}';
  }
  out += "]}";
  return out;
}
} // namespace TurnHub
