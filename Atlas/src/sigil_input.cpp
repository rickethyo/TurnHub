// Physical Sigil adapter. Translates ESP-NOW events into Intents: every Sigil
// picks from the action menu Atlas offers (sigil_menu.cpp) and sends
// SelectAction, and its Left/Right keys send LifeAdjust and LifeResponse. The
// adapter never mutates canonical table state directly; audit_adapters.py
// enforces that.
#include "atlas_app.h"
#include "sigil_update_service.h"
#include "controller_profiles.h"
#include "harness_link.h"
#include "profile_picker.h"
#include "runtime_diagnostics.h"
#include "serial_log.h"
#include "sigil_menu.h"
#include "web_api.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

namespace {


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
  const IntentResult result = dispatchSeatIntent(type, IntentOrigin::PhysicalSigil, actor);

  if (!result.accepted()) {
    serialLog.print("ATLAS|INTENT|");
    serialLog.print(name);
    serialLog.print("|REJECTED|");
    serialLog.println(result.message);
  }
}

const char *activityKind(PacketType type) {
  switch (type) {

    case PacketType::SelectAction: return "sigil_menu";
    case PacketType::PickerKey: return "sigil_picker";
    case PacketType::LifeAdjust:
    case PacketType::LifeResponse: return "sigil_life";
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

// The menu's Pass: queues or cancels the pass for this Sigil's active seat.
void passFromMenu(uint8_t sigilId) {
  if (hubState != HubState::Running) return;
  const PlayerSeat *active = game.activePlayer();
  if (active == nullptr || active->controllerId != sigilId) return;
  logRejected("GAME|PASS", sigilId,
      dispatchSeatIntent(IntentType::Pass, IntentOrigin::PhysicalSigil, *active));
}

}  // namespace
void handleSelectAction(uint8_t sigilId, int32_t value) {
  using TurnHubProtocol::SigilAction;
  const uint8_t raw = TurnHubProtocol::selectedAction(value);
  // A choice from an out-of-date menu, or one no longer offered, is dropped
  // and the current menu resent; the player sees the new choices.
  if (TurnHubProtocol::selectedRevision(value) != sigilMenuRevision(sigilId) ||
      raw >= static_cast<uint8_t>(SigilAction::Count) ||
      (sigilMenuFor(sigilId).actions & (1u << raw)) == 0) {
    serialLog.print("ATLAS|MENU|STALE|");
    serialLog.print(sigilId);
    serialLog.print("|");
    serialLog.println(raw);
    invalidateSigilMenu(sigilId);
    return;
  }
  const SigilAction action = static_cast<SigilAction>(raw);
  serialLog.print("ATLAS|MENU|SELECT|");
  serialLog.print(sigilId);
  serialLog.print("|");
  serialLog.println(raw);
  switch (action) {
    case SigilAction::Join:
      // A picker Sigil chooses who joins first (profile_picker.cpp).
      if (pickerSigil(sigilId)) {
        openProfilePicker(sigilId, millis());
        break;
      }
      logRejected("MENU|JOIN", sigilId, dispatchModuleIntent(IntentType::Join, sigilId, 1));
      break;
    case SigilAction::CycleStarter:
      logRejected("MENU|STARTER", sigilId, dispatchModuleIntent(IntentType::SelectStarter, sigilId, 1,
          static_cast<int32_t>(TurnHub::StarterSelection::CycleModule)));
      break;
    case SigilAction::RandomStarter:
      logRejected("MENU|STARTER", sigilId, dispatchModuleIntent(IntentType::SelectStarter, sigilId, 1,
          static_cast<int32_t>(TurnHub::StarterSelection::Random)));
      break;
    case SigilAction::AddSeatB:
      // A picker Sigil chooses who sits in seat B, as Join does for seat A.
      if (pickerSigil(sigilId)) {
        openProfilePicker(sigilId, millis(), 2);
        break;
      }
      logRejected("MENU|SECONDARY", sigilId, dispatchModuleIntent(IntentType::Join, sigilId, 2));
      break;
    case SigilAction::RemoveSeatB:
      logRejected("MENU|SECONDARY", sigilId, dispatchModuleIntent(IntentType::Leave, sigilId, 2));
      break;
    case SigilAction::StartGame: {
      // The menu choice is the whole deliberate gesture: arm, then start.
      const IntentResult armed = dispatchModuleIntent(IntentType::ArmStart, sigilId);
      logRejected("MENU|START", sigilId,
          armed.accepted() ? dispatchModuleIntent(IntentType::StartGame, sigilId) : armed);
      break;
    }
    case SigilAction::CancelStart:
      logRejected("MENU|CANCEL_START", sigilId, dispatchModuleIntent(IntentType::CancelStart, sigilId));
      break;
    case SigilAction::Pass:
      passFromMenu(sigilId);
      break;
    case SigilAction::AdjustLife:
      // Not selectable: it only frees Left/Right, which send LifeAdjust packets.
      break;
    case SigilAction::SwitchSeat:
      // View state only (which seat the display and life keys follow).
      if (leds.switchShownSeat(sigilId, game)) invalidateSigilMenu(sigilId);
      break;
    case SigilAction::CancelPass:
      logRejected("MENU|CANCEL_PASS", sigilId, dispatchModuleIntent(IntentType::CancelPass, sigilId));
      break;
    case SigilAction::Pause:
      dispatchPauseOrResume(sigilId, IntentType::Pause);
      break;
    case SigilAction::Resume:
      dispatchPauseOrResume(sigilId, IntentType::Resume);
      break;
    case SigilAction::ClaimWin: {
      const PlayerSeat *active = game.activePlayer();
      if (active != nullptr && active->controllerId == sigilId) {
        logRejected("MENU|WIN", sigilId,
            dispatchSeatIntent(IntentType::ClaimWin, IntentOrigin::PhysicalSigil, *active));
      }
      break;
    }
    case SigilAction::ConfirmWin:
      respondToWinClaim(sigilId, IntentType::ConfirmWin);
      break;
    case SigilAction::DenyWin:
      respondToWinClaim(sigilId, IntentType::DenyWin);
      break;
    case SigilAction::BeginElimination:
      logRejected("MENU|ELIMINATE", sigilId, dispatchModuleIntent(IntentType::BeginElimination, sigilId));
      break;
    case SigilAction::NextTarget:
      logRejected("MENU|ELIMINATE", sigilId, dispatchModuleIntent(IntentType::CycleElimination, sigilId));
      break;
    case SigilAction::Eliminate:
      logRejected("MENU|ELIMINATE", sigilId, dispatchModuleIntent(IntentType::Eliminate, sigilId));
      break;
    case SigilAction::CancelElimination:
      logRejected("MENU|ELIMINATE", sigilId, dispatchModuleIntent(IntentType::CancelElimination, sigilId));
      break;
    case SigilAction::Rematch:
      logRejected("MENU|REMATCH", sigilId, dispatchModuleIntent(IntentType::Rematch, sigilId));
      break;
    case SigilAction::ResetTable:
      logRejected("MENU|RESET", sigilId, dispatchModuleIntent(IntentType::ResetGame, sigilId));
      break;
    case SigilAction::Leave:
      // Slot 1 leaves the whole Sigil, seat B included.
      logRejected("MENU|LEAVE", sigilId, dispatchModuleIntent(IntentType::Leave, sigilId, 1));
      break;
    case SigilAction::LinkPhone:
      // Proof of possession: only someone holding this Sigil can choose it.
      TurnHubWebApi::notePhysicalAction(sigilId);
      break;
    case SigilAction::Count:
      break;
  }
}

// LifeAdjust: a batched change to one of this Sigil's own players. The
// ChangeLife handler checks ownership, state and limits.
void handleLifeAdjust(uint8_t sigilId, int32_t value) {
  const uint8_t player = TurnHubProtocol::lifeAdjustPlayer(value);
  const int32_t delta = TurnHubProtocol::lifeAdjustDelta(value);
  const PlayerSeat *seat = game.playerByNumber(player);
  if (seat == nullptr || seat->controllerId != sigilId || delta == 0) {
    serialLog.print("ATLAS|LIFE|ADJUST|IGNORED|");
    serialLog.println(sigilId);
    return;
  }
  Intent intent;
  intent.type = IntentType::ChangeLife;
  intent.actor.origin = IntentOrigin::PhysicalSigil;
  intent.actor.controllerId = seat->controllerId;
  intent.actor.slot = seat->slot;
  intent.actor.playerNumber = seat->playerNumber;
  intent.payload.targetPlayer = player;
  intent.payload.value = delta;
  const IntentResult result = intents.dispatch(intent);
  serialLog.print("ATLAS|LIFE|ADJUST|SIGIL|");
  serialLog.print(sigilId);
  serialLog.print("|PLAYER|");
  serialLog.print(player);
  serialLog.print("|DELTA|");
  serialLog.println(delta);
  logRejected("LIFE|ADJUST", sigilId, result);
}

// LifeResponse: approve or deny the request this Sigil showed. The tag must
// match the pending request, so an answer never lands on a newer one.
void handleLifeResponse(uint8_t sigilId, int32_t value) {
  const uint8_t target = TurnHubProtocol::lifeResponseTarget(value);
  const PlayerSeat *seat = game.playerByNumber(target);
  const TurnHub::LifeChangeRequest *request = game.lifeChangeFor(target);
  if (seat == nullptr || seat->controllerId != sigilId || request == nullptr ||
      request->state != TurnHub::LifeChangeState::Pending ||
      (request->id & 0x3F) != TurnHubProtocol::lifeResponseTag(value)) {
    serialLog.print("ATLAS|LIFE|RESPONSE|STALE|");
    serialLog.println(sigilId);
    invalidateSigilMenu(sigilId);
    return;
  }
  Intent intent;
  intent.type = IntentType::RespondLifeChange;
  intent.actor.origin = IntentOrigin::PhysicalSigil;
  intent.actor.controllerId = seat->controllerId;
  intent.actor.slot = seat->slot;
  intent.actor.playerNumber = seat->playerNumber;
  intent.payload.requestId = request->id;
  intent.payload.flags = TurnHubProtocol::lifeResponseApprove(value) ? 1 : 0;
  logRejected("LIFE|RESPONSE", sigilId, intents.dispatch(intent));
}

void processSigilEvents() {
  if (hubState != HubState::Lobby) {
    sigilBus.closePairing();
    // A code check can't finish outside the lobby: tell each waiting Sigil no.
    if (sigilBus.pendingPairingCount() > 0) sigilBus.cancelPendingPairings();
  }
  SigilEvent event;
  while (sigilBus.poll(event)) {
    if (event.sigilId >= MAX_PHYSICAL_SIGILS) continue;
    if (event.type == PacketType::Hello) {
      leds.invalidate(event.sigilId);
      invalidateSigilMenu(event.sigilId);
      invalidateProfilePicker(event.sigilId);
      continue;
    }
    if (event.type == PacketType::SigilUpdateStatus) {
      noteSigilUpdateStatus(event.sigilId, event.value, millis());
      continue;
    }
    // Test-harness progress is shown on the touchscreen; it is not gameplay.
    if (event.type == PacketType::HarnessReport) {
      noteHarnessReport(event.sigilId, event.value, millis());
      continue;
    }
    TurnHub::recordActivity(activityKind(event.type), String("sigil=") + String(event.sigilId));
    if (connectionBlocked(event.sigilId)) continue;
    switch (event.type) {

      case PacketType::SelectAction: handleSelectAction(event.sigilId, event.value); break;
      case PacketType::PickerKey: handlePickerKey(event.sigilId, event.value, millis()); break;
      case PacketType::LifeAdjust: handleLifeAdjust(event.sigilId, event.value); break;
      case PacketType::LifeResponse: handleLifeResponse(event.sigilId, event.value); break;
      default: break;
    }
  }
}

}  // namespace TurnHubAtlas
