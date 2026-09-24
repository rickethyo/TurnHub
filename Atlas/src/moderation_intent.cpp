// Game Master moderation: reset a player's connections, remove them from the
// table, pass for them, or change their nudge preference. Every action is
// authorized against the moderator's persisted account permissions, and the
// private moderation counters are saved before any disconnection happens.

#include "atlas_app.h"
#include "account_access.h"
#include "profile_store.h"

namespace TurnHubAtlas {

namespace {

using TurnHub::ModerationAction;
using TurnHubAccounts::Account;

IntentResult saveNudgePreference(const String &target, Account &account, bool muted) {
  account.nudgeMuted = muted;
  return TurnHubAccounts::save(target, account)
      ? IntentResult::accept("Nudge preference saved")
      : IntentResult::reject(IntentStatus::Rejected, "Could not save account");
}

// Passes immediately for the active player, skipping the PASS grace period.
IntentResult passForPlayer(bool joined, uint8_t controller, const PlayerSeat &seat) {
  if (!joined || hubState != HubState::Running || game.activePlayerNumber() != seat.playerNumber) {
    return IntentResult::reject(IntentStatus::InvalidState, "Target is not the active player");
  }
  if (game.hasWinClaim() || eliminationTargetPlayer) {
    return IntentResult::reject(IntentStatus::Conflict, "Resolve table decisions first");
  }
  clearPendingPass("GAME_MASTER");
  if (!game.passTurn(controller, millis())) {
    return IntentResult::reject(IntentStatus::InvalidState, "Pass rejected");
  }
  const PlayerSeat *next = game.activePlayer();
  if (next) audio.turnPassed(controller, next->controllerId);
  leds.invalidateAll();
  return IntentResult::accept("Turn passed by Game Master");
}

// Removal needs a seat that can leave now: a lobby seat without a dependent
// secondary seat, or a living player while no table decision is open.
IntentResult validateRemoval(bool joined, uint8_t controller, uint8_t slot, const PlayerSeat &seat) {
  if (!joined) return IntentResult::reject(IntentStatus::InvalidState, "Target is not in this game");
  if (hubState == HubState::Lobby) {
    if (slot == 1 && lobby.hasSecondary(controller)) {
      return IntentResult::reject(IntentStatus::Conflict, "Remove the secondary seat first");
    }
  } else if ((hubState != HubState::Running && hubState != HubState::Paused) ||
      game.hasWinClaim() || eliminationTargetPlayer || game.isEliminated(seat.playerNumber)) {
    return IntentResult::reject(IntentStatus::InvalidState,
        "Resolve table decisions first, or target already removed");
  }
  return IntentResult::accept();
}

// Leaves the lobby, or concedes an active game, through the normal handlers.
IntentResult removeFromTable(const String &target, uint8_t controller, uint8_t slot,
    const PlayerSeat &seat) {
  Intent removal;
  removal.type = hubState == HubState::Lobby ? IntentType::LeaveProfile : IntentType::Concede;
  removal.actor.origin = IntentOrigin::Browser;
  removal.actor.controllerId = controller;
  removal.actor.slot = slot;
  removal.actor.playerNumber = seat.playerNumber;
  strncpy(removal.payload.profileId, target.c_str(), sizeof(removal.payload.profileId) - 1);
  return intents.dispatch(removal);
}

}  // namespace

IntentResult handleModerateIntent(const Intent &intent, void *) {
  using namespace TurnHubAccounts;
  const String actor(intent.payload.moderatorId);
  const String target(intent.payload.profileId);
  Account moderator, account;
  if (intent.actor.origin != IntentOrigin::Browser || !load(actor, moderator) ||
      moderator.archived || !(moderator.permissions & GameMaster)) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Game Master permission required");
  }
  if (!load(target, account) || account.archived) {
    return IntentResult::reject(IntentStatus::InvalidActor, "Account unavailable or archived");
  }
  const int32_t code = intent.payload.value;
  if (code < static_cast<int32_t>(ModerationAction::ResetConnections) ||
      code > static_cast<int32_t>(ModerationAction::UnmuteNudges)) {
    return IntentResult::reject(IntentStatus::Unsupported, "Unknown moderation action");
  }
  const auto action = static_cast<ModerationAction>(code);
  if (action == ModerationAction::MuteNudges || action == ModerationAction::UnmuteNudges) {
    return saveNudgePreference(target, account, action == ModerationAction::MuteNudges);
  }
  const bool isReset = action == ModerationAction::ResetConnections;
  const bool isRemoval = action == ModerationAction::RemovePlayer;
  if ((isReset && !(moderator.permissions & ResetConnections)) ||
      (isRemoval && !(moderator.permissions & RemovePlayer))) {
    return IntentResult::reject(IntentStatus::Unauthorized, "This moderation permission is disabled");
  }

  uint8_t controller = INVALID_ID, slot = 1;
  PlayerSeat seat;
  const bool joined = resolveProfileParticipant(target, controller, slot) &&
      seatForModuleSlot(controller, slot, seat);
  if (action == ModerationAction::PassTurn) return passForPlayer(joined, controller, seat);

  // Reset and removal force a new sign-in, which only a PIN can authorize.
  if (!TurnHubProfiles::hasPinForProfile(target)) {
    return IntentResult::reject(IntentStatus::Conflict,
        "Target needs a PIN for secure reconnection; ask them to set one first");
  }
  if (isReset && account.reconnectRequired) {
    return IntentResult::reject(IntentStatus::Conflict, "Connections already reset");
  }
  if (isRemoval) {
    const IntentResult removable = validateRemoval(joined, controller, slot, seat);
    if (!removable.accepted()) return removable;
  }

  const Account previous = account;
  uint32_t &counter = isReset ? account.connectionResets : account.gameRemovals;
  if (counter == UINT32_MAX) return IntentResult::reject(IntentStatus::Rejected, "Counter full");
  ++counter;
  account.reconnectRequired = true;
  if (!save(target, account)) {
    return IntentResult::reject(IntentStatus::Rejected,
        "Could not persist moderation; no disconnection performed");
  }
  if (isRemoval) {
    const IntentResult removed = removeFromTable(target, controller, slot, seat);
    if (!removed.accepted()) {
      save(target, previous);  // Roll back the counter and reconnect flag.
      return removed;
    }
  }
  TurnHubWebApi::revokeConnections(target);
  if (joined) {
    lobby.setHeld(controller, false);
    clearPendingPass("CONNECTION_RESET");
  }
  return IntentResult::accept(isReset ? "Connections reset; sign in again to reconnect" : "Removed from game");
}

}  // namespace TurnHubAtlas
