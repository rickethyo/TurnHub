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
void releaseBrowser(uint8_t controllerId) {
  if (controllerId >= TurnHub::MAX_PHYSICAL_SIGILS && controllerId < TurnHub::MAX_CONTROLLERS) {
    browserProfiles[controllerId - TurnHub::MAX_PHYSICAL_SIGILS][0] = '\0';
  }
}
bool bindPhysical(uint8_t controllerId, uint8_t slot, const String &profileId) {
  auto *bus = TurnHub::SigilBus::activeInstance();
  const auto *record = controllerId < TurnHub::MAX_PHYSICAL_SIGILS && bus ? bus->record(controllerId) : nullptr;
  if (!record || !TurnHubProfiles::bindSeatToProfile(record->mac, slot, profileId)) return false;
  bus->send(controllerId, TurnHubProtocol::PacketType::DisplayProfileRequest);
  return true;
}
}  // namespace TurnHubControllers
