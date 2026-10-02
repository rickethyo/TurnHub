#include <Arduino.h>
#include <WiFi.h>

#include "atlas_display.h"
#include "atlas_speaker.h"
#include "sd_card.h"
#include "secure_link_backend.h"

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
// Deep sleep is firmware-only: counted instead.
unsigned fixtureSleeps = 0;
void TurnHubAtlas::sleepAtlas() { ++fixtureSleeps; }
void TurnHubAtlas::releaseSleepWakePins() {}

// The microSD card is firmware-only (Arduino SD library); host builds have no card.
void TurnHubAtlas::beginSdCard() {}
String TurnHubAtlas::sdCardDiagnosticsJson() { return "{\"state\":\"no_card\"}"; }
TurnHubStorage::BlobStore *TurnHubAtlas::sdBlobStore() { return nullptr; }
bool fixtureSdCardReady = true;
bool TurnHubAtlas::sdCardReady() { return fixtureSdCardReady; }
uint32_t TurnHubAtlas::sdCardGeneration() { return 0; }
// No pack on a host card: every page falls back to the built-in one.
bool TurnHubAtlas::sdServePortalFile(WebServer &, const char *, const char *) { return false; }
// Firmware-only (mbedTLS); the link logic itself is host-tested in
// Sigil/tests/host/secure_link_scenarios.cpp.
bool TurnHubAtlas::runSecureLinkSelfTest() { return true; }
// Firmware-only NVS erase + restart (factory_reset.cpp) and SD wipe
// (sd_card.cpp): counted instead, with the order they ran in.
unsigned fixtureFactoryResets = 0;
unsigned fixtureSdWipes = 0;
bool fixtureSdWipedBeforeErase = false;
namespace TurnHubAtlas {
void eraseSettingsAndRestart() {
  fixtureSdWipedBeforeErase = fixtureSdWipes > fixtureFactoryResets;
  ++fixtureFactoryResets;
}
int wipeSdCard() { ++fixtureSdWipes; return fixtureSdCardReady ? 0 : -1; }
}

// The speaker is firmware-only (ESP32 DAC); host builds have none.
TurnHub::ToneOutput *TurnHubAtlas::beginAtlasSpeaker() { return nullptr; }
void TurnHubAtlas::serviceAtlasSpeaker(uint32_t) {}

#include "sigil_bus.h"
#include "../../../Sigil/tests/host/test_crypto.h"
#include <cassert>
void fixtureConnectOta(TurnHub::SigilRecord &r) {
  TurnHubTest::TestCrypto crypto;
  TurnHubSecureLink::SigilSession peer;
  uint8_t key[TurnHubSecureLink::KEY_BYTES]={1};peer.configure(0,key);
  TurnHubSecureLink::SecureHelloPacket hello;TurnHubSecureLink::SecureHelloAckPacket ack;
  assert(peer.makeHello(crypto,TurnHubProtocol::encodeHelloInfo(0,9,0,TurnHubProtocol::CAPABILITY_DISPLAY_OLED),hello));
  assert(r.session.acceptHello(crypto,key,0,hello,ack));
}
