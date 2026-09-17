#pragma once

#include <Arduino.h>

#include "game_engine.h"
#include "lobby.h"
#include "sigil_bus.h"
#include "turnhub_types.h"

namespace TurnHub {

class LedRenderer {
 public:
  explicit LedRenderer(SigilBus &bus);

  void invalidate(uint8_t sigilId);
  void invalidateAll();

  void render(
      HubState state,
      const Lobby &lobby,
      const GameEngine &game,
      uint32_t countdownStartedAtMs,
      uint8_t eliminationTargetPlayer,
      uint8_t winConfirmationPlayer,
      uint32_t nowMs);

 private:
  struct Cache {
    bool valid = false;
    uint8_t blue = 0;
    bool red = false;
    bool green = false;
    bool displayValid = false;
    int32_t displayPayload = 0;
  };

  void set(uint8_t sigilId, uint8_t blue, bool red, bool green);
  void off(uint8_t sigilId);
  void syncDisplay(
      uint8_t sigilId,
      HubState state,
      const Lobby &lobby,
      const GameEngine &game,
      uint8_t eliminationTargetPlayer,
      uint8_t winConfirmationPlayer);

  void renderUnjoined(uint8_t sigilId, uint32_t nowMs);
  void renderLobby(uint8_t sigilId, const Lobby &lobby, uint32_t nowMs);
  void renderStarting(
      uint8_t sigilId,
      const Lobby &lobby,
      uint32_t countdownStartedAtMs,
      uint32_t nowMs);
  void renderRunning(uint8_t sigilId, const GameEngine &game, uint32_t nowMs);
  void renderPaused(
      uint8_t sigilId,
      const GameEngine &game,
      uint8_t eliminationTargetPlayer,
      uint8_t winConfirmationPlayer,
      uint32_t nowMs);
  void renderGameOver(
      uint8_t sigilId,
      const Lobby &lobby,
      const GameEngine &game,
      uint32_t nowMs);

  static uint8_t breatheValue(uint32_t nowMs);
  static bool playerNumberRedOn(uint8_t playerNumber, uint32_t nowMs);
  static bool seatPulse(uint8_t slot, bool shared, uint32_t nowMs);
  static uint8_t starterBlueValue(
      const Lobby &lobby,
      uint8_t sigilId,
      uint32_t nowMs);

  SigilBus &bus_;
  Cache cache_[MAX_PHYSICAL_SIGILS];
};

}  // namespace TurnHub
