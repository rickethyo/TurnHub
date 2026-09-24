#pragma once

#include <Arduino.h>
#include "storage.h"

// Atlas's microSD slot. Optional storage: gameplay never waits for or depends
// on the card, and nothing authoritative lives on it yet. The card is mounted
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
// Record store on the card (directory /turnhub), or nullptr if no card is
// mounted. Callers must treat nullptr and every error as "card unavailable".
TurnHubStorage::BlobStore *sdBlobStore();

}  // namespace TurnHubAtlas
