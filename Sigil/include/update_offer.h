#pragma once

// Whether a Sigil takes a SigilUpdateOffer (SIGIL_OTA.md, "Update
// sequence"). Pure logic, host-tested; the download itself is
// sigil_updater.cpp. Atlas resends an offer until it hears a status, so the
// same token again means "tell me how it went", never "start over".

#include <string.h>

#include "firmware_package.h"
#include "protocol.h"

namespace TurnHubSigil {

enum class OfferDecision : uint8_t {
  Ignore,  // Not for this Sigil, or malformed: say nothing.
  Accept,  // Start the update.
  Repeat,  // Same job as before: resend its last status.
  Refuse,  // Answer Refused with `error`.
};

struct OfferCheck {
  OfferDecision decision;
  uint8_t error;  // TurnHubFirmwarePackage::Error or TurnHubProtocol::UpdateError.
};

struct UpdaterMemory {
  bool hasJob = false;
  uint8_t token[TurnHubProtocol::UPDATE_TOKEN_BYTES] = {};
  bool running = false;
};

inline OfferCheck checkUpdateOffer(const TurnHubProtocol::SigilUpdateOfferPacket &offer,
    uint8_t sigilId, TurnHubFirmwarePackage::Product product,
    TurnHubFirmwarePackage::Version running, const UpdaterMemory &memory) {
  using TurnHubFirmwarePackage::Error;
  if (!TurnHubProtocol::validUpdateOffer(offer) || offer.sigilId != sigilId) {
    return {OfferDecision::Ignore, 0};
  }
  if (memory.hasJob &&
      memcmp(memory.token, offer.token, TurnHubProtocol::UPDATE_TOKEN_BYTES) == 0) {
    return {OfferDecision::Repeat, 0};
  }
  if (memory.running) {
    return {OfferDecision::Refuse, static_cast<uint8_t>(TurnHubProtocol::UpdateError::Busy)};
  }
  if (offer.product != static_cast<uint8_t>(product)) {
    return {OfferDecision::Refuse, static_cast<uint8_t>(Error::WrongProduct)};
  }
  if (!TurnHubFirmwarePackage::versionAllowed(running,
          TurnHubFirmwarePackage::Version{offer.major, offer.minor, offer.patch})) {
    return {OfferDecision::Refuse, static_cast<uint8_t>(Error::OlderVersion)};
  }
  return {OfferDecision::Accept, 0};
}

// The download URL for a token: lowercase hex, as Atlas's route expects.
inline void updateTokenHex(const uint8_t token[TurnHubProtocol::UPDATE_TOKEN_BYTES],
    char out[2 * TurnHubProtocol::UPDATE_TOKEN_BYTES + 1]) {
  static const char HEX_DIGITS[] = "0123456789abcdef";
  for (size_t i = 0; i < TurnHubProtocol::UPDATE_TOKEN_BYTES; ++i) {
    out[2 * i] = HEX_DIGITS[token[i] >> 4];
    out[2 * i + 1] = HEX_DIGITS[token[i] & 0x0F];
  }
  out[2 * TurnHubProtocol::UPDATE_TOKEN_BYTES] = '\0';
}

}  // namespace TurnHubSigil
