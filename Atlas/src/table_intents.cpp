// Table lifecycle Intent handlers: profile participation, seat membership,
// starter selection, the start countdown, rematch/reset, elimination
// selection, pairing and next-game settings. Also owns the lifecycle
// transitions (empty lobby, rematch lobby, game start, game over).

#include "atlas_app.h"
#include "atlas_display.h"
#include "sigil_update_service.h"
#include "account_access.h"
#include "controller_profiles.h"
#include "game_settings_store.h"
#include "pairing_settings.h"
#include "speaker_settings.h"
#include "wifi_password_store.h"
#include "profile_store.h"
#include "sd_card.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

namespace {

bool validSlot(uint8_t slot) { return slot == 1 || slot == 2; }

// Common precondition for controller-issued table intents. Browser
// controllers own only slot 1 and may never impersonate a physical Sigil.
// A nonzero playerNumber must still name the actor's current seat after any
// lobby renumbering.
bool validTableActor(const Intent &intent, IntentResult &rejection) {
  const uint8_t module = intent.actor.controllerId;
  const uint8_t slot = intent.actor.slot;
  if (module >= MAX_CONTROLLERS || !validSlot(slot) ||
      (module >= MAX_PHYSICAL_SIGILS &&
          (slot != 1 || intent.actor.origin == IntentOrigin::PhysicalSigil))) {
    rejection = IntentResult::reject(IntentStatus::InvalidActor, "Invalid module or seat");
    return false;
  }
  if (intent.actor.playerNumber != 0) {
    PlayerSeat seat;
    if (!seatForModuleSlot(module, slot, seat) || seat.playerNumber != intent.actor.playerNumber) {
      rejection = IntentResult::reject(IntentStatus::InvalidActor, "This seat is no longer at the table");
      return false;
    }
  }
  return true;
}

// No table host (owner decision 2026-09-25): any controller with a seat at the
// table may start, pick the starter, change next-game settings, rematch or
// reset. Start keeps its countdown, which any seated player can cancel.
bool seatedAtTable(uint8_t module) {
  return lobby.isJoined(module) || game.controllerInGame(module);
}

void resetCountdown() {
  countdownStartedAtMs = 0;
  lastCountdownSecond = -1;
}

void clearPhysicalSeatProfiles() {
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) TurnHubControllers::releasePhysical(id, 1);
}

void printPlayer(const PlayerSeat &player) {
  serialLog.print("Player ");
  serialLog.print(player.playerNumber);
  serialLog.print(" / Sigil ");
  serialLog.print(player.controllerId);
  serialLog.print(player.slotName());
}

}  // namespace

// --- Lifecycle transitions ---------------------------------------------------

void clearDecisionState() {
  eliminationTargetPlayer = 0;
  pendingPass = PendingPassState{};
}

void enterEmptyLobby(const Intent *cause) {
  const HubState previous = hubState;
  clearPhysicalSeatProfiles();
  hubState = HubState::Lobby;
  lobby.resetEmpty();
  game.reset();
  for (uint8_t id = MAX_PHYSICAL_SIGILS; id < MAX_CONTROLLERS; ++id) {
    TurnHubControllers::releaseBrowser(id);
  }
  resetCountdown();
  clearDecisionState();
  audio.clear();
  leds.invalidateAll();
  serialLog.print("ATLAS|LOBBY|EMPTY|RESET|ORIGIN|");
  serialLog.print(intentOriginName(cause ? cause->actor.origin : IntentOrigin::System));
  if (cause) {
    serialLog.print("|CONTROLLER|");
    serialLog.print(cause->actor.controllerId);
  }
  serialLog.print("|FROM|");
  serialLog.println(stateName(previous));
}

namespace {

void enterRematchLobby() {
  // The completed match retains identities on Atlas until the rematch decision.
  for (uint8_t i = 0; i < game.playerCount(); ++i) {
    const auto *seat = game.playerAt(i);
    if (seat && seat->controllerId < MAX_PHYSICAL_SIGILS && seat->profileId[0]) {
      TurnHubControllers::bindPhysical(seat->controllerId, seat->slot, String(seat->profileId));
    }
  }
  hubState = HubState::Lobby;
  lobby.resetForRematch();
  game.reset();
  resetCountdown();
  clearDecisionState();
  audio.clear();
  leds.invalidateAll();
  serialLog.println("ATLAS|LOBBY|REMATCH");
}

void beginCountdown() {
  if (lobby.playerCount() < 2 || !gameSettingsAvailable) return;
  hubState = HubState::Starting;
  countdownStartedAtMs = millis();
  lastCountdownSecond = -1;
  lobby.clearStartArm();
  serialLog.println("ATLAS|LOBBY|COUNTDOWN|START");
}

void cancelCountdown() {
  if (hubState != HubState::Starting) return;
  const uint16_t targets = lobbyAudioMask();
  hubState = HubState::Lobby;
  resetCountdown();
  lobby.clearStartArm();
  audio.clear();
  audio.countdownCancelled(targets);
  serialLog.println("ATLAS|LOBBY|COUNTDOWN|CANCEL");
}

void startGame() {
  PlayerSeat starter;
  if (!lobby.starterOrDefault(starter) || lobby.playerCount() < 2) {
    cancelCountdown();
    return;
  }

  PlayerSeat players[MAX_PLAYERS];
  const uint8_t count = lobby.buildPlayers(players, MAX_PLAYERS);
  // Capture each seat's profile now so statistics follow the person even if
  // controller assignments change during the match.
  for (uint8_t i = 0; i < count; ++i) {
    const String profile = TurnHubControllers::profileForSeat(players[i].controllerId, players[i].slot);
    strncpy(players[i].profileId, profile.c_str(), sizeof(players[i].profileId) - 1);
  }

  if (!game.start(players, count, starter, millis(), nextGameSettings)) {
    cancelCountdown();
    return;
  }

  hubState = HubState::Running;
  resetCountdown();
  clearDecisionState();
  leds.invalidateAll();
  audio.gameStart(gameAudioMask());

  serialLog.print("ATLAS|GAME|START|");
  printPlayer(starter);
  const TurnHub::GameSettings &settings = game.settings();
  serialLog.print("|PROFILE|");
  serialLog.print(TurnHub::gameProfileKey(settings.profile));
  serialLog.print("|LIFE|");
  serialLog.print(settings.startingLife);
  serialLog.print("|TIMER_MS|");
  serialLog.print(settings.turnTimerMs);
  if (settings.twoHeadedGiant) serialLog.print("|TEAMS|2HG");
  serialLog.print("|PLAYERS|");
  serialLog.println(count);
}

}  // namespace

void finishGameState() {
  // GameEngine has already persisted results using its captured profile IDs.
  // Seat names stay bound through GameOver so the Sigils can show who won
  // (test feedback, 2026-09-28); leaving GameOver releases them: Reset via
  // enterEmptyLobby(), Rematch by rebinding the match's profiles.
  clearPendingPass("GAME_OVER");
  hubState = HubState::GameOver;
  eliminationTargetPlayer = 0;
  leds.invalidateAll();
  audio.gameOver(gameAudioMask());

  if (game.endedInDraw()) {
    serialLog.println("ATLAS|GAME|OVER|DRAW");
  } else {
    serialLog.print("ATLAS|GAME|OVER|WINNER|");
    serialLog.println(game.winnerPlayerNumber());
  }
}

