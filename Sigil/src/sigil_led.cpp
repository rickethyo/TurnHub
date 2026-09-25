#include "sigil_led.h"

#include <math.h>

namespace TurnHubSigil {

using TurnHubProtocol::LedCue;
using TurnHubProtocol::LedOverlay;
using TurnHubProtocol::LedStyle;

namespace {

// Palette. Hues follow the Atlas screen where they overlap; meaning never
// rests on hue alone.
constexpr Rgb WHITE{255, 255, 255};
constexpr Rgb GREEN{0, 255, 0};
constexpr Rgb BLUE{0, 90, 255};
constexpr Rgb AMBER{255, 120, 0};
constexpr Rgb MAGENTA{255, 0, 180};
constexpr Rgb RED{255, 0, 0};
constexpr Rgb CYAN{0, 200, 200};
constexpr Rgb PURPLE{150, 0, 255};
constexpr Rgb ORANGE{255, 50, 0};
constexpr Rgb GOLD{255, 170, 0};
constexpr uint8_t WAITING_LEVEL = 64;

Rgb scaled(Rgb color, uint8_t level) {
  return Rgb{static_cast<uint8_t>(color.r * level / 255), static_cast<uint8_t>(color.g * level / 255),
      static_cast<uint8_t>(color.b * level / 255)};
}

uint8_t blink(uint32_t t, uint32_t periodMs, uint32_t onMs) {
  return periodMs && t % periodMs < onMs ? 255 : 0;
}

uint8_t breathe(uint32_t t, uint32_t periodMs) {
  const float phase = static_cast<float>(t % periodMs) / static_cast<float>(periodMs);
  const float sine = (sinf(phase * 6.2831853f - 1.5707963f) + 1.0f) / 2.0f;
  return static_cast<uint8_t>(powf(sine, 2.2f) * 255.0f);
}

// count pulses of onMs (count 1 = seat A, 2 = seat B), then dark to periodMs.
uint8_t pulses(uint32_t t, uint32_t periodMs, uint32_t onMs, uint8_t count) {
  const uint32_t p = t % periodMs;
  for (uint8_t i = 0; i < count; ++i) {
    if (p >= i * 2 * onMs && p < i * 2 * onMs + onMs) return 255;
  }
  return 0;
}

// Player number as flashes: n flashes, then a gap.
uint8_t countFlashes(uint32_t t, uint8_t n) {
  constexpr uint32_t FLASH_MS = 360, ON_MS = 180, GAP_MS = 3000;
  if (n == 0) return 0;
  const uint32_t p = t % (n * FLASH_MS + GAP_MS);
  return p < n * FLASH_MS && p % FLASH_MS < ON_MS ? 255 : 0;
}

struct Look {
  Rgb color;
  uint8_t level;  // Temporal level now (0-255).
};

bool reduced(LedStyle style) { return style == LedStyle::ReducedMotion; }
bool mono(LedStyle style) { return style == LedStyle::MonochromeSafe; }

// Slow blink used wherever reduced motion replaces faster movement.
uint8_t slow(uint32_t t) { return blink(t, 4000, 2000); }

// Seat-focused cues pulse once (A) or twice (B) when the Sigil is shared,
// and stay steady when it carries one seat.
uint8_t seatLevel(const TurnHubProtocol::LedStateFields &s, uint32_t t, uint32_t onMs) {
  if (reduced(s.style)) return 255;
  return s.sharedSeat ? pulses(t, 1800, onMs, s.seatSlot) : 255;
}

// Primary cue as one color and a temporal level.
Look cueLook(const TurnHubProtocol::LedStateFields &s, uint32_t now, uint32_t anchored) {
  const bool rm = reduced(s.style);
  switch (s.cue) {
    case LedCue::Unassigned: return {WHITE, rm ? slow(now) : blink(now, 1500, 500)};
    case LedCue::Joined: return {CYAN, rm ? static_cast<uint8_t>(80) : countFlashes(now, s.playerNumber)};
    case LedCue::Starting: return {AMBER, rm ? static_cast<uint8_t>(255) : blink(anchored, 1000, 250)};
    case LedCue::TurnStarted: return {GREEN, rm ? static_cast<uint8_t>(255) : blink(anchored, 400, 200)};
    case LedCue::YourTurn: return {GREEN, rm ? static_cast<uint8_t>(255) : breathe(now, 2600)};
    case LedCue::Waiting: return {BLUE, WAITING_LEVEL};
    case LedCue::Paused: return {AMBER, rm ? slow(now) : breathe(now, 2600)};
    case LedCue::ConfirmationNeeded: return {MAGENTA, seatLevel(s, now, 180)};
    case LedCue::EliminationSelect: return {RED, seatLevel(s, now, mono(s.style) ? 600 : 180)};
    default: return Look{Rgb(), 0};
  }
}

// Highest-priority overlay for the center pixel, if any. Host is lowest and
// is left off the one-LED view, where it would hide the cue.
bool overlayLook(const TurnHubProtocol::LedStateFields &s, uint32_t now, bool includeHost, Look &out) {
  const auto has = [&s](LedOverlay o) { return (s.overlays & (1u << static_cast<uint8_t>(o))) != 0; };
  const bool rm = reduced(s.style);
  if (has(LedOverlay::TimerExpired)) { out = {RED, 255}; return true; }
  if (has(LedOverlay::TurnWarning)) { out = {ORANGE, rm ? slow(now) : blink(now, 1500, 750)}; return true; }
  if (has(LedOverlay::Winner)) { out = {GOLD, seatLevel(s, now, 180)}; return true; }
  if (has(LedOverlay::Starter)) { out = {WHITE, seatLevel(s, now, 180)}; return true; }
  if (has(LedOverlay::LongTurn)) {
    out = {CYAN, rm || !mono(s.style) ? static_cast<uint8_t>(255) : blink(now, 4000, 250)};
    return true;
  }
  if (includeHost && has(LedOverlay::Host)) { out = {PURPLE, 255}; return true; }
  return false;
}

}  // namespace

void SigilLedModel::applyLedState(int32_t value, uint32_t nowMs) {
  state_ = TurnHubProtocol::decodeLedState(value);
  semantic_ = true;
  anchorMs_ = nowMs - state_.anchorAgeMs;
}

void SigilLedModel::applyLegacyRed(bool on) { semantic_ = false; legacyRed_ = on; }
void SigilLedModel::applyLegacyGreen(bool on) { semantic_ = false; legacyGreen_ = on; }
void SigilLedModel::applyLegacyBlue(uint8_t level) { semantic_ = false; legacyBlue_ = level; }

void SigilLedModel::clear() {
  state_ = TurnHubProtocol::LedStateFields();
  semantic_ = false;
  legacyRed_ = legacyGreen_ = false;
  legacyBlue_ = 0;
  passAck_ = false;
}

void SigilLedModel::setPairing(bool active, uint32_t nowMs) {
  if (active && !pairing_) pairingStartMs_ = nowMs;
  pairing_ = active;
}

void SigilLedModel::flashPassAck(uint32_t nowMs) {
  passAck_ = true;
  passAckUntilMs_ = nowMs + PASS_ACK_FLASH_MS;
}

LedFrame SigilLedModel::render(uint32_t nowMs) const {
  LedFrame frame;
  const auto fill = [&frame](Rgb color) {
    for (auto &pixel : frame.pixels) pixel = color;
    frame.single = color;
  };

  // Sigil-local states win: Atlas cannot see them.
  if (pairing_) {
    const bool rm = reduced(state_.style);
    fill(scaled(RED, rm ? static_cast<uint8_t>(255) : blink(nowMs - pairingStartMs_, 500, 250)));
    return frame;
  }
  if (passAck_ && static_cast<int32_t>(nowMs - passAckUntilMs_) < 0) {
    fill(GREEN);
    return frame;
  }

  if (!semantic_) {
    fill(Rgb{static_cast<uint8_t>(legacyRed_ ? 255 : 0), static_cast<uint8_t>(legacyGreen_ ? 255 : 0),
        legacyBlue_});
    return frame;
  }

  const TurnHubProtocol::LedStateFields &s = state_;
  const uint32_t anchored = nowMs - anchorMs_;
  const Look cue = cueLook(s, nowMs, anchored);
  const Rgb cueColor = scaled(cue.color, cue.level);

  // Ring (1-6).
  switch (s.cue) {
    case LedCue::Unassigned:
      if (reduced(s.style)) {
        for (uint8_t i = 1; i < LED_PIXELS; ++i) frame.pixels[i] = cueColor;
      } else {
        // One white pixel circling: "join me".
        frame.pixels[1 + (nowMs / 250) % 6] = WHITE;
      }
      break;
    case LedCue::Joined:
      // Player number as that many steady ring pixels.
      for (uint8_t i = 1; i < LED_PIXELS && i <= s.playerNumber; ++i) frame.pixels[i] = CYAN;
      break;
    case LedCue::ConfirmationNeeded:
    case LedCue::EliminationSelect:
      // Shared Sigil: only the focused seat's half (A = 1-3, B = 4-6).
      for (uint8_t i = 1; i < LED_PIXELS; ++i) {
        const bool seatB = i >= 4;
        if (!s.sharedSeat || seatB == (s.seatSlot == 2)) frame.pixels[i] = cueColor;
      }
      break;
    default:
      for (uint8_t i = 1; i < LED_PIXELS; ++i) frame.pixels[i] = cueColor;
      break;
  }

  Look overlay{Rgb(), 0};
  const bool hasOverlay = overlayLook(s, nowMs, true, overlay);
  // A winner's whole ring celebrates in gold once the game is over.
  if (s.cue == LedCue::GameOver && hasOverlay && overlay.color == GOLD) {
    for (uint8_t i = 1; i < LED_PIXELS; ++i) frame.pixels[i] = scaled(GOLD, overlay.level);
  }
  // Center: the top overlay, else the cue (Joined 7+ lights it too).
  if (hasOverlay) {
    frame.pixels[LED_CENTER] = scaled(overlay.color, overlay.level);
  } else if (s.cue == LedCue::Joined) {
    if (s.playerNumber > 6) frame.pixels[LED_CENTER] = CYAN;
  } else if (s.cue != LedCue::Unassigned || reduced(s.style)) {
    frame.pixels[LED_CENTER] = cueColor;
  }

  // One LED: an overlay (not Host) replaces the cue; otherwise the cue's
  // temporal form (player number as flashes, seat as one or two pulses).
  Look single{Rgb(), 0};
  if (overlayLook(s, nowMs, false, single)) {
    frame.single = scaled(single.color, single.level);
  } else {
    frame.single = cueColor;
  }
  return frame;
}

}  // namespace TurnHubSigil
