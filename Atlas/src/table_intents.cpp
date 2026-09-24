// Table lifecycle Intent handlers: profile participation, seat membership,
// starter selection, the start countdown, rematch/reset, elimination
// selection, pairing and next-game settings. Also owns the lifecycle
// transitions (empty lobby, rematch lobby, game start, game over).

#include "atlas_app.h"
#include "controller_profiles.h"
#include "game_settings_store.h"
#include "profile_store.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

HubState hubState = HubState::Lobby;
PendingPassState pendingPass;
uint32_t countdownStartedAtMs = 0;
int8_t lastCountdownSecond = -1;
uint8_t eliminationTargetPlayer = 0;
uint8_t winArmedModule = INVALID_ID;
uint8_t winArmedPlayer = 0;

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

void resetCountdown() {
  countdownStartedAtMs = 0;
  lastCountdownSecond = -1;
}

void clearPhysicalSeatProfiles() {
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    const auto *record = sigilBus.record(id);
    if (!record) continue;
    TurnHubProfiles::resetTransientSeatBindings(record->mac);
    sigilBus.syncDisplayProfile(id);
  }
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
  winArmedModule = INVALID_ID;
  winArmedPlayer = 0;
  pendingPass = PendingPassState{};
  resetGestureState();
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
  serialLog.print("|PLAYERS|");
  serialLog.println(count);
}

}  // namespace

void finishGameState() {
  // GameEngine has already persisted results using its captured profile IDs.
  clearPhysicalSeatProfiles();
  clearPendingPass("GAME_OVER");
  hubState = HubState::GameOver;
  eliminationTargetPlayer = 0;
  winArmedModule = INVALID_ID;
  winArmedPlayer = 0;
  leds.invalidateAll();
  audio.gameOver(gameAudioMask());

  serialLog.print("ATLAS|GAME|OVER|WINNER|");
  serialLog.println(game.winnerPlayerNumber());
}

void updateCountdown(uint32_t nowMs) {
  if (hubState != HubState::Starting) return;

  const uint32_t elapsed = nowMs - countdownStartedAtMs;
  const int8_t second = static_cast<int8_t>(elapsed / 1000);
  if (second >= 0 && second < 3 && second != lastCountdownSecond) {
    lastCountdownSecond = second;
    serialLog.print("ATLAS|LOBBY|COUNTDOWN|");
    serialLog.println(3 - second);
    audio.countdownTone(lobbyAudioMask(), static_cast<uint8_t>(second));
  }
  if (elapsed >= START_COUNTDOWN_MS) dispatchSystemIntent(IntentType::CompleteStart);
}

// --- Profile participation (JoinProfile / LeaveProfile / BindProfile) ----------

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
  TurnHubControllers::releaseBrowser(existing);
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
  if (slot == 2 && !lobby.hasSecondary(module)) {
    return IntentResult::reject(IntentStatus::Conflict, "Join seat B on the Sigil first");
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
      // Keep the guest's table position, host and secondary seat.
      lobby.leave(existing);
    } else {
      lobby.replaceController(existing, module);
    }
    TurnHubControllers::releaseBrowser(existing);
  } else if (!joined && !occupied) {
    lobby.join(module);
  }
  return participationChanged(intent.type);
}

}  // namespace

