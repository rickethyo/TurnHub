#include "audio_controller.h"

#include "protocol.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHub {

namespace {
constexpr AudioNote TURN_PASS[] = {{1000, 80, 0}};

constexpr AudioNote PAUSE[] = {
    {1000, 90, 35},
    {650, 140, 0},
};

constexpr AudioNote RESUME[] = {
    {650, 90, 35},
    {1000, 140, 0},
};

constexpr AudioNote TIMER_EXPIRED[] = {
    {620, 150, 80},
    {620, 150, 0},
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

template <size_t N>
constexpr AudioPattern pattern(const AudioNote (&notes)[N]) {
  return AudioPattern{notes, static_cast<uint8_t>(N)};
}

AudioPattern cuePattern(AudioCue cue) {
  switch (cue) {
    case AudioCue::TurnStarted: return pattern(TURN_PASS);
    case AudioCue::Pause: return pattern(PAUSE);
    case AudioCue::Resume: return pattern(RESUME);
    case AudioCue::TimerExpired: return pattern(TIMER_EXPIRED);
    case AudioCue::ActionRequired: return pattern(ACTION_REQUIRED);
    case AudioCue::GameStart: return pattern(GAME_START);
    case AudioCue::GameOver: return pattern(GAME_OVER);
    default: return {nullptr, 0};
  }
}
}  // namespace

static_assert(MAX_PHYSICAL_SIGILS < 15, "Sigil audio bits must not reach ATLAS_SPEAKER_MASK");

AudioController::AudioController(SigilBus &bus)
    : bus_(bus) {}

uint16_t AudioController::maskForSigil(uint8_t sigilId) {
  if (sigilId >= MAX_PHYSICAL_SIGILS) {
    return 0;
  }
  return static_cast<uint16_t>(1u << sigilId);
}

bool AudioController::play(AudioCue cue, uint16_t targetMask) {
  targetMask = static_cast<uint16_t>(targetMask & ~mutedMask_);
  if (!speakerAudible()) targetMask = static_cast<uint16_t>(targetMask & ~ATLAS_SPEAKER_MASK);
  if (targetMask == 0 || cuePattern(cue).count == 0) {
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

  const AudioPattern view = cuePattern(current_.cue);
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

void AudioController::pause(uint16_t targetMask) { play(AudioCue::Pause, targetMask); }
void AudioController::resume(uint16_t targetMask) { play(AudioCue::Resume, targetMask); }
void AudioController::gameStart(uint16_t targetMask) { play(AudioCue::GameStart, targetMask); }
void AudioController::gameOver(uint16_t targetMask) { play(AudioCue::GameOver, targetMask); }

}  // namespace TurnHub
