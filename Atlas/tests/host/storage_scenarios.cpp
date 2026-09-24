#include <cassert>
#include <cstring>
#include <iostream>
#include <type_traits>
#include "identity.h"
#include "account_access.h"
#include "nvs_blob_store.h"
#include "profile_stats_storage.h"
#include "profile_policy.h"
#include "game_settings_store.h"
#include "pairing_settings.h"
#include "accessibility_prefs.h"

using namespace TurnHubStorage;
using namespace TurnHubProfiles;
constexpr char key[] = "sAB12CD34";

namespace FakeNvs {
std::map<std::string, std::vector<uint8_t>> blobs;
std::map<std::string, std::string> strings;
std::map<std::string, uint8_t> bytes;
int openError = ESP_OK, readError = ESP_OK, setError = ESP_OK, commitError = ESP_OK;
int writes = 0, commits = 0;
int reads = 0, failReadAt = 0;
const char *openedNamespace = nullptr;
}

void identityContracts() {
  using namespace TurnHubIdentity;
  static_assert(!std::is_same<MatchId, ParticipantId>::value, "IDs must be distinct");
  static_assert(!std::is_convertible<ControllerId, ProfileId>::value, "No identity coercion");
  ProfileId profile;
  assert(!profile.valid());
  assert(ProfileId::parse("aB12Cd34", profile));
  assert(strcmp(profile.c_str(), "aB12Cd34") == 0);
  for (const char *invalid : {"", "123", "123456789", "ABCDEFGZ"}) {
    assert(!ProfileId::parse(invalid, profile));
    assert(strcmp(profile.c_str(), "aB12Cd34") == 0);
  }
  assert(!ProfileId::parse(nullptr, profile));
  MatchId match;
  assert(MatchId::parse("0123456789ABCDEF0123456789ABCDEF", match));
  assert(match.valid());
  assert(!MatchId::parse("AB12CD34", match));
}

void existingRecords() {
  FakeNvs::reset();
  NvsBlobStore store;
  assert(store.begin("turnhub") == Status::Ok);
  assert(strcmp(FakeNvs::openedNamespace, "turnhub") == 0);
  ProfileStats out;
  out.gamesPlayed = 91;
  assert(readStoredStats(store, key, out) == Status::NotFound);
  assert(out.gamesPlayed == 91);  // Repository never publishes partial/default reads.

  // Seed a deployed ESP32 v1 byte image independently of the struct encoder.
  std::vector<uint8_t> legacy(72, 0);
  legacy[0] = 0x78; legacy[1] = 0x56; legacy[2] = 0x34; legacy[3] = 0x12;
  legacy[16] = 17; legacy[20] = 4; legacy[64] = 1;
  legacy[66] = static_cast<uint8_t>(LastGameResult::Win);
  FakeNvs::blobs[key] = legacy;
  assert(readStoredStats(store, key, out) == Status::Ok);
  assert(out.totalTurnMs == 0x12345678ULL);
  assert(out.gamesPlayed == 17 && out.gamesWon == 4);
  assert(out.lastGameResult == LastGameResult::Win);
  assert(FakeNvs::writes == 0 && FakeNvs::blobs[key] == legacy);
  // Fault after the successful size probes, during the actual value read.
  FakeNvs::failReadAt = FakeNvs::reads + 3;
  ProfileStats unchanged;
  unchanged.gamesPlayed = 99;
  assert(readStoredStats(store, key, unchanged) == Status::IoError);
  assert(unchanged.gamesPlayed == 99 && FakeNvs::blobs[key] == legacy);
  FakeNvs::failReadAt = 0;
  ++out.gamesPlayed;
  assert(writeStoredStats(store, key, out) == Status::Ok);
  ProfileStats again;
  assert(readStoredStats(store, key, again) == Status::Ok);
  assert(again.gamesPlayed == 18 && again.gamesWon == 4);
  assert(again.totalTurnMs == out.totalTurnMs);
  assert(FakeNvs::writes == 1 && FakeNvs::commits == 1);
  // Draw reuses byte 4 (formerly the never-written "Completed"), so a draw
  // result stays a valid v1 image; 5 and above remain corrupt.
  static_assert(static_cast<uint8_t>(LastGameResult::Draw) == 4, "v1 result byte changed");
  again.lastGameResult = LastGameResult::Draw;
  assert(writeStoredStats(store, key, again) == Status::Ok);
  assert(FakeNvs::blobs[key][66] == 4);
  assert(readStoredStats(store, key, out) == Status::Ok && out.lastGameResult == LastGameResult::Draw);
  std::vector<uint8_t> future = FakeNvs::blobs[key];
  future[66] = 5;
  FakeNvs::blobs[key] = future;
  assert(readStoredStats(store, key, out) == Status::Corrupt);
  FakeNvs::blobs[key] = legacy;
  assert(writeStoredStats(store, "s12345678", ProfileStats{}) == Status::Ok);
}

