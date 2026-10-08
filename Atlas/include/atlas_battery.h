#pragma once

#include <stdint.h>

#include "battery_gauge.h"

// Atlas's battery: samples the cell voltage on BATTERY_ADC_PIN through the
// board's divider four times a second, keeps a smoothed charge estimate and
// tells USB power from the cell (battery_gauge.h). Presentation only: nothing in play depends on it.
// Firmware-only; host tests use the stubs in test_globals.cpp.

namespace TurnHubAtlas {

void beginAtlasBattery();
void serviceAtlasBattery(uint32_t nowMs);
// The latest estimate; not present until the first sample, or with no cell.
const TurnHub::BatteryReading &atlasBattery();
// True while Atlas runs from its cell, USB unplugged (logged as
// ATLAS|POWER|BATTERY / ATLAS|POWER|USB); false at power-up and with no cell.
bool atlasOnBattery();

}  // namespace TurnHubAtlas
