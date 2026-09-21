#include "lobby.h"

#include <esp_system.h>

namespace TurnHub {

Lobby::Lobby() {
  resetEmpty();
}

bool Lobby::validModule(uint8_t controllerId) const {
  return controllerId < MAX_CONTROLLERS;
}

int Lobby::joinedIndex(uint8_t controllerId) const {
  for (uint8_t i = 0; i < joinedCount_; ++i) {
    if (joinedOrder_[i] == controllerId) {
      return i;
    }
  }
  return -1;
}

bool Lobby::isJoined(uint8_t controllerId) const {
  return joinedIndex(controllerId) >= 0;
}

bool Lobby::hasSecondary(uint8_t controllerId) const {
  return validModule(controllerId) && secondary_[controllerId];
}

uint8_t Lobby::hostController() const {
  return joinedCount_ > 0 ? joinedOrder_[0] : INVALID_ID;
}

uint8_t Lobby::joinedControllerCount() const {
  return joinedCount_;
}

uint8_t Lobby::playerCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < joinedCount_; ++i) {
    const uint8_t module = joinedOrder_[i];
    ++count;
    if (secondary_[module]) {
      ++count;
    }
  }
  return count;
}

uint8_t Lobby::join(uint8_t controllerId) {
  if (!validModule(controllerId)) {
    return 0;
  }

  const uint8_t existing = playerNumber(controllerId, 1);
  if (existing != 0) {
    return existing;
  }

  if (joinedCount_ >= MAX_CONTROLLERS || playerCount() >= MAX_PLAYERS) {
    return 0;
  }

  participants_[controllerId][0] = nextParticipant_++;
  joinedOrder_[joinedCount_++] = controllerId;
  return playerNumber(controllerId, 1);
}

bool Lobby::leave(uint8_t controllerId) {
  const int index = joinedIndex(controllerId);
  if (index < 0) {
    return false;
  }

  for (uint8_t i = static_cast<uint8_t>(index); i + 1 < joinedCount_; ++i) {
    joinedOrder_[i] = joinedOrder_[i + 1];
  }

  --joinedCount_;
  secondary_[controllerId] = false;
  held_[controllerId] = false;
  sharedChord_[controllerId] = false;
  suppressNextShort_[controllerId] = false;
  actionLong_[controllerId] = false;

  if (starterSelected_ && starterModule_ == controllerId) {
    starterSelected_ = false;
    starterModule_ = INVALID_ID;
    starterSlot_ = 1;
  }

  if (startArmedBy_ == controllerId) {
    startArmedBy_ = INVALID_ID;
  }

  return true;
}

bool Lobby::replaceController(uint8_t oldController, uint8_t newController) {
  const int index = joinedIndex(oldController);
  if (index < 0 || !validModule(newController) || isJoined(newController) ||
      hasSecondary(oldController)) return false;
  joinedOrder_[index] = newController;
  participants_[newController][0] = participants_[oldController][0];
  participants_[oldController][0] = 0;
  if (starterSelected_ && starterModule_ == oldController) starterModule_ = newController;
  clearStartArm();
  return true;
}

uint8_t Lobby::buildPlayers(PlayerSeat *out, uint8_t capacity) const {
  if (out == nullptr || capacity == 0) {
    return 0;
  }

  uint8_t count = 0;
  uint8_t number = 1;

  for (uint8_t i = 0; i < joinedCount_ && count < capacity; ++i) {
    const uint8_t module = joinedOrder_[i];
    out[count] = PlayerSeat{number++, module, 1};
    out[count++].participantId = participants_[module][0];

    if (secondary_[module] && count < capacity) {
      out[count] = PlayerSeat{number++, module, 2};
      out[count++].participantId = participants_[module][1];
    }
  }

  return count;
}

uint8_t Lobby::playersForController(
    uint8_t controllerId,
    PlayerSeat *out,
    uint8_t capacity) const {
  if (!isJoined(controllerId) || out == nullptr || capacity == 0) {
    return 0;
  }

  uint8_t count = 0;
  const uint8_t primary = playerNumber(controllerId, 1);
  if (primary != 0 && count < capacity) {
    out[count] = PlayerSeat{primary, controllerId, 1};
    out[count++].participantId = participants_[controllerId][0];
  }

  if (secondary_[controllerId] && count < capacity) {
    const uint8_t second = playerNumber(controllerId, 2);
    if (second != 0) {
      out[count] = PlayerSeat{second, controllerId, 2};
      out[count++].participantId = participants_[controllerId][1];
    }
  }

  return count;
}

uint8_t Lobby::playerNumber(uint8_t controllerId, uint8_t slot) const {
  uint8_t number = 1;

  for (uint8_t i = 0; i < joinedCount_; ++i) {
    const uint8_t module = joinedOrder_[i];

    if (module == controllerId) {
      if (slot == 1) {
        return number;
      }
      if (slot == 2 && secondary_[module]) {
        return static_cast<uint8_t>(number + 1);
      }
      return 0;
    }

    ++number;
    if (secondary_[module]) {
      ++number;
    }
  }

  return 0;
}

