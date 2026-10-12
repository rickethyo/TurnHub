# Atlas battery estimation and protection

*Experimental; host logic verified by regression tests, physical acceptance
pending.* Atlas E32R28T uses a nominal 250 mAh single-cell LiPo, its divider
and GPIO34 ADC. There is no fuel-gauge, dependable USB-sense or charger-status
signal. USB, charging and cell presence remain inferences. A charger holding an
empty connector high can look like an installed full battery. The existing
`present`/`charging` API fields retain these documented limitations.

## Measurement and displayed percentage

`atlas_battery.cpp` averages eight calibrated ADC reads every 250 ms and
scales the divider by `config.h`. `PowerSourceTracker` consumes those values
independently. Four readings are averaged for each nominal one-second gauge
sample. `battery_gauge.h` then keeps three separate quantities:

- `rawMillivolts`: this short-window input, useful for diagnosing load changes.
- `millivolts` and `voltagePercent`: median-of-three followed by the existing
  fixed-point 16-sample EMA and piecewise-linear generic LiPo curve.
- `percent`: a historical estimate with hysteresis and controlled movement,
  shown by the touchscreen and existing HTTP/Android consumers.

An isolated one-second spike/dip is rejected by the median. Normal downward
movement needs a two-point discrepancy and moves one point per 30 seconds.
A gap of at least five points lasting 30 seconds allows one point per five
seconds; a gap of at least 20 points lasting 15 seconds allows one per second.
These faster paths reconcile a wrong estimate and actual rapid discharge of
this small backup cell. They do not wait for the percentage to protect voltage.
On inferred battery power, upward corrections require a gap of at least 15
points sustained for two minutes and then rise one point per 30 seconds until
within hysteresis of the voltage target. That accepted correction stops when
the discrepancy disappears or reverses.
Smaller load-recovery rises do not claim new charge. Hysteresis may leave the
shown value one point from the voltage target.

A source change rebases voltage filters and resets charging/correction timers,
while retaining the shown percentage. USB unplugging never directly maps its
first loaded sample into a replacement percentage. During inferred USB power,
the estimate rises from the existing percentage using
`36000 * BATTERY_CAPACITY_MAH / BATTERY_CHARGE_MA` milliseconds per point
(currently 30 seconds), capped at 90 until the reading has held within 12 mV
at or above 4.08 V for four minutes. It then rises one point per minute and
can reach 100 only after 20 minutes at that limit. There is no jump to 85 on
reaching the plateau. Ten seconds below the plateau resets taper/full evidence.
Full suppresses the lightning bolt. Critical voltage overrides inferred
charging. Actual elapsed unsigned `millis` differences drive movement and
charge; one late service call earns at most two seconds, not missed minutes.

## Persistent history

The firmware adapter owns a single `battery/gauge` NVS blob through the existing
`NvsBlobStore`. The pure `battery_checkpoint.h` owns its codec, plausibility
rules and write policy; calculation code has no ESP32/NVS dependency.

The 16-byte, explicit little-endian v1 record contains a magic signature,
format/calibration version, displayed percentage, filtered voltage,
present/source evidence, nominal capacity, reserved zero byte, and an FNV-1a
checksum. It contains no charging elapsed time, full/taper timer or trusted
current power-source claim. Bump the calibration version for changes to the
curve/divider; capacity changes automatically invalidate older records.

After five live gauge samples the saved percentage is checked against the
median/faster-filtered live voltage and can be restored only if:

- Size, signature, version, reserved fields, ranges, capacity and checksum pass.
- A cell-like live reading exists, between 2.5 and 4.35 V; absence tombstones
  never restore a percentage.
- Voltage differs from saved voltage by at most 300 mV, allowing the measured
  roughly 210 mV charger step and loaded/resting differences.
- Percentage is within 20 points of the envelope between the curve at live
  voltage and live voltage minus the charging offset. At or below 3.58 V,
  values above 10% are rejected regardless of that envelope.

These tolerances are engineering starting points, not calibrated capacity
measurements. No dependable cold-boot wall clock exists: elapsed downtime,
self-discharge or a replaced cell at identical voltage cannot be known. Live
sustained evidence reconciles accepted but inaccurate history. With a pending checkpoint the ordinary startup estimate is withheld from
consumers for roughly five seconds until validation completes. Critical voltage
and its warning remain available immediately. Fresh boots without a checkpoint
use the ordinary initial voltage estimate.

