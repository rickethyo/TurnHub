#pragma once

#include <Arduino.h>

namespace AtlasConfig {

// Atlas board: LCDwiki 2.8" ESP32-32E display module (E32R28T, resistive
// touch). Pin map from the vendor's pin allocation table; see
// Documentation/engineering/HARDWARE_REFERENCE.md. The previous prototype's
// Pair button and status/Pair LEDs are not carried over.

// Physical-presence control: the on-board BOOT button (GPIO0, active-low with
// an on-board pull-up). It is only sampled after boot, where it is an ordinary
// input; holding it through reset still enters the ROM download mode.
constexpr uint8_t MASTER_BUTTON_PIN = 0;

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

// microSD slot on VSPI, shared with the "SPI" expansion header (CS IO27).
constexpr int8_t SD_SCLK_PIN = 18;
constexpr int8_t SD_MOSI_PIN = 23;
constexpr int8_t SD_MISO_PIN = 19;
constexpr int8_t SD_CS_PIN = 5;

// Common-anode RGB LED: each channel is active low.
constexpr uint8_t RGB_RED_PIN = 22;
constexpr uint8_t RGB_GREEN_PIN = 16;
constexpr uint8_t RGB_BLUE_PIN = 17;

// Speaker amplifier: enable is active low, audio is the DAC on IO26.
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

// The master (BOOT) button is active-low.
inline bool masterButtonPressed() {
  return digitalRead(MASTER_BUTTON_PIN) == LOW;
}

}  // namespace AtlasConfig
