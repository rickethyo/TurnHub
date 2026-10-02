// Atlas's on-board speaker. See atlas_speaker.h.
#include "atlas_speaker.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#include "config.h"
#include "serial_log.h"

namespace TurnHubAtlas {
namespace {

// A small synthesizer on the built-in DAC (IO26 is DAC channel 2), streamed by
// I2S0 so the CPU only fills buffers. Each note is a soft chime: a quick
// attack, a fundamental with a quieter octave and twelfth, and an exponential
// ring-out that continues briefly past the note, so a cue's notes blend like a
// struck bell instead of the harsh on/off square wave used until 2026-10-02.
// Pitches are tuned to the nearest note of the major pentatonic scale, so
// every cue is consonant whatever its source frequencies. A gentle soft clip
// keeps it loud on the small speaker (a plain DAC sine was too quiet on
// Medium, 2026-09-25).
constexpr i2s_port_t SPEAKER_I2S = I2S_NUM_0;
constexpr uint32_t SAMPLE_RATE = 22050;
constexpr size_t FRAMES_PER_BUFFER = 128;
constexpr uint8_t VOICES = 4;
constexpr uint8_t PENDING_CAPACITY = 8;
constexpr uint16_t MIN_TONE_HZ = 130;
constexpr uint16_t MAX_TONE_HZ = 5000;
constexpr float TWO_PI_F = 6.28318531f;
// The amplifier stays on this long after the last voice ends, so the tail
// finishes and back-to-back cues do not click it on and off.
constexpr uint32_t AMP_HOLD_MS = 250;

struct Note {
  float hz;
  uint16_t durationMs;
  uint8_t volume;
};

struct Voice {
  bool active = false;
  float phase = 0;      // Cycles, 0..1.
  float step = 0;       // Cycles per sample.
  float level = 0;      // Current envelope level.
  float peak = 0;       // Envelope target after the attack.
  float ring = 0;       // Per-sample decay while the note is held.
  float release = 0;    // Per-sample decay after the note's duration.
  uint32_t attackLeft = 0;
  uint32_t holdLeft = 0;
};

// Loudness per volume step (1 low .. 3 high), before the soft clip.
float peakFor(uint8_t volume) {
  switch (volume) {
    case 1: return 0.35f;
    case 2: return 0.6f;
    default: return 0.85f;
  }
}

// Nearest pitch in the major pentatonic scale on C (C D E G A).
float pentatonic(float hz) {
  static const bool IN_SCALE[12] = {true, false, true, false, true, false, false, true, false, true, false, false};
  const int midi = static_cast<int>(lroundf(69.0f + 12.0f * log2f(hz / 440.0f)));
  for (int offset = 0; offset < 6; ++offset) {
    const int up = midi + offset;
    const int down = midi - offset;
    const int pick = IN_SCALE[up % 12] ? up : (IN_SCALE[down % 12] ? down : -1);
    if (pick >= 0) return 440.0f * powf(2.0f, (pick - 69) / 12.0f);
  }
  return hz;
}

// One sine cycle; the synthesizer reads it instead of calling sinf per sample.
constexpr uint16_t SINE_SIZE = 1024;
float sineTable[SINE_SIZE];

void buildSineTable() {
  for (uint16_t i = 0; i < SINE_SIZE; ++i) sineTable[i] = sinf(TWO_PI_F * i / SINE_SIZE);
}

// Sine of `cycles` (any non-negative value; only the fraction counts).
inline float sine(float cycles) {
  return sineTable[static_cast<uint32_t>(cycles * SINE_SIZE) & (SINE_SIZE - 1)];
}

// A cheap tanh (Pade), exact enough for a soft clip on an 8-bit DAC.
inline float softClip(float x) {
  if (x > 3.0f) return 1.0f;
  if (x < -3.0f) return -1.0f;
  const float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// The decay factor that loses 1/e of the level in tauMs.
float decayPerSample(float tauMs) {
  return expf(-1000.0f / (tauMs * SAMPLE_RATE));
}

class AtlasSpeaker final : public TurnHub::ToneOutput {
 public:
  bool begin() {
    pinMode(AtlasConfig::AUDIO_ENABLE_PIN, OUTPUT);
    amplifier(false);
    buildSineTable();
    i2s_config_t config = {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
    config.sample_rate = SAMPLE_RATE;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
    config.intr_alloc_flags = 0;
    config.dma_buf_count = 4;
    config.dma_buf_len = FRAMES_PER_BUFFER;
    config.use_apll = false;
    if (i2s_driver_install(SPEAKER_I2S, &config, 0, nullptr) != ESP_OK) return false;
    // DAC channel 2 is IO26 (AUDIO_DAC_PIN). Channel 1 is IO25, the touch
    // controller's clock, so it must stay a plain GPIO: i2s_set_pin(nullptr)
    // would enable both DAC channels (and setting the mode afterwards never
    // turns channel 1 off), leaving the DAC driving IO25 and every touch
    // reading scrambled.
    i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);
    return xTaskCreatePinnedToCore(&AtlasSpeaker::run, "speaker", 3072, this, 3, nullptr, 0) == pdPASS;
  }

  void tone(uint16_t frequencyHz, uint16_t durationMs, uint8_t volume) override {
    if (volume == 0 || durationMs == 0) return;
    const float hz = pentatonic(constrain(frequencyHz, MIN_TONE_HZ, MAX_TONE_HZ));
    portENTER_CRITICAL(&lock_);
    if (pendingCount_ < PENDING_CAPACITY) {
      pending_[(pendingHead_ + pendingCount_) % PENDING_CAPACITY] = {hz, durationMs, volume};
      ++pendingCount_;
    }
    portEXIT_CRITICAL(&lock_);
  }

 private:
  static void run(void *self) { static_cast<AtlasSpeaker *>(self)->loop(); }

  void loop() {
    uint16_t buffer[FRAMES_PER_BUFFER * 2];
    for (;;) {
      takePending();
      const bool sounding = render(buffer);
      const uint32_t now = millis();
      if (sounding) lastSoundMs_ = now;
      amplifier(sounding || now - lastSoundMs_ < AMP_HOLD_MS);
      size_t written = 0;
      i2s_write(SPEAKER_I2S, buffer, sizeof(buffer), &written, portMAX_DELAY);
    }
  }

  void takePending() {
    Note notes[PENDING_CAPACITY];
    uint8_t count = 0;
    portENTER_CRITICAL(&lock_);
    while (pendingCount_ > 0) {
      notes[count++] = pending_[pendingHead_];
      pendingHead_ = (pendingHead_ + 1) % PENDING_CAPACITY;
      --pendingCount_;
    }
    portEXIT_CRITICAL(&lock_);
    for (uint8_t i = 0; i < count; ++i) start(notes[i]);
  }

  // A new note takes a free voice, else the quietest one.
  void start(const Note &note) {
    Voice *voice = &voices_[0];
    for (Voice &candidate : voices_) {
      if (!candidate.active) { voice = &candidate; break; }
      if (candidate.level < voice->level) voice = &candidate;
    }
    voice->active = true;
    voice->phase = 0;
    voice->step = note.hz / SAMPLE_RATE;
    voice->peak = peakFor(note.volume);
    voice->attackLeft = SAMPLE_RATE * 4 / 1000;  // 4 ms: soft, no click.
    voice->holdLeft = static_cast<uint32_t>(note.durationMs) * SAMPLE_RATE / 1000;
    // Higher notes ring shorter, as a real chime does.
    voice->ring = decayPerSample(note.hz > 1200 ? 220.0f : 320.0f);
    voice->release = decayPerSample(70.0f);
  }

  // Fills one buffer; true while any voice is audible.
  bool render(uint16_t *buffer) {
    bool any = false;
    for (size_t i = 0; i < FRAMES_PER_BUFFER; ++i) {
      float mix = 0;
      for (Voice &v : voices_) {
        if (!v.active) continue;
        if (v.attackLeft > 0) {
          v.level += (v.peak - v.level) / static_cast<float>(v.attackLeft);
          --v.attackLeft;
        } else if (v.holdLeft > 0) {
          v.level *= v.ring;
          --v.holdLeft;
        } else {
          v.level *= v.release;
          if (v.level < 0.002f) { v.active = false; continue; }
        }
        const float p = v.phase;
        mix += v.level * (sine(p) + 0.32f * sine(2 * p) + 0.12f * sine(3 * p)) * (1.0f / 1.44f);
        v.phase += v.step;
        if (v.phase >= 1.0f) v.phase -= 1.0f;
        any = true;
      }
      // Soft clip: loud without the harshness of hard clipping.
      const float shaped = softClip(1.6f * mix) * (1.0f / 0.9217f);
      const int32_t level = 128 + static_cast<int32_t>(lroundf(constrain(shaped, -1.0f, 1.0f) * 127.0f));
      // The built-in DAC takes the high byte of each 16-bit sample; both
      // channels carry the same sample.
      const uint16_t sample = static_cast<uint16_t>(level << 8);
      buffer[2 * i] = sample;
      buffer[2 * i + 1] = sample;
    }
    return any;
  }

  // Active low; off between cues so an idle speaker does not hiss.
  void amplifier(bool on) {
    if (on == amplifierOn_) return;
    amplifierOn_ = on;
    digitalWrite(AtlasConfig::AUDIO_ENABLE_PIN, on ? LOW : HIGH);
  }

  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  Note pending_[PENDING_CAPACITY] = {};
  uint8_t pendingHead_ = 0;
  uint8_t pendingCount_ = 0;
  Voice voices_[VOICES];
  uint32_t lastSoundMs_ = 0;
  bool amplifierOn_ = true;  // So the first amplifier(false) writes the pin.
};

AtlasSpeaker speaker;

}  // namespace

TurnHub::ToneOutput *beginAtlasSpeaker() {
  if (!speaker.begin()) {
    TurnHub::serialLog.println("ATLAS|SPEAKER|START_FAILED");
    // Notes queue harmlessly; nothing plays.
  } else {
    TurnHub::serialLog.println("ATLAS|SPEAKER|READY");
  }
  return &speaker;
}

void serviceAtlasSpeaker(uint32_t) {
  // The synthesizer runs in its own task; nothing to do per loop.
}

}  // namespace TurnHubAtlas
