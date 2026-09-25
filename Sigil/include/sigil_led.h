#pragma once

// Sigil status-light renderer. Atlas decides the meaning (LedState: cue,
// overlays, seat, style); this turns it into colors and patterns locally, so
// animation is smooth and costs no radio traffic. Pure logic, host-tested.
//
// One frame covers the NeoPixel Jewel 7: pixel 0 is the center, 1-6 the
// ring. The ring shows the primary cue (spatially where that helps: player
// number as lit pixels, seat A/B as ring halves); the center shows the most
// important overlay. single() is the same state for a one-LED Sigil, carried
// by timing instead of position. Every cue differs by pattern or position,
// not only by color (ACCESSIBILITY.md).

#include <stdint.h>

#include "protocol.h"

namespace TurnHubSigil {

struct Rgb {
  uint8_t r, g, b;
  // Constructors, not member initializers: the ESP32 toolchain is C++11,
  // where those would stop {r, g, b} brace initialization.
  constexpr Rgb() : r(0), g(0), b(0) {}
  constexpr Rgb(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
  bool operator==(const Rgb &o) const { return r == o.r && g == o.g && b == o.b; }
  bool operator!=(const Rgb &o) const { return !(*this == o); }
};

constexpr uint8_t LED_PIXELS = 7;
constexpr uint8_t LED_CENTER = 0;
constexpr uint32_t PASS_ACK_FLASH_MS = 250;

struct LedFrame {
  Rgb pixels[LED_PIXELS];
  Rgb single;  // One-LED view of the same state.
};

class SigilLedModel {
 public:
  // Atlas's semantic state. Replaces any legacy channel values.
  void applyLedState(int32_t value, uint32_t nowMs);
  // Legacy SetRed/SetGreen/SetBlue from an Atlas that predates LedState.
  void applyLegacyRed(bool on);
  void applyLegacyGreen(bool on);
  void applyLegacyBlue(uint8_t level);
  // Unpaired or forgotten: dark until Atlas says otherwise.
  void clear();

  // Sigil-local conditions Atlas cannot drive.
  void setPairing(bool active, uint32_t nowMs);
  void flashPassAck(uint32_t nowMs);

  LedFrame render(uint32_t nowMs) const;

  const TurnHubProtocol::LedStateFields &state() const { return state_; }
  bool semantic() const { return semantic_; }

 private:
  TurnHubProtocol::LedStateFields state_;
  bool semantic_ = false;
  uint32_t anchorMs_ = 0;
  bool legacyRed_ = false, legacyGreen_ = false;
  uint8_t legacyBlue_ = 0;
  bool pairing_ = false;
  uint32_t pairingStartMs_ = 0;
  bool passAck_ = false;
  uint32_t passAckUntilMs_ = 0;
};

}  // namespace TurnHubSigil
