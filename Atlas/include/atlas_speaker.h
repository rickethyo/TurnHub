#pragma once

#include <stdint.h>

#include "audio_controller.h"

// ES3C40P ES8311 codec over I2S, FM8002E amplifier; synthesized cues
// run in a background task. AudioController owns cue selection and volume.
// Firmware-only; host tests use test_globals.cpp stubs.

namespace TurnHubAtlas {

// Parks the amplifier off and returns the speaker, never nullptr on firmware.
TurnHub::ToneOutput *beginAtlasSpeaker();
// Kept for the main loop's call; the synthesizer task needs no servicing.
void serviceAtlasSpeaker(uint32_t nowMs);

}  // namespace TurnHubAtlas
