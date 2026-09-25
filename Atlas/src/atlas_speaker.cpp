// Atlas's on-board speaker. See atlas_speaker.h.
#include "atlas_speaker.h"

#include <Arduino.h>
#include <driver/dac.h>

#include "config.h"
#include "serial_log.h"

namespace TurnHubAtlas {
namespace {

// The DAC cosine generator covers roughly 130 Hz to 55 kHz; every cue note
// is well inside that.
constexpr uint16_t MIN_TONE_HZ = 130;
constexpr uint16_t MAX_TONE_HZ = 10000;

dac_cw_scale_t scaleFor(uint8_t volume) {
  switch (volume) {
    case 1: return DAC_CW_SCALE_8;  // Low
    case 2: return DAC_CW_SCALE_4;  // Medium
    default: return DAC_CW_SCALE_1;  // High
  }
}

class AtlasSpeaker final : public TurnHub::ToneOutput {
 public:
  void begin() {
    pinMode(AtlasConfig::AUDIO_ENABLE_PIN, OUTPUT);
    silence();
  }

  void tone(uint16_t frequencyHz, uint16_t durationMs, uint8_t volume) override {
    if (volume == 0 || durationMs == 0) return;
    dac_cw_config_t config = {};
    config.en_ch = DAC_CHANNEL_2;  // GPIO26
    config.scale = scaleFor(volume);
    config.phase = DAC_CW_PHASE_0;
    config.freq = constrain(frequencyHz, MIN_TONE_HZ, MAX_TONE_HZ);
    config.offset = 0;
    if (dac_cw_generator_config(&config) != ESP_OK) {
      TurnHub::serialLog.println("ATLAS|SPEAKER|TONE_FAILED");
      return;
    }
    dac_cw_generator_enable();
    dac_output_enable(DAC_CHANNEL_2);
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
    dac_cw_generator_disable();
    dac_output_disable(DAC_CHANNEL_2);
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