RTC slow memory holds the newest credible record each gauge update after startup,
including a tombstone for confirmed absence or a contradicted display estimate. It is used preferentially only after an actual
deep-sleep reset, with the same live-voltage validation. Timer wakes that return
immediately to sleep leave it intact. Watchdog/brownout/cold/software restarts
use NVS. Charging/full timers always restart from zero; source detection starts
independently with its existing USB assumption. Factory reset erases NVS and a
normal reset ignores any retained RTC record.

Flash writes require at least 60 seconds since boot or the previous attempt,
and a five-point change or changed presence/source evidence. After ten minutes,
a smaller percentage change or at least 50 mV change can refresh the record.
Unchanged records are not rewritten. Failures retry no faster than once a minute;
only successful writes advance the saved baseline. Obviously contradictory
shown estimates invalidate earlier history with a tombstone, preventing an old
high RTC percentage being revived when USB raises voltage after low-cell sleep.
These storage-confidence tombstones do not change public battery presence. Three consecutive sub-2.5 V gauge samples
clear the gauge; an absence tombstone prevents restoring the old cell on a
subsequent wake. Short unplug/replacement sessions before a permitted NVS write
can leave older flash history, which is subject to the same plausibility checks.
RTC continuity avoids a special unthrottled flash commit on every sleep.

## Source inference and safety

Source detection is independent of all percentage smoothing. A step is compared
with the preceding four-sample mean, with a frozen baseline during confirmation.
Measured large steps (at least 180 mV) need three samples (about 0.75 seconds);
weaker steps of at least 100 mV need six (about 1.5 seconds). Slow detection
uses 60 mV drift over up to a minute of one-second averages and three consecutive
corroborating averages. Isolated load dips and rebounds do not switch the source.
Cold start keeps the screen on until evidence supports battery power. A load
change sustained long enough can still mimic USB; no voltage-only algorithm
reliably distinguishes all power-path, charger and absent-cell states.

The separate safety signal uses the median input and a four-sample EMA.
`criticalLow` enters at 3.58 V and clears at 3.64 V. Inferred battery operation
with critical voltage for 30 seconds requests the existing System Sleep Intent,
even when shown charge is high. At 3.45 V or lower, emergency protection uses a
five-second confirmation and also operates while the cold-boot source is still
assumed USB. Filtering precedes these waits; thresholds must be validated against
regulator dropout and cell load. Isolated dips cannot trigger either path.
The LOW BATTERY warning uses the more conservative displayed/voltage estimate
on battery, retains 15/20% hysteresis, and always asserts on critical voltage.

Existing idle sleep, update/reset interlocks, game interruption recovery, touch
peek, sleep GPIO holding and resting-voltage USB wake tests remain. Emergency
sleep with uncertain source also arms the battery USB timer wake path. The
regulator, bridge and LED continue draining a sleeping cell; the protection
board remains the last stop. USB wake itself is an existing uncalibrated
resting-voltage heuristic and requires bench acceptance.

## Calibration and physical acceptance

Keep `LIPO_CURVE`: its generic data are not measured Atlas capacity data.
Interpolation is already continuous and monotonic; the middle has slopes as
high as seven points per 20 mV (3.78–3.80 V), so 30 mV of load/noise can imply
roughly ten percentage points. Median filtering, EMA and historical movement
address that sensitivity without inventing new calibration points.

With the actual cell, board, divider, protection PCB and normal firmware load:

1. Compare ADC and a meter at the connector across the usable voltage range.
2. Run a timed discharge from a documented full charge to safe shutdown, logging
   raw/filtered/safety voltage, voltage percent, displayed percent and source.
   Measure current/coulombs if possible; time alone assumes constant load.
3. Repeat with screen dark, touch peek, Wi-Fi/ESP-NOW traffic, speaker and
   backlight loads. Establish regulator brownout margin and threshold latency;
   confirm sleep/recovery before power fails, including an active game.
4. Capture USB unplug/replug at full/middle/low charge; test short dips, long
   load changes, partial/full/cell-less boot, protected/disconnected cell and
   replacement. Check source debounce and backlight responsiveness.
5. Restart with valid/old/corrupt records, cut power during an NVS write, sleep
   and wake through touch/BOOT/USB, and leave asleep for hours. Verify charging
   timers restart, retained charge is plausible and empty wakes wait for USB.
6. Time a complete charge and taper; measure sleep current, charger current and
   temperature. The existing 300 mA charger can exceed the small cell's rating
   ([Hardware](HARDWARE.md)); changing capacity/current requires corresponding
   constants, plausibility review and renewed acceptance.

Use repeated loaded discharge traces to replace curve points, bump checkpoint
calibration version, and replay traces through host tests. Record cell capacity,
temperature, age, screen/radio load, charge termination and safe end voltage.
No fresh hardware measurements or flashing were performed for this change.
