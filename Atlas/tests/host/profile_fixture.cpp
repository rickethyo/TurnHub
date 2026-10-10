#include "avatars.h"
#include "avatar_artwork.h"
#include <vector>
#include <cstring>
#include "profile_fixture.h"
#include "account_access.h"
#include "game_settings_store.h"
#include "pairing_settings.h"
#include "setup_stage.h"
#include "speaker_settings.h"
#include "table_code_setting.h"
namespace TurnHub {
GameSettings fixtureSettings;
TurnHubStorage::Status loadGameSettings(GameSettings &value) {value=fixtureSettings;return TurnHubStorage::Status::Ok;}
TurnHubStorage::Status saveGameSettings(const GameSettings &value) {
  if (!ProfileFixture::gameSettingsWritable) return TurnHubStorage::Status::IoError;
  fixtureSettings=value;return TurnHubStorage::Status::Ok;
}
uint32_t fixturePairingWindowSaved=0;
TurnHubStorage::Status loadPairingWindow(uint32_t &windowMs) {
  if (!fixturePairingWindowSaved) return TurnHubStorage::Status::NotFound;
  windowMs=fixturePairingWindowSaved;return TurnHubStorage::Status::Ok;
}
TurnHubStorage::Status savePairingWindow(uint32_t windowMs) {
  if (!ProfileFixture::gameSettingsWritable) return TurnHubStorage::Status::IoError;
  fixturePairingWindowSaved=windowMs;return TurnHubStorage::Status::Ok;
}
// -1: no record (a new Atlas), otherwise the saved SetupStage.
int fixtureSetupStageSaved=-1;
TurnHubStorage::Status loadSetupStage(SetupStage &stage) {
  if (fixtureSetupStageSaved<0) return TurnHubStorage::Status::NotFound;
  stage=static_cast<SetupStage>(fixtureSetupStageSaved);return TurnHubStorage::Status::Ok;
}
TurnHubStorage::Status saveSetupStage(SetupStage stage) {
  if (!ProfileFixture::gameSettingsWritable) return TurnHubStorage::Status::IoError;
  fixtureSetupStageSaved=static_cast<int>(stage);return TurnHubStorage::Status::Ok;
}
int fixtureSpeakerVolumeSaved=-1;
TurnHubStorage::Status loadSpeakerVolume(uint8_t &volume) {
  if (fixtureSpeakerVolumeSaved<0) return TurnHubStorage::Status::NotFound;
  volume=static_cast<uint8_t>(fixtureSpeakerVolumeSaved);return TurnHubStorage::Status::Ok;
}
TurnHubStorage::Status saveSpeakerVolume(uint8_t volume) {
  if (!ProfileFixture::gameSettingsWritable) return TurnHubStorage::Status::IoError;
  fixtureSpeakerVolumeSaved=volume;return TurnHubStorage::Status::Ok;
}
TurnHubStorage::Status loadTableCodeRequired(bool &) { return TurnHubStorage::Status::NotFound; }
TurnHubStorage::Status saveTableCodeRequired(bool) {
  return ProfileFixture::gameSettingsWritable ? TurnHubStorage::Status::Ok : TurnHubStorage::Status::IoError;
}
}
namespace ProfileFixture {
bool gameSettingsWritable = true;
void (*afterStatsSave)() = nullptr;
std::map<std::string,Profile> profiles;
std::map<std::string,String> bindings;
String key(const uint8_t *mac,uint8_t slot) { return String(mac[5])+":"+String(slot); }
}
namespace TurnHubProfiles {
using namespace ProfileFixture;
bool begin() { return true; }
bool ready() { return true; }
bool profileExists(const String &id) { return profiles.count(id)!=0; }
String createProfile() {
  char id[9]; do { snprintf(id,sizeof(id),"%08lX",static_cast<unsigned long>(esp_random())); } while(profileExists(id));
  profiles[id]=Profile{}; return id;
}
String createProfileWithCredentials(const String &name,const String &pin,PinHasher hash) {
  char ids[MAX_LOGIN_PROFILES][9];
  if(listProfileIds(ids,MAX_LOGIN_PROFILES)>=MAX_LOGIN_PROFILES) return String();
  const String id=createProfile(); profiles[id].name=name; profiles[id].hash=hash(id,pin); return id;
}
String createProfileWithName(const String &name) {
  char ids[MAX_LOGIN_PROFILES][9];
  if(name.length()==0||listProfileIds(ids,MAX_LOGIN_PROFILES)>=MAX_LOGIN_PROFILES) return String();
  const String id=createProfile(); profiles[id].name=name; return id;
}
size_t listProfileIds(char (*ids)[9],size_t capacity) {
  size_t n=0; for(const auto &p:profiles) {
    if(n==capacity)break;
    if(p.second.name.empty() && p.second.hash.empty() && !p.second.stats.gamesPlayed)continue;
    memcpy(ids[n++],p.first.c_str(),9);
  } return n;
}
String profileIdForSeat(const uint8_t *mac,uint8_t slot) {
  return boundProfileIdForSeat(mac,slot);
}
String boundProfileIdForSeat(const uint8_t *mac,uint8_t slot) {
  const auto found=bindings.find(key(mac,slot));
  return found==bindings.end() ? String() : found->second;
}
bool seatIsPersistent(const uint8_t *,uint8_t) { return false; }
bool setSeatPersistent(const uint8_t *,uint8_t,bool) { return false; }
bool resetTransientSeatBindings(const uint8_t *mac) {
  bindings.erase(key(mac,1));
  bindings.erase(key(mac,2));
  return true;
}
bool releaseSeatBinding(const uint8_t *mac,uint8_t slot) { if(slot!=1&&slot!=2)return false; bindings.erase(key(mac,slot)); return true; }
bool bindSeatToProfile(const uint8_t *mac,uint8_t slot,const String &id) { if(!profileExists(id))return false; bindings[key(mac,slot)]=id; return true; }
bool moveSeatProfile(const uint8_t *mac,uint8_t from,uint8_t to,const String &id) {
  if(boundProfileIdForSeat(mac,from)!=id || boundProfileIdForSeat(mac,to).length())return false;
  bindings.erase(key(mac,from));bindings[key(mac,to)]=id;return true;
}
String nameForProfile(const String &id) { return profileExists(id)?profiles[id].name:String(); }
bool setNameForProfile(const String &id,const String &name) { if(!profileExists(id))return false; profiles[id].name=name; return true; }
String storedPinHashForProfile(const String &id) { return profileExists(id)?profiles[id].hash:String(); }
bool hasPinForProfile(const String &id) { return storedPinHashForProfile(id).length()==64; }
bool loadPolicyForProfile(const String &id,ProfilePolicy &policy) {
  if(!profileExists(id)||!profiles[id].policyReadable)return false;
  policy=profiles[id].policy;return true;
}
bool savePolicyForProfile(const String &id,const ProfilePolicy &policy) {
  if(!profileExists(id)||!profiles[id].policyReadable)return false;
  profiles[id].policy=policy;return true;
}
bool loadAccessibilityForProfile(const String &id,AccessibilityPrefs &prefs) {
  prefs=AccessibilityPrefs{};
  if(!profileExists(id))return false;
  prefs=profiles[id].accessibility;
  return true;
}
bool saveAccessibilityForProfile(const String &id,const AccessibilityPrefs &prefs) {
  if(!profileExists(id)||!validAccessibilityPrefs(prefs))return false;
  profiles[id].accessibility=prefs;
  return true;
}
bool setPinHashForProfile(const String &id,const String &hash) { if(!profileExists(id))return false;profiles[id].hash=hash;return true; }
bool clearPinForProfile(const String &id) { return setPinHashForProfile(id,""); }
String storedPinHashForSeat(const uint8_t *mac,uint8_t slot) { return storedPinHashForProfile(profileIdForSeat(mac,slot)); }
bool setPinHashForSeat(const uint8_t *mac,uint8_t slot,const String &hash) { return setPinHashForProfile(profileIdForSeat(mac,slot),hash); }
bool clearPinForSeat(const uint8_t *mac,uint8_t slot) { return clearPinForProfile(profileIdForSeat(mac,slot)); }
bool hasPinForSeat(const uint8_t *mac,uint8_t slot) { return hasPinForProfile(profileIdForSeat(mac,slot)); }
String deviceName(const uint8_t *) { return String(); }
bool setDeviceName(const uint8_t *,const String &) { return true; }
bool loadStatsForProfile(const String &id,ProfileStats &stats,bool *detailed) { if(detailed)*detailed=true; if(!profileExists(id))return false;stats=profiles[id].stats;return true; }
std::map<std::string, uint32_t> jewelColors;
bool jewelColorForProfile(const String &id, uint32_t &rgb) {
  const auto it = jewelColors.find(id.c_str());
  if (it == jewelColors.end()) return false;
  rgb = it->second;
  return true;
}
bool saveJewelColorForProfile(const String &id, bool set, uint32_t rgb) {
  if (!profileExists(id)) return false;
  if (set) jewelColors[id.c_str()] = rgb; else jewelColors.erase(id.c_str());
  return true;
}
std::map<std::string, uint8_t> avatars;
uint8_t avatarForProfile(const String &id) {
  const auto it = avatars.find(id.c_str());
  return it == avatars.end() ? 0 : it->second;
}
bool saveAvatarForProfile(const String &id, uint8_t avatar) {
  if (!profileExists(id)) return false;
  if (avatar) avatars[id.c_str()] = avatar; else avatars.erase(id.c_str());
  return true;
}
namespace {
class ArtworkFixture final : public TurnHubStorage::BlobStore {
 public:
  TurnHubStorage::Status read(const char *key, void *data, size_t capacity, size_t &size) override {
    using TurnHubStorage::Status;
    size = 0; if (!ProfileFixture::artworkAvailable) return Status::Unavailable;
    auto it = records.find(key); if (it == records.end()) return Status::NotFound;
    size = it->second.size(); if (!data) return Status::Ok;
    if (capacity < size) return Status::Corrupt;
    memcpy(data, it->second.data(), size); return Status::Ok;
  }
  TurnHubStorage::Status write(const char *key, const void *data, size_t size) override {
    if (!ProfileFixture::artworkAvailable || !ProfileFixture::artworkWritable) return TurnHubStorage::Status::IoError;
    if (!size || size > 1024) return TurnHubStorage::Status::InvalidArgument;
    const auto *bytes = static_cast<const uint8_t *>(data);
    records[key] = std::vector<uint8_t>(bytes, bytes + size); return TurnHubStorage::Status::Ok;
  }
  TurnHubStorage::Status remove(const char *key) override {
    if (!ProfileFixture::artworkAvailable || !ProfileFixture::artworkWritable) return TurnHubStorage::Status::IoError;
    return records.erase(key) ? TurnHubStorage::Status::Ok : TurnHubStorage::Status::NotFound;
  }
  std::map<std::string, std::vector<uint8_t>> records;
} artwork;
}
TurnHubStorage::BlobStore *profileArtworkStore() { return ProfileFixture::artworkAvailable ? &artwork : nullptr; }
String artworkPath(const String &id, bool pending) {
  if (!profileArtworkStore() || !profileExists(id)) return {};
  TurnHubArtwork::Image image;
  if (TurnHubArtwork::metadata(artwork, (String(pending ? "p" : "a") + id).c_str(), image) != TurnHubStorage::Status::Ok) return {};
  char revision[9]; snprintf(revision, sizeof(revision), "%08lx", static_cast<unsigned long>(image.revision));
  return String("/api/avatar?profileId=") + id + "&revision=" + revision + (pending ? "&pending=1" : "");
}
const uint8_t *artworkThumbnail(const String &id, uint32_t &revision) {
  static uint8_t thumbnail[257]; revision = 0;
  if (avatarForProfile(id) != TurnHubAvatars::AVATAR_CUSTOM) return nullptr;
  TurnHubArtwork::Image image;
  if (TurnHubArtwork::metadata(artwork, (String("a") + id).c_str(), image) != TurnHubStorage::Status::Ok) return nullptr;
  char key[16]; TurnHubArtwork::thumbnailKey(key, image.revision); size_t size = 0;
  if (artwork.read(key, thumbnail, sizeof(thumbnail), size) != TurnHubStorage::Status::Ok || size != sizeof(thumbnail) || thumbnail[0] != 1) return nullptr;
  revision = image.revision; return thumbnail + 1;
}

void setLuxuryStore(TurnHubStorage::BlobStore *) {}
bool luxuryStoreAvailable() { return true; }
bool saveStatsForProfile(const String &id,const ProfileStats &stats) {
  if(!profileExists(id))return false;
  profiles[id].stats=stats;
  if(afterStatsSave)afterStatsSave();
  return true;
}
bool loadModerationStatsForProfile(const String &id,ModerationStats &stats) { if(!profileExists(id))return false;stats=profiles[id].moderation;return true; }
bool saveModerationStatsForProfile(const String &id,const ModerationStats &stats) { if(!profileExists(id))return false;profiles[id].moderation=stats;return true; }
}

namespace TurnHubAccounts {
std::map<std::string,Account> accounts;
String primary;
bool load(const String &id,Account &a){if(!TurnHubProfiles::profileExists(id))return false;a=accounts[id];return true;}
bool save(const String &id,const Account &a){if(!TurnHubProfiles::profileExists(id))return false;accounts[id]=a;return true;}
bool primaryAdmin(String &id){id=primary;return true;}
bool establishAdmin(const String &id){if(primary.length()||!TurnHubProfiles::hasPinForProfile(id))return false;accounts[id].permissions|=Admin;primary=id;return true;}
}

namespace ProfileFixture { bool artworkAvailable = true; bool artworkWritable = true; }
