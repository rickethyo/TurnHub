// Physical Sigil adapter. Translates ESP-NOW button events into Intents and
// keeps the transport-local gesture bookkeeping (held, chord and suppression
// flags) that decides which Intent a press means. It never mutates canonical
// table state directly; audit_adapters.py enforces that.
//
// Gesture map ("win hold" is Sigil's 5 s ActionWin hold):
//   Lobby     Action short: join, then cycle starter  Action long: arm start
//             release after arming: start (host)      Action + PASS: toggle seat B
//             PASS: random starter (host)             win hold while armed: reset
//   Starting  Action down: cancel the countdown
//   Running   PASS: queue/cancel a pass               Action down: cancel queued pass
//             Action long: pause and arm a win claim
//   Paused    Action long: resume / cancel elimination
//             Action + PASS: begin elimination        PASS: eliminate selection
//             Action short: cycle elimination         win hold: claim win (armed)
//   Win claim Action short: confirm                   PASS: deny
//   GameOver  Action short: rematch (host)            Action long: reset (host)

#include "atlas_app.h"
#include "controller_profiles.h"
#include "runtime_diagnostics.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

namespace {

// Released Action presses are ignored this long after they cancelled a PASS,
// so the cancelling press does not also trigger its own gesture.
constexpr uint32_t ACTION_CANCEL_RELEASE_CLEAR_MS = 250;

bool eliminationChord[MAX_PHYSICAL_SIGILS] = {};
bool suppressEliminationShort[MAX_PHYSICAL_SIGILS] = {};
bool suppressActionAfterPassCancel[MAX_PHYSICAL_SIGILS] = {};
uint32_t suppressActionReleasedAtMs[MAX_PHYSICAL_SIGILS] = {};

// ATLAS|<prefix>|REJECTED|<sigil>|<message>
void logRejected(const char *prefix, uint8_t sigilId, const IntentResult &result) {
  if (result.accepted()) return;
  serialLog.print("ATLAS|");
  serialLog.print(prefix);
  serialLog.print("|REJECTED|");
  serialLog.print(sigilId);
  serialLog.print("|");
  serialLog.println(result.message);
}

// Confirms or denies the open win claim when this Sigil owns the seat whose
// answer Atlas is waiting for.
void respondToWinClaim(uint8_t sigilId, IntentType response) {
  const PlayerSeat *expected = game.playerByNumber(game.nextWinConfirmationPlayerNumber());
  if (expected == nullptr || expected->controllerId != sigilId) return;
  dispatchSeatIntent(response, IntentOrigin::PhysicalSigil, *expected);
}

// Pause and resume act for the controller's first living seat.
void dispatchPauseOrResume(uint8_t sigilId, IntentType type) {
  const char *name = type == IntentType::Pause ? "PAUSE" : "RESUME";
  PlayerSeat actor;
  if (!firstLivingSeatForModule(sigilId, actor)) {
    serialLog.print("ATLAS|INTENT|");
    serialLog.print(name);
    serialLog.print("|REJECTED_MODULE|");
    serialLog.println(sigilId);
    return;
  }
  const uint32_t flags = type == IntentType::Pause ? TurnHub::ARM_WIN_ON_PAUSE : 0;
  const IntentResult result = dispatchSeatIntent(type, IntentOrigin::PhysicalSigil, actor, flags);
  if (!result.accepted()) {
    serialLog.print("ATLAS|INTENT|");
    serialLog.print(name);
    serialLog.print("|REJECTED|");
    serialLog.println(result.message);
  }
}

const char *activityKind(PacketType type) {
  switch (type) {
    case PacketType::Pass: return "sigil_pass";
    case PacketType::ActionDown: return "sigil_down";
    case PacketType::ActionUp: return "sigil_up";
    case PacketType::ActionShort: return "sigil_short";
    case PacketType::ActionLong: return "sigil_long";
    case PacketType::ActionWin: return "sigil_win";
    default: return "sigil_event";
  }
}

// Keep forced connection resets effective for an occupied Sigil, but let an
// unjoined Sigil reach the normal lobby join validator. That validator can
// return the policy/PIN reason instead of losing the button event silently.
bool connectionBlocked(uint8_t sigilId) {
  if (hubState == HubState::Lobby && !lobby.isJoined(sigilId)) return false;
  bool blocked = false;
  for (uint8_t slot = 1; slot <= 2; ++slot) {
    // A saved secondary binding is not an active connection until that seat
    // joins. Do not let an archived or reset placeholder disable the primary
    // seat on the same physical Sigil.
    if (lobby.playerNumber(sigilId, slot) == 0) continue;
    const String id = TurnHubControllers::existingProfileForSeat(sigilId, slot);
    if (!id.length() || !TurnHubWebApi::connectionBlocked(id)) continue;
    serialLog.print("ATLAS|SIGIL|CONNECTION_BLOCKED|");
    serialLog.print(sigilId);
    serialLog.print("|SLOT|");
    serialLog.print(slot);
    serialLog.print("|PROFILE|");
    serialLog.println(id);
    TurnHub::recordActivity("connection_blocked",
        String("sigil=") + String(sigilId) + " slot=" + String(slot) + " profile=" + id);
    blocked = true;
  }
  return blocked;
}

}  // namespace

