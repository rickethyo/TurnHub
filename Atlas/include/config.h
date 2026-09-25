#pragma once

#include <Arduino.h>

namespace AtlasConfig {

// Atlas board: LCDwiki 2.8" ESP32-32E display module (E32R28T, resistive
// touch). Pin map from the vendor's pin allocation table; see
// Documentation/engineering/HARDWARE_REFERENCE.md. The previous prototype's
// Pair button, status/Pair LEDs and master button are not carried over: the
// touchscreen is Atlas's only physical input (GPIO0 stays the flashing-only
// BOOT strap).

// ILI9341V TFT on its own SPI bus (HSPI). Reset is tied to the ESP32 EN line.
constexpr int8_t TFT_SCLK_PIN = 14;
constexpr int8_t TFT_MOSI_PIN = 13;
constexpr int8_t TFT_MISO_PIN = 12;
constexpr int8_t TFT_CS_PIN = 15;
constexpr int8_t TFT_DC_PIN = 2;
constexpr int8_t TFT_BACKLIGHT_PIN = 21;  // Active high.

// XPT2046 resistive touch controller on a separate SPI bus.
constexpr int8_t TOUCH_SCLK_PIN = 25;
constexpr int8_t TOUCH_MOSI_PIN = 32;
constexpr int8_t TOUCH_MISO_PIN = 39;
constexpr int8_t TOUCH_CS_PIN = 33;
constexpr int8_t TOUCH_IRQ_PIN = 36;  // Active low.

// Screen rotation: 3 is landscape with the board's left-edge connector
// pigtails leaving from the top of the screen (1 would point them at the user).
constexpr uint8_t TFT_ROTATION = 3;

// Touch calibration: raw 12-bit XPT2046 readings at the screen edges, and
// how the raw axes map onto the rotated screen. Starting values from common
// ESP32 2.8" boards; confirm them with the ATLAS|TOUCH|RAW serial lines.
constexpr uint16_t TOUCH_RAW_X_MIN = 200;
constexpr uint16_t TOUCH_RAW_X_MAX = 3700;
constexpr uint16_t TOUCH_RAW_Y_MIN = 240;
constexpr uint16_t TOUCH_RAW_Y_MAX = 3800;
constexpr bool TOUCH_SWAP_XY = false;
constexpr bool TOUCH_INVERT_X = true;
constexpr bool TOUCH_INVERT_Y = true;
// Minimum pressure (Z) that counts as a touch.
constexpr uint16_t TOUCH_PRESSURE_MIN = 400;

// microSD slot on VSPI, shared with the "SPI" expansion header (CS IO27).
constexpr int8_t SD_SCLK_PIN = 18;
constexpr int8_t SD_MOSI_PIN = 23;
constexpr int8_t SD_MISO_PIN = 19;
constexpr int8_t SD_CS_PIN = 5;
// Conservative clock for bring-up; raise once cards are proven on the bench.
constexpr uint32_t SD_SPI_HZ = 4000000;

// Common-anode RGB LED: each channel is active low.
constexpr uint8_t RGB_RED_PIN = 22;
constexpr uint8_t RGB_GREEN_PIN = 16;
constexpr uint8_t RGB_BLUE_PIN = 17;

// Speaker amplifier: enable is active low; audio input is IO26 (the DAC pin,
// driven as an LEDC square wave by atlas_speaker.cpp).
constexpr uint8_t AUDIO_ENABLE_PIN = 4;
constexpr uint8_t AUDIO_DAC_PIN = 26;

// Battery voltage divider (input-only ADC pin).
constexpr uint8_t BATTERY_ADC_PIN = 34;

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
