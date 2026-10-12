#pragma once

#include <stdint.h>

// Atlas battery state of charge from the cell voltage alone (no fuel-gauge
// chip on the E32R28T). Pure logic, host-tested; atlas_battery.cpp feeds it
// the ADC readings on firmware.
//
// A single-cell LiPo's voltage is flat through the middle of its charge and
// falls fast at both ends, so percent comes from a discharge curve, not a
// straight line from 3.0 to 4.2 V. The curve is a typical light-load (about
// 0.5 C) LiPo curve: Atlas draws roughly 150-250 mA, which is about 1 C for a
// 250 mAh cell, so the reading sags under load and the estimate errs low.
// *Needs verification* against a timed discharge on the real board.

namespace TurnHub {

struct BatteryCurvePoint {
  uint16_t millivolts;
  uint8_t percent;
};

// Highest voltage first. Below the last point is 0 %, above the first 100 %.
constexpr BatteryCurvePoint LIPO_CURVE[] = {
    {4180, 100}, {4100, 92}, {4040, 85}, {3980, 77}, {3940, 70}, {3900, 62},
    {3860, 54}, {3830, 46}, {3800, 38}, {3780, 31}, {3760, 25}, {3740, 19},
    {3710, 14}, {3680, 10}, {3640, 6}, {3580, 3}, {3450, 0},
};
constexpr uint8_t LIPO_CURVE_POINTS = sizeof(LIPO_CURVE) / sizeof(LIPO_CURVE[0]);

// Piecewise-linear between the curve's points.
inline uint8_t lipoPercentFromMillivolts(uint32_t millivolts) {
  if (millivolts >= LIPO_CURVE[0].millivolts) return 100;
  for (uint8_t i = 1; i < LIPO_CURVE_POINTS; ++i) {
    const BatteryCurvePoint &hi = LIPO_CURVE[i - 1];
    const BatteryCurvePoint &lo = LIPO_CURVE[i];
    if (millivolts >= lo.millivolts) {
      const uint32_t span = hi.millivolts - lo.millivolts;
      const uint32_t into = millivolts - lo.millivolts;
      return static_cast<uint8_t>(lo.percent + ((hi.percent - lo.percent) * into + span / 2) / span);
    }
  }
  return 0;
}

struct BatteryReading {
  bool present = false;     // A cell is connected (or the charger holds the line up).
  uint16_t millivolts = 0;  // Smoothed cell voltage.
  uint8_t percent = 0;      // Shown charge, 0-100; meaningful only when present.
  bool low = false;         // At or below BATTERY_LOW_PERCENT.
  bool charging = false;    // Inferred USB charging, not a hardware charger signal.
  uint16_t rawMillivolts = 0; // Short-window ADC average, before gauge filtering.
  uint16_t safetyMillivolts = 0; // Median + faster filter, independent of display.
  uint8_t voltagePercent = 0;
  bool criticalLow = false; // Voltage-based protection; never cosmetic percent.
  bool emergencyLow = false;
  uint32_t corrections = 0; // Accelerated reconciliation events.
};

// Below this the line is floating or shorted: no cell.
constexpr uint16_t BATTERY_ABSENT_BELOW_MV = 2500;
constexpr uint8_t BATTERY_LOW_PERCENT = 15;
// The low warning clears only once the charge is this much above the threshold.
constexpr uint8_t BATTERY_LOW_CLEAR_MARGIN = 5;

// Charging. The board's TP4054 charges at about 300 mA (R27, 3.3 kOhm, on
// PROG) to 4.2 V, then tapers and stops; with USB in, the board runs from USB,
// so all of it goes into the cell. Plugged in, the reading is the charger's,
// not the cell's: the cell plus the charge current through its resistance
// (about 210 mV on the bench), and then the 4.2 V limit. Read through the
// curve it says "full" at once. So while charging the percent is counted, not
// read: it climbs from the last on-battery percent at the rate the charger
// can fill the cell, more slowly once the reading has reached the limit, and
// shows 100 only after a while there. *Needs verification*: an estimate; the
// board can't measure the charge current.
constexpr uint16_t BATTERY_CAPACITY_MAH = 250;  // The 502030 cell.
constexpr uint16_t BATTERY_CHARGE_MA = 300;     // TP4054 with R27 = 3.3 kOhm.
// One point of charge at full charge current: capacity / 100 / current.
constexpr uint32_t CHARGE_MS_PER_POINT = 36000UL * BATTERY_CAPACITY_MAH / BATTERY_CHARGE_MA;
// At the charger's voltage limit the current tapers; half the rate on average.
constexpr uint32_t CHARGE_LIMIT_MS_PER_POINT = 2 * CHARGE_MS_PER_POINT;
// The reading has reached the limit when it holds this steady, this high, for
// CHARGE_LIMIT_SAMPLES samples.
constexpr uint16_t CHARGE_LIMIT_MIN_MV = 4080;
constexpr uint16_t CHARGE_LIMIT_SPREAD_MV = 12;
constexpr uint8_t CHARGE_LIMIT_SAMPLES = 240;  // 4 minutes; climbing to it, the reading rises 30 mV or more in that time.
// Below the limit the count stops here; 100 only after this long at the limit.
constexpr uint8_t CHARGE_BELOW_LIMIT_MAX_PERCENT = 90;
constexpr uint32_t CHARGE_FULL_AFTER_LIMIT_MS = 20UL * 60 * 1000;
// Started on USB, nothing is known but the charger's reading: the cell is
// taken as about this much below it until the count takes over.
constexpr uint16_t CHARGE_RISE_MV = 210;

// Median-of-three rejects isolated load spikes; retain the 16 s EMA for SoC.
// Timers use unsigned differences and actual elapsed time, never loop counts.
class BatteryGauge {
 public:
  static constexpr uint8_t SMOOTHING_SAMPLES = 16;
  static constexpr uint32_t SAMPLE_MS = 1000;
  static constexpr uint8_t PERCENT_HYSTERESIS = 2;
  static constexpr uint16_t CRITICAL_MV = 3580;
  static constexpr uint16_t CRITICAL_CLEAR_MV = 3640;
  static constexpr uint16_t EMERGENCY_MV = 3450;