void protectedRecords() {
  FakeNvs::reset();
  NvsBlobStore store;
  assert(store.begin("turnhub") == Status::Ok);
  ProfileStats out;
  out.gamesPlayed = 99;
  auto reject = [&](const std::vector<uint8_t> &bytes, Status expected) {
    FakeNvs::blobs[key] = bytes;
    assert(readStoredStats(store, key, out) == expected);
    assert(out.gamesPlayed == 99);
    assert(writeStoredStats(store, key, ProfileStats{}) == expected);
    assert(FakeNvs::writes == 0 && FakeNvs::blobs[key] == bytes);
  };
  reject({}, Status::Corrupt);
  reject(std::vector<uint8_t>(71, 0), Status::Corrupt);
  reject(std::vector<uint8_t>(73, 0), Status::Corrupt);
  std::vector<uint8_t> future(72, 0);
  future[64] = 2;
  reject(future, Status::UnsupportedSchema);
  future[64] = 1; future[66] = 255;
  reject(future, Status::Corrupt);

  FakeNvs::readError = ESP_ERR_NVS_TYPE_MISMATCH;
  assert(readStoredStats(store, key, out) == Status::Corrupt);
  assert(writeStoredStats(store, key, ProfileStats{}) == Status::Corrupt);
  FakeNvs::readError = ESP_ERR_NVS_INVALID_HANDLE;
  assert(readStoredStats(store, key, out) == Status::IoError);
  assert(writeStoredStats(store, key, ProfileStats{}) == Status::IoError);
  assert(FakeNvs::writes == 0);
  FakeNvs::readError = ESP_OK;
  ProfileStats invalid;
  invalid.schemaVersion = 2;
  assert(writeStoredStats(store, "s12345678", invalid) == Status::UnsupportedSchema);
  assert(FakeNvs::writes == 0);
}

void backendFailures() {
  FakeNvs::reset();
  NvsBlobStore store;
  ProfileStats stats;
  assert(readStoredStats(store, key, stats) == Status::Unavailable);
  assert(writeStoredStats(store, key, stats) == Status::Unavailable);
  FakeNvs::openError = ESP_ERR_NVS_INVALID_HANDLE;
  assert(store.begin("turnhub") == Status::Unavailable);
  FakeNvs::openError = ESP_OK;
  assert(store.begin("turnhub") == Status::Ok);  // Retry after transient failure.
  size_t size = 123;
  assert(store.read(nullptr, nullptr, 0, size) == Status::InvalidArgument);
  assert(size == 0);
  assert(store.write("1234567890123456", &stats, sizeof(stats)) == Status::InvalidArgument);
  assert(store.write(key, nullptr, sizeof(stats)) == Status::InvalidArgument);
  FakeNvs::blobs[key] = std::vector<uint8_t>(72, 0);
  uint8_t small[2] = {};
  assert(store.read(key, small, sizeof(small), size) == Status::Corrupt);
  assert(size == 72);
  FakeNvs::blobs.clear();
  FakeNvs::setError = ESP_ERR_NVS_INVALID_HANDLE;
  assert(writeStoredStats(store, key, stats) == Status::IoError);
  assert(FakeNvs::commits == 0);
  FakeNvs::setError = ESP_OK;
  FakeNvs::commitError = ESP_ERR_NVS_INVALID_HANDLE;
  assert(writeStoredStats(store, key, stats) == Status::IoError);
  assert(FakeNvs::commits == 1);  // Commit failure is never success.
}

