#pragma once

#include <Arduino.h>

#include "turnhub_types.h"

namespace TurnHub {

// Who is at the table before a game starts. Controllers join in order; each
// has seat A (slot 1) and, for physical Sigils, an optional seat B (slot 2).
// Player numbers follow controller order and each shared Sigil's A/B order.
// They renumber as seats change.
class Lobby {
 public:
  Lobby();

  bool isJoined(uint8_t controllerId) const;
  bool hasSecondary(uint8_t controllerId) const;
  // First joined controller, or INVALID_ID when the table is empty.
  uint8_t hostController() const;
  uint8_t joinedControllerCount() const;
  uint8_t playerCount() const;

  // Seats controller's seat A; returns its player number (existing number if
  // already joined, 0 when full or invalid).
  uint8_t join(uint8_t controllerId);
  bool leave(uint8_t controllerId);
  // Moves a joined seat A (and its participant) to a different controller.
  bool replaceController(uint8_t oldController, uint8_t newController);
  // Adds or removes seat B; reports which and the affected seat.
  bool toggleSecondary(uint8_t controllerId, bool &added, PlayerSeat &affected);
  // Turn order: moves a joined controller (its seat B with it) one place
  // earlier (-1) or later (+1). Player numbers follow the new order; the
  // chosen starter stays the same seat. False at either end.
  bool moveController(uint8_t controllerId, int8_t direction);
  // Moving toward the other seat swaps A/B; otherwise moves the whole Sigil.
  bool moveSeat(uint8_t controllerId, uint8_t slot, int8_t direction);
  bool setSecondaryFirst(uint8_t controllerId, bool first);
  bool secondaryFirst(uint8_t controllerId) const;

  // 0 when that seat is not at the table.
  uint8_t playerNumber(uint8_t controllerId, uint8_t slot = 1) const;
  uint8_t playersForController(uint8_t controllerId, PlayerSeat *out, uint8_t capacity) const;
  // Every seat in table order; returns the count.
  uint8_t buildPlayers(PlayerSeat *out, uint8_t capacity) const;

  // Selects the controller's seat A, or alternates A/B on repeat selection.
  bool selectStarter(uint8_t controllerId, PlayerSeat &selected);
  bool selectStarterSeat(uint8_t controllerId, uint8_t slot, PlayerSeat &selected);
  bool randomStarter(PlayerSeat &selected);
  // The selected starter, else the first seat at the table.
  bool starterOrDefault(PlayerSeat &selected) const;
  bool selectedStarter(PlayerSeat &selected) const;

  void resetEmpty();
  // Rebuilds the table from a recovered match's seats.
  bool restorePlayers(const PlayerSeat *players, uint8_t count, uint8_t starter);
  // Keeps the seats; clears the starter and start arm.
  void resetForRematch();

  // The host controller that armed a start (INVALID_ID when unarmed).
  void setStartArmedBy(uint8_t controllerId);
  void clearStartArm();
  uint8_t startArmedBy() const;

 private:
  int joinedIndex(uint8_t controllerId) const;
  bool validModule(uint8_t controllerId) const;

  uint8_t joinedOrder_[MAX_CONTROLLERS];
  uint8_t joinedCount_ = 0;
  bool secondary_[MAX_CONTROLLERS] = {};
  bool secondaryFirst_[MAX_CONTROLLERS] = {};
  uint32_t participants_[MAX_CONTROLLERS][2] = {};
  uint32_t nextParticipant_ = 1;

  bool starterSelected_ = false;
  uint8_t starterModule_ = INVALID_ID;
  uint8_t starterSlot_ = 1;

  uint8_t startArmedBy_ = INVALID_ID;
};

}  // namespace TurnHub