  void reset() { *this = BatteryGauge(); }
  void addSample(uint32_t mv, bool usb = false) {
    addTimedSample(mv, usb, clock_ + SAMPLE_MS);
  }
  void addTimedSample(uint32_t cellMv, bool usb, uint32_t now) {
    uint32_t dt = timed_ ? now - clock_ : 0;
    if (dt > 2000) dt = 2000; // A blocked loop cannot invent charge or evidence.
    clock_ = now; timed_ = true;
    const uint16_t mv = static_cast<uint16_t>(cellMv > 5000 ? 5000 : cellMv);
    reading_.rawMillivolts = mv;
    if (mv < BATTERY_ABSENT_BELOW_MV) {
      if (++absentCount_ >= 3) {
        reset(); clock_ = now; timed_ = true;
      }
      return;
    }
    absentCount_ = 0;
    if (!seeded_) {
      seeded_ = true; onUsb_ = usb;
      for (auto &v : samples_) v = mv;
      averageMv16_ = safetyMv16_ = mv * 16;
      reading_.present = true;
      reading_.percent = lipoPercentFromMillivolts(usb && mv > CHARGE_RISE_MV ? mv - CHARGE_RISE_MV : mv);
      if (usb && reading_.percent > CHARGE_BELOW_LIMIT_MAX_PERCENT) reading_.percent = CHARGE_BELOW_LIMIT_MAX_PERCENT;
    }
    if (usb != onUsb_) {
      // Rebase voltage filters across a genuine source step, keep shown SoC.
      onUsb_ = usb; chargeMs_ = limitMs_ = 0; atLimit_ = false;
      recentCount_ = recentNext_ = 0; recentMs_ = belowLimitMs_ = 0;
      downMs_ = upMs_ = fastMs_ = fallingMs_ = moveMs_ = 0; upCorrection_ = false;
      averageMv16_ = safetyMv16_ = mv * 16;
      for (auto &v : samples_) v = mv;
      dt = 0;
    }
    samples_[sampleNext_] = mv;
    sampleNext_ = static_cast<uint8_t>((sampleNext_ + 1) % 3);
    uint16_t a = samples_[0], b = samples_[1], c = samples_[2];
    if (a > b) { const uint16_t t = a; a = b; b = t; }
    if (b > c) { const uint16_t t = b; b = c; c = t; }
    if (a > b) b = a;
    averageMv16_ += (static_cast<int32_t>(b * 16) - averageMv16_) / SMOOTHING_SAMPLES;
    safetyMv16_ += (static_cast<int32_t>(b * 16) - safetyMv16_) / 4;
    reading_.millivolts = static_cast<uint16_t>((averageMv16_ + 8) / 16);
    reading_.safetyMillivolts = static_cast<uint16_t>((safetyMv16_ + 8) / 16);
    reading_.voltagePercent = lipoPercentFromMillivolts(reading_.millivolts);
    if (reading_.safetyMillivolts <= CRITICAL_MV) reading_.criticalLow = true;
    else if (reading_.safetyMillivolts >= CRITICAL_CLEAR_MV) reading_.criticalLow = false;
    reading_.emergencyLow = reading_.safetyMillivolts <= EMERGENCY_MV;
    if (usb && !reading_.criticalLow) countCharge(b, dt);
    else followCurve(dt); // Invalid low voltage overrides inferred charging.
    reading_.charging = usb && !reading_.criticalLow && reading_.percent < 100;
    const uint8_t warning = !usb || reading_.criticalLow
        ? (reading_.percent < reading_.voltagePercent ? reading_.percent : reading_.voltagePercent)
        : reading_.percent;
    if (reading_.criticalLow || warning <= BATTERY_LOW_PERCENT) reading_.low = true;
    else if (warning >= BATTERY_LOW_PERCENT + BATTERY_LOW_CLEAR_MARGIN) reading_.low = false;
  }
  const BatteryReading &reading() const { return reading_; }
  bool onUsb() const { return onUsb_; }
  // Storage adapter calls only after checkpoint validation against live ADC.
  void restorePercent(uint8_t percent) {
    if (seeded_ && percent <= 100) reading_.percent = percent;
    reading_.charging = onUsb_ && !reading_.criticalLow && reading_.percent < 100;
    if (reading_.criticalLow || reading_.percent <= BATTERY_LOW_PERCENT) reading_.low = true;
    chargeMs_ = limitMs_ = downMs_ = upMs_ = fastMs_ = fallingMs_ = moveMs_ = 0;
    upCorrection_ = false;
    atLimit_ = false; recentCount_ = recentNext_ = 0; recentMs_ = belowLimitMs_ = 0;
  }
  void reseed(uint32_t mv) { reset(); addSample(mv); }

