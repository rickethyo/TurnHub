// The S3 Atlas's board parts that have no driver yet (atlas_board.h): no
// speaker, battery or status LED, and BOOT sleep. Its screen is
// atlas_display_s3.cpp; this file shrinks as the other drivers land. The
// Gen 1 E32R28T drivers live on legacy/gen1. Firmware-only; host tests use
// test_globals.cpp instead.

#include "atlas_board.h"
#include "atlas_battery.h"
#include "atlas_display.h"
#include "atlas_speaker.h"

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_sleep.h>

#include "config.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

namespace {
TurnHub::BatteryReading noBattery;  // present = false
constexpr gpio_num_t WAKE_BOOT_PIN = static_cast<gpio_num_t>(AtlasConfig::BOOT_BUTTON_PIN);
}  // namespace

void beginAtlasStatusLed() {}
void setAtlasStatusLed(bool, bool, bool) {}

void logAtlasBoard() {
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  // The JEDEC ID's last byte is log2 of the part's size; the image header's
  // size (ESP.getFlashChipSize) is only what the build assumed.
  uint32_t jedec = 0;
  esp_flash_read_id(nullptr, &jedec);
  const uint8_t sizeCode = jedec & 0xFF;
  const uint32_t flashKB = sizeCode >= 10 && sizeCode < 32 ? (1UL << sizeCode) / 1024UL : 0;
  char line[160];
  snprintf(line, sizeof(line),
           "ATLAS|BOARD|%s|REV|%u|CORES|%u|FLASH_ID|%06lX|FLASH_KB|%lu|HEADER_KB|%lu|PSRAM_KB|%lu",
           ESP.getChipModel(), static_cast<unsigned>(chip.revision),
           static_cast<unsigned>(chip.cores), static_cast<unsigned long>(jedec),
           static_cast<unsigned long>(flashKB),
           static_cast<unsigned long>(ESP.getFlashChipSize() / 1024UL),
           static_cast<unsigned long>(ESP.getPsramSize() / 1024UL));
  serialLog.println(line);
}

// The S3 Atlas draws its screen in atlas_display_s3.cpp.

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
