#include "turnhub_offline.h"

#include <algorithm>
#include <set>
#include <utility>

namespace turnhub {

namespace {

// An entry's own draws first, then the table's.
class EntryRandom : public Random {
 public:
  EntryRandom(const std::vector<int> &draws, Random &fallback) : draws_(draws), fallback_(fallback) {}
  int nextInt(int bound) override {
    while (next_ < draws_.size()) {
      const int v = draws_[next_++];
      if (v >= 0 && v < bound) return v;
    }
    return fallback_.nextInt(bound);
  }

 private:
  const std::vector<int> &draws_;
  size_t next_ = 0;
  Random &fallback_;
};

// Applies every turn timeout that fell due up to [nowMs], each at its own
// deadline, so an automatic pass starts the next turn when it would have live.
Game catchUp(Game g, int64_t nowMs, Random &random) {
  for (int guard = 0; guard < 4 * Game::MAX_PLAYERS; ++guard) {
    if (!g.active || g.state != TableState::Running || g.turnTimerMs <= 0 || g.timeoutHandled || !g.runningSinceMs) break;
    const int64_t dueElapsed = g.turnStartedAtElapsedMs + g.turnTimerMs + g.turnExtensionMs;
    const int64_t dueAt = *g.runningSinceMs + (dueElapsed - g.elapsedMs);
    if (dueAt > nowMs) break;
    const Game next = g.applyTurnTimeout(std::max(dueAt, *g.runningSinceMs), &random);
    if (next.revision == g.revision) break;
    g = next;
  }
  return g;
}

}  // namespace

Reconciled reconcile(const Game &base, std::vector<OfflineEntry> entries, int64_t untilMs, Random &random) {
  std::stable_sort(entries.begin(), entries.end(), [](const OfflineEntry &a, const OfflineEntry &b) {
    if (a.atMs != b.atMs) return a.atMs < b.atMs;
    if (a.device != b.device) return a.device < b.device;
    return a.seq < b.seq;
  });
  Reconciled out{base, {}};
  std::set<std::pair<std::string, int64_t>> seen;
  for (const auto &entry : entries) {
    if (!seen.insert({entry.device, entry.seq}).second) continue;
    out.game = catchUp(out.game, entry.atMs, random);
    // Offline play ends with the match: starting, resetting or leaving it
    // waits for Atlas (its lobby and statistics own that step).
    const std::string &kind = entry.action.get("do").asString();
    if (kind == "start" || kind == "rematch" || kind == "seanModeRematch" || kind == "reset" || kind == "backToSetup" ||
        kind == "addPlayer" || kind == "removePlayer" || kind == "clearPlayers") {
      out.outcomes.push_back({entry.device, entry.seq, false});
      continue;
    }
    Json action = entry.action;
    action.set("nowMs", entry.atMs);
    // A seat action names the phone's own copy of the game; in the merged
    // game it acts on the game as it now stands (the match still has to match).
    if (action.get("do").asString() == "seat") action.set("revision", out.game.revision);
    EntryRandom draws(entry.draws, random);
    const Game next = applyAction(out.game, action, draws);
    const bool applied = next.revision != out.game.revision;
    if (applied) out.game = next;
    out.outcomes.push_back({entry.device, entry.seq, applied});
  }
  out.game = catchUp(out.game, untilMs, random);
  return out;
}

Game rebased(const Game &game, int64_t deltaMs) {
  Game g = game;
  g.startedAtMs += deltaMs;
  if (g.runningSinceMs) *g.runningSinceMs += deltaMs;
  return g;
}

bool offlineEntryFromJson(const Json &json, const std::string &device, OfflineEntry &out) {
  if (!json.isObject() || !json.get("seq").isNumber() || !json.get("atMs").isNumber() ||
      !json.get("action").isObject() || !json.get("action").get("do").isString()) return false;
  out = OfflineEntry{};
  out.device = device;
  out.seq = json.get("seq").asInt();
  out.atMs = json.get("atMs").asInt();
  out.action = json.get("action");
  const Json &draws = json.get("draws");
  for (size_t i = 0; i < draws.size() && i < 64; ++i) out.draws.push_back(static_cast<int>(draws[i].asInt()));
  return out.seq > 0;
}

Json toJson(const OfflineOutcome &outcome) {
  Json j = Json::object();
  j.set("device", outcome.device);
  j.set("seq", outcome.seq);
  j.set("applied", outcome.applied);
  return j;
}

}  // namespace turnhub
