#pragma once

#include <stdint.h>

namespace TurnHub {

// Semantic requests sent to the authoritative Atlas application layer.
//
// An Intent describes what a controller wants to happen. It does NOT describe
// transport details and it does NOT mutate game state by itself. Physical
// Sigils, browser sessions, the Atlas master control, simulators, and future
// applications should converge on these operations before game rules run.
enum class IntentType : uint8_t {
  None = 0,

  // Core game actions.
  Pass,
  Pause,
  Resume,
  TogglePause,
  Concede,
  ClaimWin,
  ConfirmWin,
  DenyWin,

  // Lobby / lifecycle actions.
  Join,
  Leave,
  SelectStarter,
  StartGame,
  Rematch,
  ResetGame,

  // Game-profile actions. Payload meaning is defined by the handler.
  ChangeLife,
  ChangeCounter,

  // Non-state-changing table interaction.
  NudgePlayer,
  NudgeTable,

  // Device-management actions. Pairing is table/device state owned by Atlas,
  // not game state, but it still follows the request -> validate -> mutate rule.
  PairRequest,
  PairConfirm,
  ForgetPairing,

  Count,
};

enum class IntentOrigin : uint8_t {
  Unknown = 0,
  PhysicalSigil,
  Browser,
  AtlasHardware,
  Simulator,
  System,
};

// Identifies the controller/seat making the request. Fields may be unresolved
// when an adapter first constructs an Intent. Atlas is responsible for mapping
// device/session identity to the canonical Player where required.
struct IntentActor {
  IntentOrigin origin = IntentOrigin::Unknown;
  uint8_t moduleId = 0xFF;
  uint8_t slot = 0;
  uint8_t playerNumber = 0;
};

// Fixed-size generic payload keeps the application boundary transport-neutral
// and heap-free. Each IntentType documents which fields it uses as it is
// migrated into the dispatcher.
struct IntentPayload {
  uint8_t targetPlayer = 0;
  int32_t value = 0;
  uint32_t flags = 0;
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
    case IntentType::Pass: return "PASS";
    case IntentType::Pause: return "PAUSE";
    case IntentType::Resume: return "RESUME";
    case IntentType::TogglePause: return "TOGGLE_PAUSE";
    case IntentType::Concede: return "CONCEDE";
    case IntentType::ClaimWin: return "CLAIM_WIN";
    case IntentType::ConfirmWin: return "CONFIRM_WIN";
    case IntentType::DenyWin: return "DENY_WIN";
    case IntentType::Join: return "JOIN";
    case IntentType::Leave: return "LEAVE";
    case IntentType::SelectStarter: return "SELECT_STARTER";
    case IntentType::StartGame: return "START_GAME";
    case IntentType::Rematch: return "REMATCH";
    case IntentType::ResetGame: return "RESET_GAME";
    case IntentType::ChangeLife: return "CHANGE_LIFE";
    case IntentType::ChangeCounter: return "CHANGE_COUNTER";
    case IntentType::NudgePlayer: return "NUDGE_PLAYER";
    case IntentType::NudgeTable: return "NUDGE_TABLE";
    case IntentType::PairRequest: return "PAIR_REQUEST";
    case IntentType::PairConfirm: return "PAIR_CONFIRM";
    case IntentType::ForgetPairing: return "FORGET_PAIRING";
    case IntentType::Count: return "COUNT";
    default: return "UNKNOWN";
  }
}

}  // namespace TurnHub
