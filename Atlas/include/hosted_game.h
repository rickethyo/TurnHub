#pragma once

// Phone-hosted games (plans/phone-hosted-game.md, owner 2026-11-04).
//
// A hub phone or tablet runs the game with the shared rules core and pushes
// each new state here. Atlas never decides anything about a hosted game: it
// keeps the latest state to show on its screen and Sigils, queues the inputs
// its own controls make for the hub to apply, and lets one hub at a time hold
// the table (the lease). Pure logic, host-tested; the HTTP routes are in
// web_hosted_api.cpp. Contract: protocol/hosted-v1.md.

#include <cstdint>
#include <string>
#include <vector>

#include "turnhub_core.h"

namespace TurnHubHosted {

// A hub that stops pushing (backgrounded, out of range) loses the table after
// this long; any push renews it.
constexpr uint32_t LEASE_MS = 30000;
constexpr size_t MAX_STATE_BYTES = 65536;
constexpr uint8_t MAX_INPUTS = 32;
constexpr size_t MAX_INPUT_BYTES = 512;

struct Lease {
  bool held = false;
  std::string token;      // The hub's web session.
  std::string profileId;  // Empty for the shared table tablet.
  std::string name;       // Shown on Atlas: "Hosted by ...".
  uint32_t renewedAtMs = 0;
};

// One input from Atlas's own controls (a Sigil press, an Atlas screen tap),
// for the hub to apply through its seat-action door. Atlas names the seat;
// the hub still checks it against the game.
struct Input {
  uint32_t seq = 0;
  std::string id;      // Action UUID: the hub applies each once.
  int player = 0;      // 1-based player number, 0 for a table (lobby) action.
  std::string action;  // The core's action JSON.
};

enum class Claim : uint8_t { Granted, Busy };

class HostedTable {
 public:
  // Take or renew the table. Another live hub's lease is never taken over.
  Claim claim(const std::string &token, const std::string &profileId, const std::string &name, uint32_t nowMs);
  // Only the holder releases; the queue goes with it, and an unfinished game
  // stays for another phone to resume (resumeJson).
  bool release(const std::string &token);
  // An Admin's Return to lobby: ends whoever's lease, like release().
  void releaseAny() { endLease(); }
  bool holds(const std::string &token, uint32_t nowMs) const;
  // A hub holds the table and has pushed a game.
  bool live(uint32_t nowMs) const;

  // The holder's new state (the core's game JSON). A newer revision of the
  // same game replaces it; the same revision is accepted only unchanged
  // (a retry); a different gameId starts over, and setup (no gameId yet) is
  // always taken. Renews the lease.
  bool publish(const std::string &token, const std::string &stateJson, uint32_t nowMs, std::string &error);

  const Lease &lease() const { return lease_; }
  const turnhub::Game &game() const { return game_; }
  const std::string &stateJson() const { return state_; }
  bool hasGame() const { return !state_.empty(); }

  // Atlas's own controls. Returns the input's seq, or 0 when the table has
  // no hub or the input is not valid. The oldest input drops when full.
  uint32_t queue(int player, const std::string &id, const std::string &actionJson, uint32_t nowMs);
  // Inputs after [seq] for the holder, oldest first.
  std::vector<Input> inputsAfter(uint32_t seq) const;
  uint32_t lastSeq() const { return nextSeq_ - 1; }

  // The last hub's unfinished game (started, not over) once no hub holds the
  // table, for another signed-in phone to resume; empty when there is none.
  // A hub that pushes the same gameId takes it back.
  std::string resumeJson(uint32_t nowMs) const;
  const std::string &resumeHost(uint32_t nowMs) const;

  // Forgets everything, including a game to resume.
  void clear();

 private:
  // Ends the lease and queue; keeps an unfinished game to resume.
  void endLease();
  bool leaseLive(uint32_t nowMs) const { return lease_.held && nowMs - lease_.renewedAtMs < LEASE_MS; }
  Lease lease_;
  turnhub::Game game_;
  std::string state_;
  std::vector<Input> inputs_;
  uint32_t nextSeq_ = 1;
  std::string resume_;      // The last hub's unfinished game, after a new hub took the table.
  std::string resumeHost_;
  std::string resumeGameId_;
};

// Game 1's hosted table (the Atlas screen and Sigils follow Game 1).
HostedTable &hostedTable();

// Sigil seats: the hub binds a player to a Sigil using sigilSeat
// "sigil:<id>:<slot>" (slot 1 = seat A, 2 = seat B), separate from localId.
// Old snapshots using localId for a Sigil seat remain readable. Atlas never edits it.
std::string sigilLocalId(uint8_t sigilId, uint8_t slot);
// The 0-based index of the player on that Sigil seat, or -1.
int sigilPlayer(const turnhub::Game &game, uint8_t sigilId, uint8_t slot);
// The players on a Sigil (seat A first), as 0-based indexes; returns the count.
uint8_t sigilPlayers(const turnhub::Game &game, uint8_t sigilId, int out[2]);
// The Sigil seat of a 0-based player index, or false.
bool playerSigil(const turnhub::Game &game, int index, uint8_t &sigilId, uint8_t &slot);

}  // namespace TurnHubHosted
