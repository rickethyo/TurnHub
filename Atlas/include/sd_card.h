#pragma once

#include <Arduino.h>
#include "storage.h"

// Atlas's microSD slot. Optional storage: gameplay never waits for or depends
// on the card. Only luxury records (detailed statistics) and diagnostics live
// on it; core records stay in NVS. A separate low-priority worker task drains
// diagnostics and handles hot-plug (sd_hotplug.h): it notices a pulled card
// and mounts one inserted later, without a restart, never on the gameplay
// loop. Firmware-only (needs the Arduino SD library); host tests use the
// stubs in test_globals.cpp.

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
// Changes whenever a card is mounted or dropped. The application loop
// compares it to re-point the luxury store (refreshSdLuxuryStore, main.cpp).
uint32_t sdCardGeneration();
// Atlas factory reset (owner decision 2026-09-29): deletes everything on a
// mounted card, then unmounts it, under the card lock, so the card is empty
// as after a format (this framework cannot reformat a card). Atlas recreates
// its folder at the next mount. Returns the entries removed, or -1 with no
// working card. Blocks the caller; only for the reset just before restart.
int wipeSdCard();

}  // namespace TurnHubAtlas
