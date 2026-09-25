#pragma once

// Link to a hardware test harness (TestHarness/): a paired Sigil whose Hello
// carries CAPABILITY_HARNESS. The touchscreen starts its premade tests through
// here, and its HarnessReport progress is kept for the screen. Presentation
// and developer tooling only: the harness plays through the normal Sigil
// packets and Atlas's handlers, so nothing here touches table or game state.

#include <Arduino.h>

#include "protocol.h"

namespace TurnHubAtlas {

// A report older than this means the harness stopped answering.
constexpr uint32_t HARNESS_REPORT_STALE_MS = 7000;

// The online harness's Sigil ID, or INVALID_ID when none is connected.
uint8_t harnessSigilId(uint32_t nowMs);

// Asks the harness to run a test, or to stop the one running. False when no
// harness is online or the radio refused the packet.
bool requestHarnessTest(TurnHubProtocol::HarnessTest test, uint32_t nowMs);
bool requestHarnessStop(uint32_t nowMs);

// From the Sigil adapter: a HarnessReport packet. Ignored unless that Sigil
// is the harness.
void noteHarnessReport(uint8_t sigilId, int32_t value, uint32_t nowMs);

// The latest fresh report from the online harness; false when there is none.
bool harnessReport(uint32_t nowMs, TurnHubProtocol::HarnessReportFields &report);

// Clears the stored report (tests, and boot).
void resetHarnessLink();

}  // namespace TurnHubAtlas
