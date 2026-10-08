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

 private:
  bool seeded_ = false;
  int32_t averageMv16_ = 0;
  BatteryReading reading_;
};

}  // namespace TurnHub
