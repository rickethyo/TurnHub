#pragma once

#include <stdint.h>

// The Atlas board boundary. A board provides this file's functions and those
// in atlas_display.h, atlas_battery.h and atlas_speaker.h; game code never
// touches pins directly. Master builds atlas_board_none.cpp (no screen,
// speaker, battery or status LED) until the ESP32-S3 board profile lands; the
// Gen 1 E32R28T implementation lives on legacy/gen1. Host tests stub these in
// test_globals.cpp.

namespace TurnHubAtlas {

// Parks the board's status light off.
void beginAtlasStatusLed();
// The status light's red, green and blue channels. An extra cue only: every
// state it shows is also said in words (front_panel.cpp).
void setAtlasStatusLed(bool red, bool green, bool blue);

}  // namespace TurnHubAtlas
