#include "audio_controller.h"

#include "protocol.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHub {

namespace {
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

// Turn timer: one short chirp for the warning, and a distinct two-note low
// figure when time runs out. Rhythm differs, not only pitch.
constexpr AudioNote TURN_WARNING[] = {{1600, 40, 0}};
constexpr AudioNote TIMER_EXPIRED[] = {
    {620, 150, 80},
    {620, 150, 0},
};

// Pass grace: two quiet falling ticks start it; a rising pair undoes it.
constexpr AudioNote PASS_PENDING[] = {
    {900, 40, 50},
    {700, 40, 0},
};
constexpr AudioNote PASS_UNDONE[] = {
    {700, 40, 50},
    {900, 40, 0},
};

constexpr AudioNote ACTION_REQUIRED[] = {
    {1150, 60, 60},
    {1150, 60, 0},
};

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
constexpr AudioPattern pattern(const AudioNote (&notes)[N]) {
  return AudioPattern{notes, static_cast<uint8_t>(N)};
}

constexpr AudioPattern SILENT{nullptr, 0};

}  // namespace

const AudioCueProfile &defaultAudioCueProfile() {
  static const AudioCueProfile profile = [] {
    AudioCueProfile p{};
    p.enabled = true;
    auto set = [&p](AudioCue cue, AudioPattern value) { p.patterns[static_cast<uint8_t>(cue)] = value; };
    set(AudioCue::PlayerJoined, pattern(PLAYER_JOINED));
    set(AudioCue::SharedPlayerAdded, pattern(SHARED_ADDED));
    set(AudioCue::SharedPlayerRemoved, pattern(SHARED_REMOVED));
    set(AudioCue::SameModulePass, pattern(SAME_MODULE_PASS));
    set(AudioCue::StarterSelected, pattern(STARTER_SELECTED));
    set(AudioCue::RandomStarter, pattern(RANDOM_STARTER));
    set(AudioCue::StartArmed, pattern(START_ARMED));
    set(AudioCue::CountdownCancelled, pattern(COUNTDOWN_CANCELLED));
    set(AudioCue::Countdown1, pattern(COUNTDOWN_1));
    set(AudioCue::Countdown2, pattern(COUNTDOWN_2));
    set(AudioCue::Countdown3, pattern(COUNTDOWN_3));
    set(AudioCue::TurnStarted, pattern(TURN_PASS));
    set(AudioCue::TurnPassed, SILENT);  // Existing behavior: only the new player hears the pass.
    set(AudioCue::Pause, pattern(PAUSE));
    set(AudioCue::Resume, pattern(RESUME));
    set(AudioCue::TurnWarning, pattern(TURN_WARNING));
    set(AudioCue::TimerExpired, pattern(TIMER_EXPIRED));
    set(AudioCue::ActionRequired, pattern(ACTION_REQUIRED));
    set(AudioCue::GameStart, pattern(GAME_START));
    set(AudioCue::GameOver, pattern(GAME_OVER));
    set(AudioCue::Nudge, pattern(NUDGE));
    set(AudioCue::NudgeTable, pattern(NUDGE_TABLE));
    set(AudioCue::EliminationArmed, pattern(ELIMINATION_ARMED));
    set(AudioCue::EliminationChanged, pattern(ELIMINATION_CHANGED));
    set(AudioCue::EliminationCancelled, pattern(ELIMINATION_CANCELLED));
    set(AudioCue::PlayerEliminated, pattern(PLAYER_ELIMINATED));
    set(AudioCue::WinClaimed, pattern(WIN_CLAIMED));
    set(AudioCue::WinConfirmed, pattern(WIN_CONFIRMED));
    set(AudioCue::WinDenied, pattern(WIN_DENIED));
    set(AudioCue::WinCancelled, pattern(WIN_CANCELLED));
    set(AudioCue::PassPending, pattern(PASS_PENDING));
    set(AudioCue::PassUndone, pattern(PASS_UNDONE));
    return p;
  }();
  return profile;
}

static_assert(MAX_PHYSICAL_SIGILS < 15, "Sigil audio bits must not reach ATLAS_SPEAKER_MASK");

AudioController::AudioController(SigilBus &bus)
    : bus_(bus), profile_(&defaultAudioCueProfile()) {}

uint16_t AudioController::maskForSigil(uint8_t sigilId) {
  if (sigilId >= MAX_PHYSICAL_SIGILS) {
    return 0;
  }
  return static_cast<uint16_t>(1u << sigilId);
}

void AudioController::setProfile(const AudioCueProfile &profile) {
  profile_ = &profile;
  if (!profile.enabled) {
    clear();  // Muting takes effect immediately, including queued cues.
  }
}

