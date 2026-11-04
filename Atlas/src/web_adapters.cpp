// Browser adapter. TurnHubWebApi authenticates the HTTP session; these
// callbacks turn an Admin's device request into an Intent and report the
// dispatcher's message back. They never mutate device state directly;
// audit_adapters.py enforces that.

#include "atlas_app.h"
#include "runtime_diagnostics.h"

namespace TurnHubAtlas {

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
  hooks.required = []() { return tableCodeRequired; };
  return hooks;
}

bool manageDevices(const String &actor, IntentType type, int32_t value, String &message) {
  if (actor.length() != 8 ||
      (type != IntentType::ForgetPairing && type != IntentType::ConfigurePairing &&
       type != IntentType::ConfigureSpeaker && type != IntentType::ConfigurePower &&
       type != IntentType::ConfigureTableCode &&
       type != IntentType::ConfigureUploadApproval &&
       type != IntentType::ResetTable &&
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
  TurnHubWebApi::configureDevices(manageDevices, []() { return pairingWindowMs; });
  TurnHubWebApi::configurePresence(presenceHooks());
  TurnHubWebApi::UpdateNoticeHooks updates;
  updates.note = noteLatestFirmware;
  updates.latest = latestFirmware;
  updates.available = firmwareUpdatesAvailable;
  TurnHubWebApi::configureUpdateNotice(updates);
  TurnHubWebApi::configureSpeaker([]() { return audio.speakerVolume(); });
  TurnHubWebApi::configureSetup([]() { return static_cast<uint8_t>(setupStage); });
  TurnHubWebApi::configureClientState(clientSnapshot, clientRevision);
}

}  // namespace TurnHubAtlas
