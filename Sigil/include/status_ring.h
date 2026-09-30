#pragma once

// NeoPixel Jewel 7 (RGBW), the status light on every Sigil: shows each
// LedFrame (sigil_led.h) pixel for pixel.

#include <stdint.h>

#include "sigil_led.h"

namespace TurnHubSigil {

void statusRingBegin(uint8_t pin);
// Redraws only when a pixel actually changes.
void statusRingShow(const LedFrame &frame);

}  // namespace TurnHubSigil
