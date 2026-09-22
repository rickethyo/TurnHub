#include "controller_profiles.h"
#include "profile_store.h"
#include "sigil_bus.h"
#include <string.h>

namespace TurnHubControllers {
namespace {
char browserProfiles[TurnHub::MAX_PLAYERS][TurnHubProfiles::PROFILE_ID_LENGTH + 1] = {};
}
String profileForSeat(uint8_t controllerId, uint8_t slot) {
  if (controllerId >= TurnHub::MAX_CONTROLLERS || (slot != 1 && slot != 2)) return String();
  if (controllerId >= TurnHub::MAX_PHYSICAL_SIGILS) {
    return slot == 1 ? String(browserProfiles[controllerId - TurnHub::MAX_PHYSICAL_SIGILS]) : String();
  }
  auto *bus = TurnHub::SigilBus::activeInstance();
  const auto *record = bus ? bus->record(controllerId) : nullptr;
  return record ? TurnHubProfiles::profileIdForSeat(record->mac, slot) : String();
}
String existingProfileForSeat(uint8_t controllerId, uint8_t slot) {
  if (controllerId >= TurnHub::MAX_PHYSICAL_SIGILS || (slot != 1 && slot != 2)) return String();
  auto *bus = TurnHub::SigilBus::activeInstance();
  const auto *record = bus ? bus->record(controllerId) : nullptr;
  return record ? TurnHubProfiles::boundProfileIdForSeat(record->mac, slot) : String();
}
uint8_t registerBrowser(const String &profileId) {
  if (!TurnHubProfiles::profileExists(profileId)) return TurnHub::INVALID_ID;
  for (uint8_t i = 0; i < TurnHub::MAX_PLAYERS; ++i) {
    if (profileId == browserProfiles[i]) return TurnHub::MAX_PHYSICAL_SIGILS + i;
  }
  for (uint8_t i = 0; i < TurnHub::MAX_PLAYERS; ++i) {
    if (browserProfiles[i][0] == '\0') {
      strncpy(browserProfiles[i], profileId.c_str(), sizeof(browserProfiles[i]) - 1);
      return TurnHub::MAX_PHYSICAL_SIGILS + i;
    }
  }
  return TurnHub::INVALID_ID;
}
bool restoreBrowser(uint8_t controllerId, const char *profileId) {
  if (controllerId < TurnHub::MAX_PHYSICAL_SIGILS || controllerId >= TurnHub::MAX_CONTROLLERS ||
      !profileId || strlen(profileId) != TurnHubProfiles::PROFILE_ID_LENGTH) return false;
  memcpy(browserProfiles[controllerId - TurnHub::MAX_PHYSICAL_SIGILS], profileId,
      TurnHubProfiles::PROFILE_ID_LENGTH + 1);
  return true;
}
void releaseBrowser(uint8_t controllerId) {
  if (controllerId >= TurnHub::MAX_PHYSICAL_SIGILS && controllerId < TurnHub::MAX_CONTROLLERS) {
    browserProfiles[controllerId - TurnHub::MAX_PHYSICAL_SIGILS][0] = '\0';
  }
}
bool bindPhysical(uint8_t controllerId, uint8_t slot, const String &profileId) {
  auto *bus = TurnHub::SigilBus::activeInstance();
  const auto *record = controllerId < TurnHub::MAX_PHYSICAL_SIGILS && bus ? bus->record(controllerId) : nullptr;
  if (!record || (slot != 1 && slot != 2)) return false;
  const uint8_t other = slot == 1 ? 2 : 1;
  const bool moving = TurnHubProfiles::boundProfileIdForSeat(record->mac, other) == profileId;
  if (!(moving ? TurnHubProfiles::moveSeatProfile(record->mac, other, slot, profileId) :
      TurnHubProfiles::bindSeatToProfile(record->mac, slot, profileId))) return false;
  bus->syncDisplayProfile(controllerId);
  return true;
}
}  // namespace TurnHubControllers
