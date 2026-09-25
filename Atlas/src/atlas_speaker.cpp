// Atlas's on-board speaker. See atlas_speaker.h.
#include "atlas_speaker.h"

#include <Arduino.h>

#include "config.h"
#include "serial_log.h"

namespace TurnHubAtlas {
namespace {

// Square wave from the LED PWM peripheral on the amplifier input (IO26). A
// square wave's odd harmonics fall where a small speaker is most efficient,
// so it is much louder than the DAC's sine for the same swing. Channel 4 uses
// its own timer, away from the backlight's channel 7.
constexpr uint8_t SPEAKER_LEDC_CHANNEL = 4;
constexpr uint8_t SPEAKER_LEDC_BITS = 8;
constexpr uint16_t MIN_TONE_HZ = 130;
constexpr uint16_t MAX_TONE_HZ = 10000;

// Duty sets loudness: the fundamental scales with sin(pi * duty), so these
// give about 45%, 68% and 90% amplitude (50% duty would be the loudest). Each
// level was lowered about 10% on 2026-09-25 at the owner's request.
uint32_t dutyFor(uint8_t volume) {
  switch (volume) {
    case 1: return 38;   // Low, ~0.45
    case 2: return 60;   // Medium, ~0.68
    default: return 91;  // High, ~0.90
  }
}

class AtlasSpeaker final : public TurnHub::ToneOutput {
 public:
  void begin() {
    pinMode(AtlasConfig::AUDIO_ENABLE_PIN, OUTPUT);
    ledcSetup(SPEAKER_LEDC_CHANNEL, 1000, SPEAKER_LEDC_BITS);
    ledcAttachPin(AtlasConfig::AUDIO_DAC_PIN, SPEAKER_LEDC_CHANNEL);
    silence();
  }

  void tone(uint16_t frequencyHz, uint16_t durationMs, uint8_t volume) override {
    if (volume == 0 || durationMs == 0) return;
    const uint32_t hz = constrain(frequencyHz, MIN_TONE_HZ, MAX_TONE_HZ);
    if (ledcChangeFrequency(SPEAKER_LEDC_CHANNEL, hz, SPEAKER_LEDC_BITS) == 0) {
      TurnHub::serialLog.println("ATLAS|SPEAKER|TONE_FAILED");
      return;
    }
    ledcWrite(SPEAKER_LEDC_CHANNEL, dutyFor(volume));
    digitalWrite(AtlasConfig::AUDIO_ENABLE_PIN, LOW);  // Amplifier on.
    playing_ = true;
    stopAtMs_ = millis() + durationMs;
  }

  void service(uint32_t nowMs) {
    if (playing_ && static_cast<int32_t>(nowMs - stopAtMs_) >= 0) silence();
  }

 private:
  // Amplifier off between notes so an idle speaker does not hiss.
  void silence() {
    digitalWrite(AtlasConfig::AUDIO_ENABLE_PIN, HIGH);
    ledcWrite(SPEAKER_LEDC_CHANNEL, 0);
    playing_ = false;
  }

  bool playing_ = false;
  uint32_t stopAtMs_ = 0;
};

AtlasSpeaker speaker;

}  // namespace

TurnHub::ToneOutput *beginAtlasSpeaker() {
  speaker.begin();
  TurnHub::serialLog.println("ATLAS|SPEAKER|READY");
  return &speaker;
}

void serviceAtlasSpeaker(uint32_t nowMs) {
  speaker.service(nowMs);
}

}  // namespace TurnHubAtlas
