// Atlas's own device Intent handlers: pairing (open, confirm, forget,
// window), speaker, power, table code, upload approval, first-run setup,
// Return to lobby, factory reset and sleep. Games belong to the hub phone
// (hosted_game.h); "between games" here means no hosted game is under way.

#include <string.h>

#include "atlas_app.h"
#include "home_wifi.h"
#include "home_tls.h"
#include "atlas_battery.h"
#include "atlas_display.h"
#include "config.h"
#include "hosted_game.h"
#include "sigil_update_service.h"
#include "account_access.h"
#include "pairing_settings.h"
#include "speaker_settings.h"
#include "table_code_setting.h"
#include "upload_approval.h"
#include "wifi_password_store.h"
#include "profile_store.h"
#include "sd_card.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {


// Pairing is deliberately authorized by the Atlas hardware Intent.
IntentResult handlePairRequestIntent(const Intent &intent, void *) {
  if (intent.actor.origin != IntentOrigin::AtlasHardware) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Use Menu, then Pair a Sigil, on the Atlas screen");
  }
  if (!betweenGames(millis())) {
    return IntentResult::reject(IntentStatus::InvalidState, "Pair devices between games");
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

// Forgets one paired Sigil.
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

// Payload: value = Sigil ID or FORGET_ALL_SIGILS. Between games only, so no
// player loses their controller mid-game.
IntentResult handleForgetPairingIntent(const Intent &intent, void *) {
  if (sigilUpdatesBusy()) return IntentResult::reject(IntentStatus::Conflict, "Wait for the firmware update to finish");
  // The BOOT button held on Atlas itself (AtlasHardware) is physical presence.
  if (!adminIntent(intent) && intent.actor.origin != IntentOrigin::AtlasHardware) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Admin permission required");
  }
  if (!betweenGames(millis())) {
    return IntentResult::reject(IntentStatus::InvalidState, "Forget devices between games");
  }
  const int32_t target = intent.payload.value;
  const bool all = target == TurnHub::FORGET_ALL_SIGILS;
  if (!all && (target < 0 || target >= MAX_PHYSICAL_SIGILS || !sigilBus.record(target))) {
    return IntentResult::reject(IntentStatus::InvalidActor, "That Sigil is not paired with Atlas");
  }
  uint8_t paired = 0;
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if ((!all && id != target) || !sigilBus.record(id)) continue;
    // Nobody seated in the hub's game loses their controller.
    if (sigilInGame(id, millis())) {
      return IntentResult::reject(IntentStatus::Conflict,
          all ? "Players are seated on Sigils in the hub's game" : "That Sigil is in the hub's game");
    }
    ++paired;
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
  if (!betweenGames(millis())) {
    return IntentResult::reject(IntentStatus::InvalidState, "Pair devices between games");
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
// unaffected.
IntentResult handleConfigureSpeakerIntent(const Intent &intent, void *) {
  // At Atlas itself the volume is a knob anyone at the table may turn; remotely it is Admin's.
  if (intent.actor.origin != IntentOrigin::AtlasHardware && !adminIntent(intent)) {
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

// Payload: value = packed power choices (packPowerSettings). Like the
// speaker volume, anyone at the Atlas screen may change them; remotely they
// are Admin's.
IntentResult handleConfigurePowerIntent(const Intent &intent, void *) {
  if (intent.actor.origin != IntentOrigin::AtlasHardware && !adminIntent(intent)) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Admin permission required");
  }
  TurnHub::PowerSettings settings;
  if (!TurnHub::unpackPowerSettings(intent.payload.value, settings)) {
    return IntentResult::reject(IntentStatus::Rejected, "Unknown screen or sleep choice");
  }
  if (TurnHub::savePowerSettings(settings) != TurnHubStorage::Status::Ok) {
    return IntentResult::reject(IntentStatus::Rejected, "Screen and sleep settings could not be saved");
  }
  powerSettings = settings;
  char line[96];
  snprintf(line, sizeof(line), "ATLAS|POWER|SCREEN_OFF|%s|IN_GAMES|%s|AUTO_SLEEP|%s",
      TurnHub::SCREEN_OFF_CHOICES[settings.screenOff], TurnHub::GAME_SCREEN_CHOICES[settings.gameScreen],
      TurnHub::AUTO_SLEEP_CHOICES[settings.autoSleep]);
  serialLog.println(line);
  return IntentResult::accept("Screen and sleep saved");
}

// Payload: value = 1 to require a table presence code for protected device
// actions, 0 to stop. Turning it off while it is on takes a code the Admin
// actually entered, so a remote Admin cannot drop the extra step.
IntentResult handleConfigureTableCodeIntent(const Intent &intent, void *) {
  if (!adminIntent(intent)) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Admin permission required");
  }
  if (intent.payload.value != 0 && intent.payload.value != 1) {
    return IntentResult::reject(IntentStatus::Rejected, "Choose on or off");
  }
  const bool required = intent.payload.value == 1;
  if (required == tableCodeRequired) {
    return IntentResult::accept(required ? "The table code is already on" : "The table code is already off");
  }
  if (!required && presenceCodeRemainingMs(String(intent.payload.moderatorId), millis()) == 0) {
    return IntentResult::reject(IntentStatus::Unauthorized,
        "Verify at the table with a code before turning the table code off");
  }
  if (TurnHub::saveTableCodeRequired(required) != TurnHubStorage::Status::Ok) {
    return IntentResult::reject(IntentStatus::Rejected, "The table code setting could not be saved");
  }
  tableCodeRequired = required;
  // Grants made while it was off were never codes; start clean either way.
  resetPresence();
  serialLog.print("ATLAS|PRESENCE|REQUIRED|");
  serialLog.println(required ? "ON" : "OFF");
  return IntentResult::accept(required ? "Table code turned on" : "Table code turned off");
}

IntentResult handleConfigureUploadApprovalIntent(const Intent &intent, void *) {
  if (!adminIntent(intent)) return IntentResult::reject(IntentStatus::Unauthorized, "Admin permission required");
  if (intent.payload.value != 0 && intent.payload.value != 1)
    return IntentResult::reject(IntentStatus::Rejected, "Choose on or off");
  const bool required = intent.payload.value == 1;
  if (required != uploadApprovalRequired) {
    if (TurnHub::saveUploadApprovalRequired(required) != TurnHubStorage::Status::Ok)
      return IntentResult::reject(IntentStatus::Rejected, "Upload approval setting could not be saved");
    uploadApprovalRequired = required;
  }
  return IntentResult::accept(required ? "Upload approval turned on" : "Upload approval turned off");
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

// A Sigil is already paired, e.g. during the phone's Sigils step.
static bool sigilPaired() {
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if (sigilBus.record(id) != nullptr) return true;
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
    // The phone's last step: a verified Admin and a working home network.
    // Atlas restarts afterwards, so not mid-match.
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
    if (!betweenGames(millis())) {
      return IntentResult::reject(IntentStatus::InvalidState, "Finish setup between games");
    }
    if (!homeWifiEnabled() || !homeWifiConfigured() || !homeWifiConnected() ||
        !homeTlsRunning() || strlen(homeTlsPin()) != 64) {
      return IntentResult::reject(IntentStatus::Rejected, "Connect Atlas to your home Wi-Fi before finishing setup");
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

// An Admin's Return to lobby: the hub phone loses its hold on the table (its
// game stays on that phone, and here to resume), so another phone can host.
// It needs that Admin verified at the table (presence code), so someone is
// there, for example when a hub phone left with the table still held.
IntentResult handleResetTableIntent(const Intent &intent, void *) {
  if (!adminIntent(intent)) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Admin permission required");
  }
  if (!presenceConfirmedFor(String(intent.payload.moderatorId), millis())) {
    return IntentResult::reject(IntentStatus::Unauthorized,
        "Verify at the table first: enter the code the Atlas screen shows");
  }
  const bool held = TurnHubHosted::hostedTable().lease().held;
  TurnHubHosted::hostedTable().releaseAny();
  leds.invalidateAll();
  serialLog.print("ATLAS|ADMIN|RESET_TABLE|");
  serialLog.println(held ? "HUB_RELEASED" : "NO_HUB");
  return IntentResult::accept(held
      ? "The phone's hold on the table ended; another phone can host now"
      : "No phone holds the table");
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
    if (!atBootButton && !betweenGames(millis())) {
      return IntentResult::reject(IntentStatus::InvalidState, "Factory reset Atlas between games");
    }
    if (!atlasResetScheduled) {
      atlasResetScheduled = true;
      atlasResetAtMs = millis() + FACTORY_RESET_DELAY_MS;
      serialLog.println("ATLAS|FACTORY_RESET|ATLAS|SCHEDULED");
    }
    return IntentResult::accept("Atlas is erasing its settings and microSD card, then restarting");
  }
  if (!betweenGames(millis())) {
    return IntentResult::reject(IntentStatus::InvalidState, "Factory reset Sigils between games");
  }
  if (target < 0 || target >= MAX_PHYSICAL_SIGILS || !sigilBus.record(target)) {
    return IntentResult::reject(IntentStatus::InvalidActor, "That Sigil is not paired with Atlas");
  }
  const uint8_t id = static_cast<uint8_t>(target);
  if (sigilInGame(id, millis())) {
    return IntentResult::reject(IntentStatus::Conflict, "That Sigil is in the hub's game");
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
bool atlasSleepEmpty = false;
uint32_t atlasSleepAtMs = 0;
uint32_t lastActivityMs = 0;
bool emptySeen = false;
uint32_t emptySinceMs = 0;
bool emergencySeen = false;
uint32_t emergencySinceMs = 0;
}  // namespace

// Menu > Device Sleep: the touchscreen only (whoever taps it is at the
// table), between games, with no update or factory reset under way. Deep
// sleep loses RAM, so phones sign in again; nothing saved is touched. Sigils
// show Atlas lost until Atlas wakes.
// Auto sleep (System origin, serviceAutoSleep) follows the same rules, except
// that an empty cell sleeps mid-game too: the hub phone keeps the game.
IntentResult handleSleepIntent(const Intent &intent, void *) {
  const bool system = intent.actor.origin == IntentOrigin::System;
  const bool empty = system && intent.payload.value == SLEEP_REASON_EMPTY;
  if (intent.actor.origin != IntentOrigin::AtlasHardware && !system) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Sleep from the Atlas screen");
  }
  if (!empty && !betweenGames(millis())) {
    return IntentResult::reject(IntentStatus::InvalidState, "Put Atlas to sleep between games");
  }
  if (sigilUpdatesBusy() || ota.inProgress() || atlasResetScheduled) {
    return IntentResult::reject(IntentStatus::Conflict, "Wait for the update or reset to finish");
  }
  if (!atlasSleepScheduled) {
    atlasSleepScheduled = true;
    atlasSleepEmpty = empty;
    atlasSleepAtMs = millis() + SLEEP_DELAY_MS;
    serialLog.println(empty ? "ATLAS|SLEEP|SCHEDULED|BATTERY_EMPTY"
        : system ? "ATLAS|SLEEP|SCHEDULED|IDLE" : "ATLAS|SLEEP|SCHEDULED");
  }
  if (empty) return IntentResult::accept("Battery empty: sleeping. Plug in USB to wake");
  return IntentResult::accept("Going to sleep. Touch the screen to wake");
}

bool sleepScheduled() { return atlasSleepScheduled; }

void serviceSleep(uint32_t nowMs) {
  if (!atlasSleepScheduled || static_cast<int32_t>(nowMs - atlasSleepAtMs) < 0) return;
  atlasSleepScheduled = false;
  serialLog.println("ATLAS|SLEEP|ENTER");
  sleepAtlas(atlasSleepEmpty);
}

void noteAtlasActivity(uint32_t nowMs) { lastActivityMs = nowMs; }

void serviceAutoSleep(uint32_t nowMs) {
  if (atlasSleepScheduled) return;
  // Idle sleep requires inferred battery power. Severe undervoltage can
  // override the cold-boot USB assumption independently of shown charge.
  if (!atlasOnBattery() && !atlasBattery().emergencyLow) {
    lastActivityMs = nowMs;
    emptySeen = emergencySeen = false;
    return;
  }
  Intent intent;
  intent.type = IntentType::Sleep;
  intent.actor.origin = IntentOrigin::System;
  const TurnHub::BatteryReading &battery = atlasBattery();
  if (battery.present && battery.criticalLow) {
    if (!emptySeen) {
      emptySeen = true;
      emptySinceMs = nowMs;
    }
    if (battery.emergencyLow) {
      if (!emergencySeen) { emergencySeen = true; emergencySinceMs = nowMs; }
    } else emergencySeen = false;
    if (nowMs - emptySinceMs >= AtlasConfig::BATTERY_EMPTY_CONFIRM_MS ||
        (emergencySeen && nowMs - emergencySinceMs >= AtlasConfig::BATTERY_EMERGENCY_CONFIRM_MS)) {
      intent.payload.value = SLEEP_REASON_EMPTY;
      // Refused (an update is running): try again after another confirm wait.
      if (!intents.dispatch(intent).accepted()) emptySinceMs = emergencySinceMs = nowMs;
      return;
    }
  } else {
    emptySeen = emergencySeen = false;
  }
  // Auto sleep off (power_settings.h): only the empty cell sleeps Atlas.
  const uint32_t idleMs = TurnHub::autoSleepIdleMs(powerSettings);
  if (idleMs == 0) {
    lastActivityMs = nowMs;
    return;
  }
  if (nowMs - lastActivityMs < idleMs) return;
  // A full idle wait again before the next try, whatever happens now.
  lastActivityMs = nowMs;
  if (pairingActive || !betweenGames(nowMs)) return;
  intent.payload.value = SLEEP_REASON_IDLE;
  intents.dispatch(intent);
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

}  // namespace TurnHubAtlas
