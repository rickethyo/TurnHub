// Browser adapter. TurnHubWebApi authenticates the HTTP session and resolves
// its seat; these callbacks turn the request into an Intent (or a read-only
// snapshot) and report the dispatcher's message back to the page. They never
// mutate canonical table state directly; audit_adapters.py enforces that.

#include "atlas_app.h"
#include "runtime_diagnostics.h"

namespace TurnHubAtlas {

namespace {

// Builds a Browser Intent for the authenticated seat and dispatches it.
bool dispatchBrowserSeatIntent(uint8_t controller, uint8_t slot, IntentType type,
    const TurnHub::IntentPayload &payload, String &message) {
  PlayerSeat seat;
  if (!seatForModuleSlot(controller, slot, seat)) {
    message = "Join the table first";
    return false;
  }
  Intent intent;
  intent.type = type;
  intent.actor.origin = IntentOrigin::Browser;
  intent.actor.controllerId = controller;
  intent.actor.slot = slot;
  intent.actor.playerNumber = seat.playerNumber;
  intent.payload = payload;
  // Life and Commander damage always apply to the authenticated participant.
  if (type == IntentType::ChangeLife || type == IntentType::ChangeCounter) {
    intent.payload.targetPlayer = seat.playerNumber;
  }
  const IntentResult result = intents.dispatch(intent);
  message = result.message;
  return result.accepted();
}

// Session controls that map one-to-one onto a seat Intent. Join, Leave and
// AttachPhysical go through handleProfileControl instead.
bool seatIntentForControl(WebControl control, IntentType &type) {
  switch (control) {
    case WebControl::Pass: type = IntentType::Pass; return true;
    case WebControl::PauseResume: type = IntentType::TogglePause; return true;
    case WebControl::Concede: type = IntentType::Concede; return true;
    case WebControl::ClaimWin: type = IntentType::ClaimWin; return true;
    case WebControl::ConfirmWin: type = IntentType::ConfirmWin; return true;
    case WebControl::DenyWin: type = IntentType::DenyWin; return true;
    case WebControl::SelectStarter: type = IntentType::SelectStarter; return true;
    case WebControl::Start: type = IntentType::StartGame; return true;
    case WebControl::CancelStart: type = IntentType::CancelStart; return true;
    case WebControl::Rematch: type = IntentType::Rematch; return true;
    case WebControl::Reset: type = IntentType::ResetGame; return true;
    case WebControl::Nudge: type = IntentType::NudgePlayer; return true;
    case WebControl::Join:
    case WebControl::Leave:
    case WebControl::AttachPhysical:
    case WebControl::MoveEarlier:
    case WebControl::MoveLater:
      return false;
  }
  return false;
}

}  // namespace

bool resolveWebSeat(uint8_t controllerId, uint8_t slot, SeatSnapshot &snapshot) {
  PlayerSeat seat;
  if (!seatForModuleSlot(controllerId, slot, seat)) {
    snapshot = SeatSnapshot{};
    return false;
  }
  snapshot.exists = true;
  snapshot.playerNumber = seat.playerNumber;
  snapshot.active = false;
  snapshot.eliminated = false;
  // "host" now means this seat may use table actions (start, rematch, reset,
  // next-game settings): every seated player may, since 2026-09-25.
  snapshot.host = true;
  snapshot.lifeAvailable = table().game.hasPlayers();
  snapshot.life = table().game.lifeTotal(seat.playerNumber);
  if (table().game.hasPlayers()) {
    snapshot.eliminated = table().game.isEliminated(seat.playerNumber);
    snapshot.active = table().game.hasTurn(seat.playerNumber) && !snapshot.eliminated;
    snapshot.winner = table().game.isWinner(seat.playerNumber);
    snapshot.team = table().game.teamOf(seat.playerNumber);
  } else if (nextGameSettings.twoHeadedGiant) {
    snapshot.team = static_cast<uint8_t>((seat.playerNumber + 1) / TurnHub::TEAM_SIZE);
  }
  return true;
}

bool handleWebControl(uint8_t controllerId, uint8_t slot, WebControl control, String &message) {
  PlayerSeat seat;
  if (!seatForModuleSlot(controllerId, slot, seat)) {
    message = "This seat is no longer at the table";
    return false;
  }
  if (control == WebControl::MoveEarlier || control == WebControl::MoveLater) {
    // Only the tablet route offers these (a session verified at the table).
    Intent intent;
    intent.type = IntentType::MoveSeat;
    intent.actor.origin = IntentOrigin::TableTablet;
    intent.payload.targetPlayer = seat.playerNumber;
    intent.payload.value = control == WebControl::MoveEarlier ? -1 : 1;
    const IntentResult result = intents.dispatch(intent);
    message = result.message;
    return result.accepted();
  }
  IntentType type;
  if (!seatIntentForControl(control, type)) {
    message = "Use authenticated profile participation";
    return false;
  }
  const IntentResult result = dispatchSeatIntent(type, IntentOrigin::Browser, seat);
  message = result.message;
  return result.accepted();
}

// Join/Leave come from the signed-in browser; AttachPhysical is the Sigil
// confirmation step and is issued with PhysicalSigil origin.
bool handleProfileControl(const String &profileId, WebControl control,
    uint8_t controllerId, uint8_t slot, String &message) {
  Intent intent;
  intent.type = control == WebControl::Join ? IntentType::JoinProfile :
      control == WebControl::Leave ? IntentType::LeaveProfile : IntentType::BindProfile;
  intent.actor.origin =
      control == WebControl::AttachPhysical ? IntentOrigin::PhysicalSigil : IntentOrigin::Browser;
  intent.actor.controllerId = controllerId;
  intent.actor.slot = slot;
  strncpy(intent.payload.profileId, profileId.c_str(), sizeof(intent.payload.profileId) - 1);
  const IntentResult result = intents.dispatch(intent);
  message = result.message;
  return result.accepted();
}

bool readGameSettings(TurnHub::GameSettings &settings, bool &editable) {
  settings = table().game.hasPlayers() ? table().game.settings() : nextGameSettings;
  editable = table().hubState == HubState::Lobby;
  return gameSettingsAvailable;
}

bool configureGame(uint8_t controller, uint8_t slot,
    const TurnHub::GameSettings &settings, String &message) {
  TurnHub::IntentPayload payload;
  payload.flags = static_cast<uint32_t>(settings.profile) |
      (settings.twoHeadedGiant ? TurnHub::GAME_FLAG_TWO_HEADED_GIANT : 0);
  payload.value = settings.startingLife;
  payload.durationMs = settings.turnTimerMs;
  return dispatchBrowserSeatIntent(controller, slot, IntentType::ConfigureGame, payload, message);
}

bool changeLife(uint8_t controller, uint8_t slot, int32_t delta, String &message) {
  TurnHub::IntentPayload payload;
  payload.value = delta;
  return dispatchBrowserSeatIntent(controller, slot, IntentType::ChangeLife, payload, message);
}

bool readCounters(uint8_t controller, uint8_t slot, TurnHubWebApi::CounterSnapshot &snapshot) {
  PlayerSeat seat;
  if (!table().game.hasPlayers() || !seatForModuleSlot(controller, slot, seat)) return false;
  // This is an already-owned workspace. Aggregate assignment creates a
  // second ~470-byte snapshot on the pinned Xtensa compiler's stack.
  snapshot.editable = snapshot.commanderEnabled = false;
  snapshot.player = snapshot.playerCount = 0;
  for (auto &source : snapshot.sources) source = 0;
  for (auto &row : snapshot.damage) for (auto &value : row) value = 0;
  for (auto &request : snapshot.requests) request = TurnHub::LifeChangeRequest{};
  snapshot.player = seat.playerNumber;
  snapshot.playerCount = table().game.playerCount();
  snapshot.editable = (table().hubState == HubState::Running || table().hubState == HubState::Paused) &&
      !table().game.isEliminated(seat.playerNumber) && !table().eliminationTargetPlayer && !table().game.hasWinClaim();
  snapshot.commanderEnabled = table().game.settings().profile == TurnHub::GameProfile::Commander;
  for (uint8_t i = 0; i < table().game.playerCount(); ++i) {
    const auto source = table().game.playerAt(i)->playerNumber;
    snapshot.sources[i] = source;
    for (uint8_t c = 0; c < TurnHub::COMMANDERS_PER_PLAYER; ++c) {
      snapshot.damage[i][c] = table().game.commanderDamage(seat.playerNumber, source, c + 1);
    }
    // Only requests this participant made or must answer are visible.
    const auto *request = table().game.lifeChangeFor(source);
    if (request && (request->actor == seat.playerNumber || request->target == seat.playerNumber)) {
      snapshot.requests[i] = *request;
    }
  }
  return true;
}

bool changeCounter(uint8_t controller, uint8_t slot, IntentType type,
    const TurnHub::IntentPayload &payload, String &message) {
  return dispatchBrowserSeatIntent(controller, slot, type, payload, message);
}

bool moderateAccount(const String &actor, const String &target,
    const String &action, String &message) {
  using TurnHub::ModerationAction;
  if (actor.length() != 8 || target.length() != 8) {
    message = "Invalid account";
    return false;
  }
  Intent intent;
  intent.type = IntentType::Moderate;
  intent.actor.origin = IntentOrigin::Browser;
  strncpy(intent.payload.moderatorId, actor.c_str(), sizeof(intent.payload.moderatorId) - 1);
  strncpy(intent.payload.profileId, target.c_str(), sizeof(intent.payload.profileId) - 1);
  static const struct { const char *name; ModerationAction action; } ACTION_NAMES[] = {
      {"reset", ModerationAction::ResetConnections},
      {"remove", ModerationAction::RemovePlayer},
      {"pass", ModerationAction::PassTurn},
      {"mute", ModerationAction::MuteNudges},
      {"unmute", ModerationAction::UnmuteNudges},
  };
  intent.payload.value = -1;  // Unknown names are rejected as unsupported.
  for (const auto &entry : ACTION_NAMES) {
    if (action == entry.name) intent.payload.value = static_cast<int32_t>(entry.action);
  }

  const IntentResult result = intents.dispatch(intent);
  message = result.message;
  TurnHub::recordActivity(result.accepted() ? "moderation" : "moderation_rejected",
      String("actor=") + actor + " target=" + target + " action=" + action +
          " result=" + result.message);
  return result.accepted();
}

// Table presence for the web API: authorization state in front_panel.cpp,
// not table state, so it bypasses the Intent path like the old unlock window.
TurnHubWebApi::PresenceHooks presenceHooks() {
  TurnHubWebApi::PresenceHooks hooks;
  hooks.remainingMs = [](const String &profile) { return presenceRemainingMs(profile, millis()); };
  hooks.request = [](const String &profile, bool setup) { return requestPresenceCode(profile, setup, millis()); };
  hooks.confirm = [](const String &profile, uint32_t code) {
    return static_cast<TurnHubWebApi::PresenceResult>(confirmPresenceCode(profile, code, millis()));
  };
  hooks.revoke = [](const String &profile) { revokePresence(profile); };
  return hooks;
}

bool manageDevices(const String &actor, IntentType type, int32_t value, String &message) {
  if (actor.length() != 8 ||
      (type != IntentType::ForgetPairing && type != IntentType::ConfigurePairing &&
       type != IntentType::ConfigureSpeaker && type != IntentType::ResetTable &&
       type != IntentType::FactoryReset && type != IntentType::PairConfirm && type != IntentType::UpdateSigil &&
       type != IntentType::AdvanceSetup)) {
    message = "Invalid request";
    return false;
  }
  Intent intent;
  intent.type = type;
  intent.actor.origin = IntentOrigin::Browser;
  strncpy(intent.payload.moderatorId, actor.c_str(), sizeof(intent.payload.moderatorId) - 1);
  intent.payload.value = value;
  const IntentResult result = intents.dispatch(intent);
  message = result.message;
  TurnHub::recordActivity(result.accepted() ? "devices" : "devices_rejected",
      String("actor=") + actor + " intent=" + TurnHub::intentName(type) +
          " value=" + String(value) + " result=" + result.message);
  return result.accepted();
}

void registerWebCallbacks() {
  TurnHubWebApi::configure(resolveWebSeat, handleWebControl, handleProfileControl,
      resolveProfileParticipant);
  TurnHubWebApi::configureGameControls(readGameSettings, configureGame, changeLife);
  TurnHubWebApi::configureCounterControls(readCounters, changeCounter);
  TurnHubWebApi::configureModeration(moderateAccount);
  TurnHubWebApi::configureDevices(manageDevices, []() { return pairingWindowMs; });
  TurnHubWebApi::configureAccessibility([]() { applyAllSigilAccessibility(millis()); });
  TurnHubWebApi::configurePresence(presenceHooks());
  TurnHubWebApi::UpdateNoticeHooks updates;
  updates.note = noteLatestFirmware;
  updates.latest = latestFirmware;
  updates.available = firmwareUpdatesAvailable;
  TurnHubWebApi::configureUpdateNotice(updates);
  TurnHubWebApi::configureSpeaker([]() { return audio.speakerVolume(); });
  TurnHubWebApi::configureSetup([]() { return static_cast<uint8_t>(setupStage); });
  TurnHubWebApi::configureClientState(clientSnapshot, clientRevision);
  TurnHubWebApi::TableHooks games;
  games.count = MAX_GAME_TABLES;
  games.profileTable = tableForProfile;
  games.select = selectTable;
  TurnHubWebApi::configureTables(games);
}

}  // namespace TurnHubAtlas
