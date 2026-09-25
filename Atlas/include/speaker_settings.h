#pragma once
#include "storage.h"

namespace TurnHub {

// Volume of Atlas's own speaker (table-wide cues). Admins choose it in the
// portal; 0 turns the speaker off without affecting Sigil sound. Sound is
// always supplementary: every cue also shows on screens (ACCESSIBILITY.md).
enum class SpeakerVolume : uint8_t { Off = 0, Low = 1, Medium = 2, High = 3 };
constexpr uint8_t SPEAKER_VOLUME_MAX = 3;
constexpr uint8_t DEFAULT_SPEAKER_VOLUME = static_cast<uint8_t>(SpeakerVolume::Medium);

inline bool validSpeakerVolume(int32_t volume) {
  return volume >= 0 && volume <= SPEAKER_VOLUME_MAX;
}

inline const char *speakerVolumeName(uint8_t volume) {
  switch (volume) {
    case 0: return "off";
    case 1: return "low";
    case 2: return "medium";
    case 3: return "high";
    default: return "unknown";
  }
}

TurnHubStorage::Status loadSpeakerVolume(uint8_t &volume);
TurnHubStorage::Status saveSpeakerVolume(uint8_t volume);

// "spkvol" image. Schema 1: {1, volume 0-3}.
constexpr char SPEAKER_VOLUME_KEY[] = "spkvol";
constexpr size_t SPEAKER_VOLUME_SIZE = 2;

inline TurnHubStorage::Status readSpeakerVolume(TurnHubStorage::BlobStore &store, uint8_t &volume) {
  using TurnHubStorage::Status;
  uint8_t data[SPEAKER_VOLUME_SIZE] = {};
  size_t size = 0;
  Status status = store.read(SPEAKER_VOLUME_KEY, nullptr, 0, size);
  if (status != Status::Ok) return status;
  if (size != sizeof(data)) return Status::Corrupt;
  status = store.read(SPEAKER_VOLUME_KEY, data, sizeof(data), size);
  if (status != Status::Ok) return status;
  if (size != sizeof(data)) return Status::Corrupt;
  if (data[0] != 1) return Status::UnsupportedSchema;
  if (!validSpeakerVolume(data[1])) return Status::Corrupt;
  volume = data[1];
  return Status::Ok;
}

inline TurnHubStorage::Status writeSpeakerVolume(TurnHubStorage::BlobStore &store, uint8_t volume) {
  if (!validSpeakerVolume(volume)) return TurnHubStorage::Status::InvalidArgument;
  const uint8_t data[SPEAKER_VOLUME_SIZE] = {1, volume};
  return store.write(SPEAKER_VOLUME_KEY, data, sizeof(data));
}

}  // namespace TurnHub