void resetGestureState() {
  for (uint8_t i = 0; i < MAX_PHYSICAL_SIGILS; ++i) {
    eliminationChord[i] = false;
    suppressEliminationShort[i] = false;
    suppressActionAfterPassCancel[i] = false;
    suppressActionReleasedAtMs[i] = 0;
  }
}

void updateActionCancelSuppression(uint32_t nowMs) {
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if (suppressActionAfterPassCancel[id] && suppressActionReleasedAtMs[id] != 0 &&
        nowMs - suppressActionReleasedAtMs[id] >= ACTION_CANCEL_RELEASE_CLEAR_MS) {
      suppressActionAfterPassCancel[id] = false;
      suppressActionReleasedAtMs[id] = 0;
    }
  }
}

void handleLobbyShort(uint8_t sigilId) {
  const auto result = dispatchModuleIntent(
      lobby.isJoined(sigilId) ? IntentType::SelectStarter : IntentType::Join,
      sigilId, 1, static_cast<int32_t>(TurnHub::StarterSelection::CycleModule));
  TurnHub::recordActivity(result.accepted() ? "lobby_action" : "lobby_rejected",
      String("sigil=") + String(sigilId) + " result=" + result.message);
  logRejected("LOBBY|JOIN", sigilId, result);
}

void handlePass(uint8_t sigilId) {
  if (hubState == HubState::Lobby) {
    if (lobby.isHeld(sigilId)) {
      // Action + PASS chord toggles the secondary seat.
      lobby.setSharedChord(sigilId, true);
      lobby.setSuppressNextShort(sigilId, true);
      const auto result = dispatchModuleIntent(
          lobby.hasSecondary(sigilId) ? IntentType::Leave : IntentType::Join, sigilId, 2);
      logRejected("LOBBY|SECONDARY", sigilId, result);
      return;
    }
    const auto result = dispatchModuleIntent(IntentType::SelectStarter, sigilId, 1,
        static_cast<int32_t>(TurnHub::StarterSelection::Random));
    logRejected("LOBBY|STARTER", sigilId, result);
    return;
  }

  if (hubState == HubState::Paused) {
    if (game.hasWinClaim()) {
      respondToWinClaim(sigilId, IntentType::DenyWin);
      return;
    }
    if (lobby.isHeld(sigilId)) {
      // Action + PASS chord begins an elimination selection.
      eliminationChord[sigilId] = true;
      suppressEliminationShort[sigilId] = true;
      dispatchModuleIntent(IntentType::BeginElimination, sigilId);
      return;
    }
    if (eliminationTargetPlayer != 0) dispatchModuleIntent(IntentType::Eliminate, sigilId);
    return;
  }

  if (hubState != HubState::Running) return;
  const PlayerSeat *active = game.activePlayer();
  if (active == nullptr || active->controllerId != sigilId) return;
  logRejected("GAME|PASS", sigilId,
      dispatchSeatIntent(IntentType::Pass, IntentOrigin::PhysicalSigil, *active));
}

void handleActionDown(uint8_t sigilId) {
  lobby.setHeld(sigilId, true);
  lobby.setActionLong(sigilId, false);
  lobby.setSharedChord(sigilId, false);
  eliminationChord[sigilId] = false;

  if (hubState == HubState::Running &&
      dispatchModuleIntent(IntentType::CancelPass, sigilId).accepted()) {
    suppressActionAfterPassCancel[sigilId] = true;
    suppressActionReleasedAtMs[sigilId] = 0;
    return;
  }
  if (hubState == HubState::Starting) dispatchModuleIntent(IntentType::CancelStart, sigilId);
}

void handleActionUp(uint8_t sigilId) {
  lobby.setHeld(sigilId, false);

  const bool usedLobbyChord = lobby.sharedChord(sigilId);
  const bool usedEliminationChord = eliminationChord[sigilId];
  const bool wasLong = lobby.actionLong(sigilId);

  lobby.setSharedChord(sigilId, false);
  eliminationChord[sigilId] = false;
  lobby.setActionLong(sigilId, false);

  if (suppressActionAfterPassCancel[sigilId]) {
    suppressActionReleasedAtMs[sigilId] = millis();
    return;
  }
  // A long chord already consumed its release; a short chord's release is
  // still followed by an ActionShort that the suppression flag swallows.
  if (usedLobbyChord) {
    if (wasLong) lobby.setSuppressNextShort(sigilId, false);
    return;
  }
  if (usedEliminationChord) {
    if (wasLong) suppressEliminationShort[sigilId] = false;
    return;
  }
  if (hubState == HubState::Lobby && lobby.startArmedBy() == sigilId &&
      sigilId == lobby.hostController()) {
    dispatchModuleIntent(IntentType::StartGame, sigilId);
  }
}

