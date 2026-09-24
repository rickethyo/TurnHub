#include "profile_fixture.h"
#include "account_access.h"
#include "game_settings_store.h"
namespace TurnHub {
GameSettings fixtureSettings;
TurnHubStorage::Status loadGameSettings(GameSettings &value) {value=fixtureSettings;return TurnHubStorage::Status::Ok;}
TurnHubStorage::Status saveGameSettings(const GameSettings &value) {
  if (!ProfileFixture::gameSettingsWritable) return TurnHubStorage::Status::IoError;
  fixtureSettings=value;return TurnHubStorage::Status::Ok;
}
}
namespace ProfileFixture {
bool gameSettingsWritable = true;
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
bool setPinHashForProfile(const String &id,const String &hash) { if(!profileExists(id))return false;profiles[id].hash=hash;return true; }
bool clearPinForProfile(const String &id) { return setPinHashForProfile(id,""); }
String storedPinHashForSeat(const uint8_t *mac,uint8_t slot,bool *legacy) { if(legacy)*legacy=false;return storedPinHashForProfile(profileIdForSeat(mac,slot)); }
bool setPinHashForSeat(const uint8_t *mac,uint8_t slot,const String &hash) { return setPinHashForProfile(profileIdForSeat(mac,slot),hash); }
bool clearPinForSeat(const uint8_t *mac,uint8_t slot) { return clearPinForProfile(profileIdForSeat(mac,slot)); }
bool hasPinForSeat(const uint8_t *mac,uint8_t slot) { return hasPinForProfile(profileIdForSeat(mac,slot)); }
String deviceName(const uint8_t *) { return String(); }
bool setDeviceName(const uint8_t *,const String &) { return true; }
bool loadStatsForProfile(const String &id,ProfileStats &stats) { if(!profileExists(id))return false;stats=profiles[id].stats;return true; }
bool saveStatsForProfile(const String &id,const ProfileStats &stats) { if(!profileExists(id))return false;profiles[id].stats=stats;return true; }
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
