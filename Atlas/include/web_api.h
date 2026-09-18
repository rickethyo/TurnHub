#pragma once

#include <Arduino.h>
#include <WebServer.h>

namespace TurnHubWebApi {

enum class WebControl : uint8_t {
  Pass,
  PauseResume,
  Concede,
  ClaimWin,
  ConfirmWin,
  DenyWin,
  SelectStarter,
};

struct SeatSnapshot {
  bool exists = false;
  uint8_t playerNumber = 0;
  bool active = false;
  bool eliminated = false;
};

using ResolveSeatCallback = bool (*)(
    uint8_t moduleId,
    uint8_t slot,
    SeatSnapshot &snapshot);

using ControlCallback = bool (*)(
    uint8_t moduleId,
    uint8_t slot,
    WebControl control,
    String &message);

// Connect the web layer to the authoritative Atlas lobby/game state.
void configure(
    ResolveSeatCallback resolveSeatCallback,
    ControlCallback controlCallback);

// Register device-management, browser-session, profile, and authenticated
// web-control endpoints on the Atlas WebServer.
void begin(WebServer &server);

// Called only for real physical Sigil button activity. A pending browser claim
// is approved when the user proves possession by pressing Action on that Sigil.
void notePhysicalAction(uint8_t sigilId);

}  // namespace TurnHubWebApi
