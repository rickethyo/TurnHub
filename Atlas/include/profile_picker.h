#pragma once

// Sigil profile picker (menu Sigils 0.8.0+, e-ink and OLED; see ProfilePickerPacket in
// protocol.h). On a picker Sigil, the menu's Join opens a list of names
// instead of joining as a guest: Guest first, then the saved profiles by
// name. Atlas owns the list, the page and the choice; the Sigil draws a page
// of three names and reports compass keys. Browsing changes nothing. Guest
// dispatches Join; a profile asks "Join as <name>?" and then dispatches
// PickProfile, whose handler applies the profile's physical-use policy.
// Presentation and adapter only: every change goes through the dispatcher.

#include <Arduino.h>

#include "protocol.h"

namespace TurnHubAtlas {

// The picker closes by itself after this long without a key.
constexpr uint32_t PICKER_IDLE_MS = 60000;

// This Sigil draws the picker (menu, new enough firmware, not a harness).
bool pickerSigil(uint8_t sigilId);
bool pickerOpen(uint8_t sigilId);
// Opens the list on its first page (the menu's Join on a picker Sigil).
void openProfilePicker(uint8_t sigilId, uint32_t nowMs);
// A PickerKey from the Sigil. Keys from an older page are dropped.
void handlePickerKey(uint8_t sigilId, int32_t value, uint32_t nowMs);
// Closes pickers that no longer apply (not the lobby, joined another way,
// offline, idle) and sends each picker Sigil its current page. Call every loop.
void syncProfilePickers(uint32_t nowMs);
// Resend this Sigil's picker state (every Hello; also tells it Closed).
void invalidateProfilePicker(uint8_t sigilId);
// The page Atlas would send this Sigil now (tests and diagnostics).
TurnHubProtocol::ProfilePickerPacket profilePickerPage(uint8_t sigilId);
void resetProfilePickers();

}  // namespace TurnHubAtlas
