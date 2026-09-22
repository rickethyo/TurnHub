#pragma once
#include <Arduino.h>
#include "turnhub_types.h"

namespace TurnHubControllers {
// Atlas registration table, separate from radio discovery and profile storage.
String profileForSeat(uint8_t controllerId, uint8_t slot);
String existingProfileForSeat(uint8_t controllerId, uint8_t slot);
uint8_t registerBrowser(const String &profileId);
bool restoreBrowser(uint8_t controllerId, const char *profileId);
void releaseBrowser(uint8_t controllerId);
bool bindPhysical(uint8_t controllerId, uint8_t slot, const String &profileId);
}  // namespace TurnHubControllers
