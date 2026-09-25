// In-game Intent handlers: PASS (with its cancellable grace period),
// pause/resume, concession, win claims, life and Commander counters, and the
// one-shot turn-timer cues. Each handler validates against canonical state
// before it mutates GameEngine or the table-decision state in atlas_app.h.

#include "atlas_app.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

TurnTimerCueState turnTimerCue;

namespace {

// The player whose answer the table now waits for hears ActionRequired on
// their own Sigil. Supplementary only: the portal/app and e-ink show the
// same request as text. Browser-only seats have no Sigil to sound.
void cueActionRequired(uint8_t playerNumber) {
  const PlayerSeat *seat = game.playerByNumber(playerNumber);
  if (seat != nullptr && seat->controllerId < MAX_PHYSICAL_SIGILS) {
    audio.actionRequired(seat->controllerId);
  }
}

// ATLAS|INTENT|<name>|ORIGIN|<origin>|PLAYER|<n>
void logIntent(const char *name, IntentOrigin origin, uint8_t playerNumber) {
  serialLog.print("ATLAS|INTENT|");
  serialLog.print(name);
  serialLog.print("|ORIGIN|");
  serialLog.print(intentOriginName(origin));
  serialLog.print("|PLAYER|");
  serialLog.println(playerNumber);
}

bool gameInProgress() {
  return hubState == HubState::Running || hubState == HubState::Paused;
}

// A win claim or elimination selection blocks every other table decision.
bool tableDecisionPending() {
  return game.hasWinClaim() || eliminationTargetPlayer != 0;
}

void disarmWinClaim() {
  winArmedModule = INVALID_ID;
  winArmedPlayer = 0;
}

IntentResult rejectMissingSeat() {
  return IntentResult::reject(IntentStatus::InvalidActor, "This seat is no longer at the table");
}

}  // namespace

// --- PASS ---------------------------------------------------------------------

void clearPendingPass(const char *reason) {
  if (!pendingPass.active) return;

  serialLog.print("ATLAS|GAME|PASS|CANCEL|PLAYER|");
  serialLog.print(pendingPass.seat.playerNumber);
  serialLog.print("|ORIGIN|");
  serialLog.print(intentOriginName(pendingPass.origin));
  if (reason != nullptr && reason[0] != '\0') {
    serialLog.print("|REASON|");
    serialLog.print(reason);
  }
  serialLog.println();

  pendingPass = PendingPassState{};
  leds.invalidateAll();
}

bool cancelPendingPassForModule(uint8_t sigilId, const char *reason) {
  if (!pendingPass.active || pendingPass.seat.controllerId != sigilId) return false;
  clearPendingPass(reason);
  return true;
}

// PASS is a toggle: the first request arms a grace period, a second request
// from the same seat inside it cancels. CommitPass applies it afterwards.
IntentResult handlePassIntent(const Intent &intent, void *) {
  if (hubState != HubState::Running) {
    return IntentResult::reject(IntentStatus::InvalidState,
        "Pass is only available during a running game");
  }
  const PlayerSeat *active = game.activePlayer();
  if (active == nullptr) {
    return IntentResult::reject(IntentStatus::InvalidState, "There is no active player");
  }
  if (intent.actor.playerNumber != active->playerNumber ||
      intent.actor.controllerId != active->controllerId ||
      intent.actor.slot != active->slot) {
    return IntentResult::reject(IntentStatus::Unauthorized, "It is not this seat's turn");
  }

  logIntent("PASS", intent.actor.origin, active->playerNumber);

  if (pendingPass.active) {
    if (pendingPass.seat.sameSeat(*active)) {
      clearPendingPass("PASS");
      return IntentResult::accept("Pending pass cancelled");
    }
    return IntentResult::reject(IntentStatus::Conflict, "Atlas rejected the pass");
  }

  pendingPass.active = true;
  pendingPass.seat = *active;
  pendingPass.requestedAtMs = millis();
  pendingPass.origin = intent.actor.origin;

  serialLog.print("ATLAS|GAME|PASS|PENDING|PLAYER|");
  serialLog.print(active->playerNumber);
  serialLog.print("|ORIGIN|");
  serialLog.print(intentOriginName(intent.actor.origin));
  serialLog.print("|GRACE_MS|");
  serialLog.println(PASS_GRACE_MS);
  leds.invalidateAll();
  return IntentResult::accept("Pass queued. Press Pass or Action within 3 seconds to cancel.");
}

