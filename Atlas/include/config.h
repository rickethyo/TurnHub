#pragma once

#include <stddef.h>
#include <stdint.h>
#include <Arduino.h>

namespace AtlasConfig {

// Arduino-ESP32 2.0.17's loopTask runs setup, gameplay and synchronous HTTP.
// Provision headroom here; main.cpp overrides the core's weak size function.
constexpr size_t LOOP_TASK_STACK_BYTES = 16 * 1024;
constexpr uint32_t LOOP_STACK_WARNING_BYTES = 3 * 1024;

// Board pins. env:atlas is board-less on a generic ESP32 (the Gen 1 E32R28T
// pin map lives on legacy/gen1); env:atlas-s3 builds for the 4-inch ES3C40P
// (TURNHUB_BOARD_ES3C40P; pins from its schematic, see
// hardware/ESP32_S3_REFRESH.md, not yet bench-verified).
#if defined(TURNHUB_BOARD_ES3C40P)
// microSD over 4-bit SDMMC (the board has no SPI wiring to the slot).
constexpr int8_t SD_CLK_PIN = 38;
constexpr int8_t SD_CMD_PIN = 40;
constexpr int8_t SD_D0_PIN = 39;
constexpr int8_t SD_D1_PIN = 41;
constexpr int8_t SD_D2_PIN = 48;
constexpr int8_t SD_D3_PIN = 47;
// Conservative clock for bring-up (SDMMC default speed is 20 MHz).
constexpr uint32_t SD_MMC_KHZ = 10000;
// ST7796S on its own SPI bus (DC 46 and backlight 45 are strapping pins; the
// panel resets with the board). Native 320 x 480, used in portrait.
constexpr int8_t LCD_SCK_PIN = 12;
constexpr int8_t LCD_MOSI_PIN = 11;
constexpr int8_t LCD_MISO_PIN = 13;
constexpr int8_t LCD_DC_PIN = 46;
constexpr int8_t LCD_CS_PIN = 10;
constexpr uint8_t LCD_BACKLIGHT_PIN = 45;  // High = lit.
// Portrait orientation (0, or 2 for upside down) and IPS colour inversion.
// *Needs verification*: flip these if the picture is upside down or its
// colours are negative.
constexpr uint8_t LCD_ROTATION = 0;
constexpr bool LCD_INVERT = true;
// FT6336U capacitive touch (I2C 0x38), on the bus the codec will share.
constexpr int8_t TOUCH_SDA_PIN = 16;
constexpr int8_t TOUCH_SCL_PIN = 15;
constexpr int8_t TOUCH_RST_PIN = 18;
constexpr int8_t TOUCH_INT_PIN = 17;
#else
// microSD on the ESP32's default VSPI pins.
constexpr int8_t SD_SCLK_PIN = 18;
constexpr int8_t SD_MOSI_PIN = 23;
constexpr int8_t SD_MISO_PIN = 19;
constexpr int8_t SD_CS_PIN = 5;
// Conservative clock for bring-up; raise once cards are proven on the bench.
constexpr uint32_t SD_SPI_HZ = 4000000;
#endif

// The board's BOOT button (GPIO0, pulled up, low when pressed). GPIO0 is a
// strapping pin only at reset, so it is free to use once Atlas is running:
// quick press pairs, a medium hold forgets every Sigil, a long hold factory
// resets Atlas (three_part_button.h).
constexpr uint8_t BOOT_BUTTON_PIN = 0;

// Auto sleep on the cell (owner 2026-10-08: keep the small backup cell from
// running flat). Between games, Atlas sleeps after this long with no player
// action (an Intent from a Sigil, phone or the touchscreen, or a touch). On
// USB it never sleeps by itself.
constexpr uint32_t BATTERY_IDLE_SLEEP_MS = 5UL * 60UL * 1000UL;
// Voltage protection is independent of displayed percent (battery_gauge.h).
// An interrupted match comes back paused; only USB wakes an empty Atlas.
// The empty reading must hold this long, so one sag under load can't trip it.
constexpr uint32_t BATTERY_EMPTY_CONFIRM_MS = 30UL * 1000UL;
// Severe undervoltage also protects a cold boot whose source is still uncertain.
constexpr uint32_t BATTERY_EMERGENCY_CONFIRM_MS = 5UL * 1000UL;

constexpr char WIFI_SSID[] = "TurnHub-Atlas";
constexpr uint8_t WIFI_CHANNEL = 6;
// Shipped pre-setup WPA2 passphrase, used only while no owner-set password is
// stored, so the Android app can join a new or factory-reset Atlas without
// asking. It is public by design; setup must replace it. Must match
// WifiCredentials.DEFAULT_ATLAS_PASSPHRASE in the Android app and
// protocol/http-v1.md.
constexpr char WIFI_DEFAULT_PASSWORD[] = "TurnHub-Setup";
// NVS location of the owner-set AP password. The default above is never
// written here, so erasing NVS returns Atlas to the shipped default.
constexpr char WIFI_PREF_NAMESPACE[] = "atlas-net";
constexpr char WIFI_PREF_KEY[] = "ap-pass";
// WPA2-PSK passphrase length limits.
constexpr size_t WIFI_PASSWORD_MIN_LENGTH = 8;
constexpr size_t WIFI_PASSWORD_MAX_LENGTH = 63;

constexpr uint16_t HTTP_PORT = 80;

}  // namespace AtlasConfig
