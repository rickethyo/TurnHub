#include <cstring>
#include <string>
#include <Arduino.h>
#include <WiFi.h>

#include "atlas_battery.h"
#include "atlas_board.h"
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
// No TFT is initialized on the host, so its hardware screen test is unavailable.
bool TurnHubAtlas::startAtlasScreenTest() { return false; }
static bool fixtureInverted = false;
bool TurnHubAtlas::atlasDisplayInverted() { return fixtureInverted; }
bool TurnHubAtlas::chooseAtlasDisplayInverted(bool inverted) { fixtureInverted = inverted; return true; }
static TurnHubTheme::Id fixtureTheme = TurnHubTheme::Id::Graphite;
TurnHubTheme::Id TurnHubAtlas::atlasDisplayTheme() { return fixtureTheme; }
bool TurnHubAtlas::chooseAtlasDisplayTheme(TurnHubTheme::Id theme) {
  if (!TurnHubTheme::valid(static_cast<uint8_t>(theme))) return false;
  fixtureTheme = theme;
  return true;
}
// Deep sleep is firmware-only: counted instead.
unsigned fixtureSleeps = 0;
bool fixtureSleptEmpty = false;
void TurnHubAtlas::sleepAtlas(bool batteryEmpty) { ++fixtureSleeps; fixtureSleptEmpty = batteryEmpty; }
void TurnHubAtlas::stayAsleepWithoutUsb() {}
void TurnHubAtlas::releaseSleepWakePins() {}

// The microSD card is firmware-only (Arduino SD library); host builds have no card.
void TurnHubAtlas::beginSdCard() {}
String TurnHubAtlas::sdCardDiagnosticsJson() { return "{\"state\":\"no_card\"}"; }
TurnHubStorage::BlobStore *TurnHubAtlas::sdBlobStore() { return nullptr; }
TurnHubHistory::ArchiveFiles *TurnHubAtlas::sdArchiveFiles() { return nullptr; }
bool fixtureSdCardReady = true;
bool TurnHubAtlas::sdCardReady() { return fixtureSdCardReady; }
uint32_t TurnHubAtlas::sdCardGeneration() { return 0; }
bool fixtureSdUsbShared = false;
bool TurnHubAtlas::shareSdCardOverUsb(bool share) {
  if (share && !fixtureSdCardReady) return false;
  fixtureSdUsbShared = share;
  return true;
}
bool TurnHubAtlas::sdCardUsbShareWanted() { return fixtureSdUsbShared; }
bool TurnHubAtlas::sdCardOnUsb() { return fixtureSdUsbShared; }
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

// The battery ADC is firmware-only; scenarios set the reading directly.
TurnHub::BatteryReading fixtureBattery;
void TurnHubAtlas::beginAtlasBattery() {}
void TurnHubAtlas::serviceAtlasBattery(uint32_t) {}
const TurnHub::BatteryReading &TurnHubAtlas::atlasBattery() { return fixtureBattery; }
bool fixtureOnBattery = false;
bool TurnHubAtlas::atlasOnBattery() { return fixtureOnBattery; }

#include "sigil_bus.h"
#include "../../../Sigil/tests/host/test_crypto.h"
#include <cassert>
void fixtureConnectOta(TurnHub::SigilRecord &r) {
  TurnHubTest::TestCrypto crypto;
  TurnHubSecureLink::SigilSession peer;
  uint8_t key[TurnHubSecureLink::KEY_BYTES]={1};peer.configure(0,key);
  TurnHubSecureLink::SecureHelloPacket hello;TurnHubSecureLink::SecureHelloAckPacket ack;
  assert(peer.makeHello(crypto,TurnHubProtocol::encodeHelloInfo(0,9,0,0),hello));
  assert(r.session.acceptHello(crypto,key,0,hello,ack));
}

// The status light is board-specific; host builds have none.
void TurnHubAtlas::beginAtlasStatusLed() {}
void TurnHubAtlas::logAtlasBoard() {}
void TurnHubAtlas::setAtlasStatusLed(bool, bool, bool) {}

// Home Wi-Fi's radio work is firmware-only (home_wifi_link.cpp); the
// setting and its outcomes are kept here so screens and routes can be tested.
#include "home_wifi.h"
bool fixtureHomeWifiOn = false;
std::string fixtureHomeSsid;
unsigned fixtureHomeChecks = 0;
bool TurnHubAtlas::homeWifiEnabled() { return fixtureHomeWifiOn; }
bool TurnHubAtlas::homeWifiConfigured() { return !fixtureHomeSsid.empty(); }
bool TurnHubAtlas::homeWifiConnected() { return false; }
uint8_t TurnHubAtlas::homeWifiNetworkChannel() { return 0; }
bool TurnHubAtlas::setHomeWifiEnabled(bool on) {
  if (on && fixtureHomeSsid.empty()) return false;
  fixtureHomeWifiOn = on;
  if (on) ++fixtureHomeChecks;
  return true;
}
bool TurnHubAtlas::storeHomeWifiNetwork(const char *ssid, const char *) {
  if (ssid == nullptr || ssid[0] == '\0' || strlen(ssid) > TurnHub::HOME_WIFI_SSID_MAX_LENGTH) return false;
  fixtureHomeSsid = ssid;
  return true;
}
bool TurnHubAtlas::forgetHomeWifiNetwork() { fixtureHomeSsid.clear(); fixtureHomeWifiOn = false; return true; }
const char *TurnHubAtlas::homeWifiSsid() { return fixtureHomeSsid.c_str(); }
void TurnHubAtlas::requestHomeWifiCheck() { ++fixtureHomeChecks; }
TurnHub::HomeWifiResult TurnHubAtlas::homeWifiResult() { return TurnHub::HomeWifiResult::None; }
int32_t TurnHubAtlas::homeWifiLastCheckAgeMs(uint32_t) { return -1; }
void TurnHubAtlas::serviceHomeWifi(uint32_t) {}