bool AudioController::play(AudioCue cue, uint16_t targetMask) {
  targetMask = static_cast<uint16_t>(targetMask & ~mutedMask_);
  if (!speakerAudible()) targetMask = static_cast<uint16_t>(targetMask & ~ATLAS_SPEAKER_MASK);
  if (targetMask == 0 || !profile_->enabled || profile_->pattern(cue).count == 0) {
    return false;
  }

  if (queueCount_ >= QUEUE_CAPACITY) {
    serialLog.println("ATLAS|AUDIO|QUEUE_FULL");
    return false;
  }

  queue_[queueTail_] = Job{cue, targetMask};
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

  const AudioPattern &view = profile_->pattern(current_.cue);
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

  targetMask = static_cast<uint16_t>(targetMask & ~mutedMask_);
  for (uint8_t sigilId = 0; sigilId < MAX_PHYSICAL_SIGILS; ++sigilId) {
    if ((targetMask & maskForSigil(sigilId)) == 0) {
      continue;
    }
    bus_.buzzer(sigilId, payload);
  }
  if ((targetMask & ATLAS_SPEAKER_MASK) != 0 && speakerAudible()) {
    speaker_->tone(frequencyHz, durationMs, speakerVolume_);
  }
}

void AudioController::actionRequired(uint8_t sigilId) { play(AudioCue::ActionRequired, maskForSigil(sigilId)); }
void AudioController::playerJoined(uint8_t sigilId) { play(AudioCue::PlayerJoined, maskForSigil(sigilId)); }
void AudioController::sharedPlayerAdded(uint8_t sigilId) { play(AudioCue::SharedPlayerAdded, maskForSigil(sigilId)); }
void AudioController::sharedPlayerRemoved(uint8_t sigilId) { play(AudioCue::SharedPlayerRemoved, maskForSigil(sigilId)); }
void AudioController::sameModulePass(uint8_t sigilId) { play(AudioCue::SameModulePass, maskForSigil(sigilId)); }
void AudioController::starterSelected(uint8_t sigilId) { play(AudioCue::StarterSelected, maskForSigil(sigilId)); }
void AudioController::randomStarter(uint8_t sigilId) { play(AudioCue::RandomStarter, maskForSigil(sigilId)); }
void AudioController::startArmed(uint8_t sigilId) { play(AudioCue::StartArmed, maskForSigil(sigilId)); }
void AudioController::countdownCancelled(uint16_t targetMask) { play(AudioCue::CountdownCancelled, targetMask); }

void AudioController::countdownTone(uint16_t targetMask, uint8_t secondIndex) {
  play(secondIndex == 0 ? AudioCue::Countdown1 :
       secondIndex == 1 ? AudioCue::Countdown2 : AudioCue::Countdown3, targetMask);
}

// The turn change and turn-timer cues also sound on Atlas's speaker, so a
// player at the table without a Sigil (browser, phone) hears whose turn began.
void AudioController::turnPassed(uint8_t fromSigilId, uint8_t toSigilId) {
  if (fromSigilId == toSigilId) {
    play(AudioCue::SameModulePass, maskForSigil(toSigilId) | ATLAS_SPEAKER_MASK);
    return;
  }
  play(AudioCue::TurnPassed, maskForSigil(fromSigilId));
  play(AudioCue::TurnStarted, maskForSigil(toSigilId) | ATLAS_SPEAKER_MASK);
}

void AudioController::pause(uint16_t targetMask) { play(AudioCue::Pause, targetMask); }
void AudioController::resume(uint16_t targetMask) { play(AudioCue::Resume, targetMask); }
void AudioController::turnWarning(uint8_t sigilId) { play(AudioCue::TurnWarning, maskForSigil(sigilId) | ATLAS_SPEAKER_MASK); }
void AudioController::timerExpired(uint8_t sigilId) { play(AudioCue::TimerExpired, maskForSigil(sigilId) | ATLAS_SPEAKER_MASK); }
void AudioController::gameStart(uint16_t targetMask) { play(AudioCue::GameStart, targetMask); }
void AudioController::gameOver(uint16_t targetMask) { play(AudioCue::GameOver, targetMask); }
void AudioController::eliminationArmed(uint8_t sigilId) { play(AudioCue::EliminationArmed, maskForSigil(sigilId)); }
void AudioController::eliminationTargetChanged(uint8_t sigilId) { play(AudioCue::EliminationChanged, maskForSigil(sigilId)); }
void AudioController::eliminationCancelled(uint8_t sigilId) { play(AudioCue::EliminationCancelled, maskForSigil(sigilId)); }
void AudioController::playerEliminated(uint8_t sigilId) { play(AudioCue::PlayerEliminated, maskForSigil(sigilId) | ATLAS_SPEAKER_MASK); }
void AudioController::winClaimed(uint16_t targetMask) { play(AudioCue::WinClaimed, targetMask); }
void AudioController::winConfirmed(uint16_t targetMask) { play(AudioCue::WinConfirmed, targetMask); }
void AudioController::winDenied(uint16_t targetMask) { play(AudioCue::WinDenied, targetMask); }
void AudioController::winCancelled(uint16_t targetMask) { play(AudioCue::WinCancelled, targetMask); }
void AudioController::passPending(uint16_t targetMask) { play(AudioCue::PassPending, targetMask); }
void AudioController::passUndone(uint16_t targetMask) { play(AudioCue::PassUndone, targetMask); }

}  // namespace TurnHub