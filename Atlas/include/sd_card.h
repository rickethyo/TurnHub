#pragma once

#include <Arduino.h>
#include "storage.h"
#include "firmware_package.h"
#include "portal_pack.h"

class WebServer;

// Atlas's microSD slot. Gameplay never waits for or depends on the card. Luxury
// records (detailed statistics), diagnostics and the web portal pack live on
// it; core records stay in NVS. Every Atlas ships with a card, which the V1
// portal needs (owner decision 2026-10-02); without one Atlas serves the
// built-in portal from flash. A separate low-priority worker task drains
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

// --- Web portal pack (portal_pack.h, PORTAL_PACK.md) --------------------------
// The card as the portal installer's Files, or nullptr without a working card.
// Each call takes the card lock. Call from the application task.
TurnHubPortal::Files *sdPortalFiles();
// The installed pack's version; false when there is no card or no pack.
// Cached per mount; sdPortalChanged() after an install refreshes it.
bool sdPortalVersion(TurnHubFirmwarePackage::Version &version);
void sdPortalChanged();
// Streams `relPath` (a safePackPath) from the live pack: its .gz copy with
// Content-Encoding: gzip when there is one. False (nothing sent) when the card,
// the pack or the file is missing. Holds the card lock while it streams.
bool sdServePortalFile(WebServer &server, const char *relPath, const char *cacheControl);
// Atlas factory reset (owner decision 2026-09-29): deletes everything on a
// mounted card, then unmounts it, under the card lock, so the card is empty as
// after a format (this framework cannot reformat a card). The one exception is
// the installed web portal pack (TurnHubPortal::ROOT_DIR): it is product
// software, not table data, and the portal must survive a reset (2026-10-02).
// Atlas recreates its folder at the next mount. Returns the entries removed, or -1 with no
// working card. Blocks the caller; only for the reset just before restart.
int wipeSdCard();

}  // namespace TurnHubAtlas