void updateCountdown(uint32_t nowMs) {
  if (hubState != HubState::Starting) return;

  constexpr int8_t COUNTDOWN_SECONDS = static_cast<int8_t>(START_COUNTDOWN_MS / 1000);
  // nowMs is sampled before the loop's handlers run, so a countdown begun
  // later in the same pass reads as slightly in the future: treat it as 0.
  uint32_t elapsed = nowMs - countdownStartedAtMs;
  if (elapsed > 0x7FFFFFFFUL) elapsed = 0;
  const int8_t second = static_cast<int8_t>(elapsed / 1000);
  if (second < COUNTDOWN_SECONDS && second != lastCountdownSecond) {
    lastCountdownSecond = second;
    serialLog.print("ATLAS|LOBBY|COUNTDOWN|");
    serialLog.println(COUNTDOWN_SECONDS - second);
    audio.countdownTone(lobbyAudioMask(), static_cast<uint8_t>(second));
  }
  if (elapsed >= START_COUNTDOWN_MS) dispatchSystemIntent(IntentType::CompleteStart);
}

// --- Profile participation (JoinProfile / LeaveProfile / BindProfile / PickProfile) ----------

namespace {

// Every applied participation change disarms a pending start.
IntentResult participationChanged(IntentType type) {
  lobby.clearStartArm();
  leds.invalidateAll();
  return IntentResult::accept(type == IntentType::LeaveProfile ? "Left the table" : "Ready at the table");
}

IntentResult joinProfile(const Intent &intent, const String &profile, bool joined) {
  if (intent.actor.origin != IntentOrigin::Browser) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Profile login required");
  }
  if (joined) {
    return IntentResult::accept("Already at the table; this browser controls your existing player");
  }
  const uint8_t controller = TurnHubControllers::registerBrowser(profile);
  const uint8_t player = controller == INVALID_ID ? 0 : lobby.join(controller);
  if (player == 0) {
    TurnHubControllers::releaseBrowser(controller);
    return IntentResult::reject(IntentStatus::Conflict, "Table is full");
  }
  serialLog.print("ATLAS|LOBBY|JOIN|BROWSER|");
  serialLog.print(controller);
  serialLog.print("|PLAYER|");
  serialLog.print(player);
  serialLog.print("|PROFILE|");
  serialLog.println(profile);
  return participationChanged(intent.type);
}

IntentResult leaveProfile(const Intent &intent, const String &profile, bool joined,
    uint8_t existing, uint8_t existingSlot) {
  if (intent.actor.origin != IntentOrigin::Browser || !joined) {
    return IntentResult::reject(IntentStatus::InvalidActor, "No participant to leave");
  }
  if (existingSlot == 2) {
    bool added;
    PlayerSeat affected;
    lobby.toggleSecondary(existing, added, affected);
  } else {
    if (lobby.hasSecondary(existing)) {
      return IntentResult::reject(IntentStatus::Conflict, "Secondary player must leave first");
    }
    lobby.leave(existing);
  }
  // A profile seated on a Sigil (a phone leaving for it, or a Game Master
  // removal) frees that seat, as the Sigil's own Leave does, so the Sigil
  // stops showing the name and its next Join is not this profile.
  if (existing < MAX_PHYSICAL_SIGILS) {
    TurnHubControllers::releasePhysical(existing, existingSlot);
  } else {
    TurnHubControllers::releaseBrowser(existing);
  }
  serialLog.print("ATLAS|LOBBY|LEAVE|BROWSER|");
  serialLog.print(existing);
  serialLog.print("|SLOT|");
  serialLog.print(existingSlot);
  serialLog.print("|PROFILE|");
  serialLog.println(profile);
  return participationChanged(intent.type);
}

// Attaches a signed-in profile to a physical Sigil seat, moving it off any
// browser controller while keeping its table position.
IntentResult bindPhysicalProfile(const Intent &intent, const String &profile, bool joined,
    uint8_t existing, uint8_t existingSlot) {
  const uint8_t module = intent.actor.controllerId;
  const uint8_t slot = intent.actor.slot;
  if (intent.actor.origin != IntentOrigin::PhysicalSigil || module >= MAX_PHYSICAL_SIGILS ||
      !validSlot(slot)) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Physical seat confirmation required");
  }
  if (joined && existing == module && existingSlot == slot) {
    return IntentResult::accept("Controller already attached");
  }
  // The Sigil's picker may add seat B with the chosen profile in one step
  // (playtest 2026-09-29, item 8); a phone binds only to a seat B that exists.
  const bool addsSeatB = slot == 2 && intent.type == IntentType::PickProfile &&
      lobby.isJoined(module) && !lobby.hasSecondary(module);
  if (slot == 2 && !lobby.hasSecondary(module) && !addsSeatB) {
    return IntentResult::reject(IntentStatus::Conflict, "Join seat B on the Sigil first");
  }
  if (addsSeatB && joined) {
    return IntentResult::reject(IntentStatus::Conflict,
        "That profile is already at the table; leave there first");
  }
  const bool occupied = slot == 1 ? lobby.isJoined(module) : lobby.hasSecondary(module);
  // Physical confirmation can adopt a guest, but not another account.
  const String targetProfile = TurnHubControllers::profileForSeat(module, slot);
  if (targetProfile.length() && targetProfile != profile) {
    return IntentResult::reject(IntentStatus::Conflict,
        "This Sigil belongs to another profile; that player must leave first");
  }
  if (joined && existing < MAX_PHYSICAL_SIGILS && existing != module) {
    return IntentResult::reject(IntentStatus::Conflict, "Profile already has a physical Sigil");
  }
  if (!joined && !occupied && lobby.playerCount() >= MAX_PLAYERS) {
    return IntentResult::reject(IntentStatus::Conflict, "Table is full");
  }
  if (!TurnHubControllers::bindPhysical(module, slot, profile)) {
    return IntentResult::reject(IntentStatus::Rejected, "Could not save controller assignment");
  }
  if (joined && existing != module) {
    if (occupied) {
      // Keep the guest's table position and secondary seat.
      lobby.leave(existing);
    } else {
      lobby.replaceController(existing, module);
    }
    TurnHubControllers::releaseBrowser(existing);
  } else if (addsSeatB) {
    bool added = false;
    PlayerSeat affected;
    if (!lobby.toggleSecondary(module, added, affected) || !added) {
      return IntentResult::reject(IntentStatus::InvalidState, "Could not add seat B");
    }
    audio.sharedPlayerAdded(module);
  } else if (!joined && !occupied) {
    lobby.join(module);
  }
  return participationChanged(intent.type);
}

// Owner request (2026-10-07): during a game a Sigil may take over a player
// already in it (seated from a phone or the tablet), keeping that player's
// place, life and clock. Nobody new joins mid-game until venue lobbies.
IntentResult attachInGame(const Intent &intent, const String &profile, bool joined, uint8_t existing) {
  const uint8_t module = intent.actor.controllerId;
  if (intent.actor.origin != IntentOrigin::PhysicalSigil || module >= MAX_PHYSICAL_SIGILS ||
      intent.actor.slot != 1) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Physical seat confirmation required");
  }
  if (!joined || !game.controllerInGame(existing)) {
    return IntentResult::reject(IntentStatus::InvalidActor, "Only players in this game can join it now");
  }
  if (existing == module) return IntentResult::accept("Controller already attached");
  if (existing < MAX_PHYSICAL_SIGILS) {
    return IntentResult::reject(IntentStatus::Conflict, "Profile already has a physical Sigil");
  }
  if (game.controllerInGame(module) || lobby.isJoined(module)) {
    return IntentResult::reject(IntentStatus::Conflict, "This Sigil is already playing");
  }
  if (!TurnHubControllers::bindPhysical(module, 1, profile)) {
    return IntentResult::reject(IntentStatus::Rejected, "Could not save controller assignment");
  }
  lobby.replaceController(existing, module);
  game.replaceController(existing, module);
  if (pendingPass.active && pendingPass.seat.controllerId == existing) pendingPass.seat.controllerId = module;
  TurnHubControllers::releaseBrowser(existing);
  leds.invalidateAll();
  serialLog.print("ATLAS|GAME|ATTACH|SIGIL|");
  serialLog.print(module);
  serialLog.print("|FROM|");
  serialLog.print(existing);
  serialLog.print("|PROFILE|");
  serialLog.println(profile);
  return IntentResult::accept("Joined the game on this Sigil");
}

}  // namespace