void handleActionShort(uint8_t sigilId) {
  if (suppressActionAfterPassCancel[sigilId]) {
    suppressActionAfterPassCancel[sigilId] = false;
    suppressActionReleasedAtMs[sigilId] = 0;
    return;
  }
  if (lobby.consumeSuppressNextShort(sigilId)) return;
  if (suppressEliminationShort[sigilId]) {
    suppressEliminationShort[sigilId] = false;
    return;
  }

  if (hubState == HubState::Paused && game.hasWinClaim()) {
    respondToWinClaim(sigilId, IntentType::ConfirmWin);
    return;
  }
  if (hubState == HubState::Paused && eliminationTargetPlayer != 0) {
    dispatchModuleIntent(IntentType::CycleElimination, sigilId);
    return;
  }
  if (hubState == HubState::Lobby) {
    handleLobbyShort(sigilId);
    return;
  }
  if (hubState == HubState::GameOver && sigilId == lobby.hostController()) {
    dispatchModuleIntent(IntentType::Rematch, sigilId);
  }
}

void handleActionLong(uint8_t sigilId) {
  if (suppressActionAfterPassCancel[sigilId]) return;

  lobby.setActionLong(sigilId, true);
  if (hubState == HubState::Lobby && lobby.sharedChord(sigilId)) return;
  if (hubState == HubState::Paused && eliminationChord[sigilId]) return;

  switch (hubState) {
    case HubState::Lobby:
      dispatchModuleIntent(IntentType::ArmStart, sigilId);
      break;
    case HubState::Running:
      dispatchPauseOrResume(sigilId, IntentType::Pause);
      break;
    case HubState::Paused:
      if (game.hasWinClaim()) {
        serialLog.println("ATLAS|GAME|RESUME|DENIED_WIN_CLAIM");
      } else if (eliminationTargetPlayer != 0) {
        dispatchModuleIntent(IntentType::CancelElimination, sigilId);
      } else {
        dispatchPauseOrResume(sigilId, IntentType::Resume);
      }
      break;
    case HubState::GameOver:
      if (sigilId == lobby.hostController()) dispatchModuleIntent(IntentType::ResetGame, sigilId);
      break;
    default:
      break;
  }
}

void handleActionWin(uint8_t sigilId) {
  if (suppressActionAfterPassCancel[sigilId]) return;

  if (hubState == HubState::Lobby && sigilId == lobby.hostController() &&
      lobby.startArmedBy() == sigilId) {
    dispatchModuleIntent(IntentType::ResetGame, sigilId);
    return;
  }
  if (hubState != HubState::Paused || eliminationTargetPlayer != 0 || game.hasWinClaim()) return;

  // Only completes the gesture armed by this Sigil's own long-press pause.
  const PlayerSeat *active = game.activePlayer();
  if (active == nullptr || sigilId != winArmedModule || winArmedPlayer == 0 ||
      active->controllerId != sigilId || active->playerNumber != winArmedPlayer ||
      game.isEliminated(active->playerNumber)) {
    serialLog.print("ATLAS|GAME|WIN|IGNORED|SIGIL|");
    serialLog.println(sigilId);
    return;
  }
  dispatchSeatIntent(IntentType::ClaimWin, IntentOrigin::PhysicalSigil, *active,
      TurnHub::CLAIM_FROM_ARMED_PAUSE);
}

void processSigilEvents() {
  if (hubState != HubState::Lobby) sigilBus.closePairing();
  SigilEvent event;
  while (sigilBus.poll(event)) {
    if (event.sigilId >= MAX_PHYSICAL_SIGILS) continue;
    if (event.type == PacketType::Hello) {
      leds.invalidate(event.sigilId);
      continue;
    }
    TurnHub::recordActivity(activityKind(event.type), String("sigil=") + String(event.sigilId));
    if (connectionBlocked(event.sigilId)) continue;
    switch (event.type) {
      case PacketType::Pass: handlePass(event.sigilId); break;
      case PacketType::ActionDown: handleActionDown(event.sigilId); break;
      case PacketType::ActionUp: handleActionUp(event.sigilId); break;
      case PacketType::ActionShort: handleActionShort(event.sigilId); break;
      case PacketType::ActionLong: handleActionLong(event.sigilId); break;
      case PacketType::ActionWin: handleActionWin(event.sigilId); break;
      default: break;
    }
  }
}

}  // namespace TurnHubAtlas
