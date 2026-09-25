#include <cassert>
#include <iostream>
#include "profile_store.h"
#include "account_access.h"
#include "storage.h"
#include <cstring>
#include <nvs.h>

namespace FakeNvs {
std::map<std::string, std::vector<uint8_t>> blobs;
std::map<std::string, std::string> strings;
std::map<std::string, uint8_t> bytes;
int openError = ESP_OK, readError = ESP_OK, setError = ESP_OK, commitError = ESP_OK;
int writes = 0, commits = 0, reads = 0, failReadAt = 0;
const char *openedNamespace = nullptr;
}

using namespace TurnHubProfiles;
static String hashPin(const String &, const String &) { return String(std::string(64, 'a')); }

static void guestLookups() {
  // Exercise the actual store used by display sync, HTTP polling and gameplay.
  // Repeated reconnects used to create two durable records on each cycle.
  const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
  for (int reconnect = 0; reconnect < 100; ++reconnect) {
    assert(resetTransientSeatBindings(mac));
    for (uint8_t slot : {1, 2}) {
      assert(profileIdForSeat(mac, slot).length() == 0);
      assert(boundProfileIdForSeat(mac, slot).length() == 0);
      assert(nameForSeat(mac, slot).length() == 0);
      assert(!hasPinForSeat(mac, slot));
      ProfileStats stats;
      assert(!loadStatsForSeat(mac, slot, stats));
      assert(!setNameForSeat(mac, slot, "Unbound"));
      assert(!setPinHashForSeat(mac, slot, hashPin("", "")));
      assert(!saveStatsForSeat(mac, slot, stats));
    }
  }
  assert(FakeNvs::bytes.empty() && FakeNvs::strings.empty() && FakeNvs::blobs.empty());
  assert(FakeNvs::writes == 0);
}

static void existingAccounts() {
  const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
  const String id = createProfileWithCredentials("Owner", "1234", hashPin);
  assert(id.length() == 8);
  ProfileStats stats; stats.gamesPlayed = 3;
  assert(saveStatsForProfile(id, stats));
  assert(bindSeatToProfile(mac, 1, id));
  assert(bindSeatToProfile(mac, 2, id));
  FakeNvs::strings["b010203040506A"] = id;
  FakeNvs::bytes["r010203040506A"] = 1;
  assert(resetTransientSeatBindings(mac));
  assert(profileIdForSeat(mac, 1).length() == 0 && profileIdForSeat(mac, 2).length() == 0);
  assert(!FakeNvs::strings.count("b010203040506A"));
  assert(!FakeNvs::bytes.count("r010203040506A"));
  const int writes = FakeNvs::writes;
  assert(profileIdForSeat(mac, 1).length() == 0);
  assert(profileExists(id) && nameForProfile(id) == "Owner" && hasPinForProfile(id));
  assert(loadStatsForProfile(id, stats) && stats.gamesPlayed == 3);
  assert(FakeNvs::writes == writes);
  // Identical non-empty values must not commit NVS again. Failed reads must
  // still attempt the write rather than treating a fallback as stored data.
  assert(setNameForProfile(id, "Owner"));
  assert(setPinHashForProfile(id, hashPin("", "")));
  assert(FakeNvs::writes == writes);
  assert(setDeviceName(mac, "Table Sigil"));
  const int namedWrites = FakeNvs::writes;
  assert(setDeviceName(mac, "Table Sigil"));
  assert(FakeNvs::writes == namedWrites);
  assert(setNameForProfile(id, "Renamed"));
  assert(nameForProfile(id) == "Renamed" && FakeNvs::writes == namedWrites + 1);
  FakeNvs::setError = ESP_ERR_NVS_INVALID_HANDLE;
  assert(!setDeviceName(mac, "New name"));
  FakeNvs::setError = ESP_OK;
}

static void legacyPlaceholders() {
  // More empty markers than the list limit must not hide later real accounts
  // or block registration. Filtering must happen while iterating NVS.
  FakeNvs::reset();
  for (unsigned i = 0; i < MAX_LOGIN_PROFILES + 10; ++i) {
    char key[10]; snprintf(key, sizeof(key), "m%08X", i);
    FakeNvs::bytes[key] = 1;
  }
  const auto markers = FakeNvs::bytes;
  char ids[MAX_LOGIN_PROFILES][9];
  assert(listProfileIds(ids, MAX_LOGIN_PROFILES) == 0);
  assert(FakeNvs::bytes == markers && FakeNvs::writes == 0);
  const String id = createProfileWithCredentials("Real account", "1234", hashPin);
  assert(id.length() == 8 && listProfileIds(ids, MAX_LOGIN_PROFILES) == 1);
  assert(String(ids[0]) == id);
  // Preserve every kind of user data, including unreadable/future blobs.
  FakeNvs::strings["n00000000"] = "Legacy name";
  FakeNvs::strings["p00000001"] = hashPin("", "");
  FakeNvs::blobs["s00000002"] = {255};
  FakeNvs::blobs["a00000003"] = {255};
  FakeNvs::blobs["u00000004"] = {255};
  assert(listProfileIds(ids, MAX_LOGIN_PROFILES) == 6);
  assert(listProfileIds(ids, 2) == 2);
  assert(profileExists("00000005")); // Hidden is not deleted.
  // An old bound placeholder remains available for physical recovery.
  const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
  assert(bindSeatToProfile(mac, 1, "00000005"));
  assert(profileIdForSeat(mac, 1) == "00000005");
  assert(setNameForSeat(mac, 1, "Recovered"));
  assert(listProfileIds(ids, MAX_LOGIN_PROFILES) == 7);
}