IntentResult handleProfileParticipationIntent(const Intent &intent, void *) {
  const bool inGame = hubState == HubState::Running || hubState == HubState::Paused;
  const bool attaches = intent.type == IntentType::PickProfile || intent.type == IntentType::BindProfile;
  if (hubState != HubState::Lobby && !(inGame && attaches)) {
    return IntentResult::reject(IntentStatus::InvalidState, "Participation changes require the lobby");
  }
  const String profile(intent.payload.profileId);
  if (!TurnHubProfiles::profileExists(profile)) {
    return IntentResult::reject(IntentStatus::InvalidActor, "Unknown profile");
  }
  uint8_t existing = INVALID_ID, existingSlot = 1;
  const bool joined = resolveProfileParticipant(profile, existing, existingSlot);
  if (intent.type == IntentType::JoinProfile) return joinProfile(intent, profile, joined);
  if (intent.type == IntentType::LeaveProfile) {
    return leaveProfile(intent, profile, joined, existing, existingSlot);
  }
  if (intent.type == IntentType::PickProfile) {
    // Chosen on the Sigil itself: no phone proved who is holding it, so the
    // profile's "Allow physical use without a PIN" choice decides.
    // Seat B is picked from the same Sigil once seat A is at the table.
    if (!inGame && intent.actor.slot == 2 && !lobby.isJoined(intent.actor.controllerId)) {
      return IntentResult::reject(IntentStatus::Conflict, "Join seat A first");
    }
    if (!TurnHubWebApi::physicalUseAllowed(profile)) {
      return IntentResult::reject(IntentStatus::Unauthorized,
          "Sign into this profile on a phone before using a Sigil");
    }
  }
  if (inGame) return attachInGame(intent, profile, joined, existing);
  return bindPhysicalProfile(intent, profile, joined, existing, existingSlot);
}

// --- Turn order (MoveSeat) --------------------------------------------------------

// Owner decision (2026-09-29): turn order is set in the lobby only, from the
// Atlas touchscreen only, and any player may set it. Owner request
// (2026-10-07): a table tablet may set it too, as it sits at the table.
// Moving a seat moves its whole controller outside its pair; A/B can swap
// within the shared Sigil.
IntentResult handleMoveSeatIntent(const Intent &intent, void *) {
  if (intent.actor.origin != IntentOrigin::AtlasHardware && intent.actor.origin != IntentOrigin::TableTablet) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Set the turn order on the Atlas screen or a table tablet");
  }
  if (hubState != HubState::Lobby) {
    return IntentResult::reject(IntentStatus::InvalidState, "Set the turn order in the lobby");
  }
  PlayerSeat seats[MAX_PLAYERS];
  const uint8_t count = lobby.buildPlayers(seats, MAX_PLAYERS);
  const PlayerSeat *seat = nullptr;
  for (uint8_t i = 0; i < count; ++i) {
    if (seats[i].playerNumber == intent.payload.targetPlayer) seat = &seats[i];
  }
  if (seat == nullptr) return IntentResult::reject(IntentStatus::InvalidActor, "That player is not at the table");
  const int32_t direction = intent.payload.value;
  if ((direction != -1 && direction != 1) ||
      !lobby.moveSeat(seat->controllerId, seat->slot, static_cast<int8_t>(direction))) {
    return IntentResult::reject(IntentStatus::Conflict,
        direction < 0 ? "Already first in turn order" : "Already last in turn order");
  }
  leds.invalidateAll();
  serialLog.print("ATLAS|LOBBY|MOVE|CONTROLLER|");
  serialLog.print(seat->controllerId);
  serialLog.println(direction < 0 ? "|EARLIER" : "|LATER");
  return IntentResult::accept(direction < 0 ? "Moved earlier in turn order" : "Moved later in turn order");
}

IntentResult handleSetSeatSideIntent(const Intent &intent, void *) {
  if (intent.actor.origin != IntentOrigin::AtlasHardware)
    return IntentResult::reject(IntentStatus::Unauthorized, "Set seat sides on the Atlas screen");
  if (hubState != HubState::Lobby)
    return IntentResult::reject(IntentStatus::InvalidState, "Set seat sides in the lobby");
  if (intent.payload.value != 0 && intent.payload.value != 1)
    return IntentResult::reject(IntentStatus::Conflict, "Choose left or right");
  PlayerSeat seats[MAX_PLAYERS];
  const uint8_t count = lobby.buildPlayers(seats, MAX_PLAYERS);
  for (uint8_t i = 0; i < count; ++i) {
    if (seats[i].playerNumber != intent.payload.targetPlayer) continue;
    if (!lobby.setSecondaryFirst(seats[i].controllerId, intent.payload.value == 1))
      return IntentResult::reject(IntentStatus::Conflict, "This Sigil has no seat B");
    leds.invalidateAll();
    return IntentResult::accept(intent.payload.value == 1 ? "B on left: before A" : "B on right: after A");
  }
  return IntentResult::reject(IntentStatus::InvalidActor, "That player is not at the table");
}

// Owner request (Staged Changes, 2026-10-06): the Atlas screen can take one
// player out of the lobby, as Clear does for everyone. It is the player
// leaving, not a Game Master removal: no moderation record, and a phone may
// join again.
IntentResult handleRemoveSeatIntent(const Intent &intent, void *) {
  if (intent.actor.origin != IntentOrigin::AtlasHardware)
    return IntentResult::reject(IntentStatus::Unauthorized, "Remove players on the Atlas screen");
  if (hubState != HubState::Lobby)
    return IntentResult::reject(IntentStatus::InvalidState, "Remove players in the lobby");
  const uint8_t module = intent.actor.controllerId;
  const uint8_t slot = intent.actor.slot;
  if (module >= MAX_CONTROLLERS || !validSlot(slot) || lobby.playerNumber(module, slot) == 0)
    return IntentResult::reject(IntentStatus::InvalidActor, "That player is not at the table");
  if (slot == 2) {
    bool added = false;
    PlayerSeat affected;
    if (!lobby.toggleSecondary(module, added, affected) || added)
      return IntentResult::reject(IntentStatus::InvalidState, "Could not remove seat B");
  } else if (!lobby.leave(module)) {
    return IntentResult::reject(IntentStatus::InvalidActor, "That player is not at the table");
  }
  if (module < MAX_PHYSICAL_SIGILS) {
    TurnHubControllers::releasePhysical(module, slot);
    if (slot == 1) TurnHubControllers::releasePhysical(module, 2);
  } else {
    TurnHubControllers::releaseBrowser(module);
  }
  serialLog.print("ATLAS|LOBBY|REMOVE|CONTROLLER|");
  serialLog.print(module);
  serialLog.print("|SLOT|");
  serialLog.println(slot);
  lobby.clearStartArm();
  leds.invalidateAll();
  return IntentResult::accept("Removed from the table");
}

