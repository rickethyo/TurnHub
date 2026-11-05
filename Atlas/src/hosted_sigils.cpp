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
#include "profile_store.h"
#include "turnhub_json.h"

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

// source: a Sigil's ID, or ATLAS_SCREEN for the Atlas touchscreen.
constexpr uint8_t ATLAS_SCREEN = 0xFF;

void queueInput(uint8_t source, int player, const std::string &action) {
  char id[40];
  if (source == ATLAS_SCREEN) {
    snprintf(id, sizeof(id), "atlas-%lu-%lu", static_cast<unsigned long>(millis()),
        static_cast<unsigned long>(++inputCounter));
  } else {
    snprintf(id, sizeof(id), "sigil-%u-%lu-%lu", static_cast<unsigned>(source),
        static_cast<unsigned long>(millis()), static_cast<unsigned long>(++inputCounter));
  }
  const uint32_t seq = TurnHubHosted::hostedTable().queue(player, id, action, millis());
  if (source == ATLAS_SCREEN) {
    TurnHub::serialLog.print("ATLAS|HOSTED|SCREEN");
  } else {
    TurnHub::serialLog.print("ATLAS|HOSTED|SIGIL|");
    TurnHub::serialLog.print(source);
  }
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

// Choices are snapshots of public identities. Only the hub may attach or add a player.
struct JoinChoice {
  std::string name, localId, profileId;
  int player = -1;
  bool guest = false;
};
struct JoinPicker {
  TurnHubProtocol::ProfilePickerPacket packet{};
  std::vector<JoinChoice> choices;
  uint8_t slot = 1;
  int previous = -1;
  int selected = -1;
  bool open = false;
  uint8_t closeRetries = 0;
  uint32_t sentAt = 0;
  std::string host;
};
JoinPicker pickers[MAX_PHYSICAL_SIGILS];

std::string sigilGuestName(uint8_t id, uint8_t slot) {
  const auto *record = sigilBus.record(id);
  const String configured = record ? TurnHubProfiles::deviceName(record->mac) : String();
  std::string name = configured.length() ? configured.c_str() : "Sigil " + std::to_string(id + 1);
  return name + (slot == 2 ? " B" : "");
}

void sendPicker(uint8_t id) {
  auto &p = pickers[id];
  auto &packet = p.packet;
  packet.version = TurnHubProtocol::VERSION;
  packet.type = TurnHubProtocol::PacketType::ProfilePicker;
  packet.sigilId = id;
  ++packet.revision;
  packet.mode = !p.open ? TurnHubProtocol::PickerMode::Closed : p.selected >= 0 ?
      TurnHubProtocol::PickerMode::Confirm : TurnHubProtocol::PickerMode::List;
  packet.pageCount = static_cast<uint8_t>((p.choices.size() + 2) / 3);
  if (!packet.pageCount) packet.pageCount = 1;
  packet.itemCount = 0;
  memset(packet.items, 0, sizeof(packet.items));
  const size_t start = p.selected >= 0 ? p.selected : packet.page * 3;
  for (size_t i = start; i < p.choices.size() && packet.itemCount < (p.selected >= 0 ? 1 : 3); ++i) {
    const auto &choice = p.choices[i];
    auto &item = packet.items[packet.itemCount++];
    item.flags = choice.guest ? TurnHubProtocol::PICKER_ITEM_GUEST : choice.player >= 0 ? TurnHubProtocol::PICKER_ITEM_PLAYING : 0;
    snprintf(item.name, sizeof(item.name), "%s", choice.name.c_str());
  }
  p.closeRetries = p.open ? 0 : 3;
  sigilBus.sendProfilePicker(packet);
  p.sentAt = millis();
}

void openPicker(uint8_t id, uint8_t slot) {
  auto &p = pickers[id];
  p.choices.clear(); p.selected = -1; p.slot = slot; p.open = true;
  p.previous=TurnHubHosted::sigilPlayer(hostedGame(),id,slot);
  p.host = TurnHubHosted::hostedTable().lease().token;
  p.packet.page = 0; p.packet.notice = TurnHubProtocol::PickerNotice::None;
  const auto &game = hostedGame();
  for (size_t i = 0; i < game.players.size(); ++i) {
    uint8_t other = 0, seat = 0;
    const auto &player = game.players[i];
    if (!TurnHubHosted::playerSigil(game, i, other, seat))
      p.choices.push_back({player.name, player.localId, player.profileId, static_cast<int>(i), false});
  }
  const auto addProfile = [&](const std::string &profile, const std::string &name) {
    if (profile.size() != 8 || name.empty() || p.choices.size() >= 160) return;
    for (const auto &player : game.players) if (player.profileId == profile) return;
    for (const auto &choice : p.choices) if (choice.profileId == profile) return;
    p.choices.push_back({name, "", profile, -1, false});
  };
  const auto state = turnhub::Json::parse(TurnHubHosted::hostedTable().stateJson());
  const auto &hub = state.get("hostedProfiles");
  for (size_t i = 0; i < hub.size(); ++i) addProfile(hub[i].get("profileId").asString(), hub[i].get("name").asString());
  char ids[TurnHubProfiles::MAX_LOGIN_PROFILES][9] = {};
  const size_t count = TurnHubProfiles::listProfileIds(ids, TurnHubProfiles::MAX_LOGIN_PROFILES);
  for (size_t i = 0; i < count; ++i) {
    const String name = TurnHubProfiles::nameForProfile(ids[i]);
    if (TurnHubProfiles::mergedProfileFor(ids[i]) == String(ids[i])) addProfile(ids[i], name.c_str());
  }
  p.choices.push_back({sigilGuestName(id, slot), "", "", -1, true});
  sendPicker(id);
}

void pickerKey(uint8_t id, int32_t value) {
  using namespace TurnHubProtocol;
  auto &p = pickers[id];
  if (!p.open) return;
  if (pickerKeyRevision(value) != p.packet.revision) { sigilBus.sendProfilePicker(p.packet); return; }
  const auto key = static_cast<PickerKeyCode>(pickerKeyCode(value));
  if (key == PickerKeyCode::Left) {
    if (p.selected >= 0) p.selected = -1;
    else if (p.packet.page > 0) --p.packet.page;
    else p.open = false;
  } else if (p.selected >= 0 && key == PickerKeyCode::Select) {
    const auto &choice = p.choices[p.selected];
    turnhub::Json action = turnhub::Json::object();
    action.set("kind", "sigil-join"); action.set("sigil", static_cast<int>(id)); action.set("slot", static_cast<int>(p.slot));
    action.set("mode", choice.guest ? "guest" : choice.player >= 0 ? "attach" : "profile");
    action.set("name", choice.name); action.set("target", choice.localId); action.set("profileId", choice.profileId);
    action.set("playerIndex", choice.player);
    if(p.previous>=0) action.set("previousPlayer",p.previous);
    queueInput(id, 0, action.dump());
    p.open = false;
  } else if (p.selected < 0) {
    if (key == PickerKeyCode::Select && p.packet.page + 1 < p.packet.pageCount) ++p.packet.page;
    else {
      const int row = key == PickerKeyCode::Up ? 0 : key == PickerKeyCode::Right ? 1 : key == PickerKeyCode::Down ? 2 : -1;
      const int index = p.packet.page * 3 + row;
      if (row >= 0 && index < static_cast<int>(p.choices.size())) p.selected = index;
    }
  }
  sendPicker(id);
}

}  // namespace

