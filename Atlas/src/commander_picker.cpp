// Atlas owns the disposable entry flow. Only dispatched counter Intents apply hits.
#include "commander_picker.h"
#include "atlas_app.h"
#include "profile_store.h"
#include <cstring>

namespace TurnHubAtlas {
namespace {
using namespace TurnHubProtocol;
bool tableReady() { return (table().hubState == HubState::Running || table().hubState == HubState::Paused) &&
    !table().game.hasWinClaim() && table().eliminationTargetPlayer == 0; }
struct Flow {
  CommanderFlowPacket page{};
  bool sent = false;
  uint32_t lastKey = 0, participant = 0, generation = 0;
};
Flow flows[MAX_PHYSICAL_SIGILS];
void changed(Flow &f) { ++f.page.revision; f.sent = false; }
void close(Flow &f) { f.page.stage = CommanderStage::Closed; changed(f); }
bool eligible(const Flow &f) {
  const auto *seat = table().game.playerByNumber(f.page.recipient);
  return tableReady() && f.generation == table().game.matchGeneration() &&
      table().game.settings().profile == TurnHub::GameProfile::Commander && seat &&
      seat->controllerId == f.page.sigilId && seat->slot == f.page.recipientSlot &&
      seat->participantId == f.participant && !table().game.isEliminated(seat->playerNumber);
}
// Names read from the profile store (NVS) once per opened flow, not on every
// source change: cycling sources runs on the loop task.
char nameCache[MAX_PLAYERS][DISPLAY_NAME_MAX_LENGTH + 1];
uint32_t namesCached = 0;
void name(uint8_t player, char *out) {
  const bool cacheable = player >= 1 && player <= MAX_PLAYERS;
  const uint32_t bit = cacheable ? 1u << (player - 1) : 0;
  if (cacheable && (namesCached & bit)) {
    memcpy(out, nameCache[player - 1], DISPLAY_NAME_MAX_LENGTH + 1);
    return;
  }
  const auto *seat = table().game.playerByNumber(player);
  String text = seat ? TurnHubProfiles::nameForProfile(String(seat->profileId)) : String();
  if (!text.length()) text = String("Player ") + String(player);
  size_t i = 0;
  for (; i < text.length() && i < DISPLAY_NAME_MAX_LENGTH; ++i)
    out[i] = text[i] >= 32 && text[i] <= 126 ? text[i] : '?';
  out[i] = 0;
  if (cacheable) {
    memcpy(nameCache[player - 1], out, DISPLAY_NAME_MAX_LENGTH + 1);
    namesCached |= bit;
  }
}
// The next source from `from` in seat order, skipping the recipient and
// eliminated players (self-damage is rare; record it from a phone). Falls
// back to `from` when nobody else qualifies.
uint8_t nextSource(uint8_t recipient, uint8_t from, int step) {
  const int count = table().game.playerCount();
  if (count <= 0) return from;
  int player = from;
  for (int i = 0; i < count; ++i) {
    player = (player - 1 + count + step) % count + 1;
    if (player != recipient && !table().game.isEliminated(static_cast<uint8_t>(player)))
      return static_cast<uint8_t>(player);
  }
  return from;
}
// Refresh confirmation totals before accepting a key. A changed preview gets
// a new revision, requiring the player to confirm the fresh values.
void refresh(Flow &f) {
  auto &p = f.page;
  const int32_t life = table().game.lifeTotal(p.recipient);
  const int32_t damage = table().game.commanderDamage(p.recipient,p.source,p.commander);
  if (p.life != life || p.damage != damage) {
    p.life = life; p.damage = damage; changed(f);
  }
}
void result(Flow &f, const char *notice) {
  f.page.stage = CommanderStage::Result;
  strncpy(f.page.notice,notice,sizeof(f.page.notice)-1);
  f.page.notice[sizeof(f.page.notice)-1] = 0;
  changed(f);
}
void apply(Flow &f, bool undo) {
  const auto *seat = table().game.playerByNumber(f.page.recipient);
  Intent intent;
  intent.type = undo ? IntentType::UndoCommanderHit : IntentType::RecordCommanderHit;
  intent.actor.origin = IntentOrigin::PhysicalSigil;
  intent.actor.controllerId = seat->controllerId;
  intent.actor.slot = seat->slot;
  intent.actor.playerNumber = seat->playerNumber;
  intent.payload.targetPlayer = seat->playerNumber;
  intent.payload.counterSource = f.page.source;
  intent.payload.counterSlot = f.page.commander;
  intent.payload.value = f.page.amount;
  const IntentResult answer = intents.dispatch(intent);
  result(f,answer.accepted() ? (undo ? "Hit undone" : "Hit recorded") : "Entry changed or exceeds limits");
  leds.invalidate(f.page.sigilId);
}
}
void openCommanderPicker(uint8_t sigilId, uint8_t recipient, bool undo, uint32_t nowMs) {
  if (sigilId >= MAX_PHYSICAL_SIGILS) return;
  const auto *seat = table().game.playerByNumber(recipient);
  if (!seat || seat->controllerId != sigilId || table().game.isEliminated(recipient) ||
      !tableReady() ||
      table().game.settings().profile != TurnHub::GameProfile::Commander) return;
  namesCached = 0;
  Flow &f = flows[sigilId];
  const uint16_t revision = f.page.revision;
  f = Flow{};
  auto &p = f.page;
  p.version = VERSION; p.type = PacketType::CommanderFlow; p.sigilId = sigilId;
  p.revision = revision; p.stage = CommanderStage::Source;
  p.recipient = recipient; p.recipientSlot = seat->slot; f.participant = seat->participantId;
  p.source = nextSource(recipient, table().game.playerCount(), 1); p.commander = 1; p.amount = 1;
  name(recipient,p.recipientName); name(p.source,p.sourceName);
  f.lastKey = nowMs; f.generation = table().game.matchGeneration();
  if (undo) {
    const auto *hit = table().game.lastCommanderHit(recipient);
    if (hit) {
      p.source = hit->source; p.commander = hit->commander; p.amount = hit->amount;
      p.stage = CommanderStage::UndoConfirm; name(p.source,p.sourceName);
    } else result(f,"No last hit for this seat");
  }
  refresh(f); changed(f);
}
void handleCommanderKey(uint8_t sigilId, int32_t value, uint32_t nowMs) {
  if (sigilId >= MAX_PHYSICAL_SIGILS) return;
  Flow &f = flows[sigilId]; auto &p = f.page;
  if (p.stage == CommanderStage::Closed) { f.sent = false; return; }
  if (!eligible(f)) { close(f); return; }
  refresh(f);
  const uint8_t key = commanderKey(value);
  if (commanderKeyRevision(value) != p.revision || key > 4) { f.sent = false; return; }
  f.lastKey = nowMs;
  // Keys use the shared five-key order: Up, Down, Left, Right, Select.
  if (p.stage == CommanderStage::Result) { close(f); return; }
  if (key == 1) { close(f); return; } // Down cancels at every stage.
  if (key == 0) {
    if (p.stage == CommanderStage::Source || p.stage == CommanderStage::UndoConfirm) close(f);
    else {
      p.stage = static_cast<CommanderStage>(static_cast<uint8_t>(p.stage)-1);
      if (p.stage == CommanderStage::Commander && !table().game.hasPartner(p.source)) p.stage = CommanderStage::Source;
      changed(f);
    }
    return;
  }
  switch (p.stage) {
    case CommanderStage::Source:
      if (key == 2 || key == 3) {
        p.source = nextSource(p.recipient, p.source, key == 2 ? -1 : 1);
        name(p.source,p.sourceName); changed(f); refresh(f);
      } else if (key == 4) {
        // Without partners there is one commander: straight to the amount.
        if (table().game.hasPartner(p.source)) p.stage = CommanderStage::Commander;
        else { p.commander = 1; p.stage = CommanderStage::Amount; refresh(f); }
        changed(f);
      }
      break;
    case CommanderStage::Commander:
      if (key == 2 || key == 3) { p.commander = p.commander == 1 ? 2 : 1; changed(f); refresh(f); }
      else if (key == 4) { p.stage = CommanderStage::Amount; changed(f); }
      break;
    case CommanderStage::Amount:
      if (key == 2 && p.amount > 1) { --p.amount; changed(f); }
      else if (key == 3 && p.amount < 9999) { ++p.amount; changed(f); }
      else if (key == 4) { p.stage = CommanderStage::Confirm; changed(f); }
      break;
    case CommanderStage::Confirm:
    case CommanderStage::UndoConfirm:
      if (key == 4) apply(f,p.stage == CommanderStage::UndoConfirm);
      break;
    default: break;
  }
}
CommanderFlowPacket commanderPage(uint8_t sigilId) {
  return sigilId < MAX_PHYSICAL_SIGILS ? flows[sigilId].page : CommanderFlowPacket{};
}
void invalidateCommanderPicker(uint8_t sigilId) {
  if (sigilId < MAX_PHYSICAL_SIGILS) flows[sigilId].sent = false;
}
void resetCommanderPickers() {
  for (uint8_t id=0;id<MAX_PHYSICAL_SIGILS;++id) {
    const uint16_t revision = flows[id].page.revision + 1;
    flows[id] = Flow{}; auto &p = flows[id].page;
    p.version = VERSION; p.type = PacketType::CommanderFlow; p.sigilId = id; p.revision = revision;
  }
}
void syncCommanderPickers(uint32_t nowMs) {
  for (uint8_t id=0;id<MAX_PHYSICAL_SIGILS;++id) {
    TableScope scope(tableForController(id));
    Flow &f = flows[id];
    if (!f.page.version) { f.page.version = VERSION; f.page.type = PacketType::CommanderFlow; f.page.sigilId = id; }
    if (f.page.stage != CommanderStage::Closed) {
      if (!eligible(f) || !sigilBus.isOnline(id,nowMs) || nowMs-f.lastKey >= COMMANDER_IDLE_MS) close(f);
      else refresh(f);
    }
    if (!sigilBus.isOnline(id,nowMs)) { f.sent = false; continue; }
    if (!f.sent && sigilBus.sendCommanderFlow(f.page)) f.sent = true;
  }
}
}
