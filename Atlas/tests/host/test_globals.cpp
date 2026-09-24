#include <Arduino.h>
#include <WiFi.h>

#include "atlas_display.h"
#include "sd_card.h"

uint32_t testNow = 1000;
int testDigitalRead = HIGH;
SerialStub Serial;
int loggedErrors = 0;
WiFiStub WiFi;
uint32_t testRandom = 12345;
EspStub ESP;

// The TFT is firmware-only presentation; host builds draw nothing.
void TurnHubAtlas::beginAtlasDisplay() {}
void TurnHubAtlas::serviceAtlasDisplay(uint32_t) {}

// The microSD card is firmware-only (Arduino SD library); host builds have no card.
void TurnHubAtlas::beginSdCard() {}
String TurnHubAtlas::sdCardDiagnosticsJson() { return "{\"state\":\"no_card\"}"; }
TurnHubStorage::BlobStore *TurnHubAtlas::sdBlobStore() { return nullptr; }
