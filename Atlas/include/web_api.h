#pragma once

#include <Arduino.h>
#include <WebServer.h>

namespace TurnHubWebApi {

// Register device-management, browser-session, and authenticated web-control
// endpoints on the Atlas WebServer.
void begin(WebServer &server);

// Called only for real physical Sigil button activity. A pending browser claim
// is approved when the user proves possession by pressing Action on that Sigil.
void notePhysicalAction(uint8_t sigilId);

}  // namespace TurnHubWebApi
