// Shared application helpers: Intent origin names, the System Intent builder
// and the between-games checks every module asks. Nothing here changes state.

#include "atlas_app.h"
#include "hosted_game.h"

namespace TurnHubAtlas {

const char *intentOriginName(IntentOrigin origin) {
  switch (origin) {
    case IntentOrigin::PhysicalSigil: return "SIGIL";
    case IntentOrigin::Browser: return "BROWSER";
    case IntentOrigin::AtlasHardware: return "ATLAS";
    case IntentOrigin::System: return "SYSTEM";
    case IntentOrigin::Unknown:
    default:
      return "UNKNOWN";
  }
}

bool gameUnderway(uint32_t nowMs) {
  const TurnHubHosted::HostedTable &hosted = TurnHubHosted::hostedTable();
  if (!hosted.live(nowMs)) return false;
  const turnhub::TableState state = hosted.game().state;
  return state == turnhub::TableState::Starting || state == turnhub::TableState::Running ||
      state == turnhub::TableState::Paused;
}

bool betweenGames(uint32_t nowMs) { return !gameUnderway(nowMs); }

bool sigilInGame(uint8_t sigilId, uint32_t nowMs) {
  const TurnHubHosted::HostedTable &hosted = TurnHubHosted::hostedTable();
  int seats[2];
  return hosted.live(nowMs) && TurnHubHosted::sigilPlayers(hosted.game(), sigilId, seats) > 0;
}

IntentResult dispatchSystemIntent(IntentType type) {
  Intent intent;
  intent.type = type;
  intent.actor.origin = IntentOrigin::System;
  return intents.dispatch(intent);
}

}  // namespace TurnHubAtlas
