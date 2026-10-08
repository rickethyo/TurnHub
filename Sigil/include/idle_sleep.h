#pragma once

#include <stdint.h>

#include "protocol.h"

// Sigil auto sleep (owner 2026-10-08: keep the cells from running flat). The
// same deep sleep as the device menu's Sleep, chosen by itself after a while
// with no key, joystick or Pair press. Never mid-game: a player can sit out
// several turns without touching their Sigil. Pure logic, host-tested.

namespace TurnHubSigil {

// Atlas answers but the Sigil is in no game (Ready, the lobby, game over).
constexpr uint32_t IDLE_SLEEP_MS = 10UL * 60UL * 1000UL;
// Atlas is lost (asleep, off or out of range) or the Sigil isn't paired.
constexpr uint32_t IDLE_SLEEP_NO_ATLAS_MS = 3UL * 60UL * 1000UL;

struct IdleSleepInputs {
  uint32_t nowMs = 0;
  uint32_t lastInputMs = 0;
  bool atlasAbsent = false;  // Unpaired, or Atlas lost.
  TurnHubProtocol::DisplayMode mode = TurnHubProtocol::DisplayMode::Ready;
  bool busy = false;  // Pairing, a firmware update, or already going to sleep.
};

inline bool idleSleepDue(const IdleSleepInputs &in) {
  if (in.busy) return false;
  const uint32_t idle = in.nowMs - in.lastInputMs;
  if (in.atlasAbsent) return idle >= IDLE_SLEEP_NO_ATLAS_MS;
  using TurnHubProtocol::DisplayMode;
  if (in.mode == DisplayMode::Starting || in.mode == DisplayMode::Running || in.mode == DisplayMode::Paused) {
    return false;
  }
  return idle >= IDLE_SLEEP_MS;
}

}  // namespace TurnHubSigil