// --- Seat membership (Join / Leave) ---------------------------------------------

namespace {

IntentResult seatChanged(bool joining) {
  leds.invalidateAll();
  return IntentResult::accept(joining ? "Joined" : "Left");
}

IntentResult toggleSecondarySeat(uint8_t module, bool joining) {
  if (!lobby.isJoined(module) || lobby.hasSecondary(module) == joining) {
    return IntentResult::reject(IntentStatus::Conflict, "Secondary seat is already in the requested state");
  }
  if (lobby.startArmedBy() == module) lobby.clearStartArm();
  bool added = false;
  PlayerSeat affected;
  if (!lobby.toggleSecondary(module, added, affected)) {
    return IntentResult::reject(IntentStatus::InvalidState, "Could not change secondary seat");
  }
  if (added) {
    audio.sharedPlayerAdded(module);
  } else {
    // Like seat A leaving: seat B's profile goes with it.
    TurnHubControllers::releasePhysical(module, 2);
    audio.sharedPlayerRemoved(module);
  }
  serialLog.print("ATLAS|LOBBY|SECONDARY|");
  serialLog.print(module);
  serialLog.print(added ? "|ADDED|PLAYER|" : "|REMOVED|PLAYER|");
  serialLog.println(affected.playerNumber);
  return seatChanged(joining);
}

IntentResult joinPrimarySeat(uint8_t module) {
  if (lobby.isJoined(module)) return IntentResult::accept("Already joined");
  const uint8_t player = lobby.join(module);
  if (player == 0) return IntentResult::reject(IntentStatus::InvalidState, "Could not join");
  serialLog.print("ATLAS|LOBBY|JOIN|SIGIL|");
  serialLog.print(module);
  serialLog.print("|PLAYER|");
  serialLog.println(player);
  audio.playerJoined(module);
  return seatChanged(true);
}

}  // namespace

// Slot 1 joins/leaves the whole controller; slot 2 toggles its secondary seat.
IntentResult handleSeatMembershipIntent(const Intent &intent, void *) {
  IntentResult rejection;
  if (!validTableActor(intent, rejection)) return rejection;
  if (hubState != HubState::Lobby) {
    return IntentResult::reject(IntentStatus::InvalidState, "Seats can only change in the lobby");
  }
  const uint8_t module = intent.actor.controllerId;
  const uint8_t slot = intent.actor.slot;
  const bool joining = intent.type == IntentType::Join;

  if (joining && lobby.playerNumber(module, slot) == 0) {
    const String profile = TurnHubControllers::profileForSeat(module, slot);
    if (module < MAX_PHYSICAL_SIGILS && !TurnHubWebApi::physicalUseAllowed(profile)) {
      return IntentResult::reject(IntentStatus::Unauthorized,
          "Sign into this profile on a phone before using its Sigil");
    }
    uint8_t current = INVALID_ID, currentSlot = 1;
    if (resolveProfileParticipant(profile, current, currentSlot)) {
      return IntentResult::reject(IntentStatus::Conflict,
          "Profile is already playing; attach this Sigil from its signed-in phone");
    }
  }
  if (slot == 2) return toggleSecondarySeat(module, joining);
  if (joining) return joinPrimarySeat(module);
  if (!lobby.leave(module)) {
    return IntentResult::reject(IntentStatus::InvalidActor, "Module is not joined");
  }
  // Seat profiles are temporary: a Sigil that leaves is free for anyone,
  // so its next Join (or the picker's Guest) is not the old profile.
  TurnHubControllers::releasePhysical(module, 1);
  serialLog.print("ATLAS|LOBBY|LEAVE|SIGIL|");
  serialLog.println(module);
  return seatChanged(false);
}

// --- Starter selection ----------------------------------------------------------------

IntentResult handleSelectStarterIntent(const Intent &intent, void *) {
  IntentResult rejection;
  if (!validTableActor(intent, rejection)) return rejection;
  if (hubState != HubState::Lobby) {
    return IntentResult::reject(IntentStatus::InvalidState, "Starter can only be selected in the lobby");
  }
  const uint8_t module = intent.actor.controllerId;
  PlayerSeat selected;
  bool selectedOk = false;
  const auto selection = static_cast<TurnHub::StarterSelection>(intent.payload.value);
  switch (selection) {
    case TurnHub::StarterSelection::Random:
      if (!seatedAtTable(module) || lobby.playerCount() < 2) {
        return IntentResult::reject(IntentStatus::Unauthorized,
            "Choose a random starter from a seat, with two players");
      }
      selectedOk = lobby.randomStarter(selected);
      break;
    case TurnHub::StarterSelection::CycleModule:
      selectedOk = lobby.selectStarter(module, selected);
      break;
    case TurnHub::StarterSelection::ExactSeat:
      selectedOk = lobby.selectStarterSeat(module, intent.actor.slot, selected);
      break;
  }
  if (!selectedOk) {
    return IntentResult::reject(IntentStatus::InvalidActor, "Could not select this seat as starter");
  }
  if (selection == TurnHub::StarterSelection::Random) {
    audio.randomStarter(selected.controllerId);
  } else {
    audio.starterSelected(selected.controllerId);
  }
  leds.invalidateAll();
  serialLog.print("ATLAS|INTENT|SELECT_STARTER|PLAYER|");
  serialLog.println(selected.playerNumber);
  return IntentResult::accept("Selected as starting player");
}

// --- Start countdown -----------------------------------------------------------------------

// ArmStart (physical long press) arms; StartGame begins the countdown. A
// browser seat may start without arming. The Atlas touchscreen (Start in the
// lobby) is at the table itself, so it starts without a seat or arming.
IntentResult handleStartIntent(const Intent &intent, void *) {
  // A Sigil update, or an Atlas upload that restarts Atlas when it lands.
  if (sigilUpdatesBusy() || ota.inProgress()) return IntentResult::reject(IntentStatus::Conflict, "Wait for the firmware update to finish");
  const bool touchscreen = intent.actor.origin == IntentOrigin::AtlasHardware &&
      intent.type == IntentType::StartGame;
  IntentResult rejection;
  if (!touchscreen && !validTableActor(intent, rejection)) return rejection;
  const uint8_t module = intent.actor.controllerId;
  if (!gameSettingsAvailable) {
    return IntentResult::reject(IntentStatus::InvalidState,
        "Game settings storage is unavailable; restart Atlas after resolving the storage problem");
  }
  if (hubState != HubState::Lobby || (!touchscreen && !lobby.isJoined(module)) ||
      lobby.playerCount() < 2) {
    return IntentResult::reject(IntentStatus::InvalidState, "Start from a seat, with two players in the lobby");
  }
  if (!TurnHub::validTeamTable(nextGameSettings, lobby.playerCount())) {
    return IntentResult::reject(IntentStatus::InvalidState,
        "Two-Headed Giant needs an even number of players, at least 4");
  }
  if (intent.type == IntentType::ArmStart) {
    lobby.setStartArmedBy(module);
    audio.startArmed(module);
    serialLog.print("ATLAS|LOBBY|START_ARM|");
    serialLog.println(module);
  } else {
    if (!touchscreen && intent.actor.origin != IntentOrigin::Browser && lobby.startArmedBy() != module) {
      return IntentResult::reject(IntentStatus::InvalidState, "Start is not armed");
    }
    beginCountdown();
  }
  return IntentResult::accept();
}

