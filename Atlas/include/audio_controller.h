#pragma once

#include <Arduino.h>

#include "sigil_bus.h"
#include "turnhub_types.h"

namespace TurnHub {

enum class SoundId : uint8_t {
  PlayerJoined,
  SharedPlayerAdded,
  SharedPlayerRemoved,
  SameModulePass,
  StarterSelected,
  RandomStarter,
  StartArmed,
  CountdownCancelled,
  Countdown1,
  Countdown2,
  Countdown3,
  TurnPass,
  Pause,
  Resume,
  Warning,
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
};

class AudioController {
 public:
  explicit AudioController(SigilBus &bus);

  static uint16_t maskForSigil(uint8_t sigilId);

  void update(uint32_t nowMs);
  void clear();
  bool play(SoundId sound, uint16_t targetMask);

  void playerJoined(uint8_t sigilId);
  void sharedPlayerAdded(uint8_t sigilId);
  void sharedPlayerRemoved(uint8_t sigilId);
  void sameModulePass(uint8_t sigilId);
  void starterSelected(uint8_t sigilId);
  void randomStarter(uint8_t sigilId);
  void startArmed(uint8_t sigilId);
  void countdownCancelled(uint16_t targetMask);
  void countdownTone(uint16_t targetMask, uint8_t secondIndex);
  void turnPass(uint8_t sigilId);
  void pause(uint16_t targetMask);
  void resume(uint16_t targetMask);
  void warning(uint8_t sigilId);
  void gameStart(uint16_t targetMask);
  void gameOver(uint16_t targetMask);

 private:
  struct Job {
    SoundId sound = SoundId::TurnPass;
    uint16_t targetMask = 0;
  };

  static constexpr uint8_t QUEUE_CAPACITY = 16;

  bool beginNext(uint32_t nowMs);
  void sendTone(uint16_t targetMask, uint16_t frequencyHz, uint16_t durationMs);

  SigilBus &bus_;
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
