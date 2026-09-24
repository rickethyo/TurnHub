#pragma once

#include <stdint.h>

namespace TurnHub {

// Semantic requests sent to the authoritative Atlas application layer.
//
// An Intent describes what a controller wants to happen. It does NOT describe
// transport details and it does NOT mutate game state by itself. Physical
// Sigils, browser sessions, the Atlas master control, native applications,
// simulators, and future clients should converge on these operations before
// game rules run.
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

  // Atlas decision and deferred-transition requests (not radio packet IDs).
  CancelPass,
  CommitPass,
  ArmStart,
  CancelStart,
  CompleteStart,
  BeginElimination,
  CycleElimination,
  CancelElimination,
  Eliminate,

  JoinProfile,
  LeaveProfile,
  BindProfile,
  ConfigureGame,
  Moderate,
  RequestLifeChange,
  RespondLifeChange,
  ExpireLifeChanges,
  // Atlas master-button hold: end the running or paused match as a draw.
  EndMatch,
  // Admin: payload.value = pairing window in milliseconds.
  ConfigurePairing,

  Count,
};

enum class IntentOrigin : uint8_t {
  Unknown = 0,
  PhysicalSigil,
  Browser,
  AndroidApp,
  AtlasHardware,
  Simulator,
  System,
};

// Identifies the controller/seat making the request. Fields may be unresolved
// when an adapter first constructs an Intent. Atlas is responsible for mapping
// device/session identity to the canonical Player where required.
struct IntentActor {
  IntentOrigin origin = IntentOrigin::Unknown;
  uint8_t controllerId = 0xFF;
  uint8_t slot = 0;
  uint8_t playerNumber = 0;
};

// Fixed-size generic payload keeps the application boundary transport-neutral
// and heap-free. Each IntentType documents which fields it uses as it is
// migrated into the dispatcher.
// ClaimWin flag: complete an armed pause-to-claim gesture. Atlas verifies its
// arm and restores running play on denial, unlike an ordinary paused claim.
constexpr uint32_t CLAIM_FROM_ARMED_PAUSE = 1U;
// Pause flag for a gesture that can continue into a win claim. This is semantic
// gesture context, independent of transport or IntentOrigin.
constexpr uint32_t ARM_WIN_ON_PAUSE = 1U;

// SelectStarter payload.value: exact actor seat, cycle the module's seats,
// or host-requested random choice. Join/Leave actor.slot: 1 = module, 2 =
// secondary seat. Lifecycle module requests use playerNumber=0 (unresolved).
enum class StarterSelection : int32_t { ExactSeat = 0, CycleModule = 1, Random = 2 };

// ForgetPairing payload.value: one Sigil ID, or FORGET_ALL_SIGILS. The
// admin's account ID travels in payload.moderatorId (as for ConfigurePairing).
constexpr int32_t FORGET_ALL_SIGILS = -1;

// Moderate payload.value. moderatorId and profileId name the accounts.
enum class ModerationAction : int32_t {
  ResetConnections = 0,  // Force the target to sign in again.
  RemovePlayer = 1,      // Leave the lobby, or concede an active game.
  PassTurn = 2,          // Pass for the active player immediately.
  MuteNudges = 3,
  UnmuteNudges = 4,
};

struct IntentPayload {
  uint8_t targetPlayer = 0;
  int32_t value = 0;
  uint32_t flags = 0;
  uint32_t requestId = 0; // RespondLifeChange: exact pending approval.
  uint8_t counterSource = 0; // ChangeCounter: owner of the commander.
  uint8_t counterSlot = 1; // ChangeCounter: commander 1 or 2.
  uint32_t durationMs = 0; // ConfigureGame: turn timer (0 = OFF).
  char moderatorId[9] = {}; // Authenticated account issuing moderation.
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
    case IntentType::RequestLifeChange: return "REQUEST_LIFE_CHANGE";
    case IntentType::RespondLifeChange: return "RESPOND_LIFE_CHANGE";
    case IntentType::ExpireLifeChanges: return "EXPIRE_LIFE_CHANGES";
    case IntentType::ChangeCounter: return "CHANGE_COUNTER";
    case IntentType::NudgePlayer: return "NUDGE_PLAYER";
    case IntentType::NudgeTable: return "NUDGE_TABLE";
    case IntentType::PairRequest: return "PAIR_REQUEST";
    case IntentType::PairConfirm: return "PAIR_CONFIRM";
    case IntentType::ForgetPairing: return "FORGET_PAIRING";
    case IntentType::CancelPass: return "CANCEL_PASS";
    case IntentType::CommitPass: return "COMMIT_PASS";
    case IntentType::ArmStart: return "ARM_START";
    case IntentType::CancelStart: return "CANCEL_START";
    case IntentType::CompleteStart: return "COMPLETE_START";
    case IntentType::BeginElimination: return "BEGIN_ELIMINATION";
    case IntentType::CycleElimination: return "CYCLE_ELIMINATION";
    case IntentType::CancelElimination: return "CANCEL_ELIMINATION";
    case IntentType::Eliminate: return "ELIMINATE";
    case IntentType::JoinProfile: return "JOIN_PROFILE";
    case IntentType::LeaveProfile: return "LEAVE_PROFILE";
    case IntentType::BindProfile: return "BIND_PROFILE";
    case IntentType::ConfigureGame: return "CONFIGURE_GAME";
    case IntentType::Moderate: return "MODERATE";
    case IntentType::EndMatch: return "END_MATCH";
    case IntentType::ConfigurePairing: return "CONFIGURE_PAIRING";
    case IntentType::Count: return "COUNT";
    default: return "UNKNOWN";
  }
}

}  // namespace TurnHub
