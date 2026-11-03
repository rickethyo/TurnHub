#pragma once

// Offline play (owner, 2026-11-03): when Atlas drops off mid-game, each phone
// keeps the game going on its own copy of the core and queues every action it
// applies, stamped on Atlas's game clock: the last Atlas time the phone knew
// plus the phone's own elapsed time since. When Atlas answers again each phone
// sends its queue; Atlas merges every phone's entries into one timeline and
// replays them through the same rules. An entry the rules refuse at its place
// in the merged game (a pass when that player's turn had already moved on, a
// change for a player who had left) is dropped and reported to its phone.

#include <cstdint>
#include <string>
#include <vector>

#include "turnhub_core.h"

namespace turnhub {

struct OfflineEntry {
  std::string device;   // The phone that queued it (any stable id).
  int64_t seq = 0;      // That phone's order, from 1.
  int64_t atMs = 0;     // On the base game's clock (the core's nowMs).
  Json action;          // An applyAction action; its nowMs is replaced by atMs.
  std::vector<int> draws;  // The random draws the phone made for it, replayed first.
};

struct OfflineOutcome {
  std::string device;
  int64_t seq = 0;
  bool applied = false;
};

struct Reconciled {
  Game game;
  std::vector<OfflineOutcome> outcomes;  // In replay order; duplicates are left out.
};

// Replays [entries] (any order, any number of phones) on [base] in time order:
// atMs, then device, then seq. A repeated (device, seq) counts once. Turn
// timeouts fall due between entries exactly as they would have live, and once
// more at [untilMs], when the merged game catches up with now. [random]
// supplies draws an entry didn't record. Seat actions are checked against the
// merged game's revision, not the phone's local one.
Reconciled reconcile(const Game &base, std::vector<OfflineEntry> entries, int64_t untilMs, Random &random);

// The same game on a clock [deltaMs] later: absolute times move, elapsed
// times stay. Used when the base came from before an Atlas restart.
Game rebased(const Game &game, int64_t deltaMs);

// JSON forms for the transports (protocol/http-v1.md, "Offline play").
bool offlineEntryFromJson(const Json &json, const std::string &device, OfflineEntry &out);
Json toJson(const OfflineOutcome &outcome);

}  // namespace turnhub
