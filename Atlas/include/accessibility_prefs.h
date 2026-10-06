#pragma once

#include <string.h>

#include "protocol.h"
#include "storage.h"

namespace TurnHubProfiles {

// How a player's Sigil lights are styled. Presentation only: the same cues
// are selected either way (led_cues.h), and every cue also reaches the
// portal/app as text.
enum class LedStyle : uint8_t {
  Standard = 0,       // The prototype's established cadences and colors.
  ReducedMotion = 1,  // No breathing or pulsing; steady lights, slow blinks only.
  MonochromeSafe = 2, // No two cues in the same situation differ only by color.
  Count
};

// How long others' life-change requests wait for this player's answer before
// Atlas accepts them. 15 s is the default (LIFE_APPROVAL_MS).
constexpr uint16_t LIFE_APPROVAL_OPTIONS_MS[] = {15000, 30000, 60000};
constexpr uint8_t LIFE_APPROVAL_OPTION_COUNT = 3;

inline bool lifeApprovalCode(uint16_t ms, uint8_t &code) {
  for (uint8_t i = 0; i < LIFE_APPROVAL_OPTION_COUNT; ++i) {
    if (LIFE_APPROVAL_OPTIONS_MS[i] == ms) {
      code = i;
      return true;
    }
  }
  return false;
}

// A player's accessibility preferences. Atlas owns and persists them with the
// profile (ACCESSIBILITY.md, "Accessibility profiles"); they follow the
// player to whichever Sigil they sit at. Defaults reproduce existing behavior.
struct AccessibilityPrefs {
  bool sigilSound = true;
  LedStyle ledStyle = LedStyle::Standard;
  uint16_t longPressMs = TurnHubProtocol::DEFAULT_LONG_PRESS_MS;
  uint16_t winHoldMs = TurnHubProtocol::DEFAULT_WIN_HOLD_MS;
  uint16_t lifeApprovalMs = LIFE_APPROVAL_OPTIONS_MS[0];
};

inline bool validAccessibilityPrefs(const AccessibilityPrefs &prefs) {
  uint8_t code = 0;
  return prefs.ledStyle < LedStyle::Count &&
      TurnHubProtocol::validInputTiming(prefs.longPressMs, prefs.winHoldMs) &&
      lifeApprovalCode(prefs.lifeApprovalMs, code);
}

inline bool sameAccessibilityPrefs(const AccessibilityPrefs &a, const AccessibilityPrefs &b) {
  return a.sigilSound == b.sigilSound && a.ledStyle == b.ledStyle &&
      a.longPressMs == b.longPressMs && a.winHoldMs == b.winHoldMs &&
      a.lifeApprovalMs == b.lifeApprovalMs;
}

inline const char *ledStyleKey(LedStyle style) {
  switch (style) {
    case LedStyle::ReducedMotion: return "reduced-motion";
    case LedStyle::MonochromeSafe: return "monochrome-safe";
    default: return "standard";
  }
}

inline bool parseLedStyle(const char *key, LedStyle &style) {
  for (uint8_t i = 0; i < static_cast<uint8_t>(LedStyle::Count); ++i) {
    const LedStyle candidate = static_cast<LedStyle>(i);
    if (key && strcmp(key, ledStyleKey(candidate)) == 0) {
      style = candidate;
      return true;
    }
  }
  return false;
}

// What one Sigil does when two players share it (seats A and B). Each rule
// picks the more accommodating choice, so sharing never removes an
// accommodation one of the players asked for:
// - sound is off if either player turned it off;
// - Reduced motion wins over Monochrome-safe (its cues are also distinguishable
//   without color), which wins over Standard;
// - the longer hold thresholds apply.
inline AccessibilityPrefs mergeSeatPrefs(const AccessibilityPrefs &a, const AccessibilityPrefs &b) {
  AccessibilityPrefs merged;
  merged.sigilSound = a.sigilSound && b.sigilSound;
  const auto rank = [](LedStyle style) {
    return style == LedStyle::ReducedMotion ? 2 : style == LedStyle::MonochromeSafe ? 1 : 0;
  };
  merged.ledStyle = rank(a.ledStyle) >= rank(b.ledStyle) ? a.ledStyle : b.ledStyle;
  merged.longPressMs = a.longPressMs > b.longPressMs ? a.longPressMs : b.longPressMs;
  merged.winHoldMs = a.winHoldMs > b.winHoldMs ? a.winHoldMs : b.winHoldMs;
  merged.lifeApprovalMs = a.lifeApprovalMs > b.lifeApprovalMs ? a.lifeApprovalMs : b.lifeApprovalMs;
  // Each input keeps winHold >= longPress + gap, so the maxima do too.
  return merged;
}

// Stored record ("x" + profile ID): {schema 1, flags, ledStyle,
// longPressMs le16, winHoldMs le16} = 7 bytes. Flags: bit 0 Sigil sound,
// bits 1-2 the life-approval option (0 = 15 s, so older records read as the
// default). A missing record reads as the
// defaults; a malformed one is Corrupt and never overwritten silently.
constexpr uint8_t ACCESSIBILITY_SCHEMA = 1;
constexpr size_t ACCESSIBILITY_RECORD_SIZE = 7;
constexpr char ACCESSIBILITY_PREFIX = 'x';

inline TurnHubStorage::Status readAccessibilityPrefs(TurnHubStorage::BlobStore &store,
    const char *key, AccessibilityPrefs &prefs) {
  using TurnHubStorage::Status;
  size_t size = 0;
  Status status = store.read(key, nullptr, 0, size);
  if (status != Status::Ok) return status;
  if (size != ACCESSIBILITY_RECORD_SIZE) return Status::Corrupt;
  uint8_t data[ACCESSIBILITY_RECORD_SIZE] = {};
  status = store.read(key, data, sizeof(data), size);
  if (status != Status::Ok) return status;
  if (size != sizeof(data)) return Status::Corrupt;
  if (data[0] != ACCESSIBILITY_SCHEMA) return Status::UnsupportedSchema;
  if (data[1] > 0x07 || (data[1] >> 1) >= LIFE_APPROVAL_OPTION_COUNT) return Status::Corrupt;
  AccessibilityPrefs value;
  value.sigilSound = (data[1] & 0x01) != 0;
  value.lifeApprovalMs = LIFE_APPROVAL_OPTIONS_MS[data[1] >> 1];
  value.ledStyle = static_cast<LedStyle>(data[2]);
  value.longPressMs = static_cast<uint16_t>(data[3] | (data[4] << 8));
  value.winHoldMs = static_cast<uint16_t>(data[5] | (data[6] << 8));
  if (!validAccessibilityPrefs(value)) return Status::Corrupt;
  prefs = value;
  return Status::Ok;
}

// Skips the write when nothing changed, sparing flash wear.
inline TurnHubStorage::Status writeAccessibilityPrefs(TurnHubStorage::BlobStore &store,
    const char *key, const AccessibilityPrefs &prefs) {
  using TurnHubStorage::Status;
  if (!validAccessibilityPrefs(prefs)) return Status::InvalidArgument;
  AccessibilityPrefs previous;
  const Status status = readAccessibilityPrefs(store, key, previous);
  if (status != Status::Ok && status != Status::NotFound) return status;
  if (status == Status::Ok && sameAccessibilityPrefs(previous, prefs)) return Status::Ok;
  uint8_t approval = 0;
  lifeApprovalCode(prefs.lifeApprovalMs, approval);
  const uint8_t data[ACCESSIBILITY_RECORD_SIZE] = {
      ACCESSIBILITY_SCHEMA, static_cast<uint8_t>((prefs.sigilSound ? 1 : 0) | (approval << 1)),
      static_cast<uint8_t>(prefs.ledStyle),
      static_cast<uint8_t>(prefs.longPressMs & 0xFF), static_cast<uint8_t>(prefs.longPressMs >> 8),
      static_cast<uint8_t>(prefs.winHoldMs & 0xFF), static_cast<uint8_t>(prefs.winHoldMs >> 8)};
  return store.write(key, data, sizeof(data));
}

}  // namespace TurnHubProfiles
