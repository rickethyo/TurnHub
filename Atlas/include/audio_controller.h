#pragma once

#include <Arduino.h>

#include "sigil_bus.h"
#include "turnhub_types.h"

namespace TurnHub {

// Semantic audio events. Game handlers name what happened; the active
// AudioCueProfile decides whether and how it sounds. Audio is always
// supplementary: every cue has a visual/text equivalent (ACCESSIBILITY.md).
enum class AudioCue : uint8_t {
  PlayerJoined,
  SharedPlayerAdded,
  SharedPlayerRemoved,
  SameModulePass,      // Turn moved to the other seat on the same Sigil.
  StarterSelected,
  RandomStarter,
  StartArmed,
  CountdownCancelled,
  Countdown1,
  Countdown2,
  Countdown3,
  TurnStarted,         // Heard by the Sigil whose turn begins (the pass sound).
  TurnPassed,          // Heard by the Sigil that passed. Silent by default.
  Pause,
  Resume,
  TurnWarning,         // Turn timer: TURN_TIMER_WARNING_MS left. A subtle chirp.
  TimerExpired,        // Turn timer reached zero. No turn change follows.
  ActionRequired,      // A decision waits on this Sigil. Defined, not yet emitted.
  GameStart,
  GameOver,
  Nudge,
  NudgeTable,
  EliminationArmed,
  EliminationChanged,
  EliminationCancelled,
  PlayerEliminated,
  WinClaimed,
  WinConfirmed,
  WinDenied,
  WinCancelled,
  Count
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

// The one audio configuration boundary. `enabled` silences every cue without
// touching LEDs or displays; individual cues can be silenced or restyled.
struct AudioCueProfile {
  bool enabled;
  AudioPattern patterns[static_cast<uint8_t>(AudioCue::Count)];

  const AudioPattern &pattern(AudioCue cue) const { return patterns[static_cast<uint8_t>(cue)]; }
};

// The prototype's established sounds plus the turn-timer cues.
const AudioCueProfile &defaultAudioCueProfile();

class AudioController {
 public:
  explicit AudioController(SigilBus &bus);

  static uint16_t maskForSigil(uint8_t sigilId);

  // The profile must outlive the controller (static or owned by the caller).
  void setProfile(const AudioCueProfile &profile);
  const AudioCueProfile &profile() const { return *profile_; }

  void update(uint32_t nowMs);
  void clear();
  // Queues a cue for the Sigils in targetMask. False when silenced or full.
  bool play(AudioCue cue, uint16_t targetMask);

  void playerJoined(uint8_t sigilId);
  void sharedPlayerAdded(uint8_t sigilId);
  void sharedPlayerRemoved(uint8_t sigilId);
  void sameModulePass(uint8_t sigilId);
  void starterSelected(uint8_t sigilId);
  void randomStarter(uint8_t sigilId);
  void startArmed(uint8_t sigilId);
  void countdownCancelled(uint16_t targetMask);
  void countdownTone(uint16_t targetMask, uint8_t secondIndex);
  // A pass committed: the new active Sigil hears TurnStarted, the passer TurnPassed.
  void turnPassed(uint8_t fromSigilId, uint8_t toSigilId);
  void pause(uint16_t targetMask);
  void resume(uint16_t targetMask);
  void turnWarning(uint8_t sigilId);
  void timerExpired(uint8_t sigilId);
  void gameStart(uint16_t targetMask);
  void gameOver(uint16_t targetMask);

  void eliminationArmed(uint8_t sigilId);
  void eliminationTargetChanged(uint8_t sigilId);
  void eliminationCancelled(uint8_t sigilId);
  void playerEliminated(uint8_t sigilId);
  void winClaimed(uint16_t targetMask);
  void winConfirmed(uint16_t targetMask);
  void winDenied(uint16_t targetMask);
  void winCancelled(uint16_t targetMask);

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

  SigilBus &bus_;
  const AudioCueProfile *profile_;
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
