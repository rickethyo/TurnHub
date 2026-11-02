// The S3 Atlas's board parts that have no driver yet (atlas_board.h): no
// status LED, and Sleep (woken by a touch or BOOT). Its screen is atlas_display_s3.cpp
// and its battery atlas_battery_s3.cpp; this file shrinks as the other
// drivers land. The
// Gen 1 E32R28T drivers live on legacy/gen1. Firmware-only; host tests use
// test_globals.cpp instead.

#include "atlas_board.h"
#include "atlas_battery.h"
#include "atlas_display.h"
#include "atlas_speaker.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_sleep.h>

#include "config.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

namespace {
constexpr gpio_num_t WAKE_BOOT_PIN = static_cast<gpio_num_t>(AtlasConfig::BOOT_BUTTON_PIN);
// The FT6336U pulls its interrupt low while the screen is touched (an RTC
// pin, so it can wake deep sleep). Its reset line is held high through sleep
// so the controller keeps scanning, and the backlight (GPIO45, also the
// VDD_SPI strapping pin) is held low so the panel stays dark and the wake
// restart reads the 3.3 V flash strap.
constexpr gpio_num_t WAKE_TOUCH_PIN = static_cast<gpio_num_t>(AtlasConfig::TOUCH_INT_PIN);
constexpr gpio_num_t TOUCH_RESET_PIN = static_cast<gpio_num_t>(AtlasConfig::TOUCH_RST_PIN);
constexpr gpio_num_t BACKLIGHT_PIN = static_cast<gpio_num_t>(AtlasConfig::LCD_BACKLIGHT_PIN);
constexpr uint32_t TOUCH_RELEASE_WAIT_MS = 3000;

// Asleep on the cell, the timer wakes Atlas this often to look for USB. The
// first check comes soon after the load is gone, to read the resting cell
// before USB could come back.
constexpr uint64_t USB_CHECK_INTERVAL_US = 60ULL * 1000000ULL;
constexpr uint64_t USB_FIRST_CHECK_US = 5ULL * 1000000ULL;
// The board has no USB-sense pin. Asleep, the cell rests with no load, so
// the charger pushing it up this far over the last check (or to near full)
// means USB is back. *Needs verification* on the ES3C40P's cell (values
// carried over from the Gen 1 502030).
constexpr uint16_t USB_STEP_MV = 80;
constexpr uint16_t USB_FULL_MV = 4150;
constexpr uint8_t USB_CHECK_READS = 32;

// Kept through deep sleep (RTC slow memory); cleared by a cold start.
RTC_DATA_ATTR bool sleptOnBattery = false;
RTC_DATA_ATTR bool sleptEmpty = false;
RTC_DATA_ATTR uint16_t restingCellMv = 0;

uint16_t readRestingCellMv() {
  analogSetPinAttenuation(AtlasConfig::BATTERY_ADC_PIN, ADC_11db);
  uint32_t total = 0;
  for (uint8_t i = 0; i < USB_CHECK_READS; ++i) total += analogReadMilliVolts(AtlasConfig::BATTERY_ADC_PIN);
  return static_cast<uint16_t>(total / USB_CHECK_READS * AtlasConfig::BATTERY_DIVIDER_NUMERATOR /
      AtlasConfig::BATTERY_DIVIDER_DENOMINATOR);
}

void holdOutput(gpio_num_t pin, int level) {
  gpio_hold_dis(pin);
  gpio_reset_pin(pin);
  gpio_set_direction(pin, GPIO_MODE_OUTPUT);
  gpio_set_level(pin, level);
  gpio_hold_en(pin);
}

void armWakeAndSleep() {
  holdOutput(BACKLIGHT_PIN, 0);
  holdOutput(TOUCH_RESET_PIN, 1);
  gpio_deep_sleep_hold_en();
  rtc_gpio_pullup_en(WAKE_BOOT_PIN);
  rtc_gpio_pulldown_dis(WAKE_BOOT_PIN);
  esp_sleep_enable_ext0_wakeup(WAKE_BOOT_PIN, 0);
  // Mode 0 is "all low" (ESP-IDF 4) or "any low" (ESP-IDF 5); with one pin
  // both mean touched.
  rtc_gpio_init(WAKE_TOUCH_PIN);
  rtc_gpio_set_direction(WAKE_TOUCH_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(WAKE_TOUCH_PIN);
  rtc_gpio_pulldown_dis(WAKE_TOUCH_PIN);
  esp_sleep_enable_ext1_wakeup(1ULL << WAKE_TOUCH_PIN, static_cast<esp_sleep_ext1_wakeup_mode_t>(0));
  // The wake pins' pull-ups live in the RTC peripherals.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  if (sleptOnBattery) esp_sleep_enable_timer_wakeup(restingCellMv == 0 ? USB_FIRST_CHECK_US : USB_CHECK_INTERVAL_US);
  esp_deep_sleep_start();
}
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

// A touch or BOOT wakes Atlas (a restart); on the cell, so does USB coming
// back.
void sleepAtlas(bool batteryEmpty) {
  while (digitalRead(AtlasConfig::BOOT_BUTTON_PIN) == LOW) delay(10);
  // A finger still on the glass would wake Atlas straight away.
  pinMode(AtlasConfig::TOUCH_INT_PIN, INPUT_PULLUP);
  const uint32_t waitStart = millis();
  while (digitalRead(AtlasConfig::TOUCH_INT_PIN) == LOW && millis() - waitStart < TOUCH_RELEASE_WAIT_MS) delay(10);
  darkenAtlasDisplayForSleep();
  serialLog.println("ATLAS|SLEEP|POWER_DOWN");
  Serial.flush();
  sleptOnBattery = atlasOnBattery() || batteryEmpty;
  sleptEmpty = batteryEmpty;
  restingCellMv = 0;
  armWakeAndSleep();
}

void stayAsleepWithoutUsb() {
  const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
  const bool timer = wake == ESP_SLEEP_WAKEUP_TIMER;
  const bool pressed = wake == ESP_SLEEP_WAKEUP_EXT0 || wake == ESP_SLEEP_WAKEUP_EXT1;
  // A touch or BOOT starts Atlas, unless the cell is flat.
  if (!sleptOnBattery || !(timer || (pressed && sleptEmpty))) {
    sleptOnBattery = false;
    sleptEmpty = false;
    return;
  }
  const uint16_t mv = readRestingCellMv();
  if (mv >= USB_FULL_MV || (restingCellMv != 0 && mv >= restingCellMv + USB_STEP_MV)) {
    sleptOnBattery = false;
    sleptEmpty = false;
    return;
  }
  // The resting cell creeps up after the load goes; follow it.
  restingCellMv = mv;
  armWakeAndSleep();
}

void releaseSleepWakePins() {
  const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
  if (wake == ESP_SLEEP_WAKEUP_EXT0 || wake == ESP_SLEEP_WAKEUP_EXT1 || wake == ESP_SLEEP_WAKEUP_TIMER) {
    rtc_gpio_deinit(WAKE_BOOT_PIN);
    rtc_gpio_deinit(WAKE_TOUCH_PIN);
  }
  // Holds survive the wake restart; the display driver needs these pins back.
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(BACKLIGHT_PIN);
  gpio_hold_dis(TOUCH_RESET_PIN);
}


}  // namespace TurnHubAtlas
