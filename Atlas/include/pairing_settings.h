#pragma once
#include "protocol.h"
#include "storage.h"

namespace TurnHub {

// How long Atlas's pairing window stays open after Pair a Sigil is tapped on
// its screen. 60 s at least (PAIRING_WINDOW_MS, the minimum on every device);
// admins can lengthen it for players who need more time to reach the Sigil,
// never shorten it. Only Atlas's window changes: a Sigil's own window stays
// PAIRING_WINDOW_MS, so with a longer setting tap Pair on Atlas first.
constexpr uint8_t PAIRING_WINDOW_CHOICES_S[] = {60, 90, 120};
constexpr uint32_t DEFAULT_PAIRING_WINDOW_MS = TurnHubProtocol::PAIRING_WINDOW_MS;
static_assert(PAIRING_WINDOW_CHOICES_S[0] * 1000UL == TurnHubProtocol::PAIRING_WINDOW_MS,
    "The shortest Atlas choice is the 60 s minimum");

inline bool validPairingWindowMs(uint32_t windowMs) {
  for (uint8_t seconds : PAIRING_WINDOW_CHOICES_S) {
    if (windowMs == seconds * 1000UL) return true;
  }
  return false;
}

TurnHubStorage::Status loadPairingWindow(uint32_t &windowMs);
TurnHubStorage::Status savePairingWindow(uint32_t windowMs);

// "pairwin" little-endian image. Schema 1: {1, seconds}.
constexpr char PAIRING_WINDOW_KEY[] = "pairwin";
constexpr size_t PAIRING_WINDOW_SIZE = 2;

inline TurnHubStorage::Status readPairingWindow(TurnHubStorage::BlobStore &store, uint32_t &windowMs) {
  using TurnHubStorage::Status;
  uint8_t data[PAIRING_WINDOW_SIZE] = {};
  size_t size = 0;
  Status status = store.read(PAIRING_WINDOW_KEY, nullptr, 0, size);
  if (status != Status::Ok) return status;
  if (size != sizeof(data)) return Status::Corrupt;
  status = store.read(PAIRING_WINDOW_KEY, data, sizeof(data), size);
  if (status != Status::Ok) return status;
  if (size != sizeof(data)) return Status::Corrupt;
  if (data[0] != 1) return Status::UnsupportedSchema;
  uint32_t value = data[1] * 1000UL;
  // 15 and 30 s were choices before the 60 s minimum (2026-09-29): a record
  // saved then reads as the minimum rather than a shorter window.
  if (value == 15000 || value == 30000) value = DEFAULT_PAIRING_WINDOW_MS;
  if (!validPairingWindowMs(value)) return Status::Corrupt;
  windowMs = value;
  return Status::Ok;
}

inline TurnHubStorage::Status writePairingWindow(TurnHubStorage::BlobStore &store, uint32_t windowMs) {
  if (!validPairingWindowMs(windowMs)) return TurnHubStorage::Status::InvalidArgument;
  const uint8_t data[PAIRING_WINDOW_SIZE] = {1, static_cast<uint8_t>(windowMs / 1000)};
  return store.write(PAIRING_WINDOW_KEY, data, sizeof(data));
}

}  // namespace TurnHub
