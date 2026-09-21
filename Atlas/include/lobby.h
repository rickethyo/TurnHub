#pragma once

#include <Arduino.h>

#include "turnhub_types.h"

namespace TurnHub {

class Lobby {
 public:
  Lobby();

  bool isJoined(uint8_t controllerId) const;
  bool hasSecondary(uint8_t controllerId) const;
  uint8_t hostController() const;
  uint8_t joinedControllerCount() const;
  uint8_t playerCount() const;

  uint8_t join(uint8_t controllerId);
  bool leave(uint8_t controllerId);
  bool replaceController(uint8_t oldController, uint8_t newController);
  bool toggleSecondary(uint8_t controllerId, bool &added, PlayerSeat &affected);

  uint8_t playerNumber(uint8_t controllerId, uint8_t slot = 1) const;
  uint8_t playersForController(uint8_t controllerId, PlayerSeat *out, uint8_t capacity) const;
  uint8_t buildPlayers(PlayerSeat *out, uint8_t capacity) const;

  bool selectStarter(uint8_t controllerId, PlayerSeat &selected);
  bool selectStarterSeat(uint8_t controllerId, uint8_t slot, PlayerSeat &selected);
  bool randomStarter(PlayerSeat &selected);
  bool starterOrDefault(PlayerSeat &selected) const;
  bool selectedStarter(PlayerSeat &selected) const;

  void resetEmpty();
  void resetForRematch();

  void setHeld(uint8_t controllerId, bool held);
  bool isHeld(uint8_t controllerId) const;
  bool anyOtherHeld(uint8_t controllerId) const;

  void setStartArmedBy(uint8_t controllerId);
  void clearStartArm();
  uint8_t startArmedBy() const;

  void setSharedChord(uint8_t controllerId, bool value);
  bool sharedChord(uint8_t controllerId) const;

  void setSuppressNextShort(uint8_t controllerId, bool value);
  bool consumeSuppressNextShort(uint8_t controllerId);

  void setActionLong(uint8_t controllerId, bool value);
  bool actionLong(uint8_t controllerId) const;

 private:
  int joinedIndex(uint8_t controllerId) const;
  bool validModule(uint8_t controllerId) const;

  uint8_t joinedOrder_[MAX_CONTROLLERS];
  uint8_t joinedCount_ = 0;
  bool secondary_[MAX_CONTROLLERS] = {};
  uint32_t participants_[MAX_CONTROLLERS][2] = {};
  uint32_t nextParticipant_ = 1;

  bool starterSelected_ = false;
  uint8_t starterModule_ = INVALID_ID;
  uint8_t starterSlot_ = 1;

  bool held_[MAX_CONTROLLERS] = {};
  bool sharedChord_[MAX_CONTROLLERS] = {};
  bool suppressNextShort_[MAX_CONTROLLERS] = {};
  bool actionLong_[MAX_CONTROLLERS] = {};

  uint8_t startArmedBy_ = INVALID_ID;
};

}  // namespace TurnHub
