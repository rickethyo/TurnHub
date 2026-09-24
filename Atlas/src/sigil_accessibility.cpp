// Applies the seated players' accessibility preferences to each physical
// Sigil: LED style, Sigil sound and Action hold thresholds. Presentation and
// input timing only; nothing here reads or changes game state beyond looking
// up who sits where, and the Intents a Sigil produces keep their meaning.

#include "atlas_app.h"
#include "controller_profiles.h"
#include "profile_store.h"
#include "serial_log.h"

namespace TurnHubAtlas {

using TurnHub::serialLog;
using TurnHubProfiles::AccessibilityPrefs;
using TurnHubProfiles::LedStyle;

namespace {
// One Sigil is refreshed per tick, so each is revisited every ~2 s without a
// burst of NVS reads in a single loop pass.
constexpr uint32_t REFRESH_TICK_MS = 250;
// InputTiming is resent periodically: a rebooted Sigil falls back to the
// defaults and has no other way to ask for its thresholds again.
constexpr uint32_t TIMING_RESEND_MS = 10000;

struct AppliedPrefs {
  bool valid = false;
  AccessibilityPrefs prefs;
  bool timingSent = false;
  uint32_t timingSentMs = 0;
};

AppliedPrefs applied[MAX_PHYSICAL_SIGILS];
uint8_t nextSigil = 0;
uint32_t lastTickMs = 0;

const TurnHub::LedCueProfile &ledProfileFor(LedStyle style) {
  switch (style) {
    case LedStyle::ReducedMotion: return TurnHub::reducedMotionLedCueProfile();
    case LedStyle::MonochromeSafe: return TurnHub::monochromeSafeLedCueProfile();
    default: return TurnHub::defaultLedCueProfile();
  }
}

void mergeProfile(const String &profileId, AccessibilityPrefs &merged, bool &any) {
  if (profileId.length() == 0) return;
  AccessibilityPrefs prefs;
  TurnHubProfiles::loadAccessibilityForProfile(profileId, prefs);  // Defaults on failure.
  merged = any ? TurnHubProfiles::mergeSeatPrefs(merged, prefs) : prefs;
  any = true;
}

void sendTiming(uint8_t sigilId, AppliedPrefs &state, uint32_t nowMs) {
  const TurnHub::SigilRecord *record = sigilBus.record(sigilId);
  if (record == nullptr || !record->helloInfoValid ||
      (record->capabilities & TurnHubProtocol::CAPABILITY_INPUT_TIMING) == 0 ||
      !sigilBus.isOnline(sigilId, nowMs)) {
    state.timingSent = false;  // Send as soon as it (re)appears.
    return;
  }
  if (state.timingSent && nowMs - state.timingSentMs < TIMING_RESEND_MS) return;
  if (sigilBus.send(sigilId, TurnHubProtocol::PacketType::InputTiming,
          TurnHubProtocol::encodeInputTiming(state.prefs.longPressMs, state.prefs.winHoldMs))) {
    state.timingSent = true;
    state.timingSentMs = nowMs;
  }
}
}  // namespace

AccessibilityPrefs seatedAccessibility(uint8_t sigilId) {
  AccessibilityPrefs merged;
  bool any = false;
  if (sigilId >= MAX_PHYSICAL_SIGILS) return merged;
  PlayerSeat seats[2];
  const uint8_t count = game.hasPlayers() ? game.playersForController(sigilId, seats, 2) : 0;
  if (count > 0) {
    // A match uses the profiles captured at start, whoever is bound now.
    for (uint8_t i = 0; i < count; ++i) {
      mergeProfile(seats[i].profileId[0] ? String(seats[i].profileId) :
          TurnHubControllers::existingProfileForSeat(sigilId, seats[i].slot), merged, any);
    }
  } else {
    // Otherwise the Sigil's current (or last) seat bindings.
    for (uint8_t slot = 1; slot <= 2; ++slot) {
      mergeProfile(TurnHubControllers::existingProfileForSeat(sigilId, slot), merged, any);
    }
  }
  return merged;
}

void applySigilAccessibility(uint8_t sigilId, uint32_t nowMs) {
  if (sigilId >= MAX_PHYSICAL_SIGILS) return;
  AppliedPrefs &state = applied[sigilId];
  const AccessibilityPrefs prefs = seatedAccessibility(sigilId);
  const bool timingChanged = !state.valid || state.prefs.longPressMs != prefs.longPressMs ||
      state.prefs.winHoldMs != prefs.winHoldMs;
  if (!state.valid || !TurnHubProfiles::sameAccessibilityPrefs(state.prefs, prefs)) {
    serialLog.print("ATLAS|ACCESSIBILITY|SIGIL|");
    serialLog.print(sigilId);
    serialLog.print("|SOUND|");
    serialLog.print(prefs.sigilSound ? "ON" : "OFF");
    serialLog.print("|LEDS|");
    serialLog.print(TurnHubProfiles::ledStyleKey(prefs.ledStyle));
    serialLog.print("|HOLD_MS|");
    serialLog.print(prefs.longPressMs);
    serialLog.print("|");
    serialLog.println(prefs.winHoldMs);
  }
  state.prefs = prefs;
  state.valid = true;
  if (timingChanged) state.timingSent = false;

  leds.setProfile(sigilId, ledProfileFor(prefs.ledStyle));
  const uint16_t bit = AudioController::maskForSigil(sigilId);
  const uint16_t muted = audio.mutedSigils();
  audio.setMutedSigils(prefs.sigilSound ? static_cast<uint16_t>(muted & ~bit)
                                        : static_cast<uint16_t>(muted | bit));
  sendTiming(sigilId, state, nowMs);
}

void applyAllSigilAccessibility(uint32_t nowMs) {
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) applySigilAccessibility(id, nowMs);
}

void updateSigilAccessibility(uint32_t nowMs) {
  if (nowMs - lastTickMs < REFRESH_TICK_MS) return;
  lastTickMs = nowMs;
  applySigilAccessibility(nextSigil, nowMs);
  nextSigil = static_cast<uint8_t>((nextSigil + 1) % MAX_PHYSICAL_SIGILS);
}

}  // namespace TurnHubAtlas
