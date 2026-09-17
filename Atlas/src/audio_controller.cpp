#include "audio_controller.h"

#include "protocol.h"

namespace TurnHub {

namespace {

struct AudioNote {
  uint16_t frequencyHz;
  uint16_t durationMs;
  uint16_t gapMs;
};

struct PatternView {
  const AudioNote *notes;
  uint8_t count;
};

constexpr AudioNote PLAYER_JOINED[] = {
    {800, 60, 35},
    {1100, 90, 0},
};

constexpr AudioNote SHARED_ADDED[] = {
    {700, 65, 35},
    {1050, 65, 35},
    {1450, 120, 0},
};

constexpr AudioNote SHARED_REMOVED[] = {
    {1450, 65, 35},
    {1000, 65, 35},
    {650, 120, 0},
};

constexpr AudioNote SAME_MODULE_PASS[] = {
    {950, 65, 40},
    {1250, 95, 0},
};

constexpr AudioNote STARTER_SELECTED[] = {
    {1100, 70, 35},
    {1500, 110, 0},
};

constexpr AudioNote RANDOM_STARTER[] = {
    {750, 55, 35},
    {950, 55, 35},
    {1200, 55, 35},
    {1550, 120, 0},
};

constexpr AudioNote START_ARMED[] = {
    {650, 120, 0},
};

constexpr AudioNote COUNTDOWN_CANCELLED[] = {
    {650, 80, 35},
    {400, 130, 0},
};

constexpr AudioNote COUNTDOWN_1[] = {{1000, 120, 0}};
constexpr AudioNote COUNTDOWN_2[] = {{1300, 120, 0}};
constexpr AudioNote COUNTDOWN_3[] = {{1700, 120, 0}};
constexpr AudioNote TURN_PASS[] = {{1000, 80, 0}};

constexpr AudioNote PAUSE[] = {
    {1000, 90, 35},
    {650, 140, 0},
};

constexpr AudioNote RESUME[] = {
    {650, 90, 35},
    {1000, 140, 0},
};

constexpr AudioNote WARNING[] = {{500, 180, 0}};

constexpr AudioNote GAME_START[] = {
    {700, 100, 50},
    {1000, 100, 50},
    {1400, 180, 0},
};

constexpr AudioNote GAME_OVER[] = {
    {900, 150, 50},
    {1200, 150, 50},
    {1500, 150, 0},
};

constexpr AudioNote NUDGE[] = {
    {1250, 65, 35},
    {1250, 65, 0},
};

constexpr AudioNote NUDGE_TABLE[] = {
    {900, 55, 35},
    {1250, 55, 35},
    {1650, 90, 0},
};

constexpr AudioNote ELIMINATION_ARMED[] = {
    {900, 70, 40},
    {650, 110, 0},
};

constexpr AudioNote ELIMINATION_CHANGED[] = {
    {700, 55, 30},
    {1050, 80, 0},
};

constexpr AudioNote ELIMINATION_CANCELLED[] = {
    {500, 70, 35},
    {850, 95, 0},
};

constexpr AudioNote PLAYER_ELIMINATED[] = {
    {1500, 90, 45},
    {1000, 110, 45},
    {550, 180, 0},
};

constexpr AudioNote WIN_CLAIMED[] = {
    {900, 80, 40},
    {1250, 80, 40},
    {1650, 140, 0},
};

constexpr AudioNote WIN_CONFIRMED[] = {
    {1050, 70, 35},
    {1450, 110, 0},
};

constexpr AudioNote WIN_DENIED[] = {
    {1150, 75, 35},
    {700, 75, 35},
    {420, 140, 0},
};

constexpr AudioNote WIN_CANCELLED[] = {
    {850, 70, 35},
    {600, 105, 0},
};

template <size_t N>
constexpr PatternView pattern(const AudioNote (&notes)[N]) {
  return PatternView{notes, static_cast<uint8_t>(N)};
}

PatternView patternFor(SoundId sound) {
  switch (sound) {
    case SoundId::PlayerJoined: return pattern(PLAYER_JOINED);
    case SoundId::SharedPlayerAdded: return pattern(SHARED_ADDED);
    case SoundId::SharedPlayerRemoved: return pattern(SHARED_REMOVED);
    case SoundId::SameModulePass: return pattern(SAME_MODULE_PASS);
    case SoundId::StarterSelected: return pattern(STARTER_SELECTED);
    case SoundId::RandomStarter: return pattern(RANDOM_STARTER);
    case SoundId::StartArmed: return pattern(START_ARMED);
    case SoundId::CountdownCancelled: return pattern(COUNTDOWN_CANCELLED);
    case SoundId::Countdown1: return pattern(COUNTDOWN_1);
    case SoundId::Countdown2: return pattern(COUNTDOWN_2);
    case SoundId::Countdown3: return pattern(COUNTDOWN_3);
    case SoundId::TurnPass: return pattern(TURN_PASS);
    case SoundId::Pause: return pattern(PAUSE);
    case SoundId::Resume: return pattern(RESUME);
    case SoundId::Warning: return pattern(WARNING);
    case SoundId::GameStart: return pattern(GAME_START);
    case SoundId::GameOver: return pattern(GAME_OVER);
    case SoundId::Nudge: return pattern(NUDGE);
    case SoundId::NudgeTable: return pattern(NUDGE_TABLE);
    case SoundId::EliminationArmed: return pattern(ELIMINATION_ARMED);
    case SoundId::EliminationChanged: return pattern(ELIMINATION_CHANGED);
    case SoundId::EliminationCancelled: return pattern(ELIMINATION_CANCELLED);
    case SoundId::PlayerEliminated: return pattern(PLAYER_ELIMINATED);
    case SoundId::WinClaimed: return pattern(WIN_CLAIMED);
    case SoundId::WinConfirmed: return pattern(WIN_CONFIRMED);
    case SoundId::WinDenied: return pattern(WIN_DENIED);
    case SoundId::WinCancelled: return pattern(WIN_CANCELLED);
    default: return PatternView{nullptr, 0};
  }
}

}  // namespace

AudioController::AudioController(SigilBus &bus)
    : bus_(bus) {}

uint16_t AudioController::maskForSigil(uint8_t sigilId) {
  if (sigilId >= MAX_PHYSICAL_SIGILS) {
    return 0;
  }
  return static_cast<uint16_t>(1u << sigilId);
}

bool AudioController::play(SoundId sound, uint16_t targetMask) {
  if (targetMask == 0) {
    return false;
  }

  if (queueCount_ >= QUEUE_CAPACITY) {
    Serial.println("ATLAS|AUDIO|QUEUE_FULL");
    return false;
  }

  queue_[queueTail_] = Job{sound, targetMask};
  queueTail_ = static_cast<uint8_t>((queueTail_ + 1) % QUEUE_CAPACITY);
  ++queueCount_;
  return true;
}

void AudioController::clear() {
  queueHead_ = 0;
  queueTail_ = 0;
  queueCount_ = 0;
  active_ = false;
  noteIndex_ = 0;
  nextNoteAtMs_ = 0;
}

bool AudioController::beginNext(uint32_t nowMs) {
  if (queueCount_ == 0) {
    return false;
  }

  current_ = queue_[queueHead_];
  queueHead_ = static_cast<uint8_t>((queueHead_ + 1) % QUEUE_CAPACITY);
  --queueCount_;

  active_ = true;
  noteIndex_ = 0;
  nextNoteAtMs_ = nowMs;
  return true;
}

void AudioController::update(uint32_t nowMs) {
  if (!active_ && !beginNext(nowMs)) {
    return;
  }

  if (static_cast<int32_t>(nowMs - nextNoteAtMs_) < 0) {
    return;
  }

  const PatternView view = patternFor(current_.sound);
  if (view.notes == nullptr || noteIndex_ >= view.count) {
    active_ = false;
    beginNext(nowMs);
    return;
  }

  const AudioNote &note = view.notes[noteIndex_];
  sendTone(current_.targetMask, note.frequencyHz, note.durationMs);
  nextNoteAtMs_ = nowMs + note.durationMs + note.gapMs;
  ++noteIndex_;
}

void AudioController::sendTone(
    uint16_t targetMask,
    uint16_t frequencyHz,
    uint16_t durationMs) {
  const int32_t payload = TurnHubProtocol::encodeTone(frequencyHz, durationMs);

  for (uint8_t sigilId = 0; sigilId < MAX_PHYSICAL_SIGILS; ++sigilId) {
    if ((targetMask & maskForSigil(sigilId)) == 0) {
      continue;
    }
    bus_.buzzer(sigilId, payload);
  }
}

void AudioController::playerJoined(uint8_t sigilId) {
  play(SoundId::PlayerJoined, maskForSigil(sigilId));
}

void AudioController::sharedPlayerAdded(uint8_t sigilId) {
  play(SoundId::SharedPlayerAdded, maskForSigil(sigilId));
}

void AudioController::sharedPlayerRemoved(uint8_t sigilId) {
  play(SoundId::SharedPlayerRemoved, maskForSigil(sigilId));
}

void AudioController::sameModulePass(uint8_t sigilId) {
  play(SoundId::SameModulePass, maskForSigil(sigilId));
}

void AudioController::starterSelected(uint8_t sigilId) {
  play(SoundId::StarterSelected, maskForSigil(sigilId));
}

void AudioController::randomStarter(uint8_t sigilId) {
  play(SoundId::RandomStarter, maskForSigil(sigilId));
}

void AudioController::startArmed(uint8_t sigilId) {
  play(SoundId::StartArmed, maskForSigil(sigilId));
}

void AudioController::countdownCancelled(uint16_t targetMask) {
  play(SoundId::CountdownCancelled, targetMask);
}

void AudioController::countdownTone(uint16_t targetMask, uint8_t secondIndex) {
  if (secondIndex == 0) {
    play(SoundId::Countdown1, targetMask);
  } else if (secondIndex == 1) {
    play(SoundId::Countdown2, targetMask);
  } else {
    play(SoundId::Countdown3, targetMask);
  }
}

void AudioController::turnPass(uint8_t sigilId) {
  play(SoundId::TurnPass, maskForSigil(sigilId));
}

void AudioController::pause(uint16_t targetMask) {
  play(SoundId::Pause, targetMask);
}

void AudioController::resume(uint16_t targetMask) {
  play(SoundId::Resume, targetMask);
}

void AudioController::warning(uint8_t sigilId) {
  play(SoundId::Warning, maskForSigil(sigilId));
}

void AudioController::gameStart(uint16_t targetMask) {
  play(SoundId::GameStart, targetMask);
}

void AudioController::gameOver(uint16_t targetMask) {
  play(SoundId::GameOver, targetMask);
}

}  // namespace TurnHub
