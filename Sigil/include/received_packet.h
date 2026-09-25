#pragma once
#include "protocol.h"
#include <cstring>

namespace TurnHubSigil {
// Copy any radio message (7-byte Packet, picker page, game display) intact; validation and rendering stay on loop().
struct ReceivedPacket {
  uint8_t mac[6];
  uint16_t length;
  uint8_t data[sizeof(TurnHubProtocol::GameDisplayPacket)];
  static_assert(sizeof(TurnHubProtocol::ProfilePickerPacket) <= sizeof(TurnHubProtocol::GameDisplayPacket),
      "Picker page must fit the receive buffer");

  bool assign(const uint8_t *sender, const uint8_t *bytes, int size) {
    if (!sender || !bytes || (size != sizeof(TurnHubProtocol::Packet) &&
        size != sizeof(TurnHubProtocol::GameDisplayPacket) &&
        size != sizeof(TurnHubProtocol::ProfilePickerPacket))) return false;
    memcpy(mac, sender, sizeof(mac));
    length = static_cast<uint16_t>(size);
    memcpy(data, bytes, size);
    return true;
  }
};
}
