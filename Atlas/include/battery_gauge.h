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
  bool charging = false;    // On USB with a cell and below 100 %: the charger is filling it.
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
// Started on USB and already at the limit: at least this full.
constexpr uint8_t CHARGE_AT_LIMIT_MIN_PERCENT = 85;

// Smooths the samples and steadies the shown percent, so a load change (the
// backlight, a radio burst, a chime) doesn't make it jump; counts it up while
// charging (above).
class BatteryGauge {
 public:
  // Exponential average with a time constant of about SMOOTHING_SAMPLES
  // samples (one sample a second on firmware).
  static constexpr uint8_t SMOOTHING_SAMPLES = 16;
  static constexpr uint32_t SAMPLE_MS = 1000;
  // The shown percent moves only when the estimate is this far from it.
  static constexpr uint8_t PERCENT_HYSTERESIS = 2;

  void reset() { *this = BatteryGauge(); }

  // One reading of the cell voltage (already scaled for the divider), and
  // whether Atlas is on USB (charging). A change of source starts over from
  // this sample instead of easing over a minute.
  void addSample(uint32_t cellMillivolts, bool charging = false) {
    if (cellMillivolts < BATTERY_ABSENT_BELOW_MV) {
      // Unplugged: start over so a new cell isn't averaged with nothing.
      reset();
      return;
    }
    if (!seeded_ || charging != onUsb_) {
      start(cellMillivolts, charging);
    } else {
      // Fixed point (x16) so small steps aren't lost to rounding.
      const int32_t target = static_cast<int32_t>(cellMillivolts * 16);
      averageMv16_ += (target - averageMv16_) / SMOOTHING_SAMPLES;
      reading_.millivolts = static_cast<uint16_t>((averageMv16_ + 8) / 16);
      if (charging) countCharge(static_cast<uint16_t>(cellMillivolts));
      else followCurve();
    }
    // Full shows as full, not charging (owner 2026-10-08: no bolt at 100 %).
    reading_.charging = onUsb_ && reading_.percent < 100;
    if (reading_.percent <= BATTERY_LOW_PERCENT) {
      reading_.low = true;
    } else if (reading_.percent >= BATTERY_LOW_PERCENT + BATTERY_LOW_CLEAR_MARGIN) {
      reading_.low = false;
    }
  }

  const BatteryReading &reading() const { return reading_; }

  // Starts over from this sample (on the cell).
  void reseed(uint32_t cellMillivolts) {
    reset();
    addSample(cellMillivolts);
  }

 private:
  void start(uint32_t cellMillivolts, bool charging) {
    // The on-battery percent, if there is one, is where a charge starts.
    const bool fromBattery = seeded_ && !onUsb_;
    const uint8_t last = reading_.percent;
    seeded_ = true;
    averageMv16_ = static_cast<int32_t>(cellMillivolts * 16);
    reading_.present = true;
    onUsb_ = charging;
    reading_.millivolts = static_cast<uint16_t>(cellMillivolts);
    chargeMs_ = 0;
    limitMs_ = 0;
    atLimit_ = false;
    recentCount_ = 0;
    recentNext_ = 0;
    startedOnUsb_ = charging && !fromBattery;
    if (!charging) {
      reading_.percent = lipoPercentFromMillivolts(cellMillivolts);
    } else if (fromBattery) {
      reading_.percent = last;
    } else {
      const uint8_t guess = lipoPercentFromMillivolts(cellMillivolts > CHARGE_RISE_MV ? cellMillivolts - CHARGE_RISE_MV : 0);
      reading_.percent = guess < CHARGE_BELOW_LIMIT_MAX_PERCENT ? guess : CHARGE_BELOW_LIMIT_MAX_PERCENT;
    }
  }

  void followCurve() {
    const uint8_t estimate = lipoPercentFromMillivolts(reading_.millivolts);
    const int diff = static_cast<int>(estimate) - static_cast<int>(reading_.percent);
    if (diff >= PERCENT_HYSTERESIS || diff <= -PERCENT_HYSTERESIS || estimate == 0 || estimate == 100) {
      reading_.percent = estimate;
    }
  }

  // Never down while charging; up one point per CHARGE_MS_PER_POINT, half
  // that at the limit, and 100 only after CHARGE_FULL_AFTER_LIMIT_MS there.
  void countCharge(uint16_t mv) {
    recent_[recentNext_] = mv;
    recentNext_ = static_cast<uint8_t>((recentNext_ + 1) % CHARGE_LIMIT_SAMPLES);
    if (recentCount_ < CHARGE_LIMIT_SAMPLES) ++recentCount_;
    if (!atLimit_ && recentCount_ == CHARGE_LIMIT_SAMPLES) {
      uint16_t lo = recent_[0], hi = recent_[0];
      for (uint8_t i = 1; i < CHARGE_LIMIT_SAMPLES; ++i) {
        if (recent_[i] < lo) lo = recent_[i];
        if (recent_[i] > hi) hi = recent_[i];
      }
      atLimit_ = lo >= CHARGE_LIMIT_MIN_MV && hi - lo <= CHARGE_LIMIT_SPREAD_MV;
      if (atLimit_ && startedOnUsb_ && reading_.percent < CHARGE_AT_LIMIT_MIN_PERCENT) {
        reading_.percent = CHARGE_AT_LIMIT_MIN_PERCENT;
      }
    }
    chargeMs_ += SAMPLE_MS;
    if (atLimit_) limitMs_ += SAMPLE_MS;
    const uint32_t perPoint = atLimit_ ? CHARGE_LIMIT_MS_PER_POINT : CHARGE_MS_PER_POINT;
    const uint8_t cap = !atLimit_ ? CHARGE_BELOW_LIMIT_MAX_PERCENT : limitMs_ >= CHARGE_FULL_AFTER_LIMIT_MS ? 100 : 99;
    if (chargeMs_ >= perPoint) {
      chargeMs_ -= perPoint;
      if (reading_.percent < cap) ++reading_.percent;
    }
    if (cap == 100 && reading_.percent == 99) reading_.percent = 100;
  }

  bool seeded_ = false;
  bool onUsb_ = false;
  int32_t averageMv16_ = 0;
  BatteryReading reading_;
  // Charging.
  uint32_t chargeMs_ = 0;
  uint32_t limitMs_ = 0;
  bool atLimit_ = false;
  bool startedOnUsb_ = false;
  uint16_t recent_[CHARGE_LIMIT_SAMPLES] = {};
  uint8_t recentNext_ = 0;
  uint8_t recentCount_ = 0;
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
