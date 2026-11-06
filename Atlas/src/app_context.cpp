// Shared hosted-game presence checks. Nothing here changes state.

#include "atlas_app.h"
#include "hosted_game.h"

namespace TurnHubAtlas {


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


}  // namespace TurnHubAtlas
