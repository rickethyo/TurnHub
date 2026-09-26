#pragma once

// Menu Sigils (CAPABILITY_MENU): which actions each one may offer right now,
// and the MenuState transport. Availability mirrors what the Intent handlers
// would accept so the menu never offers a dead end, but it is presentation
// only: every choice still goes through the dispatcher and its validators
// (SelectAction -> handleSelectAction in sigil_input.cpp).

#include <Arduino.h>

#include "protocol.h"

namespace TurnHubAtlas {

// The actions and default for one Sigil now (revision left 0). Pure read of
// table state; host-tested.
TurnHubProtocol::MenuStateFields sigilMenuFor(uint8_t sigilId);

// Sends MenuState to each online menu Sigil whose menu changed (bumping its
// revision) or was invalidated. Call every loop.
void syncSigilMenus(uint32_t nowMs);
// Resend the current menu (every Hello, and after a stale selection).
void invalidateSigilMenu(uint8_t sigilId);
// Revision of the menu last computed for this Sigil; a SelectAction must match it.
uint8_t sigilMenuRevision(uint8_t sigilId);
// The pending life request (encodeLifeRequest) aimed at one of this Sigil's
// living players, or 0. syncSigilMenus sends it as LifeRequest to 0.8.0+
// Sigils; the Sigil answers with LifeResponse (sigil_input.cpp).
int32_t sigilLifeRequestFor(uint8_t sigilId);
// The SeatColor for one seat (its bound profile's Jewel color, or none).
int32_t sigilSeatColorFor(uint8_t sigilId, uint8_t slot);
// Forget every menu (tests, and boot).
void resetSigilMenus();

}  // namespace TurnHubAtlas
