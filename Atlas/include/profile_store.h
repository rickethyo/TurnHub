#pragma once

#include <Arduino.h>
#include "identity.h"
#include "accessibility_prefs.h"
#include "profile_policy.h"

namespace TurnHubStorage { class BlobStore; }

namespace TurnHubProfiles {

constexpr uint16_t STATS_SCHEMA_VERSION = 1;
constexpr size_t PROFILE_ID_LENGTH = TurnHubIdentity::ProfileId::length;

enum class LastGameResult : uint8_t {
  None = 0,
  Win = 1,
  Loss = 2,
  Eliminated = 3,
  // Value 4 was reserved as "Completed" but never written; draws reuse it.
  Draw = 4,
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
  // GameProfile of the last completed game. Was an unused reserved byte, so
  // older records read 0 (Generic) and the v1 layout is unchanged.
  uint8_t lastGameProfile = 0;
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
// A named profile with no PIN (tablet mode's Add player).
String createProfileWithName(const String &name);
bool profileExists(const String &profileId);

String nameForProfile(const String &profileId);
bool setNameForProfile(const String &profileId, const String &name);

String storedPinHashForProfile(const String &profileId);
bool setPinHashForProfile(const String &profileId, const String &hash);
bool clearPinForProfile(const String &profileId);
bool hasPinForProfile(const String &profileId);

bool loadPolicyForProfile(const String &profileId, ProfilePolicy &policy);
bool savePolicyForProfile(const String &profileId, const ProfilePolicy &policy);

// Per-player accessibility preferences (accessibility_prefs.h). A missing
// record loads the defaults and succeeds.
bool loadAccessibilityForProfile(const String &profileId, AccessibilityPrefs &prefs);
bool saveAccessibilityForProfile(const String &profileId, const AccessibilityPrefs &prefs);

// The profile's Jewel color (0xRRGGBB): a luxury setting on the microSD
// card (key k<profileId>), so without a card there is none. Reads are cached
// in RAM; saving (set = false removes it) refreshes the cache.
bool jewelColorForProfile(const String &profileId, uint32_t &rgb);
bool saveJewelColorForProfile(const String &profileId, bool set, uint32_t rgb);
// The profile's avatar (avatars.h: 0 none, 1..AVATAR_COUNT a preset,
// AVATAR_CUSTOM approved upload), also a luxury record on the card (v<profileId>).
uint8_t avatarForProfile(const String &profileId);
TurnHubStorage::BlobStore *profileArtworkStore();
String artworkPath(const String &profileId, bool pending = false);
const uint8_t *artworkThumbnail(const String &profileId, uint32_t &revision);
bool saveAvatarForProfile(const String &profileId, uint8_t avatar);

// Statistics are split (owner decision 2026-09-25): a small core record in
// NVS (games played and won, last result and game profile) that every Atlas
// keeps, and the full v1 record as luxury data on the microSD card. Without
// a card the detail is not recorded ("limp mode").
// Load returns the combined view; *detailed says whether the detail fields
// are backed by a record (false: only the core counts are real).
bool loadStatsForProfile(const String &profileId, ProfileStats &stats, bool *detailed = nullptr);
bool saveStatsForProfile(const String &profileId, const ProfileStats &stats);

// The card's record store, or nullptr without one. Set at boot and again
// when the card is inserted or pulled (main.cpp refreshSdLuxuryStore).
void setLuxuryStore(TurnHubStorage::BlobStore *store);
bool luxuryStoreAvailable();
// Private moderation history. A missing record reads as zero counts.
bool loadModerationStatsForProfile(const String &profileId, ModerationStats &stats);
bool saveModerationStatsForProfile(const String &profileId, const ModerationStats &stats);

// Physical-seat binding adapter. This is deliberately separate from profile
// storage so the same profile can later bind to a persistent virtual seat.
// Returns empty for guests; never creates a profile.
String profileIdForSeat(const uint8_t mac[6], uint8_t slot);
// Read an existing binding without creating a profile for an unused seat.
String boundProfileIdForSeat(const uint8_t mac[6], uint8_t slot);
// Release both seat bindings.
// Atlas profile records and statistics remain durable.
bool resetTransientSeatBindings(const uint8_t mac[6]);
// Release one seat's binding (that seat becomes a guest).
bool releaseSeatBinding(const uint8_t mac[6], uint8_t slot);
// Move a profile between this Sigil's seats, leaving the source as a guest.
bool moveSeatProfile(const uint8_t mac[6], uint8_t fromSlot, uint8_t toSlot, const String &profileId);
bool bindSeatToProfile(
    const uint8_t mac[6],
    uint8_t slot,
    const String &profileId);

// Seat helpers: the profile bound to a physical seat.
String nameForSeat(const uint8_t mac[6], uint8_t slot);
bool setNameForSeat(const uint8_t mac[6], uint8_t slot, const String &name);

String storedPinHashForSeat(const uint8_t mac[6], uint8_t slot);
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
