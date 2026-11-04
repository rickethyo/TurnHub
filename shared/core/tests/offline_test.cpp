// Offline play: several phones' queues merged and replayed on Atlas
// (turnhub_offline.h). Run by shared/core/tests/run-linux.sh.

#include <cstdio>
#include <string>
#include <vector>

#include "turnhub_offline.h"

using turnhub::Game;
using turnhub::Json;
using turnhub::OfflineEntry;

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    ++failures;
    std::printf("FAIL %s\n", what);
  }
}

class ZeroRandom : public turnhub::Random {
 public:
  int nextInt(int) override { return 0; }
};

Json act(const char *doName) {
  Json a = Json::object();
  a.set("do", doName);
  return a;
}

// Four players, A starts at t=1,000,000.
Game started(int64_t timerMs = 0) {
  ZeroRandom random;
  Game g;
  for (const char *name : {"A", "B", "C", "D"}) {
    Json a = act("addPlayer");
    a.set("name", name);
    g = turnhub::applyAction(g, a, random);
  }
  if (timerMs) {
    Json t = act("setTurnTimer");
    t.set("ms", timerMs);
    g = turnhub::applyAction(g, t, random);
    Json r = act("setTimerRules");
    r.set("timeout", "PASS_AND_DEMERIT");
    r.set("earnExtensions", false);
    r.set("earnBeforePercent", 75);
    r.set("maxEarnedPerGame", 2);
    g = turnhub::applyAction(g, r, random);
  }
  Json s = act("start");
  s.set("nowMs", int64_t(1000000));
  s.set("gameId", "m1");
  return turnhub::applyAction(g, s, random);
}

OfflineEntry entry(const char *device, int64_t seq, int64_t atMs, Json action) {
  OfflineEntry e;
  e.device = device;
  e.seq = seq;
  e.atMs = atMs;
  e.action = std::move(action);
  return e;
}

Json pass(int index) {
  Json a = act("pass");
  a.set("index", index);
  return a;
}

Json life(int index, int delta) {
  Json a = act("changeLife");
  a.set("index", index);
  a.set("delta", delta);
  return a;
}

void mergesPhonesInTimeOrder() {
  ZeroRandom random;
  const Game base = started();
  check(base.active && *base.active == 0, "A starts");
  // Phone "b" saw A pass first; phone "a" queued B's pass after it. Sent in
  // the other order, the merged game still runs A then B.
  std::vector<OfflineEntry> entries = {
      entry("a", 1, 1003000, pass(1)),
      entry("a", 2, 1004000, life(2, -3)),
      entry("b", 1, 1002000, pass(0)),
      entry("b", 2, 1005000, pass(0)),  // A's turn is long over: refused.
      entry("a", 1, 1003000, pass(1)),  // Sent twice: counted once.
  };
  const auto out = turnhub::reconcile(base, entries, 1006000, random);
  check(out.outcomes.size() == 4, "duplicate left out");
  check(out.outcomes[0].device == "b" && out.outcomes[0].applied, "A's pass first");
  check(out.outcomes[1].device == "a" && out.outcomes[1].applied, "then B's pass");
  check(out.outcomes[2].applied, "life change applied");
  check(!out.outcomes[3].applied, "stale pass refused");
  check(out.game.active && *out.game.active == 2, "C is up");
  check(out.game.players[2].life == 37, "C at 37");
  check(out.game.players[0].turnsCompleted == 1 && out.game.players[1].turnsCompleted == 1, "turns counted");
}

void timeoutsFallDueBetweenEntries() {
  ZeroRandom random;
  const Game base = started(60000);
  // Nobody touched A's turn; the timer passed it at 1,060,000, so B's pass
  // at 1,070,000 is B's own turn in the merged game.
  const auto out = turnhub::reconcile(base, {entry("a", 1, 1070000, pass(1))}, 1080000, random);
  check(out.outcomes.size() == 1 && out.outcomes[0].applied, "B passes after A timed out");
  check(out.game.players[0].demerits == 1, "A's demerit");
  check(out.game.active && *out.game.active == 2, "C is up");
  // And once more at untilMs: C's turn began at 1,070,000 and times out at 1,130,000.
  const auto later = turnhub::reconcile(base, {entry("a", 1, 1070000, pass(1))}, 1131000, random);
  check(later.game.active && *later.game.active == 3, "C timed out by now");
}

void seatActionsActOnTheMergedGame() {
  ZeroRandom random;
  const Game base = started();
  Json seat = act("seat");
  seat.set("match", "m1");
  seat.set("revision", int64_t(999));  // The phone's own copy.
  seat.set("player", 1);
  seat.set("kind", "life");
  seat.set("value", -2);
  Json wrong = seat;
  wrong.set("match", "other");
  Json rematch = act("rematch");
  rematch.set("gameId", "m2");
  const auto out = turnhub::reconcile(base, {entry("a", 1, 1001000, seat), entry("a", 2, 1002000, wrong),
      entry("a", 3, 1002500, rematch)}, 1003000, random);
  check(out.outcomes[0].applied && out.game.players[0].life == 38, "seat life applied");
  check(!out.outcomes[1].applied, "another match refused");
  check(!out.outcomes[2].applied && out.game.gameId == "m1", "a rematch waits for Atlas");
}

void rebaseMovesOnlyTheClock() {
  const Game base = started();
  const Game moved = turnhub::rebased(base, 5000);
  check(moved.runningSinceMs && *moved.runningSinceMs == *base.runningSinceMs + 5000, "running since moves");
  check(moved.gameElapsedMs(1010000 + 5000) == base.gameElapsedMs(1010000), "elapsed kept");
}

void entriesFromJson() {
  OfflineEntry e;
  const Json ok = Json::parse("{\"seq\":3,\"atMs\":1200,\"action\":{\"do\":\"pass\",\"index\":0},\"draws\":[1,0]}");
  check(turnhub::offlineEntryFromJson(ok, "tab", e) && e.seq == 3 && e.atMs == 1200 && e.draws.size() == 2, "entry parsed");
  check(!turnhub::offlineEntryFromJson(Json::parse("{\"seq\":0,\"atMs\":1,\"action\":{\"do\":\"pass\"}}"), "tab", e), "seq from 1");
  check(!turnhub::offlineEntryFromJson(Json::parse("{\"seq\":1,\"atMs\":1}"), "tab", e), "action required");
}

}  // namespace

int main() {
  mergesPhonesInTimeOrder();
  timeoutsFallDueBetweenEntries();
  seatActionsActOnTheMergedGame();
  rebaseMovesOnlyTheClock();
  entriesFromJson();
  if (failures) return 1;
  std::printf("offline: all passed\n");
  return 0;
}
