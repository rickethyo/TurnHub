#pragma once

#include <Arduino.h>

#include "game_engine.h"
#include "led_cues.h"
#include "lobby.h"
#include "sigil_bus.h"
#include "turnhub_types.h"

namespace TurnHub {

// Game semantics -> semantic LED state for one physical Sigil. Pure: reads
// Atlas state only, knows nothing about colors, cadences or transport.
SigilLedState selectSigilLedState(
    uint8_t sigilId,
    HubState state,
    const Lobby &lobby,
    const GameEngine &game,
    uint32_t countdownStartedAtMs,
    uint8_t eliminationTargetPlayer,
    uint8_t winConfirmationPlayer,
    uint32_t nowMs);

class LedRenderer {
 public:
  explicit LedRenderer(SigilBus &bus);

  void invalidate(uint8_t sigilId);
  void invalidateAll();

  // The seated player's accessibility style for one Sigil's light, sent with
  // every LedState; the Sigil draws it.
  void setStyle(uint8_t sigilId, TurnHubProtocol::LedStyle style);
  TurnHubProtocol::LedStyle style(uint8_t sigilId) const;

  void render(
      HubState state,
      const Lobby &lobby,
      const GameEngine &game,
      uint32_t countdownStartedAtMs,
      uint8_t eliminationTargetPlayer,
      uint8_t winConfirmationPlayer,
      uint32_t nowMs);

  // A shared Sigil shows one of its seats full screen, and its life keys
  // change the shown seat. This switches to its other living seat (the
  // Sigil menu's Switch seat). The choice holds until the turn comes round to
  // one of this Sigil's seats, which is then shown. View state only: it
  // changes no game state and needs no Intent. False if there is nothing to
  // switch to.
  bool switchShownSeat(uint8_t sigilId, const GameEngine &game);

 private:
  struct SeatFocus {
    uint8_t player = 0;            // 0: default (the active seat, else seat A).
    uint8_t activeWhenChosen = 0;  // The active player when it was chosen.
  };

  struct Cache {
    bool ledStateValid = false;
    uint32_t ledStateKey = 0;
    bool clockValid = false;  // TableClock reached the Sigil; resent every interval.
    uint32_t lastClockTxMs = 0;
    bool displayValid = false;
    int32_t displayPayload = 0;
    TurnHubProtocol::GameDisplayPacket gameDisplay{};
  };

  // One LedState packet per change; the Sigil draws the cue.
  void sendLedState(uint8_t sigilId, const SigilLedState &cue, uint32_t nowMs);
  void syncDisplay(
      uint8_t sigilId,
      HubState state,
      const Lobby &lobby,
      const GameEngine &game,
      uint8_t eliminationTargetPlayer,
      uint8_t winConfirmationPlayer);

  SigilBus &bus_;
  TurnHubProtocol::LedStyle styles_[MAX_PHYSICAL_SIGILS];
  Cache cache_[MAX_PHYSICAL_SIGILS];
  SeatFocus focus_[MAX_PHYSICAL_SIGILS];
};

}  // namespace TurnHub
