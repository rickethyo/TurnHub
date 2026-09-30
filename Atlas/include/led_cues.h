#pragma once

#include <Arduino.h>

#include "protocol.h"

namespace TurnHub {

// LED presentation is two separate steps:
//
//   game semantics -> SigilLedState (which cue, which facets)   LedRenderer
//   SigilLedState  -> colors and cadences on the Jewel ring     the Sigil
//
// Atlas selects the cue and sends it as LedState with the player's style
// (default, reduced motion, monochrome-safe); every Sigil draws it itself
// (Sigil/src/sigil_led.cpp). Selection never looks at colors or cadences, so
// presentation can change without touching game logic. Every cue must stay
// distinguishable by pattern/cadence, not only by hue (ACCESSIBILITY.md), and
// essential meaning also reaches the portal/app as text.
using LedCue = TurnHubProtocol::LedCue;
using LedOverlay = TurnHubProtocol::LedOverlay;

constexpr uint8_t ledOverlayBit(LedOverlay overlay) {
  return static_cast<uint8_t>(1u << static_cast<uint8_t>(overlay));
}

// Everything the Sigil needs to draw a cue; produced by selection.
struct SigilLedState {
  LedCue cue = LedCue::Off;
  uint8_t overlays = 0;
  uint8_t playerNumber = 0;  // Joined: number of flashes.
  uint8_t seatSlot = 1;      // SeatPulse target: 1 = A (one pulse), 2 = B (two).
  bool sharedSeat = false;   // SeatPulse is steady when the Sigil has one seat.
  uint32_t anchorMs = 0;     // Start of the cue (turn start, countdown start).

  bool has(LedOverlay overlay) const { return (overlays & ledOverlayBit(overlay)) != 0; }
};

}  // namespace TurnHub