// System-only: the countdown finished (see updateCountdown).
IntentResult handleCompleteStartIntent(const Intent &intent, void *) {
  if (sigilUpdatesBusy() || ota.inProgress()) return IntentResult::reject(IntentStatus::Conflict, "Wait for the firmware update to finish");
  if (intent.actor.origin != IntentOrigin::System || hubState != HubState::Starting ||
      millis() - countdownStartedAtMs < START_COUNTDOWN_MS) {
    return IntentResult::reject(IntentStatus::InvalidState, "Countdown is not complete");
  }
  startGame();
  return hubState == HubState::Running
      ? IntentResult::accept("Game started")
      : IntentResult::reject(IntentStatus::InvalidState, "Could not start game");
}

IntentResult handleCancelStartIntent(const Intent &intent, void *) {
  IntentResult rejection;
  if (intent.actor.origin != IntentOrigin::AtlasHardware && !validTableActor(intent, rejection)) {
    return rejection;
  }
  if (hubState != HubState::Starting) {
    return IntentResult::reject(IntentStatus::InvalidState, "No countdown");
  }
  // Any discovered module, or the Atlas touchscreen, can cancel the countdown.
  cancelCountdown();
  return IntentResult::accept();
}

// --- Rematch / reset ----------------------------------------------------------------------------

// Rematch keeps the finished match's participants; ResetGame empties the
// table. Reset is reachable only from GameOver or an unstarted lobby, so it
// can never discard an in-progress (including recovered) match. The Atlas
// touchscreen offers both after a game (Rematch and Reset), and a Clear hold
// in the lobby (ResetGame) that sends every player back out.
IntentResult handleResetIntent(const Intent &intent, void *) {
  if (intent.actor.origin == IntentOrigin::AtlasHardware) {
    if (intent.type == IntentType::ResetGame && hubState == HubState::Lobby) {
      serialLog.print("ATLAS|LOBBY|CLEAR|PLAYERS|");
      serialLog.println(lobby.playerCount());
      enterEmptyLobby(&intent);
      return IntentResult::accept("Lobby cleared");
    }
    if (hubState != HubState::GameOver) {
      return IntentResult::reject(IntentStatus::InvalidState, "Game is not over");
    }
    if (intent.type == IntentType::Rematch) {
      enterRematchLobby();
      return IntentResult::accept("Rematch: same players, back to the lobby");
    }
    enterEmptyLobby(&intent);
    return IntentResult::accept("Table reset to an empty lobby");
  }
  IntentResult rejection;
  if (!validTableActor(intent, rejection)) return rejection;
  const uint8_t module = intent.actor.controllerId;
  if (!seatedAtTable(module)) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Only a player at the table can reset");
  }
  if (intent.type == IntentType::Rematch) {
    if (hubState != HubState::GameOver) {
      return IntentResult::reject(IntentStatus::InvalidState, "Game is not over");
    }
    enterRematchLobby();
    return IntentResult::accept();
  }
  const bool lobbyReset = hubState == HubState::Lobby &&
      (lobby.startArmedBy() == module || intent.actor.origin == IntentOrigin::Browser);
  if (hubState != HubState::GameOver && !lobbyReset) {
    return IntentResult::reject(IntentStatus::InvalidState, "Reset is not available");
  }
  enterEmptyLobby(&intent);
  return IntentResult::accept();
}

IntentResult handleCancelPassIntent(const Intent &intent, void *) {
  IntentResult rejection;
  if (!validTableActor(intent, rejection)) return rejection;
  if (hubState != HubState::Running ||
      !cancelPendingPassForModule(intent.actor.controllerId, "ACTION")) {
    return IntentResult::reject(IntentStatus::InvalidState, "No pending pass for this controller");
  }
  audio.passUndone(gameAudioMask());
  return IntentResult::accept("Pass canceled");
}

// --- Elimination selection ------------------------------------------------------------------------

namespace {

void beginEliminationSelection(uint8_t sigilId) {
  if (hubState != HubState::Paused || game.hasWinClaim()) return;

  PlayerSeat candidates[2];
  if (game.livingPlayersForController(sigilId, candidates, 2) == 0) return;

  eliminationTargetPlayer = candidates[0].playerNumber;
  game.cancelLifeChanges();
  audio.eliminationArmed(sigilId);
  leds.invalidateAll();

  serialLog.print("ATLAS|GAME|ELIMINATION|ARMED|PLAYER|");
  serialLog.println(eliminationTargetPlayer);
}

// Steps the selection through the controller's living seats (A <-> B).
void cycleEliminationTarget(uint8_t sigilId) {
  PlayerSeat candidates[2];
  const uint8_t count = game.livingPlayersForController(sigilId, candidates, 2);
  if (count == 0) {
    eliminationTargetPlayer = 0;
    return;
  }

  uint8_t nextIndex = 0;
  for (uint8_t i = 0; i < count; ++i) {
    if (candidates[i].playerNumber == eliminationTargetPlayer) {
      nextIndex = static_cast<uint8_t>((i + 1) % count);
      break;
    }
  }

  eliminationTargetPlayer = candidates[nextIndex].playerNumber;
  audio.eliminationTargetChanged(sigilId);
  leds.invalidateAll();

  serialLog.print("ATLAS|GAME|ELIMINATION|TARGET|");
  serialLog.println(eliminationTargetPlayer);
}

void cancelEliminationSelection(const PlayerSeat &target) {
  audio.eliminationCancelled(target.controllerId);
  eliminationTargetPlayer = 0;
  leds.invalidateAll();
  serialLog.println("ATLAS|GAME|ELIMINATION|CANCEL");
}

// Unlike a concession, a surviving elimination leaves the table paused.
void confirmElimination(const PlayerSeat &target) {
  const uint8_t eliminatedNumber = target.playerNumber;
  const uint8_t sigilId = target.controllerId;
  bool gameFinished = false;
  if (!game.eliminatePlayer(eliminatedNumber, millis(), gameFinished)) return;

  eliminationTargetPlayer = 0;
  audio.playerEliminated(sigilId);
  leds.invalidateAll();

  serialLog.print("ATLAS|GAME|ELIMINATED|PLAYER|");
  serialLog.println(eliminatedNumber);

  if (gameFinished) {
    finishGameState();
  } else {
    hubState = HubState::Paused;
  }
}

}  // namespace

// Begin/Cycle/Cancel/Eliminate act on Atlas's selected target while paused.
IntentResult handleEliminationIntent(const Intent &intent, void *) {
  IntentResult rejection;
  if (!validTableActor(intent, rejection)) return rejection;
  if (hubState != HubState::Paused || game.hasWinClaim()) {
    return IntentResult::reject(IntentStatus::InvalidState, "Elimination requires a pause without a win claim");
  }
  const uint8_t module = intent.actor.controllerId;
  if (intent.type == IntentType::BeginElimination) {
    PlayerSeat actor;
    if (!firstLivingSeatForModule(module, actor)) {
      return IntentResult::reject(IntentStatus::InvalidActor, "No living seat");
    }
    beginEliminationSelection(module);
    return IntentResult::accept();
  }

  const PlayerSeat *target = game.playerByNumber(eliminationTargetPlayer);
  if (target == nullptr) {
    return IntentResult::reject(IntentStatus::InvalidState, "No elimination is selected");
  }
  if (intent.type == IntentType::CancelElimination) {
    // Preserve the existing table-wide cancellation gesture.
    cancelEliminationSelection(*target);
    return IntentResult::accept();
  }
  if (target->controllerId != module) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Another controller owns this selection");
  }
  if (intent.type == IntentType::CycleElimination) {
    cycleEliminationTarget(module);
  } else {
    confirmElimination(*target);
  }
  return IntentResult::accept();
}

