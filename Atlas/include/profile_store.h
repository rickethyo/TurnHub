#pragma once

#include <Arduino.h>

namespace TurnHubProfiles {

constexpr uint16_t STATS_SCHEMA_VERSION = 1;

enum class LastGameResult : uint8_t {
  None = 0,
  Win = 1,
  Loss = 2,
  Eliminated = 3,
  Completed = 4,
};

struct ProfileStats {
  uint64_t totalTurnMs = 0;
  uint64_t totalGameMs = 0;

  uint32_t gamesPlayed = 0;
  uint32_t gamesWon = 0;
  uint32_t gamesStarted = 0;
  uint32_t gamesEliminated = 0;
  uint32_t turnsCompleted = 0;
  uint32_t fastestTurnMs = 0;
  uint32_t longestTurnMs = 0;

  uint32_t lastGameDurationMs = 0;
  uint32_t lastGameTurns = 0;
  uint32_t lastGameTurnMs = 0;
  uint32_t lastGameFastestTurnMs = 0;
  uint32_t lastGameLongestTurnMs = 0;

  uint16_t schemaVersion = STATS_SCHEMA_VERSION;
  LastGameResult lastGameResult = LastGameResult::None;
  uint8_t reserved = 0;
};

bool begin();
bool ready();

// Seats currently bind to stable local profile IDs. The binding is separate
// from the Sigil hardware identity so a later profile-picker can move a saved
// person between seats without moving their statistics.
String profileIdForSeat(const uint8_t mac[6], uint8_t slot);

String nameForSeat(const uint8_t mac[6], uint8_t slot);
bool setNameForSeat(const uint8_t mac[6], uint8_t slot, const String &name);

// legacySource is set when the returned hash came from the old MAC+slot key.
// A successful login can then transparently rewrite it under the stable
// profile ID using the new profile-based hash material.
String storedPinHashForSeat(
    const uint8_t mac[6],
    uint8_t slot,
    bool *legacySource = nullptr);
bool setPinHashForSeat(
    const uint8_t mac[6],
    uint8_t slot,
    const String &hash);
bool clearPinForSeat(const uint8_t mac[6], uint8_t slot);
bool hasPinForSeat(const uint8_t mac[6], uint8_t slot);

String deviceName(const uint8_t mac[6]);
bool setDeviceName(const uint8_t mac[6], const String &name);

bool loadStatsForSeat(
    const uint8_t mac[6],
    uint8_t slot,
    ProfileStats &stats);
bool saveStatsForSeat(
    const uint8_t mac[6],
    uint8_t slot,
    const ProfileStats &stats);

}  // namespace TurnHubProfiles
