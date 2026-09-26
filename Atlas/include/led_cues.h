#pragma once

#include <Arduino.h>
#include <math.h>

#include "protocol.h"

namespace TurnHub {

// LED presentation is two separate steps:
//
//   game semantics -> SigilLedState (which cue, which facets)   LedRenderer
//   SigilLedState  -> channel levels via an LedCueProfile       this header
//
// Selection never looks at colors or cadences, and styles never look at game
// state, so presentation (palettes, accessibility alternatives, future user
// settings) can change without touching game logic. Every cue must stay
// distinguishable by pattern/cadence, not only by hue (ACCESSIBILITY.md), and
// essential meaning also reaches the portal/app as text.
// The cue and overlay vocabulary is the radio contract (protocol.h): Sigils
// with CAPABILITY_LED_STATE receive it and render it themselves.
using LedCue = TurnHubProtocol::LedCue;
using LedOverlay = TurnHubProtocol::LedOverlay;

constexpr uint8_t ledOverlayBit(LedOverlay overlay) {
  return static_cast<uint8_t>(1u << static_cast<uint8_t>(overlay));
}

// Everything the style step needs; produced by selection, never by styles.
struct SigilLedState {
  LedCue cue = LedCue::Off;
  uint8_t overlays = 0;
  uint8_t playerNumber = 0;  // Joined: number of flashes.
  uint8_t seatSlot = 1;      // SeatPulse target: 1 = A (one pulse), 2 = B (two).
  bool sharedSeat = false;   // SeatPulse is steady when the Sigil has one seat.
  uint32_t anchorMs = 0;     // Start of the cue (turn start, countdown start).

  bool has(LedOverlay overlay) const { return (overlays & ledOverlayBit(overlay)) != 0; }
};

enum class LedPattern : uint8_t {
  Off,
  Solid,
  Blink,        // On for onMs of every periodMs.
  Breathe,      // Smooth fade over periodMs (PWM channels; digital: upper half).
  PlayerCount,  // playerNumber flashes (onMs on, periodMs-onMs off), then delayMs dark.
  SeatPulse,    // One (seat A) or two (seat B) onMs pulses per periodMs; steady if unshared.
  Window,       // On from delayMs for onMs within each periodMs.
  Dim,          // Steady at level onMs (0-255). PWM (blue) only; digital channels read it as off.
};

// Plain aggregate (value-initialized = Off) so styles can be brace-built.
struct ChannelStyle {
  LedPattern pattern;
  uint16_t periodMs;
  uint16_t onMs;
  uint16_t delayMs;
  bool anchored;  // Phase from SigilLedState::anchorMs instead of uptime.
};

struct CueStyle {
  ChannelStyle blue, red, green;
};

struct LedCueProfile {
  CueStyle cues[static_cast<uint8_t>(LedCue::Count)];
  CueStyle overlays[static_cast<uint8_t>(LedOverlay::Count)];

