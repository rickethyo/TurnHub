#include "lobby.h"

#include <esp_system.h>

namespace TurnHub {

Lobby::Lobby() {
  resetEmpty();
}

bool Lobby::validModule(uint8_t moduleId) const {
  return moduleId < MAX_PHYSICAL_SIGILS;
}

int Lobby::joinedIndex(uint8_t moduleId) const {
  for (uint8_t i = 0; i < joinedCount_; ++i) {
    if (joinedOrder_[i] == moduleId) {
      return i;
    }
  }
  return -1;
}

bool Lobby::isJoined(uint8_t moduleId) const {
  return joinedIndex(moduleId) >= 0;
}

bool Lobby::hasSecondary(uint8_t moduleId) const {
  return validModule(moduleId) && secondary_[moduleId];
}

uint8_t Lobby::hostModule() const {
  return joinedCount_ > 0 ? joinedOrder_[0] : INVALID_ID;
}

uint8_t Lobby::joinedModuleCount() const {
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

uint8_t Lobby::join(uint8_t moduleId) {
  if (!validModule(moduleId)) {
    return 0;
  }

  const uint8_t existing = playerNumber(moduleId, 1);
  if (existing != 0) {
    return existing;
  }

  if (joinedCount_ >= MAX_PHYSICAL_SIGILS) {
    return 0;
  }

  joinedOrder_[joinedCount_++] = moduleId;
  return playerNumber(moduleId, 1);
}

bool Lobby::leave(uint8_t moduleId) {
  const int index = joinedIndex(moduleId);
  if (index < 0) {
    return false;
  }

  for (uint8_t i = static_cast<uint8_t>(index); i + 1 < joinedCount_; ++i) {
    joinedOrder_[i] = joinedOrder_[i + 1];
  }

  --joinedCount_;
  secondary_[moduleId] = false;
  held_[moduleId] = false;
  sharedChord_[moduleId] = false;
  suppressNextShort_[moduleId] = false;
  actionLong_[moduleId] = false;

  if (starterSelected_ && starterModule_ == moduleId) {
    starterSelected_ = false;
    starterModule_ = INVALID_ID;
    starterSlot_ = 1;
  }

  if (startArmedBy_ == moduleId) {
    startArmedBy_ = INVALID_ID;
  }

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
    out[count++] = PlayerSeat{number++, module, 1};

    if (secondary_[module] && count < capacity) {
      out[count++] = PlayerSeat{number++, module, 2};
    }
  }

  return count;
}

uint8_t Lobby::playersForModule(
    uint8_t moduleId,
    PlayerSeat *out,
    uint8_t capacity) const {
  if (!isJoined(moduleId) || out == nullptr || capacity == 0) {
    return 0;
  }

  uint8_t count = 0;
  const uint8_t primary = playerNumber(moduleId, 1);
  if (primary != 0 && count < capacity) {
    out[count++] = PlayerSeat{primary, moduleId, 1};
  }

  if (secondary_[moduleId] && count < capacity) {
    const uint8_t second = playerNumber(moduleId, 2);
    if (second != 0) {
      out[count++] = PlayerSeat{second, moduleId, 2};
    }
  }

  return count;
}

uint8_t Lobby::playerNumber(uint8_t moduleId, uint8_t slot) const {
  uint8_t number = 1;

  for (uint8_t i = 0; i < joinedCount_; ++i) {
    const uint8_t module = joinedOrder_[i];

    if (module == moduleId) {
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
    uint8_t moduleId,
    bool &added,
    PlayerSeat &affected) {
  if (!isJoined(moduleId)) {
    return false;
  }

  if (secondary_[moduleId]) {
    const uint8_t oldNumber = playerNumber(moduleId, 2);
    affected = PlayerSeat{oldNumber, moduleId, 2};
    secondary_[moduleId] = false;
    added = false;

    if (starterSelected_ && starterModule_ == moduleId && starterSlot_ == 2) {
      starterSelected_ = false;
      starterModule_ = INVALID_ID;
      starterSlot_ = 1;
    }

    return true;
  }

  secondary_[moduleId] = true;
  added = true;
  affected = PlayerSeat{playerNumber(moduleId, 2), moduleId, 2};
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

bool Lobby::selectStarter(uint8_t moduleId, PlayerSeat &selected) {
  if (!isJoined(moduleId)) {
    return false;
  }

  uint8_t nextSlot = 1;
  if (starterSelected_ && starterModule_ == moduleId && secondary_[moduleId]) {
    nextSlot = starterSlot_ == 1 ? 2 : 1;
  }

  starterSelected_ = true;
  starterModule_ = moduleId;
  starterSlot_ = nextSlot;

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
  starterModule_ = selected.moduleId;
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

  for (uint8_t i = 0; i < MAX_PHYSICAL_SIGILS; ++i) {
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

  for (uint8_t i = 0; i < MAX_PHYSICAL_SIGILS; ++i) {
    held_[i] = false;
    sharedChord_[i] = false;
    suppressNextShort_[i] = false;
    actionLong_[i] = false;
  }
}

void Lobby::setHeld(uint8_t moduleId, bool held) {
  if (validModule(moduleId)) {
    held_[moduleId] = held;
  }
}

bool Lobby::isHeld(uint8_t moduleId) const {
  return validModule(moduleId) && held_[moduleId];
}

bool Lobby::anyOtherHeld(uint8_t moduleId) const {
  for (uint8_t i = 0; i < MAX_PHYSICAL_SIGILS; ++i) {
    if (i != moduleId && held_[i]) {
      return true;
    }
  }
  return false;
}

void Lobby::setStartArmedBy(uint8_t moduleId) {
  startArmedBy_ = moduleId;
}

void Lobby::clearStartArm() {
  startArmedBy_ = INVALID_ID;
}

uint8_t Lobby::startArmedBy() const {
  return startArmedBy_;
}

void Lobby::setSharedChord(uint8_t moduleId, bool value) {
  if (validModule(moduleId)) {
    sharedChord_[moduleId] = value;
  }
}

bool Lobby::sharedChord(uint8_t moduleId) const {
  return validModule(moduleId) && sharedChord_[moduleId];
}

void Lobby::setSuppressNextShort(uint8_t moduleId, bool value) {
  if (validModule(moduleId)) {
    suppressNextShort_[moduleId] = value;
  }
}

bool Lobby::consumeSuppressNextShort(uint8_t moduleId) {
  if (!validModule(moduleId) || !suppressNextShort_[moduleId]) {
    return false;
  }
  suppressNextShort_[moduleId] = false;
  return true;
}

void Lobby::setActionLong(uint8_t moduleId, bool value) {
  if (validModule(moduleId)) {
    actionLong_[moduleId] = value;
  }
}

bool Lobby::actionLong(uint8_t moduleId) const {
  return validModule(moduleId) && actionLong_[moduleId];
}

}  // namespace TurnHub
