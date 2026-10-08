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
};

// Below this the line is floating or shorted: no cell.
constexpr uint16_t BATTERY_ABSENT_BELOW_MV = 2500;
constexpr uint8_t BATTERY_LOW_PERCENT = 15;
// The low warning clears only once the charge is this much above the threshold.
constexpr uint8_t BATTERY_LOW_CLEAR_MARGIN = 5;

// Smooths the samples and steadies the shown percent, so a load change (the
// backlight, a radio burst, a chime) doesn't make it jump.
class BatteryGauge {
 public:
  // Exponential average with a time constant of about SMOOTHING_SAMPLES
  // samples (one sample a second on firmware).
  static constexpr uint8_t SMOOTHING_SAMPLES = 16;
  // The shown percent moves only when the estimate is this far from it.
  static constexpr uint8_t PERCENT_HYSTERESIS = 2;

  void reset() { *this = BatteryGauge(); }

  // One reading of the cell voltage (already scaled for the divider).
  void addSample(uint32_t cellMillivolts) {
    if (cellMillivolts < BATTERY_ABSENT_BELOW_MV) {
      // Unplugged: start over so a new cell isn't averaged with nothing.
      reset();
      return;
    }
    if (!seeded_) {
      averageMv16_ = cellMillivolts * 16;
      seeded_ = true;
    } else {
      // Fixed point (x16) so small steps aren't lost to rounding.
      const int32_t target = static_cast<int32_t>(cellMillivolts * 16);
      averageMv16_ += (target - averageMv16_) / SMOOTHING_SAMPLES;
    }
    const uint16_t mv = static_cast<uint16_t>((averageMv16_ + 8) / 16);
    const uint8_t estimate = lipoPercentFromMillivolts(mv);
    if (!reading_.present) {
      reading_.percent = estimate;
    } else {
      const int diff = static_cast<int>(estimate) - static_cast<int>(reading_.percent);
      if (diff >= PERCENT_HYSTERESIS || diff <= -PERCENT_HYSTERESIS || estimate == 0 || estimate == 100) {
        reading_.percent = estimate;
      }
    }
    reading_.present = true;
    reading_.millivolts = mv;
    if (reading_.percent <= BATTERY_LOW_PERCENT) {
      reading_.low = true;
    } else if (reading_.percent >= BATTERY_LOW_PERCENT + BATTERY_LOW_CLEAR_MARGIN) {
      reading_.low = false;
    }
  }

  const BatteryReading &reading() const { return reading_; }

  // Starts over from this sample, so the estimate jumps straight to a new
  // level (USB plugged in or pulled) instead of easing over a minute.
  void reseed(uint32_t cellMillivolts) {
    reset();
    addSample(cellMillivolts);
  }

 private:
  bool seeded_ = false;
  int32_t averageMv16_ = 0;
  BatteryReading reading_;
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
  // A sample this far below the highest (or above the lowest) of the last
  // STEP_WINDOW samples is USB pulled (or plugged in).
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
      const PowerSource was = source_;
      *this = PowerSourceTracker();
      return was != source_;
    }
    const uint16_t mv = static_cast<uint16_t>(cellMillivolts > 0xFFFF ? 0xFFFF : cellMillivolts);
    PowerSource next = source_;
    if (recentCount_ > 0) {
      uint16_t lo = recent_[0], hi = recent_[0];
      for (uint8_t i = 1; i < recentCount_; ++i) {
        if (recent_[i] < lo) lo = recent_[i];
        if (recent_[i] > hi) hi = recent_[i];
      }
      if (mv + STEP_MV <= hi) next = PowerSource::Battery;
      else if (mv >= lo + STEP_MV) next = PowerSource::Usb;
    }
    recent_[recentNext_] = mv;
    recentNext_ = static_cast<uint8_t>((recentNext_ + 1) % STEP_WINDOW);
    if (recentCount_ < STEP_WINDOW) ++recentCount_;

    if (next == source_) next = driftSource(mv);
    if (next == source_) return false;
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
