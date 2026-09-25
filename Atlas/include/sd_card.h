#pragma once

#include <Arduino.h>
#include "storage.h"

// Atlas's microSD slot. Optional storage: gameplay never waits for or depends
// on the card. Only luxury records (detailed statistics) and diagnostics live
// on it; core records stay in NVS. Diagnostics are drained by a separate
// low-priority task, never the gameplay loop. The card is mounted
// once at boot; a card inserted later needs a restart. Firmware-only (needs
// the Arduino SD library); host tests use the stubs in test_globals.cpp.

namespace TurnHubAtlas {

// Mounts the card on its own VSPI bus, never formatting it, then writes and
// reads back a self-test record. Logs the outcome; never blocks startup on
// failure.
void beginSdCard();
// Card presence, type, capacity and the boot self-test result, as JSON for
// the Developer diagnostics page.
String sdCardDiagnosticsJson();
// Record store on the card, or nullptr unless mount AND read/write self-test
// succeeded and no later logging I/O error occurred. Detailed statistics use
// it (profile_store.cpp). Every call takes the card lock the log worker also
// holds while writing, and re-checks the card, so a store kept from boot turns
// Unavailable after a later I/O error. Call it from the application task.
TurnHubStorage::BlobStore *sdBlobStore();
// A card is mounted and its store works (the "NO SD CARD" warning otherwise).
bool sdCardReady();

}  // namespace TurnHubAtlas
