#pragma once

// One physical button, three gestures (Sigil Pair/BOOT button, Atlas BOOT
// button): quick press = pair, medium hold = unpair, long hold = factory
// reset. Pure logic, shared by Atlas and Sigil and host-tested.
//
//   pressed, released before UNPAIR_HOLD_MS       -> Pair (on release)
//   still held at UNPAIR_HOLD_MS                  -> Unpair (once, while held)
//   still held at FACTORY_RESET_HOLD_MS           -> FactoryReset (once, while held)
//   released after Unpair or FactoryReset         -> nothing
//
// A press that is already down when the device starts (BOOT held through a
// reset) is ignored until the button has been seen released.

#include <stdint.h>

#include "protocol.h"

namespace TurnHubProtocol {

enum class ButtonGesture : uint8_t { None, Pair, Unpair, FactoryReset };

class ThreePartButton {
 public:
  // Call every loop with the debounced state (true = pressed). Each gesture
  // is returned once.
  ButtonGesture update(bool pressed, uint32_t nowMs) {
    if (!pressed) {
      const bool wasQuick = down_ && stage_ == 0;
      down_ = false;
      armed_ = true;
      return wasQuick ? ButtonGesture::Pair : ButtonGesture::None;
    }
    if (!armed_) return ButtonGesture::None;
    if (!down_) {
      down_ = true;
      startMs_ = nowMs;
      stage_ = 0;
      return ButtonGesture::None;
    }
    const uint32_t heldMs = nowMs - startMs_;
    if (heldMs >= FACTORY_RESET_HOLD_MS && stage_ < 2) {
      stage_ = 2;
      return ButtonGesture::FactoryReset;
    }
    if (heldMs >= UNPAIR_HOLD_MS && stage_ < 1) {
      stage_ = 1;
      return ButtonGesture::Unpair;
    }
    return ButtonGesture::None;
  }

  bool down() const { return down_; }
  // Milliseconds the button has been held (0 when up), for feedback.
  uint32_t heldMs(uint32_t nowMs) const { return down_ ? nowMs - startMs_ : 0; }

 private:
  bool armed_ = false;  // Seen released at least once.
  bool down_ = false;
  uint8_t stage_ = 0;   // 0 quick, 1 unpair fired, 2 factory reset fired.
  uint32_t startMs_ = 0;
};

}  // namespace TurnHubProtocol
