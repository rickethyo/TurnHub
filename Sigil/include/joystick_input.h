#pragma once

// Analog thumbstick (KY-023 style: VRX, VRY, SW) turned into the Sigil's
// existing button inputs. Pure logic with no Arduino dependency, so the host
// tests exercise the same code the firmware runs.

#include <stdint.h>

namespace TurnHubSigil {

enum class StickDirection : uint8_t { Center, Up, Down, Left, Right };

struct StickConfig {
  // 12-bit ADC counts away from the calibrated center. Engage is well past
  // the stick's slop; release is lower so a held push cannot chatter.
  int16_t engage = 1200;
  int16_t release = 700;
  // Orientation fixes for how the unmarked module is mounted. After these,
  // a larger X means Right and a larger Y means Down.
  bool swapAxes = false;
  bool invertX = false;
  bool invertY = false;
  // Boot calibration: a resting stick reads near mid-scale and barely moves.
  // Anything else (held at boot, unplugged, floating ADC pins) disables the
  // directions rather than risk phantom presses.
  int16_t centerMin = 1200;
  int16_t centerMax = 2900;
  int16_t maxCalibrationSpread = 250;
};

class StickTracker {
 public:
  explicit StickTracker(const StickConfig &config = StickConfig()) : config_(config) {}

  // Feed resting samples at boot. Returns true when the stick looks present
  // and centered; otherwise directions stay disabled (Center forever).
  bool calibrate(const int16_t *xs, const int16_t *ys, uint8_t count);

  // Returns the settled direction for one raw reading. Only the dominant
  // axis counts, and the stick must come back to center before a different
  // direction can engage, so sliding Right into Down never fires Down.
  StickDirection update(int16_t rawX, int16_t rawY);

  StickDirection direction() const { return direction_; }
  bool enabled() const { return enabled_; }
  int16_t centerX() const { return centerX_; }
  int16_t centerY() const { return centerY_; }

 private:
  const StickConfig config_;
  bool enabled_ = false;
  int16_t centerX_ = 2048;
  int16_t centerY_ = 2048;
  StickDirection direction_ = StickDirection::Center;
  bool armed_ = true;
};

const char *stickDirectionName(StickDirection direction);

}  // namespace TurnHubSigil
