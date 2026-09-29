#pragma once
#include "intent.h"
namespace TurnHubAtlas {
// Startup supplies the credentials actually used to bring up the AP.
void beginSigilUpdates(const char *ssid, const char *password);
void serviceSigilUpdates(uint32_t nowMs);
void noteSigilUpdateStatus(uint8_t id, int32_t value, uint32_t nowMs);
bool sigilUpdatesBusy();
void invalidateSigilPackage();
TurnHub::IntentResult handleUpdateSigilIntent(const TurnHub::Intent &, void *);
}