// System-only: applies a pending PASS once its grace period has elapsed. The
// turn is charged up to the original request time, not the commit time.
IntentResult handleCommitPassIntent(const Intent &intent, void *) {
  if (intent.actor.origin != IntentOrigin::System) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Only Atlas commits deferred passes");
  }
  const IntentResult notReady =
      IntentResult::reject(IntentStatus::InvalidState, "Pending pass is not ready");
  if (!pendingPass.active) return notReady;

  const PlayerSeat *active = game.activePlayer();
  if (hubState != HubState::Running || active == nullptr ||
      !active->sameSeat(pendingPass.seat)) {
    clearPendingPass("STATE_CHANGE");
    return notReady;
  }
  if (millis() - pendingPass.requestedAtMs < PASS_GRACE_MS) return notReady;

  const PendingPassState committing = pendingPass;
  pendingPass = PendingPassState{};

  if (!game.passTurn(committing.seat.controllerId, committing.requestedAtMs)) {
    serialLog.print("ATLAS|GAME|PASS|COMMIT_REJECTED|PLAYER|");
    serialLog.print(committing.seat.playerNumber);
    serialLog.print("|ORIGIN|");
    serialLog.println(intentOriginName(committing.origin));
    leds.invalidateAll();
    return notReady;
  }

  const PlayerSeat *current = game.activePlayer();
  serialLog.print("ATLAS|GAME|PASS|COMMIT|");
  serialLog.print(committing.seat.playerNumber);
  serialLog.print("->");
  serialLog.print(current != nullptr ? current->playerNumber : 0);
  serialLog.print("|ORIGIN|");
  serialLog.println(intentOriginName(committing.origin));

  if (current != nullptr) audio.turnPassed(committing.seat.controllerId, current->controllerId);
  leds.invalidateAll();
  return IntentResult::accept("Pass committed");
}

void updatePendingPass(uint32_t nowMs) {
  if (pendingPass.active && nowMs - pendingPass.requestedAtMs >= PASS_GRACE_MS) {
    dispatchSystemIntent(IntentType::CommitPass);
  }
}

// --- Pause / resume -------------------------------------------------------------

IntentResult handlePauseIntent(const Intent &intent, void *) {
  const PlayerSeat *seat = seatForIntentActor(intent);
  if (seat == nullptr || game.isEliminated(intent.actor.playerNumber)) {
    return IntentResult::reject(IntentStatus::InvalidActor, "This player is not active in the game");
  }
  if (hubState != HubState::Running) {
    return IntentResult::reject(IntentStatus::InvalidState,
        "Pause is only available during a running game");
  }

  clearPendingPass("PAUSE");
  const PlayerSeat *active = game.activePlayer();
  if (!game.pause(millis())) {
    return IntentResult::reject(IntentStatus::Conflict, "Could not pause the game");
  }

  hubState = HubState::Paused;
  // The long-press pause gesture may continue into a win claim, but only for
  // the active player's own controller.
  if ((intent.payload.flags & TurnHub::ARM_WIN_ON_PAUSE) != 0 &&
      active != nullptr && active->controllerId == intent.actor.controllerId) {
    winArmedModule = intent.actor.controllerId;
    winArmedPlayer = active->playerNumber;
  } else {
    disarmWinClaim();
  }

  audio.pause(gameAudioMask());
  leds.invalidateAll();
  logIntent("PAUSE", intent.actor.origin, seat->playerNumber);
  return IntentResult::accept("Game paused");
}

IntentResult handleResumeIntent(const Intent &intent, void *) {
  const PlayerSeat *seat = seatForIntentActor(intent);
  if (seat == nullptr || game.isEliminated(intent.actor.playerNumber)) {
    return IntentResult::reject(IntentStatus::InvalidActor, "This player is not active in the game");
  }
  if (hubState != HubState::Paused) {
    return IntentResult::reject(IntentStatus::InvalidState,
        "Resume is only available while the game is paused");
  }
  if (game.hasWinClaim()) {
    return IntentResult::reject(IntentStatus::Conflict, "Resolve the win claim before resuming");
  }
  if (eliminationTargetPlayer != 0) {
    return IntentResult::reject(IntentStatus::Conflict, "Resolve the elimination before resuming");
  }
  if (!game.resume(millis())) {
    return IntentResult::reject(IntentStatus::Conflict, "Could not resume the game");
  }

  hubState = HubState::Running;
  disarmWinClaim();
  audio.resume(gameAudioMask());
  leds.invalidateAll();
  logIntent("RESUME", intent.actor.origin, seat->playerNumber);
  return IntentResult::accept("Game resumed");
}

