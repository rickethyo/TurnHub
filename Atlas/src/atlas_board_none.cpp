// Master's board-less Atlas (atlas_board.h): the game engine, radio, portal
// and storage run with no screen, speaker, battery or status LED. The Gen 1
// E32R28T drivers live on legacy/gen1; the ESP32-S3 board profile replaces
// this file. Firmware-only; host tests use test_globals.cpp instead.

#include "atlas_board.h"
#include "atlas_battery.h"
#include "atlas_display.h"
#include "atlas_speaker.h"

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "config.h"
#include "optional_preferences.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

namespace {
// The display theme is still a saved table setting (the Atlas screen of the
// next board draws it), so it keeps its NVS keys.
TurnHubTheme::Id theme = TurnHubTheme::Id::Graphite;
bool inverted = false;
TurnHub::BatteryReading noBattery;  // present = false
constexpr gpio_num_t WAKE_BOOT_PIN = static_cast<gpio_num_t>(AtlasConfig::BOOT_BUTTON_PIN);
}  // namespace

void beginAtlasStatusLed() {}
void setAtlasStatusLed(bool, bool, bool) {}

void beginAtlasDisplay() {
  TurnHub::OptionalPreferences prefs;
  if (prefs.begin("display", true)) {
    const String key = prefs.getString("theme", "graphite");
    inverted = prefs.getBool("inverted", false);
    prefs.end();
    for (uint8_t i = 0; i < static_cast<uint8_t>(TurnHubTheme::Id::Count); ++i) {
      const auto id = static_cast<TurnHubTheme::Id>(i);
      if (key == TurnHubTheme::palette(id).key) theme = id;
    }
  }
  serialLog.println("ATLAS|DISPLAY|NONE");
}

void serviceAtlasDisplay(uint32_t) {}
bool startAtlasScreenTest() { return false; }
TurnHubTheme::Id atlasDisplayTheme() { return theme; }

bool chooseAtlasDisplayTheme(TurnHubTheme::Id id) {
  if (!TurnHubTheme::valid(static_cast<uint8_t>(id))) return false;
  TurnHub::OptionalPreferences prefs;
  if (!prefs.begin("display", false)) return false;
  const String key(TurnHubTheme::palette(id).key);
  const bool saved = prefs.putString("theme", key) == key.length();
  prefs.end();
  if (saved) theme = id;
  return saved;
}

bool atlasDisplayInverted() { return inverted; }

bool chooseAtlasDisplayInverted(bool value) {
  TurnHub::OptionalPreferences prefs;
  if (!prefs.begin("display", false)) return false;
  const bool saved = prefs.putBool("inverted", value) == 1;
  prefs.end();
  if (saved) inverted = value;
  return saved;
}

// No battery, so a sleep is always on USB: BOOT wakes Atlas (a restart).
void sleepAtlas(bool) {
  while (digitalRead(AtlasConfig::BOOT_BUTTON_PIN) == LOW) delay(10);
  serialLog.println("ATLAS|SLEEP|POWER_DOWN");
  Serial.flush();
  rtc_gpio_pullup_en(WAKE_BOOT_PIN);
  rtc_gpio_pulldown_dis(WAKE_BOOT_PIN);
  esp_sleep_enable_ext0_wakeup(WAKE_BOOT_PIN, 0);
  esp_deep_sleep_start();
}

void stayAsleepWithoutUsb() {}

void releaseSleepWakePins() {
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) rtc_gpio_deinit(WAKE_BOOT_PIN);
}

void beginAtlasBattery() {}
void serviceAtlasBattery(uint32_t) {}
const TurnHub::BatteryReading &atlasBattery() { return noBattery; }
bool atlasOnBattery() { return false; }

TurnHub::ToneOutput *beginAtlasSpeaker() { return nullptr; }
void serviceAtlasSpeaker(uint32_t) {}

}  // namespace TurnHubAtlas