// Counts that older firmware kept in u<id> move once into o<id>. A power loss
// between the two writes must neither lose nor double them, and an unreadable
// o<id> record fails closed instead of being overwritten.
static void moderationMigration() {
  const String id = createProfileWithCredentials("Moderated", "1234", hashPin);
  assert(id.length() == 8);
  const std::string account = std::string("u") + id.c_str(), history = std::string("o") + id.c_str();
  const std::vector<uint8_t> legacy{2, 0, 0, 1, 5, 0, 0, 0, 2, 0, 0, 0};
  const std::vector<uint8_t> cleared{2, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
  FakeNvs::blobs[account] = legacy;

  ModerationStats stats;
  assert(loadModerationStatsForProfile(id, stats) && stats.connectionResets == 5 && stats.gameRemovals == 2);
  assert(FakeNvs::blobs[account] == cleared);
  TurnHubAccounts::Account loaded;
  assert(TurnHubAccounts::load(id, loaded) && loaded.reconnectRequired && !loaded.legacyConnectionResets);

  // Interrupted after o<id> was written: the account is cleared, not re-added.
  FakeNvs::blobs[account] = legacy;
  assert(loadModerationStatsForProfile(id, stats) && stats.connectionResets == 5 && stats.gameRemovals == 2);
  assert(FakeNvs::blobs[account] == cleared);

  stats.gameRemovals = 3;
  assert(saveModerationStatsForProfile(id, stats));
  assert(loadModerationStatsForProfile(id, stats) && stats.gameRemovals == 3);

  FakeNvs::blobs[history] = {9, 0, 0, 0, 0, 0, 0, 0, 0};
  FakeNvs::blobs[account] = legacy;
  assert(!TurnHubAccounts::load(id, loaded) && !loadModerationStatsForProfile(id, stats));
  assert(FakeNvs::blobs[account] == legacy && FakeNvs::blobs[history][0] == 9);
}

static void accessibilityPreferences() {
  const String id = createProfileWithCredentials("Access", "1234", hashPin);
  AccessibilityPrefs prefs;
  assert(loadAccessibilityForProfile(id, prefs) && prefs.sigilSound);  // Missing: defaults.
  prefs.sigilSound = false; prefs.ledStyle = LedStyle::ReducedMotion; prefs.longPressMs = 3500; prefs.winHoldMs = 8000;
  assert(saveAccessibilityForProfile(id, prefs));
  AccessibilityPrefs again;
  assert(loadAccessibilityForProfile(id, again) && sameAccessibilityPrefs(prefs, again));
  assert(!saveAccessibilityForProfile("FFFFFFFF", prefs));  // Unknown profile.
  prefs.winHoldMs = 3000;
  assert(!saveAccessibilityForProfile(id, prefs));  // Invalid timing is never stored.
  assert(loadAccessibilityForProfile(id, again) && again.winHoldMs == 8000);
}

// An in-memory microSD record store.
struct FakeCard : TurnHubStorage::BlobStore {
  std::map<std::string, std::vector<uint8_t>> records;
  bool broken = false;
  TurnHubStorage::Status read(const char *key, void *data, size_t capacity, size_t &size) override {
    size = 0;
    if (broken) return TurnHubStorage::Status::IoError;
    const auto found = records.find(key);
    if (found == records.end()) return TurnHubStorage::Status::NotFound;
    size = found->second.size();
    if (data == nullptr) return TurnHubStorage::Status::Ok;
    if (size > capacity) return TurnHubStorage::Status::Corrupt;
    memcpy(data, found->second.data(), size);
    return TurnHubStorage::Status::Ok;
  }
  TurnHubStorage::Status write(const char *key, const void *data, size_t size) override {
    if (broken) return TurnHubStorage::Status::IoError;
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    records[key].assign(bytes, bytes + size);
    return TurnHubStorage::Status::Ok;
  }
  TurnHubStorage::Status remove(const char *key) override {
    return records.erase(key) ? TurnHubStorage::Status::Ok : TurnHubStorage::Status::NotFound;
  }
};

// Core counts stay in NVS; the detail is luxury data on the card (limp mode
// without one), and detail older firmware left in NVS moves to the card.
static void statisticsSplit() {
  const String id = createProfileWithCredentials("Split", "1234", hashPin);
  const std::string core = std::string("c") + id.c_str(), detail = std::string("s") + id.c_str();
  ProfileStats stats; bool detailed = true;
  setLuxuryStore(nullptr);
  assert(loadStatsForProfile(id, stats, &detailed) && stats.gamesPlayed == 0 && !detailed);

  // No card: only the 12-byte core record is written.
  stats.gamesPlayed = 2; stats.gamesWon = 1; stats.totalTurnMs = 9000; stats.turnsCompleted = 4;
  stats.lastGameResult = LastGameResult::Win; stats.lastGameProfile = 2;
  assert(saveStatsForProfile(id, stats));
  assert(FakeNvs::blobs.count(core) && FakeNvs::blobs[core].size() == 12 && !FakeNvs::blobs.count(detail));
  assert(loadStatsForProfile(id, stats, &detailed) && !detailed && stats.gamesPlayed == 2 &&
      stats.gamesWon == 1 && stats.lastGameProfile == 2 && stats.lastGameResult == LastGameResult::Win &&
      stats.totalTurnMs == 0);

  // With a card: the detail goes there; the core counts stay authoritative.
  FakeCard card; setLuxuryStore(&card);
  stats.gamesPlayed = 3; stats.totalTurnMs = 12000;
  assert(saveStatsForProfile(id, stats) && card.records.count(detail) && !FakeNvs::blobs.count(detail));
  assert(loadStatsForProfile(id, stats, &detailed) && detailed && stats.gamesPlayed == 3 && stats.totalTurnMs == 12000);
  // A game played without the card: the core keeps counting, the detail lags.
  setLuxuryStore(nullptr); stats.gamesPlayed = 4; stats.totalTurnMs = 15000; assert(saveStatsForProfile(id, stats));
  setLuxuryStore(&card);
  assert(loadStatsForProfile(id, stats, &detailed) && detailed && stats.gamesPlayed == 4 && stats.totalTurnMs == 12000);
  // A failing card costs only the detail.
  card.broken = true;
  assert(loadStatsForProfile(id, stats, &detailed) && !detailed && stats.gamesPlayed == 4);
  assert(saveStatsForProfile(id, stats));
  card.broken = false;

  // Migration: a v1 record older firmware left in NVS moves to the card
  // (written, read back, compared) and only then leaves NVS.
  const String old = createProfileWithCredentials("Old", "1234", hashPin);
  const std::string oldDetail = std::string("s") + old.c_str(), oldCore = std::string("c") + old.c_str();
  ProfileStats legacy; legacy.gamesPlayed = 7; legacy.gamesWon = 5; legacy.fastestTurnMs = 800;
  std::vector<uint8_t> image(sizeof(legacy));
  memcpy(image.data(), &legacy, sizeof(legacy));
  FakeNvs::blobs[oldDetail] = image;
  setLuxuryStore(nullptr);
  assert(migrateDetailedStats() == 0 && FakeNvs::blobs.count(oldDetail));  // No card: nothing deleted.
  assert(loadStatsForProfile(old, stats, &detailed) && detailed && stats.fastestTurnMs == 800);
  setLuxuryStore(&card);
  assert(migrateDetailedStats() == 1);
  assert(!FakeNvs::blobs.count(oldDetail) && card.records[oldDetail] == image && FakeNvs::blobs.count(oldCore));
  assert(loadStatsForProfile(old, stats, &detailed) && detailed && stats.gamesPlayed == 7 &&
      stats.gamesWon == 5 && stats.fastestTurnMs == 800);
  assert(migrateDetailedStats() == 0);
  // A card that already holds different detail is never overwritten.
  FakeNvs::blobs[oldDetail] = image; ProfileStats other = legacy; other.fastestTurnMs = 1;
  memcpy(card.records[oldDetail].data(), &other, sizeof(other));
  assert(migrateDetailedStats() == 0 && FakeNvs::blobs.count(oldDetail));
  FakeNvs::blobs.erase(oldDetail);
  setLuxuryStore(nullptr);
}

int main() {
  assert(begin());
  guestLookups();
  existingAccounts();
  legacyPlaceholders();
  moderationMigration();
  accessibilityPreferences();
  statisticsSplit();
  std::cout << "PASS: real profile store guest lookups, reconnects, saved bindings, legacy placeholder filtering, moderation-count migration, accessibility preferences and the NVS/SD statistics split\n";
}
