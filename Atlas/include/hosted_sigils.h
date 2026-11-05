#pragma once

// Phone-hosted games on the Sigils and the Atlas screen: menus and inputs
// (hosted_sigils.cpp).
// Drawing is LedRenderer::renderHosted.

#include <stdint.h>

#include "protocol.h"

namespace TurnHubAtlas {

// A hub hosts the game.
bool hostedForSigil(uint8_t sigilId, uint32_t nowMs);
// The menu a Sigil gets in a hosted game.
TurnHubProtocol::MenuStateFields hostedSigilMenu(uint8_t sigilId);
// A Sigil's key or menu choice in a hosted game: queued for the hub, never applied here.
void handleHostedSigilEvent(uint8_t sigilId, TurnHubProtocol::PacketType type, int32_t value);

// The Atlas touchscreen's controls in a hosted game (Game 1): the same few
// table controls its own games offer, each queued for the hub. The active
// player acts, as the touchscreen's Pass and Pause do in Atlas's own games.
struct HostedScreenActions {
  bool start = false;   // setup with at least two players
  bool pass = false;    // a living player's turn
  bool pause = false;
  bool resume = false;  // paused, with no open win claim
  // Planechase on, in play: the active player rolls the planar die (no
  // phenomenon up), resolves a phenomenon, or planeswalks when a card says so.
  bool roll = false;
  bool resolve = false;
  bool walk = false;
};
enum class HostedScreenAction : uint8_t { Start, Pass, Pause, Resume, PlanarRoll, PlanarWalk, PlanarResolve };
HostedScreenActions hostedScreenActions();
// Queues the action for the hub if the hosted game offers it now.
bool queueHostedScreenAction(HostedScreenAction action);

}  // namespace TurnHubAtlas