void profilePolicyRecords() {
  FakeNvs::reset();
  NvsBlobStore store;
  assert(store.begin("turnhub") == Status::Ok);
  const char *policyKey = "aAB12CD34";
  ProfilePolicy out;
  assert(readStoredPolicy(store, policyKey, out) == Status::NotFound);
  assert(out.allowPhysicalWithoutPin && out.hideStatsWithoutAuthentication);
  for (bool physical : {false, true}) for (bool hidden : {false, true}) {
    ProfilePolicy chosen; chosen.allowPhysicalWithoutPin=physical; chosen.hideStatsWithoutAuthentication=hidden;
    assert(writeStoredPolicy(store, policyKey, chosen) == Status::Ok);
    NvsBlobStore reopened;
    assert(reopened.begin("turnhub") == Status::Ok);
    assert(readStoredPolicy(reopened, policyKey, out) == Status::Ok);
    assert(out.allowPhysicalWithoutPin==physical && out.hideStatsWithoutAuthentication==hidden);
    const int before=FakeNvs::writes;
    assert(writeStoredPolicy(reopened, policyKey, chosen) == Status::Ok);
    assert(FakeNvs::writes==before);
  }
  for (const auto &bytes : {std::vector<uint8_t>{}, {1,1}, {1,1,1,0}, {1,2,1}, {1,1,2}, {2,1,1}}) {
    FakeNvs::blobs[policyKey]=bytes;
    const auto expected=bytes.size()==3&&bytes[0]==2?Status::UnsupportedSchema:Status::Corrupt;
    const int before=FakeNvs::writes;
    assert(readStoredPolicy(store, policyKey, out)==expected);
    assert(writeStoredPolicy(store, policyKey, ProfilePolicy{})==expected);
    assert(FakeNvs::writes==before && FakeNvs::blobs[policyKey]==bytes);
  }
  FakeNvs::blobs.clear();
  FakeNvs::readError=ESP_ERR_NVS_INVALID_HANDLE;
  assert(writeStoredPolicy(store, policyKey, ProfilePolicy{})==Status::IoError);
  FakeNvs::readError=ESP_OK;
  FakeNvs::setError=ESP_ERR_NVS_INVALID_HANDLE;
  assert(writeStoredPolicy(store, policyKey, ProfilePolicy{})==Status::IoError);
  FakeNvs::setError=ESP_OK;
  FakeNvs::commitError=ESP_ERR_NVS_INVALID_HANDLE;
  assert(writeStoredPolicy(store, policyKey, ProfilePolicy{})==Status::IoError);
}

