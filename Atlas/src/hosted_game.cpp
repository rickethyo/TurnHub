#include "hosted_game.h"

#include "turnhub_json.h"

#include <cstdio>

namespace TurnHubHosted {

Claim HostedTable::claim(const std::string &token, const std::string &profileId, const std::string &name, uint32_t nowMs) {
  if (token.empty()) return Claim::Busy;
  if (leaseLive(nowMs) && lease_.token != token) return Claim::Busy;
  if (lease_.token != token) {
    // A new hub starts clean: the last hub's game and inputs are not its own,
    // though it may resume that game (resumeJson).
    endLease();
    lease_.held = true;
    lease_.token = token;
  }
  lease_.profileId = profileId;
  lease_.name = name.substr(0, turnhub::Game::MAX_NAME);
  lease_.renewedAtMs = nowMs;
  return Claim::Granted;
}

bool HostedTable::release(const std::string &token) {
  if (!lease_.held || lease_.token != token) return false;
  endLease();
  return true;
}

namespace {
bool unfinished(const turnhub::Game &game) {
  return !game.gameId.empty() && game.state != turnhub::TableState::Lobby && game.state != turnhub::TableState::GameOver;
}
const std::string NO_HOST;
}  // namespace

void HostedTable::endLease() {
  if (hasGame() && unfinished(game_)) {
    resume_ = state_;
    resumeHost_ = lease_.name;
    resumeGameId_ = game_.gameId;
  }
  lease_ = Lease();
  game_ = turnhub::Game();
  state_.clear();
  inputs_.clear();
}

std::string HostedTable::resumeJson(uint32_t nowMs) const {
  if (leaseLive(nowMs)) return std::string();
  // A hub that went quiet still has its game here.
  if (hasGame() && unfinished(game_)) return state_;
  return resume_;
}

const std::string &HostedTable::resumeHost(uint32_t nowMs) const {
  if (leaseLive(nowMs)) return NO_HOST;
  if (hasGame() && unfinished(game_)) return lease_.name;
  return resume_.empty() ? NO_HOST : resumeHost_;
}

bool HostedTable::holds(const std::string &token, uint32_t nowMs) const {
  return !token.empty() && leaseLive(nowMs) && lease_.token == token;
}

bool HostedTable::live(uint32_t nowMs) const { return leaseLive(nowMs) && hasGame(); }

bool HostedTable::publish(const std::string &token, const std::string &stateJson, uint32_t nowMs, std::string &error) {
  if (!holds(token, nowMs)) { error = "Another phone hosts this table"; return false; }
  if (stateJson.empty() || stateJson.size() > MAX_STATE_BYTES) { error = "That game is too large"; return false; }
  bool ok = false;
  const turnhub::Json json = turnhub::Json::parse(stateJson, &ok);
  if (!ok || !json.isObject() || !json.get("gameId").isString() ||
      !json.get("revision").isNumber() || json.get("revision").asInt() < 0) {
    error = "That game is not valid";
    return false;
  }
  const std::string gameId = json.get("gameId").asString();
  const int64_t revision = json.get("revision").asInt();
  // Setup between games has no gameId yet, and its revision may restart.
  if (hasGame() && !gameId.empty() && gameId == game_.gameId) {
    if (revision < game_.revision || (revision == game_.revision && stateJson != state_)) {
      error = "An older game state";
      return false;
    }
  }
  turnhub::Game next = turnhub::gameFromJson(json);
  if (next.players.size() > static_cast<size_t>(turnhub::Game::MAX_PLAYERS)) { error = "That game is not valid"; return false; }
  if (!hasGame() || gameId != game_.gameId) inputs_.clear();
  // The game to resume is back with a hub (this one resumed it, or its own hub came back).
  if (!gameId.empty() && gameId == resumeGameId_) { resume_.clear(); resumeHost_.clear(); resumeGameId_.clear(); }
  game_ = std::move(next);
  state_ = stateJson;
  lease_.renewedAtMs = nowMs;
  return true;
}

uint32_t HostedTable::queue(int player, const std::string &id, const std::string &actionJson, uint32_t nowMs) {
  if (!live(nowMs) || id.empty() || id.size() > 40 || actionJson.empty() || actionJson.size() > MAX_INPUT_BYTES) return 0;
  if (player < 0 || player > static_cast<int>(game_.players.size())) return 0;
  bool ok = false;
  if (!turnhub::Json::parse(actionJson, &ok).isObject() || !ok) return 0;
  for (const Input &input : inputs_) if (input.id == id) return input.seq;
  if (inputs_.size() >= MAX_INPUTS) inputs_.erase(inputs_.begin());
  Input input;
  input.seq = nextSeq_++;
  input.id = id;
  input.player = player;
  input.action = actionJson;
  inputs_.push_back(input);
  return input.seq;
}

std::vector<Input> HostedTable::inputsAfter(uint32_t seq) const {
  std::vector<Input> out;
  for (const Input &input : inputs_) if (input.seq > seq) out.push_back(input);
  return out;
}

void HostedTable::clear() {
  endLease();
  resume_.clear();
  resumeHost_.clear();
  resumeGameId_.clear();
}

std::string sigilLocalId(uint8_t sigilId, uint8_t slot) {
  return "sigil:" + std::to_string(sigilId) + ":" + std::to_string(slot);
}

int sigilPlayer(const turnhub::Game &game, uint8_t sigilId, uint8_t slot) {
  const std::string id = sigilLocalId(sigilId, slot);
  for (size_t i = 0; i < game.players.size(); ++i) if (game.players[i].localId == id) return static_cast<int>(i);
  return -1;
}

uint8_t sigilPlayers(const turnhub::Game &game, uint8_t sigilId, int out[2]) {
  uint8_t count = 0;
  for (uint8_t slot = 1; slot <= 2; ++slot) {
    const int index = sigilPlayer(game, sigilId, slot);
    if (index >= 0) out[count++] = index;
  }
  return count;
}

bool playerSigil(const turnhub::Game &game, int index, uint8_t &sigilId, uint8_t &slot) {
  if (index < 0 || index >= static_cast<int>(game.players.size())) return false;
  const std::string &id = game.players[index].localId;
  unsigned parsedId = 0, parsedSlot = 0;
  char tail = 0;
  if (id.rfind("sigil:", 0) != 0 || sscanf(id.c_str() + 6, "%u:%u%c", &parsedId, &parsedSlot, &tail) != 2 ||
      parsedId > 255 || parsedSlot < 1 || parsedSlot > 2) return false;
  sigilId = static_cast<uint8_t>(parsedId);
  slot = static_cast<uint8_t>(parsedSlot);
  return true;
}

HostedTable &hostedTable() {
  static HostedTable table;
  return table;
}

}  // namespace TurnHubHosted