 private:
  static uint32_t accrue(uint32_t value, uint32_t dt) {
    return value < 3600000 ? value + dt : value;
  }
  void followCurve(uint32_t dt) {
    const int diff = static_cast<int>(reading_.voltagePercent) - reading_.percent;
    downMs_ = diff <= -PERCENT_HYSTERESIS ? accrue(downMs_, dt) : 0;
    // Only a large, persistent contradiction justifies upward correction on battery.
    upMs_ = diff >= 15 ? accrue(upMs_, dt) : 0;
    if (upMs_ >= 120000) upCorrection_ = true;
    if (diff < PERCENT_HYSTERESIS) upCorrection_ = false;
    fastMs_ = diff <= -20 ? accrue(fastMs_, dt) : 0;
    fallingMs_ = diff <= -5 ? accrue(fallingMs_, dt) : 0;
    const bool fast = fastMs_ >= 15000;
    const bool falling = fallingMs_ >= 30000;
    const uint32_t interval = fast ? 1000 : falling ? 5000 : 30000;
    if (downMs_ || upMs_ || upCorrection_) moveMs_ = accrue(moveMs_, dt);
    else moveMs_ = 0;
    if (moveMs_ < interval) return;
    if (downMs_) {
      --reading_.percent;
      if (fast || falling) ++reading_.corrections;
      moveMs_ = 0;
    } else if (upCorrection_) {
      ++reading_.percent; ++reading_.corrections; moveMs_ = 0;
    }
  }
  void countCharge(uint16_t mv, uint32_t dt) {
    if (!dt) return;
    recentMs_ += dt;
    if (recentMs_ >= SAMPLE_MS) {
      recentMs_ %= SAMPLE_MS;
      recent_[recentNext_] = mv;
      recentNext_ = static_cast<uint8_t>((recentNext_ + 1) % CHARGE_LIMIT_SAMPLES);
      if (recentCount_ < CHARGE_LIMIT_SAMPLES) ++recentCount_;
    }
    if (recentCount_ == CHARGE_LIMIT_SAMPLES) {
      uint16_t lo = recent_[0], hi = lo;
      for (uint8_t i = 1; i < CHARGE_LIMIT_SAMPLES; ++i) {
        if (recent_[i] < lo) lo = recent_[i];
        if (recent_[i] > hi) hi = recent_[i];
      }
      const bool steady = lo >= CHARGE_LIMIT_MIN_MV && hi - lo <= CHARGE_LIMIT_SPREAD_MV;
      if (steady) atLimit_ = true;
      if (mv < CHARGE_LIMIT_MIN_MV) {
        belowLimitMs_ = accrue(belowLimitMs_, dt);
        if (belowLimitMs_ >= 10000) { atLimit_ = false; limitMs_ = 0; }
      } else belowLimitMs_ = 0;
    }
    chargeMs_ += dt;
    if (atLimit_) limitMs_ = accrue(limitMs_, dt);
    const uint32_t perPoint = atLimit_ ? CHARGE_LIMIT_MS_PER_POINT : CHARGE_MS_PER_POINT;
    const uint8_t cap = !atLimit_ ? CHARGE_BELOW_LIMIT_MAX_PERCENT : limitMs_ >= CHARGE_FULL_AFTER_LIMIT_MS ? 100 : 99;
    if (chargeMs_ >= perPoint) {
      chargeMs_ %= perPoint; // Never bank time at the cap.
      if (reading_.percent < cap) ++reading_.percent;
    }
  }
  bool seeded_ = false, onUsb_ = false, timed_ = false, atLimit_ = false, upCorrection_ = false;
  uint32_t clock_ = 0, downMs_ = 0, upMs_ = 0, fastMs_ = 0, fallingMs_ = 0, moveMs_ = 0;
  uint32_t chargeMs_ = 0, limitMs_ = 0, belowLimitMs_ = 0, recentMs_ = 0;
  int32_t averageMv16_ = 0, safetyMv16_ = 0;
  BatteryReading reading_;
  uint16_t samples_[3] = {}, recent_[CHARGE_LIMIT_SAMPLES] = {};
  uint8_t sampleNext_ = 0, absentCount_ = 0, recentNext_ = 0, recentCount_ = 0;
};

// Whether Atlas runs from USB ("shore power") or its cell. The board has no
// USB-sense pin, so this is read from the cell voltage, which steps when USB
// comes or goes: the charger holds the cell up and carries the load, and
// without it the cell sags under the load at once. Measured on the 502030
// (2026-10-08, 4 samples a second): pulling USB dropped the reading 220-240 mV
// within one sample, plugging it back raised it 200-220 mV; sample noise was
// within about 30 mV, and on the cell alone it fell about 100 mV in 45 s.
enum class PowerSource : uint8_t { Usb, Battery };

class PowerSourceTracker {
 public:
  // Steps relative to the preceding STEP_WINDOW mean require a frozen
  // baseline and corroboration before inferring USB pulled or plugged in.
  static constexpr uint16_t STEP_MV = 100;
  static constexpr uint8_t STEP_WINDOW = 4;  // 1 s at 4 samples a second.
  // The slow cue, for a start on the cell (no step to see): a steady fall of
  // DRIFT_MV over DRIFT_WINDOW samples, which the charger never allows, means
  // the cell; a steady rise of as much means charging.
  static constexpr uint16_t DRIFT_MV = 60;
  static constexpr uint16_t DRIFT_WINDOW = 240;  // 60 s at 4 samples a second.
  // The drift is judged on averages of this many samples, against at least
  // DRIFT_MIN_AVERAGES of them, so one noisy sample cannot flip it.
  static constexpr uint8_t DRIFT_AVERAGE = 4;
  static constexpr uint8_t DRIFT_AVERAGES = DRIFT_WINDOW / DRIFT_AVERAGE;
  static constexpr uint8_t DRIFT_MIN_AVERAGES = 10;