IntentResult handleTogglePauseIntent(const Intent &intent, void *) {
  if (!gameInProgress()) {
    return IntentResult::reject(IntentStatus::InvalidState, "Pause/resume is unavailable in this state");
  }
  Intent resolved = intent;
  resolved.type = hubState == HubState::Running ? IntentType::Pause : IntentType::Resume;
  return intents.dispatch(resolved);
}

// --- Ending a match as a draw ---------------------------------------------------

// The table's way out of a match nobody can or wants to finish, including one
// restored after a power loss. Only the Atlas touchscreen hold can ask, so it
// needs someone at the table. It overrides open table decisions (a win claim,
// an elimination selection, a queued PASS); statistics record a draw once.
IntentResult handleEndMatchIntent(const Intent &intent, void *) {
  if (intent.actor.origin != IntentOrigin::AtlasHardware) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Hold End match on the Atlas screen to end the match");
  }
  if (!gameInProgress()) {
    return IntentResult::reject(IntentStatus::InvalidState, "No match is in progress");
  }
  // finishGameState() then clears the queued PASS and table decisions.
  if (!game.endInDraw(millis())) {
    return IntentResult::reject(IntentStatus::Conflict, "Atlas could not end the match");
  }
  logIntent("END_MATCH", intent.actor.origin, 0);
  finishGameState();
  return IntentResult::accept("Match ended as a draw");
}

// --- Concession -----------------------------------------------------------------

// Unlike Eliminate, a concession restores running play if the match continues.
IntentResult handleConcedeIntent(const Intent &intent, void *) {
  const PlayerSeat *seat = seatForIntentActor(intent);
  if (seat == nullptr) return rejectMissingSeat();
  if (!gameInProgress()) {
    return IntentResult::reject(IntentStatus::InvalidState,
        "Concede is only available during an active game");
  }
  if (tableDecisionPending()) {
    return IntentResult::reject(IntentStatus::Conflict, "Resolve the current table decision first");
  }
  if (game.isEliminated(seat->playerNumber)) {
    return IntentResult::reject(IntentStatus::Conflict, "This player has already left the game");
  }

  clearPendingPass("CONCEDE");
  const uint32_t nowMs = millis();
  const bool restoreRunning = hubState == HubState::Running;
  if (restoreRunning) {
    if (!game.pause(nowMs)) {
      return IntentResult::reject(IntentStatus::Conflict, "Could not prepare the concession");
    }
    hubState = HubState::Paused;
  }

  bool gameFinished = false;
  if (!game.eliminatePlayer(seat->playerNumber, nowMs, gameFinished)) {
    if (restoreRunning && game.resume(nowMs)) hubState = HubState::Running;
    return IntentResult::reject(IntentStatus::Conflict, "Atlas rejected the concession");
  }

  audio.playerEliminated(seat->controllerId);
  leds.invalidateAll();
  logIntent("CONCEDE", intent.actor.origin, seat->playerNumber);

  if (gameFinished) {
    finishGameState();
  } else if (restoreRunning && game.resume(nowMs)) {
    hubState = HubState::Running;
  }
  return IntentResult::accept("Player conceded");
}

// --- Win claims -------------------------------------------------------------------

IntentResult handleClaimWinIntent(const Intent &intent, void *) {
  const PlayerSeat *resolved = seatForIntentActor(intent);
  if (resolved == nullptr) return rejectMissingSeat();
  const PlayerSeat seat = *resolved;
  if (!gameInProgress()) {
    return IntentResult::reject(IntentStatus::InvalidState, "Win claim is unavailable right now");
  }
  if (tableDecisionPending()) {
    return IntentResult::reject(IntentStatus::InvalidState, "Another table decision is already pending");
  }
  const PlayerSeat *active = game.activePlayer();
  if (active == nullptr || !active->sameSeat(seat) || game.isEliminated(seat.playerNumber)) {
    return IntentResult::reject(IntentStatus::InvalidState, "Only the active player can claim a win");
  }

  const bool armedClaim = (intent.payload.flags & TurnHub::CLAIM_FROM_ARMED_PAUSE) != 0;
  if (armedClaim && (hubState != HubState::Paused ||
      winArmedModule != seat.controllerId || winArmedPlayer != seat.playerNumber)) {
    return IntentResult::reject(IntentStatus::InvalidState, "Win claim is not armed");
  }
  clearPendingPass("WIN_CLAIM");
  // An armed claim came from running play, so a denial resumes it.
  const bool restoreRunning = hubState == HubState::Running || armedClaim;
  if (!game.beginWinClaim(seat.playerNumber, restoreRunning, millis())) {
    return IntentResult::reject(IntentStatus::InvalidState, "Could not start the win claim");
  }
  disarmWinClaim();
  leds.invalidateAll();

  if (game.gameOver()) {
    finishGameState();
  } else {
    hubState = HubState::Paused;
    audio.winClaimed(gameAudioMask());
    cueActionRequired(game.nextWinConfirmationPlayerNumber());
  }

  serialLog.print("ATLAS|INTENT|WIN|CLAIMED|PLAYER|");
  serialLog.println(seat.playerNumber);
  return IntentResult::accept("Win claim sent to the table");
}