// --- Pairing and next-game settings ------------------------------------------------------------

// Pairing is deliberately authorized by the Atlas hardware Intent.
IntentResult handlePairRequestIntent(const Intent &intent, void *) {
  if (intent.actor.origin != IntentOrigin::AtlasHardware) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Use Menu, then Pair a Sigil, on the Atlas screen");
  }
  if (hubState != HubState::Lobby) {
    return IntentResult::reject(IntentStatus::InvalidState, "Pair devices in the lobby");
  }
  if (!sigilBus.openPairing(pairingWindowMs)) {
    return IntentResult::reject(IntentStatus::Rejected, "Radio unavailable");
  }
  startPairingIndicator(millis());
  serialLog.print("ATLAS|PAIRING|ENTER|DURATION_MS|");
  serialLog.println(pairingWindowMs);
  return IntentResult::accept("Pairing window opened");
}

namespace {

// Device management comes from the web layer with the signed-in account in
// payload.moderatorId; Atlas re-checks that account's Admin permission.
bool adminIntent(const Intent &intent) {
  TurnHubAccounts::Account admin;
  return intent.actor.origin == IntentOrigin::Browser &&
      TurnHubAccounts::load(String(intent.payload.moderatorId), admin) &&
      !admin.archived && (admin.permissions & TurnHubAccounts::Admin);
}

// Forgets one paired Sigil that nobody is seated on.
bool forgetSigil(uint8_t sigilId) {
  const auto *record = sigilBus.record(sigilId);
  if (record == nullptr) return false;
  uint8_t mac[6];
  memcpy(mac, record->mac, sizeof(mac));
  if (!sigilBus.forget(sigilId)) return false;
  // Saved seat bindings are released like after a game; the name is kept so
  // re-pairing the same Sigil restores it.
  TurnHubProfiles::resetTransientSeatBindings(mac);
  return true;
}

}  // namespace

// Payload: value = Sigil ID or FORGET_ALL_SIGILS. Lobby only, and never a
// Sigil with seated players, so no participant loses their controller.
IntentResult handleForgetPairingIntent(const Intent &intent, void *) {
  if (sigilUpdatesBusy()) return IntentResult::reject(IntentStatus::Conflict, "Wait for the firmware update to finish");
  // The BOOT button held on Atlas itself (AtlasHardware) is physical presence.
  if (!adminIntent(intent) && intent.actor.origin != IntentOrigin::AtlasHardware) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Admin permission required");
  }
  if (hubState != HubState::Lobby) {
    return IntentResult::reject(IntentStatus::InvalidState, "Forget devices in the lobby, between games");
  }
  const int32_t target = intent.payload.value;
  const bool all = target == TurnHub::FORGET_ALL_SIGILS;
  if (!all && (target < 0 || target >= MAX_PHYSICAL_SIGILS || !sigilBus.record(target))) {
    return IntentResult::reject(IntentStatus::InvalidActor, "That Sigil is not paired with Atlas");
  }
  uint8_t paired = 0;
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if ((!all && id != target) || !sigilBus.record(id)) continue;
    ++paired;
    if (lobby.isJoined(id)) {
      return IntentResult::reject(IntentStatus::Conflict,
          all ? "Players are seated on a Sigil; they must leave the lobby first"
              : "Players are seated on this Sigil; they must leave the lobby first");
    }
  }
  if (paired == 0) return IntentResult::reject(IntentStatus::InvalidState, "No Sigils are paired");

  uint8_t forgotten = 0;
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if ((!all && id != target) || !sigilBus.record(id)) continue;
    if (forgetSigil(id)) ++forgotten;
  }
  leds.invalidateAll();
  serialLog.print("ATLAS|PAIRING|FORGET|");
  serialLog.print(all ? "ALL" : "ONE");
  serialLog.print("|COUNT|");
  serialLog.println(forgotten);
  if (forgotten != paired) {
    return forgotten == 0
        ? IntentResult::reject(IntentStatus::Rejected, "Pairing storage failed; nothing was forgotten")
        : IntentResult::accept("Some Sigils could not be forgotten; check the device list");
  }
  return IntentResult::accept(all ? "All Sigils forgotten" : "Sigil forgotten");
}

// Pairing v2 code check (PAIRING_AND_SECURE_LINK.md). The owner compares the code on the
// Sigil with the one on Atlas and confirms or rejects: at the Atlas screen
// (physically at the table), or as a portal Admin through the same
// presence-checked device path as Forget. Nothing is stored before Confirm.
IntentResult handlePairConfirmIntent(const Intent &intent, void *) {
  if (intent.actor.origin != IntentOrigin::AtlasHardware && !adminIntent(intent)) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Admin permission required");
  }
  if (hubState != HubState::Lobby) {
    return IntentResult::reject(IntentStatus::InvalidState, "Pair devices in the lobby");
  }
  const int32_t value = intent.payload.value;
  const bool confirm = (value & TurnHub::PAIR_CONFIRM_ACCEPT) != 0;
  const int32_t slot = value & ~TurnHub::PAIR_CONFIRM_ACCEPT;
  if (slot < 0 || slot >= MAX_PHYSICAL_SIGILS || !sigilBus.pendingPairing(slot)) {
    return IntentResult::reject(IntentStatus::InvalidActor, "No Sigil is waiting for that code");
  }
  if (!sigilBus.decidePairing(static_cast<uint8_t>(slot), confirm)) {
    return IntentResult::reject(IntentStatus::Rejected,
        confirm ? "Could not save the pairing; nothing was stored" : "Could not reject the pairing");
  }
  leds.invalidate(static_cast<uint8_t>(slot));
  return IntentResult::accept(confirm ? "Sigil paired securely" : "Pairing rejected");
}

// Payload: value = Atlas pairing window in milliseconds (60, 90 or 120 s;
// never below the 60 s minimum).
IntentResult handleConfigurePairingIntent(const Intent &intent, void *) {
  if (!adminIntent(intent)) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Admin permission required");
  }
  const uint32_t windowMs = static_cast<uint32_t>(intent.payload.value);
  if (intent.payload.value <= 0 || !TurnHub::validPairingWindowMs(windowMs)) {
    return IntentResult::reject(IntentStatus::Rejected, "Pairing window must be 60, 90 or 120 seconds");
  }
  if (TurnHub::savePairingWindow(windowMs) != TurnHubStorage::Status::Ok) {
    return IntentResult::reject(IntentStatus::Rejected, "Pairing window could not be saved");
  }
  pairingWindowMs = windowMs;
  serialLog.print("ATLAS|PAIRING|WINDOW_MS|");
  serialLog.println(windowMs);
  return IntentResult::accept("Pairing window saved");
}

// Payload: value = Atlas speaker volume, 0 (off) to 3 (high). Sigil sound is
// unaffected; it follows each seated player's accessibility preference.
IntentResult handleConfigureSpeakerIntent(const Intent &intent, void *) {
  if (!adminIntent(intent)) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Admin permission required");
  }
  if (!TurnHub::validSpeakerVolume(intent.payload.value)) {
    return IntentResult::reject(IntentStatus::Rejected, "Speaker volume must be off, low, medium or high");
  }
  const uint8_t volume = static_cast<uint8_t>(intent.payload.value);
  if (TurnHub::saveSpeakerVolume(volume) != TurnHubStorage::Status::Ok) {
    return IntentResult::reject(IntentStatus::Rejected, "Speaker volume could not be saved");
  }
  audio.setSpeakerVolume(volume);
  serialLog.print("ATLAS|SPEAKER|VOLUME|");
  serialLog.println(TurnHub::speakerVolumeName(volume));
  return IntentResult::accept("Speaker volume saved");
}

