#pragma once

#include <Arduino.h>

#include "led_cues.h"
#include "sigil_bus.h"
#include "turnhub_types.h"
#include "turnhub_core.h"

namespace TurnHub {

// Draws the hub phone's game (hosted_game.h) on the Sigils: the roster,
// each Sigil's own players, and its light cue. Atlas decides nothing here.
class LedRenderer {
 public:
  explicit LedRenderer(SigilBus &bus);

  void invalidate(uint8_t sigilId);
  void invalidateAll();

  // [game] on the Sigils bound to its players; an empty game (no hub) shows
  // every Sigil unassigned.
  void renderHosted(const turnhub::Game &game, uint32_t nowMs, uint16_t sigilMask = 0xFFFF);

 private:
  struct Cache {
    bool ledStateValid = false;
    uint32_t ledStateKey = 0;
    bool clockValid = false;  // TableClock reached the Sigil; resent every interval.
    uint32_t lastClockTxMs = 0;
    bool displayValid = false;
    int32_t displayPayload = 0;
    TurnHubProtocol::GameDisplayPacket gameDisplay{};
    uint32_t tableRevision = 0;
    uint32_t lastTableTxMs = 0;
  };

  // One LedState packet per change; the Sigil draws the cue.
  void sendLedState(uint8_t sigilId, const SigilLedState &cue, uint32_t nowMs);

  SigilBus &bus_;
  Cache cache_[MAX_PHYSICAL_SIGILS];
  struct TableCache {
    bool valid = false;
    uint32_t builtAt = 0;
    TurnHubProtocol::TableDisplayPacket pages[2] = {};
  } tableCache_;
};

}  // namespace TurnHub
