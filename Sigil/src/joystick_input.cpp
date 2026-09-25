#include "joystick_input.h"

namespace TurnHubSigil {
namespace {
int16_t absolute(int32_t value) {
  return static_cast<int16_t>(value < 0 ? -value : value);
}
}

bool StickTracker::calibrate(const int16_t *xs, const int16_t *ys, uint8_t count) {
  enabled_ = false;
  armed_ = true;
  direction_ = StickDirection::Center;
  if (!xs || !ys || count == 0) return false;
  int32_t sumX = 0, sumY = 0;
  int16_t minX = xs[0], maxX = xs[0], minY = ys[0], maxY = ys[0];
  for (uint8_t i = 0; i < count; ++i) {
    sumX += xs[i];
    sumY += ys[i];
    if (xs[i] < minX) minX = xs[i];
    if (xs[i] > maxX) maxX = xs[i];
    if (ys[i] < minY) minY = ys[i];
    if (ys[i] > maxY) maxY = ys[i];
  }
  centerX_ = static_cast<int16_t>(sumX / count);
  centerY_ = static_cast<int16_t>(sumY / count);
  const auto plausible = [this](int16_t center, int16_t low, int16_t high) {
    return center >= config_.centerMin && center <= config_.centerMax &&
        high - low <= config_.maxCalibrationSpread;
  };
  enabled_ = plausible(centerX_, minX, maxX) && plausible(centerY_, minY, maxY);
  return enabled_;
}

StickDirection StickTracker::update(int16_t rawX, int16_t rawY) {
  if (!enabled_) return direction_ = StickDirection::Center;
  int32_t dx = static_cast<int32_t>(rawX) - centerX_;
  int32_t dy = static_cast<int32_t>(rawY) - centerY_;
  if (config_.swapAxes) {
    const int32_t t = dx;
    dx = dy;
    dy = t;
  }
  if (config_.invertX) dx = -dx;
  if (config_.invertY) dy = -dy;

  // Held: stay until this direction's own axis falls back under release.
  switch (direction_) {
    case StickDirection::Right: if (dx > config_.release) return direction_; break;
    case StickDirection::Left: if (-dx > config_.release) return direction_; break;
    case StickDirection::Down: if (dy > config_.release) return direction_; break;
    case StickDirection::Up: if (-dy > config_.release) return direction_; break;
    case StickDirection::Center: break;
  }
  // Released (or never pushed): a new direction arms only once both axes
  // are back inside the release band.
  direction_ = StickDirection::Center;
  const int16_t ax = absolute(dx), ay = absolute(dy);
  if (ax < config_.release && ay < config_.release) armed_ = true;
  if (!armed_) return direction_;
  if (ax >= ay && ax > config_.engage) {
    direction_ = dx > 0 ? StickDirection::Right : StickDirection::Left;
    armed_ = false;
  } else if (ay > ax && ay > config_.engage) {
    direction_ = dy > 0 ? StickDirection::Down : StickDirection::Up;
    armed_ = false;
  }
  return direction_;
}

const char *stickDirectionName(StickDirection direction) {
  switch (direction) {
    case StickDirection::Up: return "UP";
    case StickDirection::Down: return "DOWN";
    case StickDirection::Left: return "LEFT";
    case StickDirection::Right: return "RIGHT";
    case StickDirection::Center: break;
  }
  return "CENTER";
}

}  // namespace TurnHubSigil