  const CueStyle &cue(LedCue value) const { return cues[static_cast<uint8_t>(value)]; }
  const CueStyle &overlay(LedOverlay value) const { return overlays[static_cast<uint8_t>(value)]; }
};

namespace LedStyles {
constexpr ChannelStyle off() { return ChannelStyle{}; }
constexpr ChannelStyle solid() { return ChannelStyle{LedPattern::Solid, 0, 0, 0, false}; }
constexpr ChannelStyle blink(uint16_t period, uint16_t on, bool anchored = false) {
  return ChannelStyle{LedPattern::Blink, period, on, 0, anchored};
}
constexpr ChannelStyle breathe(uint16_t period) { return ChannelStyle{LedPattern::Breathe, period, 0, 0, false}; }
constexpr ChannelStyle playerCount(uint16_t block, uint16_t on, uint16_t gap) {
  return ChannelStyle{LedPattern::PlayerCount, block, on, gap, false};
}
constexpr ChannelStyle seatPulse(uint16_t period, uint16_t on) {
  return ChannelStyle{LedPattern::SeatPulse, period, on, 0, false};
}
constexpr ChannelStyle window(uint16_t period, uint16_t start, uint16_t on) {
  return ChannelStyle{LedPattern::Window, period, on, start, false};
}
constexpr ChannelStyle dim(uint8_t level) { return ChannelStyle{LedPattern::Dim, 0, level, 0, false}; }
}  // namespace LedStyles

// The prototype's established behavior. Cadences stay at or below 2.5 Hz.
inline const LedCueProfile &defaultLedCueProfile() {
  using namespace LedStyles;
  static const LedCueProfile profile = [] {
    LedCueProfile p{};
    auto cue = [&p](LedCue c) -> CueStyle & { return p.cues[static_cast<uint8_t>(c)]; };
    auto overlay = [&p](LedOverlay o) -> CueStyle & { return p.overlays[static_cast<uint8_t>(o)]; };
    // Blue, green, red in turn: "press Action to join".
    cue(LedCue::Unassigned) = {window(1500, 0, 500), window(1500, 1000, 500), window(1500, 500, 500)};
    cue(LedCue::Joined) = {off(), playerCount(360, 180, 3000), off()};
    cue(LedCue::Starting) = {blink(1000, 250, true), off(), off()};
    cue(LedCue::TurnStarted) = {blink(400, 200, true), off(), off()};
    cue(LedCue::YourTurn) = {breathe(2600), off(), off()};
    cue(LedCue::Waiting) = {solid(), off(), off()};
    cue(LedCue::Paused) = {breathe(2600), off(), off()};
    cue(LedCue::ConfirmationNeeded) = {off(), off(), seatPulse(1800, 180)};
    cue(LedCue::EliminationSelect) = {off(), seatPulse(1800, 180), off()};
    cue(LedCue::GameOver) = {off(), off(), off()};
    cue(LedCue::Pairing) = {off(), blink(500, 250), off()};
    cue(LedCue::Disconnected) = {off(), off(), off()};
    cue(LedCue::Error) = {off(), blink(2000, 1000), off()};
    overlay(LedOverlay::Host) = {off(), off(), solid()};
    overlay(LedOverlay::Starter) = {seatPulse(1800, 180), off(), off()};
    overlay(LedOverlay::Winner) = {seatPulse(1800, 180), off(), off()};
    // Timer: a slow red pulse while time runs short, steady red once expired,
    // steady green for a long untimed turn. Pulse versus steady differs by
    // cadence, not only by color.
    overlay(LedOverlay::TurnWarning) = {off(), blink(1500, 750), off()};
    overlay(LedOverlay::TimerExpired) = {off(), solid(), off()};
    overlay(LedOverlay::LongTurn) = {off(), off(), solid()};
    return p;
  }();
  return profile;
}

// Reduced motion: no breathing, pulsing or counting flashes. Lights are steady
// or blink slowly (at most one 2-second change per 4 seconds), and your turn
// versus waiting is bright versus dim blue. Cues that share a situation also
// differ without color (steady versus slow blink), so this style is
// monochrome-safe too. Details a slow light cannot carry (player number, which
// shared seat) remain on the e-ink display and in the portal/app.
inline const LedCueProfile &reducedMotionLedCueProfile() {
  using namespace LedStyles;
  static const LedCueProfile profile = [] {
    LedCueProfile p{};
    auto cue = [&p](LedCue c) -> CueStyle & { return p.cues[static_cast<uint8_t>(c)]; };
    auto overlay = [&p](LedOverlay o) -> CueStyle & { return p.overlays[static_cast<uint8_t>(o)]; };
    const ChannelStyle slow = blink(4000, 2000);
    cue(LedCue::Unassigned) = {off(), off(), slow};
    cue(LedCue::Joined) = {dim(40), off(), off()};
    cue(LedCue::Starting) = {solid(), off(), off()};
    cue(LedCue::TurnStarted) = {solid(), off(), off()};
    cue(LedCue::YourTurn) = {solid(), off(), off()};
    cue(LedCue::Waiting) = {dim(40), off(), off()};
    cue(LedCue::Paused) = {slow, off(), off()};
    cue(LedCue::ConfirmationNeeded) = {off(), solid(), off()};
    cue(LedCue::EliminationSelect) = {off(), off(), slow};
    cue(LedCue::GameOver) = {off(), off(), off()};
    cue(LedCue::Pairing) = {off(), solid(), off()};
    cue(LedCue::Disconnected) = {off(), off(), off()};
    cue(LedCue::Error) = {off(), slow, off()};
    overlay(LedOverlay::Host) = {off(), off(), solid()};
    overlay(LedOverlay::Starter) = {solid(), off(), off()};
    overlay(LedOverlay::Winner) = {solid(), off(), off()};
    overlay(LedOverlay::TurnWarning) = {off(), slow, off()};
    overlay(LedOverlay::TimerExpired) = {off(), solid(), off()};
    overlay(LedOverlay::LongTurn) = {off(), off(), slow};
    return p;
  }();
  return profile;
}

// Monochrome-safe: the standard cadences, changed only where two cues that can
// appear in the same situation differed by color alone. Timer expired (steady
// red) versus long turn (was steady green) now differ by cadence, and a win
// confirmation (short pulses) versus choosing an elimination (long pulses).
inline const LedCueProfile &monochromeSafeLedCueProfile() {
  using namespace LedStyles;
  static const LedCueProfile profile = [] {
    LedCueProfile p = defaultLedCueProfile();
    p.cues[static_cast<uint8_t>(LedCue::EliminationSelect)] = {off(), seatPulse(1800, 600), off()};
    p.overlays[static_cast<uint8_t>(LedOverlay::LongTurn)] = {off(), off(), blink(4000, 250)};
    return p;
  }();
  return profile;
}

inline uint8_t breatheLevel(uint32_t nowMs, uint16_t periodMs) {
  if (periodMs == 0) return 0;
  const float phase = static_cast<float>(nowMs % periodMs) / static_cast<float>(periodMs);
  const float sine = (sinf(phase * 2.0f * PI - PI / 2.0f) + 1.0f) / 2.0f;
  return static_cast<uint8_t>(powf(sine, 2.2f) * 255.0f);
}

// 0-255 level for one channel. Digital channels treat >= 128 as on.
inline uint8_t ledChannelLevel(const ChannelStyle &style, const SigilLedState &state, uint32_t nowMs) {
  const uint32_t t = style.anchored ? nowMs - state.anchorMs : nowMs;
  switch (style.pattern) {
    case LedPattern::Solid:
      return 255;
    case LedPattern::Blink:
      return style.periodMs && t % style.periodMs < style.onMs ? 255 : 0;
    case LedPattern::Breathe:
      return breatheLevel(t, style.periodMs);
    case LedPattern::PlayerCount: {
      if (state.playerNumber == 0 || style.periodMs == 0) return 0;
      const uint32_t length = static_cast<uint32_t>(state.playerNumber) * style.periodMs + style.delayMs;
      const uint32_t position = t % length;
      return position < static_cast<uint32_t>(state.playerNumber) * style.periodMs &&
              position % style.periodMs < style.onMs ? 255 : 0;
    }
    case LedPattern::SeatPulse: {
      if (!state.sharedSeat) return 255;
      if (style.periodMs == 0) return 0;
      const uint32_t position = t % style.periodMs;
      const uint32_t second = 2u * style.onMs;
      const bool on = position < style.onMs ||
          (state.seatSlot == 2 && position >= second && position < second + style.onMs);
      return on ? 255 : 0;
    }
    case LedPattern::Dim:
      return static_cast<uint8_t>(style.onMs > 255 ? 255 : style.onMs);
    case LedPattern::Window:
      if (style.periodMs == 0) return 0;
      return t % style.periodMs >= style.delayMs && t % style.periodMs < style.delayMs + style.onMs ? 255 : 0;
    case LedPattern::Off:
    default:
      return 0;
  }
}

struct LedLevels {
  uint8_t blue = 0;
  bool red = false;
  bool green = false;
};

// Primary cue plus every active overlay; overlapping channels take the
// brighter value.
inline LedLevels ledLevels(const LedCueProfile &profile, const SigilLedState &state, uint32_t nowMs) {
  uint8_t blue = 0, red = 0, green = 0;
  auto apply = [&](const CueStyle &style) {
    const uint8_t b = ledChannelLevel(style.blue, state, nowMs);
    const uint8_t r = ledChannelLevel(style.red, state, nowMs);
    const uint8_t g = ledChannelLevel(style.green, state, nowMs);
    if (b > blue) blue = b;
    if (r > red) red = r;
    if (g > green) green = g;
  };
  apply(profile.cue(state.cue));
  for (uint8_t i = 0; i < static_cast<uint8_t>(LedOverlay::Count); ++i) {
    if (state.has(static_cast<LedOverlay>(i))) apply(profile.overlays[i]);
  }
  LedLevels levels;
  levels.blue = blue;
  levels.red = red >= 128;
  levels.green = green >= 128;
  return levels;
}

}  // namespace TurnHub