// --- First-run setup (FIRST_RUN_SETUP.md) ---------------------------------

void beginFirstRunSetup() {
  TurnHub::SetupStage stored = TurnHub::SetupStage::Complete;
  const auto status = TurnHub::loadSetupStage(stored);
  String admin;
  const bool adminExists = TurnHubAccounts::primaryAdmin(admin) && admin.length() > 0;
  setupStage = TurnHub::bootSetupStage(status, stored, adminExists);
  if (status == TurnHubStorage::Status::NotFound) {
    // Record the answer, so an Admin made during setup doesn't skip the rest
    // after a restart.
    if (TurnHub::saveSetupStage(setupStage) != TurnHubStorage::Status::Ok) {
      serialLog.println("ATLAS|SETUP|STAGE|SAVE_FAILED");
    }
  } else if (status != TurnHubStorage::Status::Ok) {
    serialLog.println("ATLAS|SETUP|STAGE|LOAD_FAILED");
  }
  serialLog.print("ATLAS|SETUP|STAGE|");
  serialLog.println(TurnHub::setupStageName(setupStage));
}

// A real Sigil (not the test harness or a spare) is already paired, e.g.
// during the phone's Sigils step.
static bool sigilPaired() {
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    const TurnHub::SigilRecord *record = sigilBus.record(id);
    if (record != nullptr && (record->capabilities &
        (TurnHubProtocol::CAPABILITY_HARNESS | TurnHubProtocol::CAPABILITY_SPARE)) == 0) return true;
  }
  return false;
}

// Payload: value = the next SetupStage. Stages only move forward; a factory
// reset is the way back to Welcome.
IntentResult handleAdvanceSetupIntent(const Intent &intent, void *) {
  using TurnHub::SetupStage;
  if (!TurnHub::validSetupStage(intent.payload.value)) {
    return IntentResult::reject(IntentStatus::Rejected, "Unknown setup step");
  }
  SetupStage next = static_cast<SetupStage>(intent.payload.value);
  if (next == SetupStage::Finished) {
    // The phone's last step: an Admin at the table who has replaced the
    // shipped Wi-Fi password. Atlas restarts afterwards, so not mid-match.
    if (!adminIntent(intent)) {
      return IntentResult::reject(IntentStatus::Unauthorized, "Admin permission required");
    }
    if (!presenceConfirmedFor(String(intent.payload.moderatorId), millis())) {
      return IntentResult::reject(IntentStatus::Unauthorized,
          "Verify at the table first: enter the code the Atlas screen shows");
    }
    if (setupStage != SetupStage::Welcome) {
      return IntentResult::reject(IntentStatus::InvalidState, "Setup is already finished");
    }
    if (hubState != HubState::Lobby && hubState != HubState::GameOver) {
      return IntentResult::reject(IntentStatus::InvalidState, "Finish setup between games");
    }
    const String password = TurnHub::readStoredWifiPassword();
    if (!TurnHub::validWifiPassword(password) || password == AtlasConfig::WIFI_DEFAULT_PASSWORD) {
      return IntentResult::reject(IntentStatus::Rejected, "Choose the table's own Wi-Fi password first");
    }
    // "You're all set" exists to say "Next: pair your Sigils". With Sigils
    // already paired there is nothing left to say (owner, 2026-09-30).
    if (sigilPaired()) next = SetupStage::Complete;
  } else if (next == SetupStage::Complete) {
    // "You're all set" acknowledged at the table, or by an Admin.
    if (intent.actor.origin != IntentOrigin::AtlasHardware && !adminIntent(intent)) {
      return IntentResult::reject(IntentStatus::Unauthorized, "Admin permission required");
    }
    if (setupStage != SetupStage::Finished) {
      return IntentResult::reject(IntentStatus::InvalidState,
          setupStage == SetupStage::Complete ? "Setup is already complete" : "Finish setup on a phone first");
    }
  } else {
    return IntentResult::reject(IntentStatus::InvalidState, "Setup can't go back; use factory reset");
  }
  if (TurnHub::saveSetupStage(next) != TurnHubStorage::Status::Ok) {
    return IntentResult::reject(IntentStatus::Rejected, "Setup progress could not be saved");
  }
  setupStage = next;
  serialLog.print("ATLAS|SETUP|STAGE|");
  serialLog.println(TurnHub::setupStageName(next));
  return IntentResult::accept(next == SetupStage::Finished ? "Setup finished" : "Setup complete");
}

// An Admin's way back to an empty lobby from any state, for example while the
// Sigils are being rewired and nobody at the table can finish the match. It
// needs that Admin verified at the table (presence code), so someone is there. A
// match in progress ends as a draw first (statistics once, like the End match
// hold); a countdown is canceled.
IntentResult handleResetTableIntent(const Intent &intent, void *) {
  if (!adminIntent(intent)) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Admin permission required");
  }
  if (!presenceConfirmedFor(String(intent.payload.moderatorId), millis())) {
    return IntentResult::reject(IntentStatus::Unauthorized,
        "Verify at the table first: enter the code the Atlas screen shows");
  }
  if (hubState == HubState::Starting) cancelCountdown();
  const bool matchInProgress = hubState == HubState::Running || hubState == HubState::Paused;
  if (matchInProgress) {
    if (!game.endInDraw(millis())) {
      return IntentResult::reject(IntentStatus::Conflict, "Atlas could not end the match");
    }
    finishGameState();
  }
  serialLog.print("ATLAS|ADMIN|RESET_TABLE|");
  serialLog.println(matchInProgress ? "MATCH_ENDED_AS_DRAW" : "NO_MATCH");
  enterEmptyLobby(&intent);
  return IntentResult::accept(matchInProgress
      ? "Match ended as a draw; the table is back to an empty lobby"
      : "The table is back to an empty lobby");
}

namespace {
// An Atlas factory reset waits this long so the web reply leaves first.
constexpr uint32_t FACTORY_RESET_DELAY_MS = 1500;
bool atlasResetScheduled = false;
uint32_t atlasResetAtMs = 0;
}  // namespace