void pairingWindowRecords() {
  using namespace TurnHub;
  FakeNvs::reset();
  NvsBlobStore store;
  assert(store.begin("turnhub")==Status::Ok);
  uint32_t windowMs=DEFAULT_PAIRING_WINDOW_MS;
  assert(readPairingWindow(store,windowMs)==Status::NotFound && windowMs==15000);
  for (uint32_t choice : {15000u,30000u,60000u}) {
    assert(writePairingWindow(store,choice)==Status::Ok);
    assert((FakeNvs::blobs["pairwin"]==std::vector<uint8_t>{1,static_cast<uint8_t>(choice/1000)}));
    uint32_t again=0; assert(readPairingWindow(store,again)==Status::Ok && again==choice);
  }
  for (uint32_t bad : {0u,1000u,14999u,20000u,45000u,61000u,255000u}) {
    assert(writePairingWindow(store,bad)==Status::InvalidArgument);
  }
  assert((FakeNvs::blobs["pairwin"]==std::vector<uint8_t>{1,60}));
  for (const auto &bytes : {std::vector<uint8_t>{}, {1}, {1,30,0}, {1,20}, {1,0}, {2,30}}) {
    FakeNvs::blobs["pairwin"]=bytes;
    windowMs=15000;
    const Status expected=bytes.size()==2&&bytes[0]==2?Status::UnsupportedSchema:Status::Corrupt;
    assert(readPairingWindow(store,windowMs)==expected && windowMs==15000);
  }
  FakeNvs::blobs.clear();
  FakeNvs::setError=ESP_ERR_NVS_INVALID_HANDLE;
  assert(writePairingWindow(store,30000)==Status::IoError);
  FakeNvs::setError=ESP_OK;
}

void gameSettingsRecords() {
  using namespace TurnHub;
  FakeNvs::reset();
  NvsBlobStore store;
  assert(store.begin("turnhub")==Status::Ok);
  GameSettings settings;
  assert(readGameSettings(store,settings)==Status::NotFound);
  assert(settings.profile==GameProfile::Generic && settings.startingLife==40);
  // Independent little-endian wire image: Yu-Gi-Oh!, 8000 life.
  FakeNvs::blobs["gamecfg"]={1,3,0x40,0x1f,0,0};
  assert(readGameSettings(store,settings)==Status::Ok);
  // Schema 1 predates the turn timer and reads as OFF.
  assert(settings.profile==GameProfile::Yugioh && settings.startingLife==8000 &&
      settings.turnTimerMs==TURN_TIMER_OFF);
  for (uint8_t profile=0;profile<4;++profile) for(int32_t life : {0,27,1000000})
      for(uint32_t timer : {TURN_TIMER_OFF,TURN_TIMER_MIN_MS,120000u,TURN_TIMER_MAX_MS}) {
    settings.profile=static_cast<GameProfile>(profile);settings.startingLife=life;settings.turnTimerMs=timer;
    assert(writeGameSettings(store,settings)==Status::Ok);
    assert(FakeNvs::blobs["gamecfg"].size()==10 && FakeNvs::blobs["gamecfg"][0]==2);
    NvsBlobStore reopened;assert(reopened.begin("turnhub")==Status::Ok);
    GameSettings again;assert(readGameSettings(reopened,again)==Status::Ok);
    assert(again.profile==settings.profile && again.startingLife==life && again.turnTimerMs==timer);
    const int before=FakeNvs::writes;
    assert(writeGameSettings(reopened,settings)==Status::Ok && FakeNvs::writes==before);
  }
  // Independent schema 2 image: Generic, 40 life, 90-second timer.
  FakeNvs::blobs["gamecfg"]={2,0,40,0,0,0,0x90,0x5f,0x01,0};
  assert(readGameSettings(store,settings)==Status::Ok && settings.turnTimerMs==90000);
  settings.turnTimerMs=0;settings.startingLife=-1;
  assert(writeGameSettings(store,settings)==Status::InvalidArgument);
  settings.startingLife=1000001;
  assert(writeGameSettings(store,settings)==Status::InvalidArgument);
  settings.startingLife=40;
  for(uint32_t timer : {1u,14000u,15500u,TURN_TIMER_MAX_MS+1000})
    {settings.turnTimerMs=timer;assert(writeGameSettings(store,settings)==Status::InvalidArgument);}
  for(const auto &bytes : {std::vector<uint8_t>{}, {1,0,20}, {1,0,20,0,0,0,0},
      {1,4,20,0,0,0}, {1,0,0xff,0xff,0xff,0xff}, {2,0,20,0,0,0}, {1,0,20,0,0,0,0,0,0,0},
      {2,0,20,0,0,0,0xe8,0x03,0,0}, {3,0,20,0,0,0}}) {
    FakeNvs::blobs["gamecfg"]=bytes;
    settings=GameSettings{};
    const Status expected=bytes.size()==6&&bytes[0]==3?Status::UnsupportedSchema:Status::Corrupt;
    const int before=FakeNvs::writes;
    assert(readGameSettings(store,settings)==expected && settings.startingLife==40);
    assert(writeGameSettings(store,settings)==expected && FakeNvs::writes==before);
    assert(FakeNvs::blobs["gamecfg"]==bytes);
  }
  FakeNvs::blobs.clear();
  FakeNvs::readError=ESP_ERR_NVS_INVALID_HANDLE;
  assert(writeGameSettings(store,GameSettings{})==Status::IoError);
  FakeNvs::readError=ESP_OK;FakeNvs::setError=ESP_ERR_NVS_INVALID_HANDLE;
  assert(writeGameSettings(store,GameSettings{})==Status::IoError);
  FakeNvs::setError=ESP_OK;FakeNvs::commitError=ESP_ERR_NVS_INVALID_HANDLE;
  assert(writeGameSettings(store,GameSettings{})==Status::IoError);
}


