#pragma once
#include <stddef.h>
#include <stdint.h>
#include "intent.h"
namespace TurnHubAtlas {
// Startup supplies the credentials actually used to bring up the AP.
void beginSigilUpdates(const char *ssid, const char *password);
void serviceSigilUpdates(uint32_t nowMs);
void noteSigilUpdateStatus(uint8_t id, int32_t value, uint32_t nowMs);
bool sigilUpdatesBusy();
// A short line for the Atlas screen while a Sigil update runs and for a
// minute after it ends ("Sigil 2 update: Downloading 40%", "... failed: ...").
// False when there is nothing to show.
bool sigilUpdateNotice(char *out, size_t size, uint32_t nowMs);
void invalidateSigilPackage();
TurnHub::IntentResult handleUpdateSigilIntent(const TurnHub::Intent &, void *);
}