namespace {

// Shared validation for confirm/deny: only the next player in table order
// may answer the open claim.
const PlayerSeat *winResponder(const Intent &intent, const char *noClaimMessage,
    IntentResult &rejection) {
  const PlayerSeat *seat = seatForIntentActor(intent);
  if (seat == nullptr) {
    rejection = rejectMissingSeat();
  } else if (hubState != HubState::Paused || !game.hasWinClaim()) {
    rejection = IntentResult::reject(IntentStatus::InvalidState, noClaimMessage);
    seat = nullptr;
  } else if (game.nextWinConfirmationPlayerNumber() != seat->playerNumber) {
    rejection = IntentResult::reject(IntentStatus::InvalidState, "Another player must respond first");
    seat = nullptr;
  }
  return seat;
}

}  // namespace

IntentResult handleConfirmWinIntent(const Intent &intent, void *) {
  IntentResult rejection;
  const PlayerSeat *responder = winResponder(intent, "There is no win claim to confirm", rejection);
  if (responder == nullptr) return rejection;
  const uint8_t player = responder->playerNumber;

  bool gameFinished = false;
  if (!game.confirmWinClaim(player, millis(), gameFinished)) {
    return IntentResult::reject(IntentStatus::InvalidState, "Could not confirm the win claim");
  }
  audio.winConfirmed(gameAudioMask());
  leds.invalidateAll();
  serialLog.print("ATLAS|INTENT|WIN|CONFIRMED|PLAYER|");
  serialLog.println(player);
  if (gameFinished) {
    finishGameState();
  } else {
    cueActionRequired(game.nextWinConfirmationPlayerNumber());
  }
  return IntentResult::accept("Win claim confirmed");
}

IntentResult handleDenyWinIntent(const Intent &intent, void *) {
  IntentResult rejection;
  const PlayerSeat *responder = winResponder(intent, "There is no win claim to deny", rejection);
  if (responder == nullptr) return rejection;
  const uint8_t player = responder->playerNumber;

  if (!game.denyWinClaim(player, millis())) {
    return IntentResult::reject(IntentStatus::InvalidState, "Could not deny the win claim");
  }
  // The engine restores whichever state preceded the claim.
  hubState = game.paused() ? HubState::Paused : HubState::Running;
  audio.winDenied(gameAudioMask());
  leds.invalidateAll();
  serialLog.print("ATLAS|INTENT|WIN|DENIED|PLAYER|");
  serialLog.println(player);
  return IntentResult::accept("Win claim denied");
}

// --- Life and Commander counters ----------------------------------------------------

IntentResult handleChangeLifeIntent(const Intent &intent, void *) {
  const PlayerSeat *seat = seatForIntentActor(intent);
  if (!seat || intent.payload.targetPlayer != seat->playerNumber) {
    return IntentResult::reject(IntentStatus::Unauthorized, "You can change only your own life");
  }
  if (!gameInProgress() || eliminationTargetPlayer ||
      !game.changeLife(seat->playerNumber, intent.payload.value)) {
    return IntentResult::reject(IntentStatus::InvalidState,
        "Life cannot be changed now or exceeds its limits");
  }
  return IntentResult::accept("Life updated");
}

// System-only loop tick that resolves life requests past LIFE_APPROVAL_MS.
IntentResult handleExpireLifeChangesIntent(const Intent &intent, void *) {
  if (intent.actor.origin != IntentOrigin::System) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Only Atlas expires life requests");
  }
  game.expireLifeChanges(millis());
  return IntentResult::accept();
}