static void accountRecords(){
  FakeNvs::blobs.clear();FakeNvs::setError=FakeNvs::commitError=FakeNvs::readError=ESP_OK;
  NvsBlobStore store;assert(store.begin("turnhub")==Status::Ok);
  TurnHubAccounts::Account a;a.permissions=31;a.nudgeMuted=true;a.reconnectRequired=true;a.legacyConnectionResets=513;a.legacyGameRemovals=7;
  assert(TurnHubAccounts::write(store,"u12345678",a)==Status::Ok);
  assert(FakeNvs::blobs["u12345678"]==std::vector<uint8_t>({2,31,1,1,1,2,0,0,7,0,0,0}));
  NvsBlobStore reopened;assert(reopened.begin("turnhub")==Status::Ok);
  TurnHubAccounts::Account loaded;assert(TurnHubAccounts::read(reopened,"u12345678",loaded)==Status::Ok&&loaded.legacyConnectionResets==513&&loaded.permissions==31);
  FakeNvs::blobs["u12345678"][0]=3;assert(TurnHubAccounts::write(store,"u12345678",a)==Status::UnsupportedSchema);
  FakeNvs::blobs["u12345678"]={1};assert(TurnHubAccounts::read(store,"u12345678",loaded)==Status::Corrupt);
  FakeNvs::blobs.clear();FakeNvs::commitError=ESP_ERR_NVS_INVALID_HANDLE;
  assert(TurnHubAccounts::write(store,"u12345678",a)==Status::IoError);FakeNvs::commitError=ESP_OK;
}

// o<profileId>: schema byte then two little-endian counts; never clobbered.
static void moderationRecords(){
  using TurnHubProfiles::ModerationStats;
  FakeNvs::blobs.clear();FakeNvs::setError=FakeNvs::commitError=FakeNvs::readError=ESP_OK;
  NvsBlobStore store;assert(store.begin("turnhub")==Status::Ok);
  ModerationStats stats;
  assert(TurnHubProfiles::readStoredModerationStats(store,"o12345678",stats)==Status::NotFound);
  stats.connectionResets=258;stats.gameRemovals=3;
  assert(TurnHubProfiles::writeStoredModerationStats(store,"o12345678",stats)==Status::Ok);
  assert(FakeNvs::blobs["o12345678"]==std::vector<uint8_t>({1,2,1,0,0,3,0,0,0}));
  ModerationStats loaded;
  assert(TurnHubProfiles::readStoredModerationStats(store,"o12345678",loaded)==Status::Ok&&
      loaded.connectionResets==258&&loaded.gameRemovals==3);
  FakeNvs::blobs["o12345678"][0]=2;
  assert(TurnHubProfiles::readStoredModerationStats(store,"o12345678",loaded)==Status::UnsupportedSchema);
  assert(TurnHubProfiles::writeStoredModerationStats(store,"o12345678",stats)==Status::UnsupportedSchema);
  FakeNvs::blobs["o12345678"]={1,0};
  assert(TurnHubProfiles::readStoredModerationStats(store,"o12345678",loaded)==Status::Corrupt);
  assert(TurnHubProfiles::writeStoredModerationStats(store,"o12345678",stats)==Status::Corrupt);
}