  // One raw (unsmoothed) cell reading. True when the source changed.
  bool addSample(uint32_t cellMillivolts) {
    if (cellMillivolts < BATTERY_ABSENT_BELOW_MV) {
      // No cell: Atlas can only be running from USB.
      if (++absentCount_ < 3) return false;
      const PowerSource was = source_;
      *this = PowerSourceTracker();
      return was != source_;
    }
    absentCount_ = 0;
    const uint16_t mv = static_cast<uint16_t>(cellMillivolts > 0xFFFF ? 0xFFFF : cellMillivolts);
    PowerSource next = source_;
    if (candidateCount_) {
      const bool holds = candidate_ == PowerSource::Battery
          ? mv + STEP_MV <= candidateBaseline_ : mv >= candidateBaseline_ + STEP_MV;
      if (!holds) candidateCount_ = 0;
      else if (++candidateCount_ >= candidateNeeded_) next = candidate_;
    } else if (recentCount_ == STEP_WINDOW) {
      // Mean of the preceding second, not its noisiest extreme. Freeze the
      // baseline while confirming a step; don't mistake a rebound for USB.
      uint32_t sum = 0;
      for (uint8_t i = 0; i < STEP_WINDOW; ++i) sum += recent_[i];
      const uint16_t baseline = static_cast<uint16_t>(sum / STEP_WINDOW);
      if ((source_ == PowerSource::Usb && mv + STEP_MV <= baseline) ||
          (source_ == PowerSource::Battery && mv >= baseline + STEP_MV)) {
        candidate_ = source_ == PowerSource::Usb ? PowerSource::Battery : PowerSource::Usb;
        candidateBaseline_ = baseline;
        const uint16_t delta = mv > baseline ? mv - baseline : baseline - mv;
        candidateNeeded_ = delta >= 180 ? 3 : 6; // 0.75 s bench steps, 1.5 s weaker cues.
        candidateCount_ = 1;
      }
    }
    recent_[recentNext_] = mv;
    recentNext_ = static_cast<uint8_t>((recentNext_ + 1) % STEP_WINDOW);
    if (recentCount_ < STEP_WINDOW) ++recentCount_;

    const PowerSource drift = driftSource(mv);
    if (partCount_ == 0 && next == source_ && drift != source_) {
      if (++driftConfirm_ >= 3) next = drift; // Three consecutive one-second averages.
    } else if (partCount_ == 0 && drift == source_) driftConfirm_ = 0;
    if (next == source_) return false;
    candidateCount_ = driftConfirm_ = 0;
    source_ = next;
    // A fresh baseline for the slow cue; the step window keeps its samples.
    averageCount_ = 0;
    averageNext_ = 0;
    partSum_ = 0;
    partCount_ = 0;
    return true;
  }

