#pragma once

#include <Arduino.h>

namespace AtlasConfig {

// Atlas prototype front-panel controls.
// Buttons are wired GPIO -> switch -> GND and use INPUT_PULLUP.
constexpr uint8_t PAIR_BUTTON_PIN = 32;
constexpr uint8_t MASTER_BUTTON_PIN = 33;

// Atlas prototype indicator LEDs.
// LEDs are active-high and should be wired GPIO -> resistor -> LED -> GND.
constexpr uint8_t STATUS_LED_PIN = 25;
constexpr uint8_t PAIR_LED_PIN = 26;

constexpr char WIFI_SSID[] = "TurnHub-Atlas";
constexpr uint8_t WIFI_CHANNEL = 6;

constexpr uint16_t HTTP_PORT = 80;

}  // namespace AtlasConfig