void accessibilityRecords() {
  using namespace TurnHubProfiles;
  FakeNvs::reset();
  NvsBlobStore store;
  assert(store.begin("turnhub")==Status::Ok);
  AccessibilityPrefs prefs;
  assert(readAccessibilityPrefs(store,"xABCDEF01",prefs)==Status::NotFound);
  assert(prefs.sigilSound && prefs.ledStyle==LedStyle::Standard && prefs.longPressMs==2000 && prefs.winHoldMs==5000);
  // Independent wire image: sound off, reduced motion, 3000 ms / 6000 ms.
  FakeNvs::blobs["xABCDEF01"]={1,0,1,0xb8,0x0b,0x70,0x17};
  assert(readAccessibilityPrefs(store,"xABCDEF01",prefs)==Status::Ok);
  assert(!prefs.sigilSound && prefs.ledStyle==LedStyle::ReducedMotion && prefs.longPressMs==3000 && prefs.winHoldMs==6000);
  prefs.ledStyle=LedStyle::MonochromeSafe; prefs.winHoldMs=10000;
  assert(writeAccessibilityPrefs(store,"xABCDEF01",prefs)==Status::Ok);
  assert((FakeNvs::blobs["xABCDEF01"]==std::vector<uint8_t>{1,0,2,0xb8,0x0b,0x10,0x27}));
  const int before=FakeNvs::writes;
  assert(writeAccessibilityPrefs(store,"xABCDEF01",prefs)==Status::Ok && FakeNvs::writes==before);
  AccessibilityPrefs bad=prefs; bad.longPressMs=4000; bad.winHoldMs=4500;
  assert(writeAccessibilityPrefs(store,"xABCDEF01",bad)==Status::InvalidArgument);
  bad=prefs; bad.ledStyle=static_cast<LedStyle>(3);
  assert(writeAccessibilityPrefs(store,"xABCDEF01",bad)==Status::InvalidArgument);
  for(const auto &bytes : {std::vector<uint8_t>{}, {1,1,0,0xd0,0x07,0x88},
      {1,2,0,0xd0,0x07,0x88,0x13}, {1,1,3,0xd0,0x07,0x88,0x13}, {1,1,0,0xe8,0x03,0xe8,0x03},
      {1,1,0,0xd1,0x07,0x88,0x13}, {2,1,0,0xd0,0x07,0x88,0x13}, {1,1,0,0xd0,0x07,0x88,0x13,0}}) {
    FakeNvs::blobs["xABCDEF01"]=bytes;
    AccessibilityPrefs read;
    const Status expected=bytes.size()==7&&bytes[0]==2?Status::UnsupportedSchema:Status::Corrupt;
    const int writes=FakeNvs::writes;
    assert(readAccessibilityPrefs(store,"xABCDEF01",read)==expected && read.sigilSound && read.longPressMs==2000);
    assert(writeAccessibilityPrefs(store,"xABCDEF01",AccessibilityPrefs{})==expected && FakeNvs::writes==writes);
    assert(FakeNvs::blobs["xABCDEF01"]==bytes);
  }
}

int main() {
  accountRecords();
  moderationRecords();
  identityContracts();
  existingRecords();
  protectedRecords();
  backendFailures();
  profilePolicyRecords();
  gameSettingsRecords();
  pairingWindowRecords();
  accessibilityRecords();
  std::cout << "PASS: identity, statistics, moderation history, profile policy, game settings, accessibility preferences and NVS failures\n";
}
