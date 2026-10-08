#include "atlas_battery.h"

#include <Arduino.h>

#include "config.h"
#include "serial_log.h"

namespace TurnHubAtlas {
namespace {

constexpr uint32_t SAMPLE_INTERVAL_MS = 1000;
// Reads averaged into one sample, to quiet ADC noise.
constexpr uint8_t READS_PER_SAMPLE = 8;
// Log the estimate when it moves this many points, for checking the curve
// against a timed discharge (GET /api/diagnostics/log).
constexpr uint8_t LOG_STEP_PERCENT = 5;

TurnHub::BatteryGauge gauge;
uint32_t lastSampleMs = 0;
bool sampled = false;
bool loggedPresent = false;
uint8_t loggedPercent = 0;

uint32_t readCellMillivolts() {
  uint32_t total = 0;
  for (uint8_t i = 0; i < READS_PER_SAMPLE; ++i) total += analogReadMilliVolts(AtlasConfig::BATTERY_ADC_PIN);
  const uint32_t pinMv = total / READS_PER_SAMPLE;
  return pinMv * AtlasConfig::BATTERY_DIVIDER_NUMERATOR / AtlasConfig::BATTERY_DIVIDER_DENOMINATOR;
}

void logReading(const TurnHub::BatteryReading &reading) {
  char line[64];
  if (reading.present) {
    snprintf(line, sizeof(line), "ATLAS|BATTERY|%u|%u%s", static_cast<unsigned>(reading.millivolts),
        static_cast<unsigned>(reading.percent), reading.low ? "|LOW" : "");
  } else {
    snprintf(line, sizeof(line), "ATLAS|BATTERY|NONE");
  }
  TurnHub::serialLog.println(line);
}

}  // namespace

void beginAtlasBattery() {
  pinMode(AtlasConfig::BATTERY_ADC_PIN, INPUT);
  // 11 dB covers the divided cell (about 2.1 V at full charge).
  analogSetPinAttenuation(AtlasConfig::BATTERY_ADC_PIN, ADC_11db);
  serviceAtlasBattery(millis());
}

void serviceAtlasBattery(uint32_t nowMs) {
  if (sampled && nowMs - lastSampleMs < SAMPLE_INTERVAL_MS) return;
  sampled = true;
  lastSampleMs = nowMs;
  gauge.addSample(readCellMillivolts());

  const TurnHub::BatteryReading &reading = gauge.reading();
  const int moved = static_cast<int>(reading.percent) - static_cast<int>(loggedPercent);
  if (reading.present != loggedPresent || (reading.present && (moved >= LOG_STEP_PERCENT || moved <= -LOG_STEP_PERCENT))) {
    loggedPresent = reading.present;
    loggedPercent = reading.percent;
    logReading(reading);
  }
}

const TurnHub::BatteryReading &atlasBattery() { return gauge.reading(); }

}  // namespace TurnHubAtlas
