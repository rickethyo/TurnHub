#pragma once

#include <stdint.h>

#include "audio_controller.h"

// Atlas's on-board speaker: the amplifier (enable active low) fed by an LEDC
// square wave on IO26, loudness set by duty cycle. Presentation only: AudioController decides
// which cues it plays, and every cue has a visual equivalent. Firmware-only;
// host tests use the stubs in test_globals.cpp.

namespace TurnHubAtlas {

// Parks the amplifier off and returns the speaker, never nullptr on firmware.
TurnHub::ToneOutput *beginAtlasSpeaker();
// Ends the current tone once its duration has passed. Call every loop.
void serviceAtlasSpeaker(uint32_t nowMs);

}  // namespace TurnHubAtlas
