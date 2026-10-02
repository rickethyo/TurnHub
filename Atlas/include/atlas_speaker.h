#pragma once

#include <stdint.h>

#include "audio_controller.h"

// Atlas's on-board speaker: the amplifier (enable active low) fed by a small
// chime synthesizer on the built-in DAC (IO26, streamed by I2S0) that runs in
// its own task. Presentation only: AudioController decides
// which cues it plays, and every cue has a visual equivalent. Firmware-only;
// host tests use the stubs in test_globals.cpp.

namespace TurnHubAtlas {

// Parks the amplifier off and returns the speaker, never nullptr on firmware.
TurnHub::ToneOutput *beginAtlasSpeaker();
// Kept for the main loop's call; the synthesizer task needs no servicing.
void serviceAtlasSpeaker(uint32_t nowMs);

}  // namespace TurnHubAtlas