// Erases a device's saved settings (NVS). Admin, verified at the table
// (presence code), and never during a match.
//   Atlas: every profile, statistic, pairing and setting in NVS goes; Atlas
//   restarts as new. The microSD card is not touched.
//   Sigil: Atlas tells it to erase and restart (FactoryReset packet), then
//   forgets it. A Sigil out of range is only forgotten here.
IntentResult handleFactoryResetIntent(const Intent &intent, void *) {
  if (sigilUpdatesBusy()) return IntentResult::reject(IntentStatus::Conflict, "Wait for the firmware update to finish");
  const int32_t target = intent.payload.value;
  // Atlas's own BOOT button held for FACTORY_RESET_HOLD_MS is the recovery path
  // for a stuck or unresponsive screen: whoever holds it is at the table, so no
  // Admin or table code is needed, and it works in any state (a match is lost
  // with everything else). The touchscreen's Menu > Device hold sends the same
  // Intent, between games only. It resets Atlas only, never a Sigil.
  const bool atBootButton = intent.actor.origin == IntentOrigin::AtlasHardware &&
      target == TurnHub::FACTORY_RESET_ATLAS;
  if (!atBootButton) {
    if (!adminIntent(intent)) {
      return IntentResult::reject(IntentStatus::Unauthorized, "Admin permission required");
    }
    if (!presenceConfirmedFor(String(intent.payload.moderatorId), millis())) {
      return IntentResult::reject(IntentStatus::Unauthorized,
          "Verify at the table first: enter the code the Atlas screen shows");
    }
  }
  if (target == TurnHub::FACTORY_RESET_ATLAS) {
    if (sleepScheduled()) {
      return IntentResult::reject(IntentStatus::Conflict, "Atlas is going to sleep");
    }
    if (!atBootButton && hubState != HubState::Lobby && hubState != HubState::GameOver) {
      return IntentResult::reject(IntentStatus::InvalidState, "Factory reset Atlas between games");
    }
    if (!atlasResetScheduled) {
      atlasResetScheduled = true;
      atlasResetAtMs = millis() + FACTORY_RESET_DELAY_MS;
      serialLog.println("ATLAS|FACTORY_RESET|ATLAS|SCHEDULED");
    }
    return IntentResult::accept("Atlas is erasing its settings and microSD card, then restarting");
  }
  if (hubState != HubState::Lobby) {
    return IntentResult::reject(IntentStatus::InvalidState, "Factory reset Sigils in the lobby, between games");
  }
  if (target < 0 || target >= MAX_PHYSICAL_SIGILS || !sigilBus.record(target)) {
    return IntentResult::reject(IntentStatus::InvalidActor, "That Sigil is not paired with Atlas");
  }
  const uint8_t id = static_cast<uint8_t>(target);
  if (lobby.isJoined(id)) {
    return IntentResult::reject(IntentStatus::Conflict,
        "Players are seated on this Sigil; they must leave the lobby first");
  }
  // Queued before forget() queues Unpair, so the Sigil hears it first.
  const bool reached = sigilBus.isOnline(id, millis()) &&
      sigilBus.send(id, TurnHubProtocol::PacketType::FactoryReset, TurnHubProtocol::FACTORY_RESET_CONFIRM);
  if (!forgetSigil(id)) {
    return IntentResult::reject(IntentStatus::Rejected, "Pairing storage failed; nothing was reset");
  }
  leds.invalidateAll();
  serialLog.print("ATLAS|FACTORY_RESET|SIGIL|");
  serialLog.print(id);
  serialLog.println(reached ? "|SENT" : "|OFFLINE");
  static_assert(TurnHubProtocol::UNPAIR_HOLD_MS == 3000, "Update the \"3 s\" Pair hold wording");
  return IntentResult::accept(reached
      ? "Sigil is erasing its settings and restarting; pair it again to use it"
      : "The Sigil is out of range: Atlas forgot it. Hold its Pair button for 3 s to clear it too");
}

bool factoryResetScheduled() { return atlasResetScheduled; }

namespace {
// Long enough to read "touch the screen to wake it" before the screen goes dark.
constexpr uint32_t SLEEP_DELAY_MS = 2500;
bool atlasSleepScheduled = false;
uint32_t atlasSleepAtMs = 0;
}  // namespace

// Menu > Device Sleep: the touchscreen only (whoever taps it is at the
// table), between games, with no update or factory reset under way. Deep
// sleep loses RAM, so the lobby empties and phones sign in again; nothing
// saved is touched. Sigils show Atlas lost until Atlas wakes.
IntentResult handleSleepIntent(const Intent &intent, void *) {
  if (intent.actor.origin != IntentOrigin::AtlasHardware) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Sleep from the Atlas screen");
  }
  if (hubState != HubState::Lobby && hubState != HubState::GameOver) {
    return IntentResult::reject(IntentStatus::InvalidState, "Put Atlas to sleep between games");
  }
  if (sigilUpdatesBusy() || ota.inProgress() || atlasResetScheduled) {
    return IntentResult::reject(IntentStatus::Conflict, "Wait for the update or reset to finish");
  }
  if (!atlasSleepScheduled) {
    atlasSleepScheduled = true;
    atlasSleepAtMs = millis() + SLEEP_DELAY_MS;
    serialLog.println("ATLAS|SLEEP|SCHEDULED");
  }
  return IntentResult::accept("Going to sleep. Touch the screen to wake");
}

bool sleepScheduled() { return atlasSleepScheduled; }

void serviceSleep(uint32_t nowMs) {
  if (!atlasSleepScheduled || static_cast<int32_t>(nowMs - atlasSleepAtMs) < 0) return;
  atlasSleepScheduled = false;
  serialLog.println("ATLAS|SLEEP|ENTER");
  sleepAtlas();
}

void serviceFactoryReset(uint32_t nowMs) {
  if (!atlasResetScheduled || static_cast<int32_t>(nowMs - atlasResetAtMs) < 0) return;
  atlasResetScheduled = false;
  serialLog.println("ATLAS|FACTORY_RESET|ATLAS|ERASING");
  // The card first, while the log still reaches the serial port: a factory
  // reset leaves no TurnHub data on the microSD card either (owner decision
  // 2026-09-29). No card, or a card that fails, never stops the NVS erase.
  wipeSdCard();
  eraseSettingsAndRestart();
}

// Payload: flags = GameProfile (| GAME_FLAG_TWO_HEADED_GIANT), value = starting
// life, durationMs = turn timer.
IntentResult handleGameSettingsIntent(const Intent &intent, void *) {
  PlayerSeat actor;
  // Any seated player, in the lobby (no table host).
  if (hubState != HubState::Lobby || lobby.playerCount() == 0 ||
      !seatForModuleSlot(intent.actor.controllerId, intent.actor.slot, actor) ||
      actor.playerNumber != intent.actor.playerNumber) {
    return IntentResult::reject(IntentStatus::Unauthorized,
        "Only a seated player can change game settings, in the lobby");
  }
  const uint32_t profile = intent.payload.flags & ~TurnHub::GAME_FLAG_TWO_HEADED_GIANT;
  if (profile >= static_cast<uint32_t>(TurnHub::GameProfile::Count)) {
    return IntentResult::reject(IntentStatus::Rejected, "Unknown game profile");
  }
  TurnHub::GameSettings settings;
  settings.profile = static_cast<TurnHub::GameProfile>(profile);
  settings.twoHeadedGiant = (intent.payload.flags & TurnHub::GAME_FLAG_TWO_HEADED_GIANT) != 0;
  if (settings.twoHeadedGiant && !TurnHub::teamsAllowed(settings.profile)) {
    return IntentResult::reject(IntentStatus::Rejected, "Two-Headed Giant needs the Magic or Commander profile");
  }
  settings.startingLife = intent.payload.value;
  settings.turnTimerMs = intent.payload.durationMs;
  if (!TurnHub::validTurnTimerMs(settings.turnTimerMs)) {
    return IntentResult::reject(IntentStatus::Rejected,
        "Turn timer must be off or 15 seconds to 60 minutes in whole seconds");
  }
  if (!TurnHub::validGameSettings(settings)) {
    return IntentResult::reject(IntentStatus::Rejected, "Starting life must be between 0 and 1000000");
  }
  if (!gameSettingsAvailable || TurnHub::saveGameSettings(settings) != TurnHubStorage::Status::Ok) {
    return IntentResult::reject(IntentStatus::Rejected, "Game settings could not be saved");
  }
  nextGameSettings = settings;
  return IntentResult::accept("Game settings saved on Atlas");
}

}  // namespace TurnHubAtlas
