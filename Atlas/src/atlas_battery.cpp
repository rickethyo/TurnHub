#include "atlas_battery.h"

#include <Arduino.h>
#include <esp_system.h>
#include "battery_checkpoint.h"
#include "nvs_blob_store.h"

#include "config.h"
#include "serial_log.h"

namespace TurnHubAtlas {
namespace {

// Four samples a second; source steps are independently debounced.
// The gauge consumes their average once a second.
constexpr uint32_t SAMPLE_INTERVAL_MS = 250;
constexpr uint8_t SAMPLES_PER_GAUGE = 4;
// Reads averaged into one sample, to quiet ADC noise.
constexpr uint8_t READS_PER_SAMPLE = 8;
// Log the estimate when it moves this many points, for checking the curve
// against a timed discharge (GET /api/diagnostics/log).
constexpr uint8_t LOG_STEP_PERCENT = 5;

TurnHub::BatteryGauge gauge;
TurnHub::BatteryReading publishedReading;
TurnHub::PowerSourceTracker power;
TurnHubStorage::NvsBlobStore checkpointStore;
TurnHub::BatteryCheckpointPolicy checkpointPolicy;
RTC_DATA_ATTR TurnHub::BatteryCheckpoint rtcCheckpoint;
TurnHub::BatteryCheckpoint bootCheckpoint;
TurnHub::BatteryRestore restoreStatus = TurnHub::BatteryRestore::Missing;
bool haveBootCheckpoint = false, bootWasRtc = false;
uint8_t startupSamples = 0;
uint32_t lastDiagnosticMs = 0, loggedCorrections = 0;
bool loggedCritical = false;
uint32_t gaugeSum = 0;
uint8_t gaugeCount = 0;
uint32_t lastSampleMs = 0;
bool sampled = false;
bool loggedPresent = false;
bool loggedCharging = false;
uint8_t loggedPercent = 0;

uint32_t readCellMillivolts() {
  uint32_t total = 0;
  for (uint8_t i = 0; i < READS_PER_SAMPLE; ++i) total += analogReadMilliVolts(AtlasConfig::BATTERY_ADC_PIN);
  const uint32_t pinMv = total / READS_PER_SAMPLE;
  return pinMv * AtlasConfig::BATTERY_DIVIDER_NUMERATOR / AtlasConfig::BATTERY_DIVIDER_DENOMINATOR;
}

void logReading(const TurnHub::BatteryReading &reading) {
  char line[192];
  if (reading.present) {
    snprintf(line, sizeof(line), "ATLAS|BATTERY|%u|%u%s%s", static_cast<unsigned>(reading.millivolts),
        static_cast<unsigned>(reading.percent), reading.low ? "|LOW" : "", reading.charging ? "|CHARGING" : "");
    const size_t used = strlen(line);
    snprintf(line + used, sizeof(line) - used,
        "|RAW=%u|FILTERED=%u|EST=%u|SOURCE=%s|RESTORE=%u|SAFETY=%u|CRITICAL=%u|CORRECTIONS=%lu",
        reading.rawMillivolts, reading.millivolts, reading.voltagePercent,
        gauge.onUsb() ? "USB" : "BATTERY", static_cast<unsigned>(restoreStatus),
        reading.safetyMillivolts, reading.criticalLow, static_cast<unsigned long>(reading.corrections));
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
  const uint32_t now = millis();
  size_t size = 0;
  if (checkpointStore.begin("battery") == TurnHubStorage::Status::Ok) {
    const auto status = checkpointStore.read("gauge", &bootCheckpoint, sizeof(bootCheckpoint), size);
    haveBootCheckpoint = status == TurnHubStorage::Status::Ok && size == sizeof(bootCheckpoint);
    if (status != TurnHubStorage::Status::NotFound && (!haveBootCheckpoint || !TurnHub::validBatteryCheckpoint(bootCheckpoint)))
      restoreStatus = TurnHub::BatteryRestore::Invalid;
  }
  // Write throttling compares against actual flash, even when newer RTC
  // history is selected for restoration below.
  checkpointPolicy.begin(now, haveBootCheckpoint ? &bootCheckpoint : nullptr);
  // RTC is a newer checkpoint only on an actual deep-sleep wake. Do not reuse
  // retained memory after firmware upload, brownout, watchdog or factory reset.
  if (esp_reset_reason() == ESP_RST_DEEPSLEEP && TurnHub::validBatteryCheckpoint(rtcCheckpoint)) {
    bootCheckpoint = rtcCheckpoint;
    haveBootCheckpoint = bootWasRtc = true;
  }
  serviceAtlasBattery(now);
}

void serviceAtlasBattery(uint32_t nowMs) {
  if (sampled && nowMs - lastSampleMs < SAMPLE_INTERVAL_MS) return;
  sampled = true;
  lastSampleMs = nowMs;
  const uint32_t mv = readCellMillivolts();
  const bool changed = power.addSample(mv);
  const bool usb = power.source() == TurnHub::PowerSource::Usb;
  if (changed) {
    TurnHub::serialLog.println(usb ? "ATLAS|POWER|USB" : "ATLAS|POWER|BATTERY");
    // Rebase voltage filters immediately, preserving the displayed percent.
    gauge.addTimedSample(mv, usb, nowMs);
    gaugeSum = 0;
    gaugeCount = 0;
  } else {
    gaugeSum += mv;
    if (++gaugeCount < SAMPLES_PER_GAUGE) return;
    gauge.addTimedSample(gaugeSum / SAMPLES_PER_GAUGE, usb, nowMs);
    gaugeSum = 0;
    gaugeCount = 0;
  }
  // Allow several live samples before restoring or saving, so one startup
  // conversion cannot certify an old checkpoint. Timers always start afresh.
  if (startupSamples < 5 && ++startupSamples == 5 && haveBootCheckpoint) {
    if (!TurnHub::validBatteryCheckpoint(bootCheckpoint)) restoreStatus = TurnHub::BatteryRestore::Invalid;
    else if (!TurnHub::usableBatteryCheckpoint(bootCheckpoint, gauge.reading().safetyMillivolts))
      restoreStatus = TurnHub::BatteryRestore::Contradicted;
    else {
      gauge.restorePercent(bootCheckpoint.bytes[5]);
      restoreStatus = bootWasRtc ? TurnHub::BatteryRestore::RestoredRtc : TurnHub::BatteryRestore::RestoredNvs;
    }
    TurnHub::serialLog.println("ATLAS|BATTERY|CHECKPOINT_EVALUATED");
    lastDiagnosticMs = nowMs - 60000;
  }
  if (startupSamples >= 5) {
    auto checkpoint = TurnHub::makeBatteryCheckpoint(gauge.reading(), usb);
    const bool credible = !gauge.reading().present ||
        TurnHub::usableBatteryCheckpoint(checkpoint, gauge.reading().safetyMillivolts);
    if (!credible) {
      // Invalidate older history when current voltage disproves it. Leaving
      // an old high RTC percentage here could revive it after low-cell sleep
      // when the charger raises voltage again. This tombstone affects storage
      // confidence only, never the public cell-present or safety reading.
      auto uncertain = gauge.reading();
      uncertain.present = false;
      checkpoint = TurnHub::makeBatteryCheckpoint(uncertain, usb);
    }
    rtcCheckpoint = checkpoint;
    if (checkpointPolicy.due(checkpoint, nowMs)) {
      const bool saved = checkpointStore.write("gauge", &checkpoint, sizeof(checkpoint)) == TurnHubStorage::Status::Ok;
      checkpointPolicy.attempted(checkpoint, nowMs, saved);
      TurnHub::serialLog.println(saved ? "ATLAS|BATTERY|CHECKPOINT_SAVED" : "ATLAS|BATTERY|CHECKPOINT_SAVE_FAILED");
    }
  }

  const TurnHub::BatteryReading &reading = gauge.reading();
  publishedReading = reading;
  // Consumers already represent unavailable startup ADC evidence as null.
  // Do not briefly show a voltage seed before a pending history is validated.
  if (haveBootCheckpoint && startupSamples < 5 && !reading.criticalLow) publishedReading.present = false;
  const int moved = static_cast<int>(reading.percent) - static_cast<int>(loggedPercent);
  if (reading.present != loggedPresent || reading.charging != loggedCharging ||
      reading.criticalLow != loggedCritical ||
      (reading.present && (moved >= LOG_STEP_PERCENT || moved <= -LOG_STEP_PERCENT ||
       nowMs - lastDiagnosticMs >= 60000 || reading.corrections / 5 != loggedCorrections / 5))) {
    lastDiagnosticMs = nowMs;
    loggedCritical = reading.criticalLow;
    loggedCorrections = reading.corrections;
    loggedPresent = reading.present;
    loggedCharging = reading.charging;
    loggedPercent = reading.percent;
    logReading(reading);
  }
}

const TurnHub::BatteryReading &atlasBattery() { return publishedReading; }

bool atlasOnBattery() { return power.source() == TurnHub::PowerSource::Battery; }

}  // namespace TurnHubAtlas
