#pragma once
#include "protocol.h"
#include "secure_link.h"
#include <cstring>

namespace TurnHubSigil {
// Copy any radio message intact; validation and rendering stay on loop().
// Since the secure link (PAIRING_AND_SECURE_LINK.md): Atlas's packets arrive sealed (a
// 7-byte Packet, picker page, Commander page, game display or update offer
// plus SECURE_OVERHEAD), apart from the session handshake and pairing. The
// unsealed sizes stay accepted here only so the loop can recognise and drop
// them.
struct ReceivedPacket {
  // The largest: a sealed update offer (FIRMWARE_UPDATES.md).
  static constexpr size_t MAX_BYTES =
      sizeof(TurnHubProtocol::SigilUpdateOfferPacket) + TurnHubSecureLink::SECURE_OVERHEAD;
  uint8_t mac[6];
  uint16_t length;
  uint8_t data[MAX_BYTES];
  static_assert(sizeof(TurnHubProtocol::ProfilePickerPacket) <= sizeof(TurnHubProtocol::GameDisplayPacket),
      "Picker page must fit the receive buffer");
  static_assert(sizeof(TurnHubProtocol::CommanderFlowPacket) <= sizeof(TurnHubProtocol::SigilUpdateOfferPacket),
      "Commander page must fit the receive buffer");
  static_assert(sizeof(TurnHubProtocol::GameDisplayPacket) <= sizeof(TurnHubProtocol::SigilUpdateOfferPacket),
      "Game display must fit the receive buffer");
  static_assert(MAX_BYTES <= TurnHubSecureLink::ESPNOW_MAX_BYTES, "Receive buffer past one frame");

  static bool acceptedSize(int size) {
    using TurnHubSecureLink::SECURE_OVERHEAD;
    switch (size) {
      case sizeof(TurnHubProtocol::Packet):
      case sizeof(TurnHubProtocol::GameDisplayPacket):
      case sizeof(TurnHubProtocol::ProfilePickerPacket):
      case sizeof(TurnHubSecureLink::PairAccept2Packet):
      case sizeof(TurnHubSecureLink::PairResultPacket):
      case sizeof(TurnHubSecureLink::SecureHelloAckPacket):
      case sizeof(TurnHubProtocol::Packet) + SECURE_OVERHEAD:
      case sizeof(TurnHubProtocol::ProfilePickerPacket) + SECURE_OVERHEAD:
      case sizeof(TurnHubProtocol::CommanderFlowPacket) + SECURE_OVERHEAD:
      case sizeof(TurnHubProtocol::GameDisplayPacket) + SECURE_OVERHEAD:
      case sizeof(TurnHubProtocol::SigilUpdateOfferPacket) + SECURE_OVERHEAD:
        return true;
      default:
        return false;
    }
  }

  bool assign(const uint8_t *sender, const uint8_t *bytes, int size) {
    if (!sender || !bytes || !acceptedSize(size)) return false;
    memcpy(mac, sender, sizeof(mac));
    length = static_cast<uint16_t>(size);
    memcpy(data, bytes, size);
    return true;
  }
};
}
