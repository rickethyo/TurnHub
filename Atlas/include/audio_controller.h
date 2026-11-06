#pragma once

#include <Arduino.h>

#include "sigil_bus.h"
#include "turnhub_types.h"

namespace TurnHub {

// Local presentation cues for hosted state and device feedback. Audio is always
// supplementary: every cue has a visual/text equivalent (ACCESSIBILITY.md).
enum class AudioCue : uint8_t {
  TurnStarted,
  Pause,
  Resume,
  TimerExpired,
  ActionRequired,
  GameStart,
  GameOver
};

struct AudioNote {
  uint16_t frequencyHz;
  uint16_t durationMs;
  uint16_t gapMs;
};

// An empty pattern (count 0) silences that cue.
struct AudioPattern {
  const AudioNote *notes;
  uint8_t count;
};

// Atlas's own speaker. Non-blocking: tone() starts a note and the
// implementation ends it after durationMs. volume is 1 (low) to 3 (high).
class ToneOutput {
 public:
  virtual ~ToneOutput() = default;
  virtual void tone(uint16_t frequencyHz, uint16_t durationMs, uint8_t volume) = 0;
};

class AudioController {
 public:
  explicit AudioController(SigilBus &bus);

  static uint16_t maskForSigil(uint8_t sigilId);
  // Target bit for Atlas's own speaker (Sigil IDs never reach it). It rides
  // along with table-wide cues so players without a Sigil hear them too.
  static constexpr uint16_t ATLAS_SPEAKER_MASK = 0x8000;

  // The speaker, or nullptr when Atlas has none (host tests). Volume 0
  // silences it without touching Sigil sound; 1-3 are low to high.
  void setSpeaker(ToneOutput *speaker) { speaker_ = speaker; }
  void setSpeakerVolume(uint8_t volume) { speakerVolume_ = volume; }
  uint8_t speakerVolume() const { return speakerVolume_; }

  // Sigils whose seated players turned Sigil sound off (per-player
  // accessibility preference). Muted Sigils are skipped when a cue plays,
  // including notes already queued; other Sigils still hear the cue.
  void setMutedSigils(uint16_t mask) { mutedMask_ = mask; }

  void update(uint32_t nowMs);
  void clear();
  // Queues a cue for the Sigils in targetMask. False when silenced or full.
  bool play(AudioCue cue, uint16_t targetMask);

  void pause(uint16_t targetMask);
  void resume(uint16_t targetMask);
  void gameStart(uint16_t targetMask);
  void gameOver(uint16_t targetMask);

 private:
  struct Job {
    AudioCue cue = AudioCue::TurnStarted;
    uint16_t targetMask = 0;

    Job() = default;
    Job(AudioCue cueValue, uint16_t maskValue)
        : cue(cueValue), targetMask(maskValue) {}
  };

  static constexpr uint8_t QUEUE_CAPACITY = 16;

  bool beginNext(uint32_t nowMs);
  void sendTone(uint16_t targetMask, uint16_t frequencyHz, uint16_t durationMs);

  bool speakerAudible() const { return speaker_ != nullptr && speakerVolume_ > 0; }

  SigilBus &bus_;
  uint16_t mutedMask_ = 0;
  ToneOutput *speaker_ = nullptr;
  uint8_t speakerVolume_ = 0;
  Job queue_[QUEUE_CAPACITY];
  uint8_t queueHead_ = 0;
  uint8_t queueTail_ = 0;
  uint8_t queueCount_ = 0;

  bool active_ = false;
  Job current_;
  uint8_t noteIndex_ = 0;
  uint32_t nextNoteAtMs_ = 0;
};

}  // namespace TurnHub
