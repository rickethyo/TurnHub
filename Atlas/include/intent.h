#pragma once

#include <stdint.h>

namespace TurnHub {

// Device requests to Atlas's own application layer: pairing, settings,
// first-run setup, resets and sleep.
//
// An Intent describes what a controller wants to happen. It does NOT describe
// transport details and it does NOT mutate state by itself. Games are not
// Intents: the hub runs them (hosted_game.h), and Sigil and touchscreen game
// input is queued for the hub as hosted inputs.
enum class IntentType : uint8_t {
  None = 0,

  // Pairing is table/device state owned by Atlas; it still follows the
  // request -> validate -> mutate rule.
  PairRequest,
  PairConfirm,
  ForgetPairing,

  // Admin: payload.value = pairing window in milliseconds.
  ConfigurePairing,
  // Admin: payload.value = Atlas speaker volume, 0 (off) to 3 (high).
  ConfigureSpeaker,
  // At the Atlas screen, or Admin: payload.value = packed screen-off,
  // screen-in-games and auto-sleep choices (power_settings.h).
  ConfigurePower,
  // Admin verified at the table (presence code): return the table to its
  // home screen, releasing a hosted game's lease first.
  ResetTable,
  // Admin at the table, between games: erase a device's saved settings (NVS).
  // payload.value = a Sigil ID, or FACTORY_RESET_ATLAS for Atlas itself.
  FactoryReset,
  // Presence-verified Admin: payload.value = paired Sigil ID.
  UpdateSigil,

  // First-run guided setup (setup_stage.h): payload.value = the next stage.
  // To Finished: a presence-verified Admin from a phone (payload.moderatorId).
  // To Complete: the Atlas touchscreen, or an Admin from a phone.
  AdvanceSetup,

  // Atlas touchscreen (Menu > Device), between games: deep sleep until the
  // screen is touched or BOOT is pressed. Waking restarts Atlas.
  Sleep,

  // Admin: payload.value = 1 to require a table presence code for protected
  // device actions, 0 to stop (needs a code while it is on).
  ConfigureTableCode,
  ConfigureUploadApproval,

  Count,
};

enum class IntentOrigin : uint8_t {
  Unknown = 0,
  PhysicalSigil,
  Browser,
  AtlasHardware,
  System,
};

// Who is asking. Adapters set the origin; account identity travels in the
// payload (moderatorId, profileId).
struct IntentActor {
  IntentOrigin origin = IntentOrigin::Unknown;
};

// Fixed-size generic payload keeps the application boundary transport-neutral
// and heap-free. Each IntentType documents which fields it uses.

// ForgetPairing payload.value: one Sigil ID, or FORGET_ALL_SIGILS. The
// admin's account ID travels in payload.moderatorId (as for ConfigurePairing).
constexpr int32_t FORGET_ALL_SIGILS = -1;
// FactoryReset payload.value for Atlas itself (otherwise a Sigil ID).
constexpr int32_t FACTORY_RESET_ATLAS = -1;
// PairConfirm payload.value: the waiting Sigil's slot, plus PAIR_CONFIRM_ACCEPT
// when the owner says the codes match (without it: reject). From the Atlas
// touchscreen, or an Admin's phone with moderatorId (as for ForgetPairing).
constexpr int32_t PAIR_CONFIRM_ACCEPT = 0x100;

struct IntentPayload {
  int32_t value = 0;
  char moderatorId[9] = {}; // Authenticated account issuing the request.
  char profileId[9] = {}; // Only populated by trusted Atlas authentication adapters.
};

struct Intent {
  IntentType type = IntentType::None;
  IntentActor actor{};
  IntentPayload payload{};
};

enum class IntentStatus : uint8_t {
  Accepted = 0,
  Rejected,
  Unsupported,
  InvalidActor,
  InvalidState,
  Unauthorized,
  Conflict,
};

struct IntentResult {
  IntentStatus status = IntentStatus::Unsupported;
  const char *message = "Intent not handled";

  IntentResult() = default;

  IntentResult(IntentStatus resultStatus, const char *resultMessage)
      : status(resultStatus), message(resultMessage) {}

  bool accepted() const {
    return status == IntentStatus::Accepted;
  }

  static IntentResult accept(const char *message = "Accepted") {
    return IntentResult(IntentStatus::Accepted, message);
  }

  static IntentResult reject(
      IntentStatus status,
      const char *message) {
    return IntentResult(status, message);
  }
};

inline const char *intentName(IntentType type) {
  switch (type) {
    case IntentType::None: return "NONE";
    case IntentType::PairRequest: return "PAIR_REQUEST";
    case IntentType::PairConfirm: return "PAIR_CONFIRM";
    case IntentType::ForgetPairing: return "FORGET_PAIRING";
    case IntentType::ConfigurePairing: return "CONFIGURE_PAIRING";
    case IntentType::ConfigureSpeaker: return "CONFIGURE_SPEAKER";
    case IntentType::ConfigurePower: return "CONFIGURE_POWER";
    case IntentType::ResetTable: return "RESET_TABLE";
    case IntentType::FactoryReset: return "FACTORY_RESET";
    case IntentType::UpdateSigil: return "UPDATE_SIGIL";
    case IntentType::AdvanceSetup: return "ADVANCE_SETUP";
    case IntentType::Sleep: return "SLEEP";
    case IntentType::ConfigureTableCode: return "CONFIGURE_TABLE_CODE";
    case IntentType::ConfigureUploadApproval: return "CONFIGURE_UPLOAD_APPROVAL";
    case IntentType::Count: return "COUNT";
    default: return "UNKNOWN";
  }
}

}  // namespace TurnHub