bool Lobby::toggleSecondary(
    uint8_t controllerId,
    bool &added,
    PlayerSeat &affected) {
  if (!isJoined(controllerId)) {
    return false;
  }

  if (secondary_[controllerId]) {
    const uint8_t oldNumber = playerNumber(controllerId, 2);
    affected = PlayerSeat{oldNumber, controllerId, 2};
    secondary_[controllerId] = false;
    added = false;

    if (starterSelected_ && starterModule_ == controllerId && starterSlot_ == 2) {
      starterSelected_ = false;
      starterModule_ = INVALID_ID;
      starterSlot_ = 1;
    }

    return true;
  }

  if (controllerId >= MAX_PHYSICAL_SIGILS || playerCount() >= MAX_PLAYERS) return false;
  participants_[controllerId][1] = nextParticipant_++;
  secondary_[controllerId] = true;
  added = true;
  affected = PlayerSeat{playerNumber(controllerId, 2), controllerId, 2};
  return true;
}

bool Lobby::selectedStarter(PlayerSeat &selected) const {
  if (!starterSelected_) {
    return false;
  }

  const uint8_t number = playerNumber(starterModule_, starterSlot_);
  if (number == 0) {
    return false;
  }

  selected = PlayerSeat{number, starterModule_, starterSlot_};
  return true;
}

bool Lobby::selectStarter(uint8_t controllerId, PlayerSeat &selected) {
  if (!isJoined(controllerId)) {
    return false;
  }

  uint8_t nextSlot = 1;
  if (starterSelected_ && starterModule_ == controllerId && secondary_[controllerId]) {
    nextSlot = starterSlot_ == 1 ? 2 : 1;
  }

  starterSelected_ = true;
  starterModule_ = controllerId;
  starterSlot_ = nextSlot;

  return selectedStarter(selected);
}

bool Lobby::selectStarterSeat(
    uint8_t controllerId,
    uint8_t slot,
    PlayerSeat &selected) {
  if (!isJoined(controllerId) || (slot != 1 && slot != 2)) {
    return false;
  }
  if (slot == 2 && !secondary_[controllerId]) {
    return false;
  }

  starterSelected_ = true;
  starterModule_ = controllerId;
  starterSlot_ = slot;
  return selectedStarter(selected);
}

bool Lobby::randomStarter(PlayerSeat &selected) {
  PlayerSeat players[MAX_PLAYERS];
  const uint8_t count = buildPlayers(players, MAX_PLAYERS);
  if (count == 0) {
    return false;
  }

  const uint32_t choice = esp_random() % count;
  selected = players[choice];
  starterSelected_ = true;
  starterModule_ = selected.controllerId;
  starterSlot_ = selected.slot;
  return true;
}

bool Lobby::starterOrDefault(PlayerSeat &selected) const {
  if (selectedStarter(selected)) {
    return true;
  }

  PlayerSeat players[MAX_PLAYERS];
  const uint8_t count = buildPlayers(players, MAX_PLAYERS);
  if (count == 0) {
    return false;
  }

  selected = players[0];
  return true;
}

void Lobby::resetEmpty() {
  joinedCount_ = 0;
  starterSelected_ = false;
  starterModule_ = INVALID_ID;
  starterSlot_ = 1;
  startArmedBy_ = INVALID_ID;

  for (uint8_t i = 0; i < MAX_CONTROLLERS; ++i) {
    joinedOrder_[i] = INVALID_ID;
    secondary_[i] = false;
    held_[i] = false;
    sharedChord_[i] = false;
    suppressNextShort_[i] = false;
    actionLong_[i] = false;
  }
}

void Lobby::resetForRematch() {
  starterSelected_ = false;
  starterModule_ = INVALID_ID;
  starterSlot_ = 1;
  startArmedBy_ = INVALID_ID;

  for (uint8_t i = 0; i < MAX_CONTROLLERS; ++i) {
    held_[i] = false;
    sharedChord_[i] = false;
    suppressNextShort_[i] = false;
    actionLong_[i] = false;
  }
}

void Lobby::setHeld(uint8_t controllerId, bool held) {
  if (validModule(controllerId)) {
    held_[controllerId] = held;
  }
}

bool Lobby::isHeld(uint8_t controllerId) const {
  return validModule(controllerId) && held_[controllerId];
}

bool Lobby::anyOtherHeld(uint8_t controllerId) const {
  for (uint8_t i = 0; i < MAX_CONTROLLERS; ++i) {
    if (i != controllerId && held_[i]) {
      return true;
    }
  }
  return false;
}

void Lobby::setStartArmedBy(uint8_t controllerId) {
  startArmedBy_ = controllerId;
}

void Lobby::clearStartArm() {
  startArmedBy_ = INVALID_ID;
}

uint8_t Lobby::startArmedBy() const {
  return startArmedBy_;
}

void Lobby::setSharedChord(uint8_t controllerId, bool value) {
  if (validModule(controllerId)) {
    sharedChord_[controllerId] = value;
  }
}

bool Lobby::sharedChord(uint8_t controllerId) const {
  return validModule(controllerId) && sharedChord_[controllerId];
}

void Lobby::setSuppressNextShort(uint8_t controllerId, bool value) {
  if (validModule(controllerId)) {
    suppressNextShort_[controllerId] = value;
  }
}

bool Lobby::consumeSuppressNextShort(uint8_t controllerId) {
  if (!validModule(controllerId) || !suppressNextShort_[controllerId]) {
    return false;
  }
  suppressNextShort_[controllerId] = false;
  return true;
}

void Lobby::setActionLong(uint8_t controllerId, bool value) {
  if (validModule(controllerId)) {
    actionLong_[controllerId] = value;
  }
}

bool Lobby::actionLong(uint8_t controllerId) const {
  return validModule(controllerId) && actionLong_[controllerId];
}

}  // namespace TurnHub
