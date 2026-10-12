#pragma once

#include <stdint.h>
#include "battery_gauge.h"

namespace TurnHub {

// Explicit little-endian wire format, including an absent/untrusted tombstone. No ABI,
// padding, wall-clock, charging timer or restored source assumptions.
struct BatteryCheckpoint {
  uint8_t bytes[16] = {};
};
inline uint32_t batteryCheckpointHash(const BatteryCheckpoint &c) {
  uint32_t hash = 2166136261UL;
  for (uint8_t i = 0; i < 12; ++i) hash = (hash ^ c.bytes[i]) * 16777619UL;
  return hash;
}
inline uint16_t batteryCheckpointVoltage(const BatteryCheckpoint &c) {
  return static_cast<uint16_t>(c.bytes[6] | (c.bytes[7] << 8));
}
inline BatteryCheckpoint makeBatteryCheckpoint(const BatteryReading &r, bool usb) {
  BatteryCheckpoint c;
  c.bytes[0] = 'T'; c.bytes[1] = 'H'; c.bytes[2] = 'B'; c.bytes[3] = 'G';
  c.bytes[4] = 1; // Format/calibration version; bump when curve/divider changes.
  c.bytes[5] = r.present ? r.percent : 0;
  c.bytes[6] = static_cast<uint8_t>(r.millivolts);
  c.bytes[7] = static_cast<uint8_t>(r.millivolts >> 8);
  c.bytes[8] = (r.present ? 1 : 0) | (usb ? 2 : 0);
  c.bytes[9] = static_cast<uint8_t>(BATTERY_CAPACITY_MAH);
  c.bytes[10] = static_cast<uint8_t>(BATTERY_CAPACITY_MAH >> 8);
  const uint32_t hash = batteryCheckpointHash(c);
  for (uint8_t i = 0; i < 4; ++i) c.bytes[12 + i] = static_cast<uint8_t>(hash >> (i * 8));
  return c;
}
inline bool validBatteryCheckpoint(const BatteryCheckpoint &c) {
  if (c.bytes[0] != 'T' || c.bytes[1] != 'H' || c.bytes[2] != 'B' || c.bytes[3] != 'G' ||
      c.bytes[4] != 1 || c.bytes[5] > 100 || c.bytes[8] > 3 || c.bytes[11] != 0 ||
      (c.bytes[9] | (c.bytes[10] << 8)) != BATTERY_CAPACITY_MAH) return false;
  const uint32_t hash = batteryCheckpointHash(c);
  for (uint8_t i = 0; i < 4; ++i)
    if (c.bytes[12 + i] != static_cast<uint8_t>(hash >> (i * 8))) return false;
  const uint16_t mv = batteryCheckpointVoltage(c);
  return (c.bytes[8] & 1) ? mv >= BATTERY_ABSENT_BELOW_MV && mv <= 4350 : c.bytes[5] == 0;
}
enum class BatteryRestore : uint8_t { Missing, Invalid, Contradicted, RestoredNvs, RestoredRtc };
inline bool usableBatteryCheckpoint(const BatteryCheckpoint &c, uint16_t liveMv) {
  if (!validBatteryCheckpoint(c) || !(c.bytes[8] & 1) ||
      liveMv < BATTERY_ABSENT_BELOW_MV || liveMv > 4350) return false;
  // Cold boot starts assuming USB. Both loaded and charger-elevated live
  // voltage are plausible; no dependable persisted time or source exists.
  const int delta = static_cast<int>(liveMv) - batteryCheckpointVoltage(c);
  if (delta < -300 || delta > 300) return false;
  const int hi = lipoPercentFromMillivolts(liveMv);
  const int lo = lipoPercentFromMillivolts(liveMv > CHARGE_RISE_MV ? liveMv - CHARGE_RISE_MV : 0);
  const int percent = c.bytes[5];
  if (liveMv <= BatteryGauge::CRITICAL_MV && percent > 10) return false;
  return percent <= hi + 20 && percent >= lo - 20;
}

// Pure policy; adapter acknowledges only successful commits. Failed attempts
// are throttled too. Unsigned subtraction handles millis rollover.
class BatteryCheckpointPolicy {
 public:
  static constexpr uint32_t MIN_WRITE_MS = 60000;
  static constexpr uint32_t REFRESH_MS = 600000;
  void begin(uint32_t now, const BatteryCheckpoint *saved = nullptr) {
    attemptedAt_ = savedAt_ = now;
    haveSaved_ = saved && validBatteryCheckpoint(*saved);
    if (haveSaved_) saved_ = *saved;
  }
  bool due(const BatteryCheckpoint &next, uint32_t now) const {
    if (!validBatteryCheckpoint(next) || now - attemptedAt_ < MIN_WRITE_MS) return false;
    if (!haveSaved_) return true;
    const int points = static_cast<int>(next.bytes[5]) - saved_.bytes[5];
    const int mv = static_cast<int>(batteryCheckpointVoltage(next)) - batteryCheckpointVoltage(saved_);
    const bool changed = points || mv >= 50 || mv <= -50 || next.bytes[8] != saved_.bytes[8];
    return next.bytes[8] != saved_.bytes[8] || points >= 5 || points <= -5 ||
        (changed && now - savedAt_ >= REFRESH_MS);
  }
  void attempted(const BatteryCheckpoint &next, uint32_t now, bool success) {
    attemptedAt_ = now;
    if (success) { saved_ = next; haveSaved_ = true; savedAt_ = now; }
  }
 private:
  BatteryCheckpoint saved_;
  bool haveSaved_ = false;
  uint32_t attemptedAt_ = 0, savedAt_ = 0;
};
} // namespace TurnHub
