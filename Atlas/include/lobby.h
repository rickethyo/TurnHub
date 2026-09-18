#pragma once

#include <Arduino.h>

#include "turnhub_types.h"

namespace TurnHub {

class Lobby {
 public:
  Lobby();

  bool isJoined(uint8_t moduleId) const;
  bool hasSecondary(uint8_t moduleId) const;
  uint8_t hostModule() const;
  uint8_t joinedModuleCount() const;
  uint8_t playerCount() const;

  uint8_t join(uint8_t moduleId);
  bool leave(uint8_t moduleId);
  bool toggleSecondary(uint8_t moduleId, bool &added, PlayerSeat &affected);

  uint8_t playerNumber(uint8_t moduleId, uint8_t slot = 1) const;
  uint8_t playersForModule(uint8_t moduleId, PlayerSeat *out, uint8_t capacity) const;
  uint8_t buildPlayers(PlayerSeat *out, uint8_t capacity) const;

  bool selectStarter(uint8_t moduleId, PlayerSeat &selected);
  bool selectStarterSeat(uint8_t moduleId, uint8_t slot, PlayerSeat &selected);
  bool randomStarter(PlayerSeat &selected);
  bool starterOrDefault(PlayerSeat &selected) const;
  bool selectedStarter(PlayerSeat &selected) const;

  void resetEmpty();
  void resetForRematch();

  void setHeld(uint8_t moduleId, bool held);
  bool isHeld(uint8_t moduleId) const;
  bool anyOtherHeld(uint8_t moduleId) const;

  void setStartArmedBy(uint8_t moduleId);
  void clearStartArm();
  uint8_t startArmedBy() const;

  void setSharedChord(uint8_t moduleId, bool value);
  bool sharedChord(uint8_t moduleId) const;

  void setSuppressNextShort(uint8_t moduleId, bool value);
  bool consumeSuppressNextShort(uint8_t moduleId);

  void setActionLong(uint8_t moduleId, bool value);
  bool actionLong(uint8_t moduleId) const;

 private:
  int joinedIndex(uint8_t moduleId) const;
  bool validModule(uint8_t moduleId) const;

  uint8_t joinedOrder_[MAX_PHYSICAL_SIGILS];
  uint8_t joinedCount_ = 0;
  bool secondary_[MAX_PHYSICAL_SIGILS] = {};

  bool starterSelected_ = false;
  uint8_t starterModule_ = INVALID_ID;
  uint8_t starterSlot_ = 1;

  bool held_[MAX_PHYSICAL_SIGILS] = {};
  bool sharedChord_[MAX_PHYSICAL_SIGILS] = {};
  bool suppressNextShort_[MAX_PHYSICAL_SIGILS] = {};
  bool actionLong_[MAX_PHYSICAL_SIGILS] = {};

  uint8_t startArmedBy_ = INVALID_ID;
};

}  // namespace TurnHub
