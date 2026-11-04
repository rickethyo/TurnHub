// Phone-hosted games on the Sigils (plans: STAGED_CHANGES "Phone-hosted
// games", slice 2). While a hub hosts Game 1, a Sigil at Game 1 shows the
// hub's game (LedRenderer::renderHosted) and its menu and keys queue inputs
// for the hub (hosted_game.h). Atlas decides nothing: the hub applies each
// input through the seat-action door a Play together guest uses.

#include "hosted_sigils.h"

#include <stdio.h>
#include <string>

#include "atlas_app.h"
#include "hosted_game.h"
#include "serial_log.h"
#include "sigil_menu.h"

namespace TurnHubAtlas {

namespace {

using TurnHubProtocol::SigilAction;

uint32_t inputCounter = 0;

const turnhub::Game &hostedGame() { return TurnHubHosted::hostedTable().game(); }

bool living(int index) {
  const auto &players = hostedGame().players;
  return index >= 0 && index < static_cast<int>(players.size()) && !players[index].eliminated;
}

// The seat on this Sigil whose turn it is (Two-Headed Giant: the team's), or -1.
int turnSeat(uint8_t sigilId) {
  const turnhub::Game &game = hostedGame();
  if (!game.active) return -1;
  int seats[2];
  const uint8_t count = TurnHubHosted::sigilPlayers(game, sigilId, seats);
  std::vector<int> team = game.mates(*game.active);
  team.push_back(*game.active);
  for (uint8_t i = 0; i < count; ++i)
    for (int member : team) if (member == seats[i] && living(seats[i])) return seats[i];
  return -1;
}

int livingSeat(uint8_t sigilId) {
  int seats[2];
  const uint8_t count = TurnHubHosted::sigilPlayers(hostedGame(), sigilId, seats);
  for (uint8_t i = 0; i < count; ++i) if (living(seats[i])) return seats[i];
  return -1;
}

int confirmingSeat(uint8_t sigilId) {
  const std::optional<int> confirmer = hostedGame().winConfirmer();
  if (!confirmer) return -1;
  uint8_t id = 0, slot = 0;
  return TurnHubHosted::playerSigil(hostedGame(), *confirmer, id, slot) && id == sigilId ? *confirmer : -1;
}

void queueInput(uint8_t sigilId, int player, const std::string &action) {
  char id[40];
  snprintf(id, sizeof(id), "sigil-%u-%lu-%lu", static_cast<unsigned>(sigilId),
      static_cast<unsigned long>(millis()), static_cast<unsigned long>(++inputCounter));
  const uint32_t seq = TurnHubHosted::hostedTable().queue(player, id, action, millis());
  TurnHub::serialLog.print("ATLAS|HOSTED|SIGIL|");
  TurnHub::serialLog.print(sigilId);
  TurnHub::serialLog.print(seq ? "|QUEUED|" : "|DROPPED|");
  TurnHub::serialLog.println(action.c_str());
}

// A seat action, in the Play together vocabulary (protocol/lan-v1.md).
void queueSeat(uint8_t sigilId, int index, const char *kind, int value = 0) {
  if (index < 0) return;
  queueInput(sigilId, index + 1, std::string("{\"kind\":\"") + kind + "\",\"value\":" + std::to_string(value) +
      ",\"slot\":0,\"source\":0}");
}

// A table action: a Sigil joins or leaves the hub's setup, or starts the game.
void queueTable(uint8_t sigilId, const char *kind, uint8_t slot = 0) {
  queueInput(sigilId, 0, std::string("{\"kind\":\"") + kind + "\",\"sigil\":" + std::to_string(sigilId) +
      ",\"slot\":" + std::to_string(slot) + "}");
}

}  // namespace

bool hostedForSigil(uint8_t sigilId, uint32_t nowMs) {
  return sigilId < MAX_PHYSICAL_SIGILS && tableForController(sigilId) == 0 && TurnHubHosted::hostedTable().live(nowMs);
}

TurnHubProtocol::MenuStateFields hostedSigilMenu(uint8_t sigilId) {
  using turnhub::TableState;
  const turnhub::Game &game = hostedGame();
  uint32_t actions = 0;
  const auto add = [&actions](SigilAction action) { actions |= TurnHubProtocol::sigilActionBit(action); };
  const bool seatA = TurnHubHosted::sigilPlayer(game, sigilId, 1) >= 0;
  const bool seatB = TurnHubHosted::sigilPlayer(game, sigilId, 2) >= 0;
  switch (game.state) {
    case TableState::Lobby:
      if (!seatA && !seatB) { add(SigilAction::Join); break; }
      add(seatB ? SigilAction::RemoveSeatB : SigilAction::AddSeatB);
      if (game.players.size() >= 2) add(SigilAction::StartGame);
      add(SigilAction::Leave);
      break;
    case TableState::Starting:
      break;
    case TableState::Running:
      if (turnSeat(sigilId) >= 0) { add(SigilAction::Pass); add(SigilAction::ClaimWin); }
      if (livingSeat(sigilId) >= 0) { add(SigilAction::Pause); add(SigilAction::AdjustLife); }
      break;
    case TableState::Paused:
      if (confirmingSeat(sigilId) >= 0) {
        add(SigilAction::ConfirmWin);
        add(SigilAction::DenyWin);
      } else if (!game.openWinClaim() && livingSeat(sigilId) >= 0) {
        add(SigilAction::Resume);
        add(SigilAction::AdjustLife);
      }
      break;
    case TableState::GameOver:
      break;
  }
  TurnHubProtocol::MenuStateFields fields;
  fields.actions = actions;
  return fields;
}

void handleHostedSigilEvent(uint8_t sigilId, TurnHubProtocol::PacketType type, int32_t value) {
  using TurnHubProtocol::PacketType;
  if (type == PacketType::LifeAdjust) {
    // The shown player's life, as for Atlas's own games: only this Sigil's own seat.
    const int index = static_cast<int>(TurnHubProtocol::lifeAdjustPlayer(value)) - 1;
    const int32_t delta = TurnHubProtocol::lifeAdjustDelta(value);
    uint8_t id = 0, slot = 0;
    if (delta == 0 || !TurnHubHosted::playerSigil(hostedGame(), index, id, slot) || id != sigilId) return;
    queueSeat(sigilId, index, "life", delta);
    return;
  }
  if (type != PacketType::SelectAction) return;  // Pickers and controls arrive with later slices.
  const uint8_t raw = TurnHubProtocol::selectedAction(value);
  if (TurnHubProtocol::selectedRevision(value) != sigilMenuRevision(sigilId) ||
      raw >= static_cast<uint8_t>(SigilAction::Count) || (hostedSigilMenu(sigilId).actions & (1u << raw)) == 0) {
    invalidateSigilMenu(sigilId);
    return;
  }
  switch (static_cast<SigilAction>(raw)) {
    case SigilAction::Join: queueTable(sigilId, "sigil-join", 1); break;
    case SigilAction::AddSeatB: queueTable(sigilId, "sigil-join", 2); break;
    case SigilAction::RemoveSeatB: queueTable(sigilId, "sigil-leave", 2); break;
    case SigilAction::Leave: queueTable(sigilId, "sigil-leave", 0); break;
    case SigilAction::StartGame: queueTable(sigilId, "start"); break;
    case SigilAction::Pass: queueSeat(sigilId, turnSeat(sigilId), "pass"); break;
    case SigilAction::ClaimWin: queueSeat(sigilId, turnSeat(sigilId), "win"); break;
    case SigilAction::ConfirmWin: queueSeat(sigilId, confirmingSeat(sigilId), "confirm"); break;
    case SigilAction::DenyWin: queueSeat(sigilId, confirmingSeat(sigilId), "deny"); break;
    case SigilAction::Pause: queueSeat(sigilId, livingSeat(sigilId), "pause", 1); break;
    case SigilAction::Resume: queueSeat(sigilId, livingSeat(sigilId), "pause", 0); break;
    default: break;
  }
}

}  // namespace TurnHubAtlas