  PowerSource source() const { return source_; }

 private:
  PowerSource driftSource(uint16_t mv) {
    partSum_ += mv;
    if (++partCount_ < DRIFT_AVERAGE) return source_;
    const uint16_t average = static_cast<uint16_t>(partSum_ / DRIFT_AVERAGE);
    partSum_ = 0;
    partCount_ = 0;
    PowerSource next = source_;
    if (averageCount_ >= DRIFT_MIN_AVERAGES) {
      uint16_t lo = averages_[0], hi = averages_[0];
      for (uint8_t i = 1; i < averageCount_; ++i) {
        if (averages_[i] < lo) lo = averages_[i];
        if (averages_[i] > hi) hi = averages_[i];
      }
      if (source_ == PowerSource::Usb && average + DRIFT_MV <= hi) next = PowerSource::Battery;
      if (source_ == PowerSource::Battery && average >= lo + DRIFT_MV) next = PowerSource::Usb;
    }
    averages_[averageNext_] = average;
    averageNext_ = static_cast<uint8_t>((averageNext_ + 1) % DRIFT_AVERAGES);
    if (averageCount_ < DRIFT_AVERAGES) ++averageCount_;
    return next;
  }

  // Atlas starts as if on USB, so the screen is on at power-up.
  PowerSource source_ = PowerSource::Usb;
  PowerSource candidate_ = PowerSource::Usb;
  uint16_t candidateBaseline_ = 0;
  uint8_t candidateCount_ = 0, candidateNeeded_ = 3, driftConfirm_ = 0, absentCount_ = 0;
  uint16_t recent_[STEP_WINDOW] = {};
  uint8_t recentNext_ = 0;
  uint8_t recentCount_ = 0;
  uint16_t averages_[DRIFT_AVERAGES] = {};
  uint8_t averageNext_ = 0;
  uint8_t averageCount_ = 0;
  uint32_t partSum_ = 0;
  uint8_t partCount_ = 0;
};

}  // namespace TurnHub
