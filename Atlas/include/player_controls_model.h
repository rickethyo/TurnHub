#pragma once
#include "atlas_app.h"
#include "player_controls.h"
namespace TurnHubAtlas {
inline TurnHubUi::PlayerControls playerControlsFor(const GameEngine &g, HubState state, uint8_t player, bool decision) {
  TurnHubUi::PlayerControls out{};
  const auto *c=g.countersForPlayer(player);
  if (!c) return out;
  out.values[0]=c->poison; out.values[1]=c->energy; out.values[2]=c->experience;
  out.values[3]=c->treasure; out.values[4]=c->rad;
  out.values[5]=c->commanderCasts[0]; out.values[6]=c->commanderCasts[1];
  out.values[7]=c->cityBlessing; out.values[8]=g.monarchPlayerNumber()==player;
  out.values[9]=g.initiativePlayerNumber()==player; out.values[10]=g.hasPartner(player);
  if ((state!=HubState::Running && state!=HubState::Paused) || g.hasWinClaim() || decision || g.isEliminated(player) || !magicRules(g.settings().profile)) return out;
  out.allowed=0x39F; // Five counters, blessing, monarch and initiative.
  if (commanderRules(g.settings().profile)) {
    out.allowed |= (1u<<5) | (1u<<10);
    if (g.hasPartner(player)) out.allowed |= 1u<<6;
  }
  return out;
}
inline TurnHubUi::PlayerControls playerControlsFor(uint8_t player) {
  return playerControlsFor(table().game,table().hubState,player,table().eliminationTargetPlayer!=0);
}
// Every touchscreen uses these same semantic Intent mappings and validators.
inline Intent playerControlIntent(const PlayerSeat &seat, IntentOrigin origin, uint8_t control, int8_t value) {
  Intent in;
  in.actor.origin=origin; in.actor.controllerId=seat.controllerId;
  in.actor.slot=seat.slot; in.actor.playerNumber=seat.playerNumber;
  in.payload.targetPlayer=seat.playerNumber; in.payload.value=value;
  in.type=IntentType::ChangeCounter;
  static const TurnHub::CounterKind kinds[]={TurnHub::CounterKind::Poison,TurnHub::CounterKind::Energy,
      TurnHub::CounterKind::Experience,TurnHub::CounterKind::Treasure,TurnHub::CounterKind::Rad,
      TurnHub::CounterKind::CommanderCast,TurnHub::CounterKind::CommanderCast,TurnHub::CounterKind::CityBlessing,
      TurnHub::CounterKind::Poison,TurnHub::CounterKind::Initiative,TurnHub::CounterKind::Poison};
  if (control>=TurnHubUi::PLAYER_CONTROL_COUNT) return in;
  in.payload.flags=static_cast<uint8_t>(kinds[control]); in.payload.counterSlot=control==6 ? 2 : 1;
  if (control==8) in.type=IntentType::ClaimMonarch;
  if (control==10) { in.type=IntentType::SetPartner; in.payload.flags=value; }
  return in;
}
}
