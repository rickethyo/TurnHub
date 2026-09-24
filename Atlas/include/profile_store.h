#pragma once

#include <Arduino.h>
#include "identity.h"
#include "profile_policy.h"

namespace TurnHubProfiles {

constexpr uint16_t STATS_SCHEMA_VERSION = 1;
constexpr size_t PROFILE_ID_LENGTH = TurnHubIdentity::ProfileId::length;

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

// Private moderation history: Game Master actions taken against this profile.
// Stored with the statistics, but served only to the profile's own
// PIN-authenticated session. Never shown to other accounts (including Game
// Masters), in statistics exports, on seats or on Sigil displays.
struct ModerationStats {
  uint32_t connectionResets = 0;
  uint32_t gameRemovals = 0;
};

bool begin();
bool ready();

// Profiles are durable local identities. Hardware and virtual seats only bind
// to a profile ID; names, PINs and statistics belong to the profile itself.
String createProfile();
constexpr size_t MAX_LOGIN_PROFILES = 64;
// List configured/historical profiles; skip legacy marker-only placeholders
// before applying capacity. No profile records are deleted.
size_t listProfileIds(char (*ids)[PROFILE_ID_LENGTH + 1], size_t capacity);
using PinHasher = String (*)(const String &profileId, const String &pin);
String createProfileWithCredentials(const String &name, const String &pin, PinHasher hasher);
bool profileExists(const String &profileId);

String nameForProfile(const String &profileId);
bool setNameForProfile(const String &profileId, const String &name);

String storedPinHashForProfile(const String &profileId);
bool setPinHashForProfile(const String &profileId, const String &hash);
bool clearPinForProfile(const String &profileId);
bool hasPinForProfile(const String &profileId);

bool loadPolicyForProfile(const String &profileId, ProfilePolicy &policy);
bool savePolicyForProfile(const String &profileId, const ProfilePolicy &policy);

bool loadStatsForProfile(const String &profileId, ProfileStats &stats);
bool saveStatsForProfile(const String &profileId, const ProfileStats &stats);
// A missing record reads as zero counts. Loading also migrates counts that
// older firmware kept in the account record.
bool loadModerationStatsForProfile(const String &profileId, ModerationStats &stats);
bool saveModerationStatsForProfile(const String &profileId, const ModerationStats &stats);

// Physical-seat binding adapter. This is deliberately separate from profile
// storage so the same profile can later bind to a persistent virtual seat.
// Returns empty for guests; never creates a profile. Migrates names only for
// an already-bound profile.
String profileIdForSeat(const uint8_t mac[6], uint8_t slot);
// Read an existing binding without creating a profile for an unused seat.
String boundProfileIdForSeat(const uint8_t mac[6], uint8_t slot);
// Release both seat bindings and remove legacy remembered-seat keys.
// Atlas profile records and statistics remain durable.
bool resetTransientSeatBindings(const uint8_t mac[6]);
// Move a profile between this Sigil's seats, leaving the source as a guest.
bool moveSeatProfile(const uint8_t mac[6], uint8_t fromSlot, uint8_t toSlot, const String &profileId);
bool bindSeatToProfile(
    const uint8_t mac[6],
    uint8_t slot,
    const String &profileId);

// Compatibility helpers for existing physical-seat call sites and migration.
String nameForSeat(const uint8_t mac[6], uint8_t slot);
bool setNameForSeat(const uint8_t mac[6], uint8_t slot, const String &name);

// legacySource is set when the returned hash came from the old MAC+slot key.
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