HostedScreenActions hostedScreenActions() {
  const turnhub::Game &game = hostedGame();
  HostedScreenActions actions;
  const bool turn = game.active && living(*game.active);
  switch (game.state) {
    case turnhub::TableState::Lobby: actions.start = game.players.size() >= 2; break;
    case turnhub::TableState::Running: {
      actions.pass = actions.pause = turn;
      const turnhub::Json &planes = game.planechase;
      if (turn && planes.get("enabled").asBool()) {
        bool phenomenon = false;
        const auto &up = planes.get("faceUp");
        for (size_t i = 0; i < up.size(); ++i) phenomenon = phenomenon || up[i].get("phenomenon").asBool();
        actions.roll = !phenomenon;
        actions.resolve = phenomenon;
        actions.walk = !phenomenon;
      }
      break;
    }
    case turnhub::TableState::Paused: actions.resume = turn && !game.openWinClaim(); break;
    default: break;
  }
  return actions;
}

bool queueHostedScreenAction(HostedScreenAction action) {
  const HostedScreenActions offered = hostedScreenActions();
  const int active = hostedGame().active ? *hostedGame().active : -1;
  switch (action) {
    case HostedScreenAction::Start:
      if (!offered.start) return false;
      queueInput(ATLAS_SCREEN, 0, "{\"kind\":\"start\"}");
      return true;
    case HostedScreenAction::Pass:
      if (!offered.pass) return false;
      queueSeat(ATLAS_SCREEN, active, "pass");
      return true;
    case HostedScreenAction::Pause:
      if (!offered.pause) return false;
      queueSeat(ATLAS_SCREEN, active, "pause", 1);
      return true;
    case HostedScreenAction::Resume:
      if (!offered.resume) return false;
      queueSeat(ATLAS_SCREEN, active, "pause", 0);
      return true;
    // The hub rolls the die and acts for the active player (lan-v1.md planar commands).
    case HostedScreenAction::PlanarRoll:
      if (!offered.roll) return false;
      queueSeat(ATLAS_SCREEN, active, "planar-roll");
      return true;
    case HostedScreenAction::PlanarWalk:
      if (!offered.walk) return false;
      queueSeat(ATLAS_SCREEN, active, "planar-walk");
      return true;
    case HostedScreenAction::PlanarResolve:
      if (!offered.resolve) return false;
      queueSeat(ATLAS_SCREEN, active, "planar-resolve");
      return true;
  }
  return false;
}

