#pragma once
#include "protocol.h"
namespace TurnHubAtlas {
constexpr uint32_t COMMANDER_IDLE_MS = 60000;
void openCommanderPicker(uint8_t sigilId, uint8_t recipient, bool undo, uint32_t nowMs);
void handleCommanderKey(uint8_t sigilId, int32_t value, uint32_t nowMs);
void syncCommanderPickers(uint32_t nowMs);
void invalidateCommanderPicker(uint8_t sigilId);
void resetCommanderPickers();
TurnHubProtocol::CommanderFlowPacket commanderPage(uint8_t sigilId);
}
