#pragma once

// NeoPixel Jewel 7 (RGBW) on the E-ink Sigil: shows each LedFrame
// (sigil_led.h) pixel for pixel. Compiled only when TURNHUB_STATUS_RING is
// set: other builds still use the ring's GPIO as the Pass button.

#include <stdint.h>

#include "sigil_led.h"

namespace TurnHubSigil {

void statusRingBegin(uint8_t pin);
// Redraws only when a pixel actually changes.
void statusRingShow(const LedFrame &frame);

}  // namespace TurnHubSigil