void syncHostedPickers(uint32_t nowMs) {
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    auto &p = pickers[id];
    if (p.open && (!TurnHubHosted::hostedTable().live(nowMs) ||
        hostedGame().state != turnhub::TableState::Lobby || p.host != TurnHubHosted::hostedTable().lease().token)) {
      p.open = false; sendPicker(id);
    }
    // Retry the exact page (same revision); duplicate keys cannot confirm a newer page.
    if ((p.open || p.closeRetries) && nowMs - p.sentAt >= 500) {
      sigilBus.sendProfilePicker(p.packet); p.sentAt = nowMs;
      if (!p.open) --p.closeRetries;
    }
  }
}

bool hostedForSigil(uint8_t sigilId, uint32_t nowMs) {
  return sigilId < MAX_PHYSICAL_SIGILS && TurnHubHosted::hostedTable().live(nowMs);
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
      if(seatA) add(SigilAction::ChangePlayer);
      if(seatB) add(SigilAction::ChangeSeatB);
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
  if (sigilId >= MAX_PHYSICAL_SIGILS) return;
  auto &picker = pickers[sigilId];
  if (picker.open && (hostedGame().state != turnhub::TableState::Lobby ||
      picker.host != TurnHubHosted::hostedTable().lease().token)) { picker.open = false; sendPicker(sigilId); return; }
  if (type == PacketType::PickerKey) { pickerKey(sigilId, value); return; }
  if (type != PacketType::SelectAction) return;
  const uint8_t raw = TurnHubProtocol::selectedAction(value);
  if (TurnHubProtocol::selectedRevision(value) != sigilMenuRevision(sigilId) ||
      raw >= static_cast<uint8_t>(SigilAction::Count) || (hostedSigilMenu(sigilId).actions & (1u << raw)) == 0) {
    invalidateSigilMenu(sigilId);
    return;
  }
  switch (static_cast<SigilAction>(raw)) {
    case SigilAction::Join: openPicker(sigilId, 1); break;
    case SigilAction::AddSeatB: openPicker(sigilId, 2); break;
    case SigilAction::ChangePlayer: openPicker(sigilId, 1); break;
    case SigilAction::ChangeSeatB: openPicker(sigilId, 2); break;
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