namespace {

IntentResult changeCommanderDamage(const PlayerSeat &seat, const TurnHub::IntentPayload &payload) {
  // Players record only the Commander damage they received.
  if (payload.targetPlayer != seat.playerNumber) {
    return IntentResult::reject(IntentStatus::Unauthorized,
        "Record only your own received Commander damage");
  }
  if (game.settings().profile != TurnHub::GameProfile::Commander) {
    return IntentResult::reject(IntentStatus::Conflict,
        "Commander damage requires an MTG Commander game");
  }
  if (!game.playerByNumber(payload.counterSource) || payload.counterSlot < 1 ||
      payload.counterSlot > TurnHub::COMMANDERS_PER_PLAYER) {
    return IntentResult::reject(IntentStatus::Conflict, "Select a valid commander owner and commander");
  }
  const int64_t recorded =
      game.commanderDamage(seat.playerNumber, payload.counterSource, payload.counterSlot);
  if (payload.value < 0 && recorded + payload.value < 0) {
    return IntentResult::reject(IntentStatus::Conflict,
        "Cannot remove more Commander damage than recorded; use a positive number to add damage");
  }
  if (!game.changeCommanderDamage(seat.playerNumber, payload.counterSource,
          payload.counterSlot, payload.value)) {
    return IntentResult::reject(IntentStatus::Conflict,
        "Commander damage must be nonzero and keep damage and life within their limits");
  }
  return IntentResult::accept("Commander damage and life updated");
}

}  // namespace

// RequestLifeChange, RespondLifeChange and ChangeCounter share one living,
// undecided-table precondition.
IntentResult handleCounterIntent(const Intent &intent, void *) {
  const PlayerSeat *seat = seatForIntentActor(intent);
  if (!seat || game.isEliminated(seat->playerNumber)) {
    return IntentResult::reject(IntentStatus::InvalidActor, "A living participant is required");
  }
  if (!gameInProgress() || tableDecisionPending()) {
    return IntentResult::reject(IntentStatus::InvalidState, "Resolve the current table decision first");
  }
  const auto &payload = intent.payload;
  switch (intent.type) {
    case IntentType::RequestLifeChange:
      if (!game.requestLifeChange(seat->playerNumber, payload.targetPlayer, payload.value, millis())) {
        return IntentResult::reject(IntentStatus::Conflict,
            "Request unavailable: check the target, pending request and life limits");
      }
      cueActionRequired(payload.targetPlayer);
      return IntentResult::accept(
          "Life change requested; Atlas accepts it after 15 seconds unless rejected");
    case IntentType::RespondLifeChange:
      // flags: 1 accepts, 0 rejects.
      if (payload.flags > 1 ||
          !game.respondLifeChange(seat->playerNumber, payload.requestId, payload.flags == 1, millis())) {
        return IntentResult::reject(IntentStatus::Conflict,
            "Request ended, changed or could not be applied; refresh the current total");
      }
      return IntentResult::accept(payload.flags ? "Life change accepted" : "Life change rejected");
    case IntentType::ChangeCounter:
      return changeCommanderDamage(*seat, payload);
    default:
      return IntentResult::reject(IntentStatus::Unsupported, "Unknown counter action");
  }
}

// --- Turn-timer cues ------------------------------------------------------------------

// Turn-timer phases are derived from the turn anchor (never stored). This only
// turns a phase *transition* on the running turn into a one-shot semantic cue;
// LEDs render the phase continuously through LedRenderer. Expiry never passes
// the turn. A new turn (player or completed-turn count changes) re-arms it, and
// a paused game keeps its last phase so resuming does not repeat a cue.
void updateTurnTimerCues(uint32_t nowMs) {
  using TurnHub::TurnTimerPhase;
  if (hubState == HubState::Paused) return;
  const PlayerSeat *active = hubState == HubState::Running ? game.activePlayer() : nullptr;
  if (active == nullptr) {
    turnTimerCue = TurnTimerCueState{};
    return;
  }
  const TurnHub::PlayerStats *stats = game.statsForPlayer(active->playerNumber);
  const uint32_t turns = stats != nullptr ? stats->turnsCompleted : 0;
  if (active->playerNumber != turnTimerCue.player || turns != turnTimerCue.turnsCompleted) {
    turnTimerCue = TurnTimerCueState{};
    turnTimerCue.player = active->playerNumber;
    turnTimerCue.turnsCompleted = turns;
  }
  const TurnTimerPhase phase = game.turnTimerPhase(nowMs);
  if (phase == turnTimerCue.phase) return;
  turnTimerCue.phase = phase;
  if (phase == TurnTimerPhase::Warning) {
    audio.turnWarning(active->controllerId);
  } else if (phase == TurnTimerPhase::Expired) {
    audio.timerExpired(active->controllerId);
  } else {
    return;  // LongTurn stays a quiet visual cue; Normal needs nothing.
  }
  serialLog.print("ATLAS|TIMER|");
  serialLog.print(TurnHub::turnTimerPhaseName(phase));
  serialLog.print("|PLAYER|");
  serialLog.println(active->playerNumber);
}

}  // namespace TurnHubAtlas