IntentResult handleProfileParticipationIntent(const Intent &intent, void *) {
  if (hubState != HubState::Lobby) {
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
  return bindPhysicalProfile(intent, profile, joined, existing, existingSlot);
}

// --- Seat membership (Join / Leave) ---------------------------------------------

namespace {

IntentResult seatChanged(bool joining) {
  leds.invalidateAll();
  return IntentResult::accept(joining ? "Joined" : "Left");
}

IntentResult toggleSecondarySeat(uint8_t module, bool joining) {
  if (lobby.startArmedBy() == module) lobby.clearStartArm();
  if (!lobby.isJoined(module) || lobby.hasSecondary(module) == joining) {
    return IntentResult::reject(IntentStatus::Conflict, "Secondary seat is already in the requested state");
  }
  bool added = false;
  PlayerSeat affected;
  if (!lobby.toggleSecondary(module, added, affected)) {
    return IntentResult::reject(IntentStatus::InvalidState, "Could not change secondary seat");
  }
  if (added) {
    audio.sharedPlayerAdded(module);
  } else {
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
  if (module == lobby.hostController()) {
    serialLog.print("ATLAS|LOBBY|HOST|");
    serialLog.println(module);
  }
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
          "Sign into this profile in the portal before using its Sigil");
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
      if (module != lobby.hostController() || lobby.playerCount() < 2) {
        return IntentResult::reject(IntentStatus::Unauthorized,
            "Only the host can choose a random starter with two players");
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
// browser host may start without arming.
IntentResult handleStartIntent(const Intent &intent, void *) {
  IntentResult rejection;
  if (!validTableActor(intent, rejection)) return rejection;
  const uint8_t module = intent.actor.controllerId;
  if (!gameSettingsAvailable) {
    return IntentResult::reject(IntentStatus::InvalidState,
        "Game settings storage is unavailable; restart Atlas after resolving the storage problem");
  }
  if (hubState != HubState::Lobby || module != lobby.hostController() || lobby.playerCount() < 2) {
    return IntentResult::reject(IntentStatus::InvalidState, "Only the host can start a lobby with two players");
  }
  if (intent.type == IntentType::ArmStart) {
    if (lobby.anyOtherHeld(module)) {
      return IntentResult::reject(IntentStatus::Conflict, "Another controller is held");
    }
    lobby.setStartArmedBy(module);
    audio.startArmed(module);
    serialLog.print("ATLAS|LOBBY|START_ARM|");
    serialLog.println(module);
  } else {
    if (intent.actor.origin != IntentOrigin::Browser && lobby.startArmedBy() != module) {
      return IntentResult::reject(IntentStatus::InvalidState, "Start is not armed");
    }
    beginCountdown();
  }
  return IntentResult::accept();
}

// System-only: the countdown finished (see updateCountdown).
IntentResult handleCompleteStartIntent(const Intent &intent, void *) {
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
  if (!validTableActor(intent, rejection)) return rejection;
  if (hubState != HubState::Starting) {
    return IntentResult::reject(IntentStatus::InvalidState, "No countdown");
  }
  // Existing behavior: any discovered module can cancel the countdown.
  cancelCountdown();
  return IntentResult::accept();
}

// --- Rematch / reset ----------------------------------------------------------------------------

// Rematch keeps the finished match's participants; ResetGame empties the
// table. Reset is reachable only from GameOver or an unstarted lobby, so it
// can never discard an in-progress (including recovered) match.
IntentResult handleResetIntent(const Intent &intent, void *) {
  IntentResult rejection;
  if (!validTableActor(intent, rejection)) return rejection;
  const uint8_t module = intent.actor.controllerId;
  if (module != lobby.hostController()) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Only the host can reset");
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
  return IntentResult::accept("Pass cancelled");
}

// --- Elimination selection ------------------------------------------------------------------------

namespace {

void beginEliminationSelection(uint8_t sigilId) {
  if (hubState != HubState::Paused || game.hasWinClaim()) return;

  PlayerSeat candidates[2];
  if (game.livingPlayersForController(sigilId, candidates, 2) == 0) return;

  eliminationTargetPlayer = candidates[0].playerNumber;
  game.cancelLifeChanges();
  winArmedModule = INVALID_ID;
  winArmedPlayer = 0;
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
    return IntentResult::reject(IntentStatus::Unauthorized, "Use the Atlas Pair button");
  }
  if (hubState != HubState::Lobby) {
    return IntentResult::reject(IntentStatus::InvalidState, "Pair devices in the lobby");
  }
  if (!sigilBus.openPairing()) {
    return IntentResult::reject(IntentStatus::Rejected, "Radio unavailable");
  }
  startPairingIndicator(millis());
  serialLog.print("ATLAS|PAIRING|ENTER|DURATION_MS|");
  serialLog.println(TurnHubProtocol::PAIRING_WINDOW_MS);
  return IntentResult::accept("Pairing window opened");
}

// Payload: flags = GameProfile, value = starting life, durationMs = turn timer.
IntentResult handleGameSettingsIntent(const Intent &intent, void *) {
  PlayerSeat actor;
  if (hubState != HubState::Lobby || lobby.playerCount() == 0 ||
      intent.actor.controllerId != lobby.hostController() || intent.actor.slot != 1 ||
      !seatForModuleSlot(intent.actor.controllerId, intent.actor.slot, actor) ||
      actor.playerNumber != intent.actor.playerNumber) {
    return IntentResult::reject(IntentStatus::Unauthorized,
        "Only the table host can change game settings in the lobby");
  }
  if (intent.payload.flags >= static_cast<uint32_t>(TurnHub::GameProfile::Count)) {
    return IntentResult::reject(IntentStatus::Rejected, "Unknown game profile");
  }
  TurnHub::GameSettings settings;
  settings.profile = static_cast<TurnHub::GameProfile>(intent.payload.flags);
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
