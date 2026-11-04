#pragma once

// Phone-hosted games on the Sigils: menus and inputs (hosted_sigils.cpp).
// Drawing is LedRenderer::renderHosted.

#include <stdint.h>

#include "protocol.h"

namespace TurnHubAtlas {

// A hub hosts this Sigil's game (Game 1).
bool hostedForSigil(uint8_t sigilId, uint32_t nowMs);
// The menu a Sigil gets in a hosted game.
TurnHubProtocol::MenuStateFields hostedSigilMenu(uint8_t sigilId);
// A Sigil's key or menu choice in a hosted game: queued for the hub, never applied here.
void handleHostedSigilEvent(uint8_t sigilId, TurnHubProtocol::PacketType type, int32_t value);

}  // namespace TurnHubAtlas
