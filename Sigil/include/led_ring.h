#pragma once

// The optional WS2812 LED ring: what it shows for the Sigil's state. Pure
// logic, host-tested; the board driver only shifts the frame out. The ring
// repeats what the screen already says (turn, attention, pairing, Atlas
// lost), so nothing depends on it (ACCESSIBILITY.md).

#include <stdint.h>

#include "protocol.h"

namespace TurnHubSigil {

constexpr uint8_t RING_MAX_LEDS = 24;
// About 20 mA per channel at full drive. The board shares USB's ~500 mA with
// the backlight and radio, so the whole ring is held to this many mA.
constexpr uint16_t RING_BUDGET_MA = 200;
constexpr uint16_t RING_MA_PER_CHANNEL = 20;
// Overall level, out of 255: bare WS2812s at full drive are glaring at a
// table (first ring on the bench, 2026-10-30), so every frame is scaled down.
#ifndef TURNHUB_RING_BRIGHTNESS
#define TURNHUB_RING_BRIGHTNESS 48
#endif
constexpr uint8_t RING_BRIGHTNESS = TURNHUB_RING_BRIGHTNESS;

struct RingInput {
  bool paired = false;
  bool atlasLost = false;
  bool pairing = false;
  int32_t state = 0;  // The DisplayState payload (protocol.h).
};

struct RingPixel {
  uint8_t r, g, b;
};

// Full-saturation seat hue, the same hue order as the screen's seat tokens.
inline RingPixel ringSeatColor(uint8_t number, uint8_t value) {
  static const uint16_t hues[] = {38, 168, 12, 205, 95, 280, 330, 60};
  const uint16_t h = hues[(number + 7) % 8];
  const uint8_t sector = static_cast<uint8_t>(h / 60);
  const uint16_t f = static_cast<uint16_t>((h % 60) * value / 60);
  const uint8_t up = static_cast<uint8_t>(f), down = static_cast<uint8_t>(value - f);
  switch (sector) {
    case 0: return {value, up, 0};
    case 1: return {down, value, 0};
    case 2: return {0, value, up};
    case 3: return {0, down, value};
    case 4: return {up, 0, value};
    default: return {value, 0, down};
  }
}

// A 0..255..0 triangle over periodMs.
inline uint8_t ringPulse(uint32_t nowMs, uint32_t periodMs) {
  const uint32_t t = nowMs % periodMs, half = periodMs / 2;
  return static_cast<uint8_t>((t < half ? t : periodMs - t) * 255 / half);
}

inline RingPixel ringScale(RingPixel p, uint8_t level) {
  return {static_cast<uint8_t>(p.r * level / 255), static_cast<uint8_t>(p.g * level / 255),
      static_cast<uint8_t>(p.b * level / 255)};
}

// Scales the frame down so it never draws more than RING_BUDGET_MA.
inline void ringLimitCurrent(RingPixel *frame, uint8_t count) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < count; ++i) sum += frame[i].r + frame[i].g + frame[i].b;
  const uint32_t budget = static_cast<uint32_t>(RING_BUDGET_MA) * 255 / RING_MA_PER_CHANNEL;
  if (sum <= budget) return;
  const uint8_t level = static_cast<uint8_t>(budget * 255 / sum);
  for (uint8_t i = 0; i < count; ++i) frame[i] = ringScale(frame[i], level);
}

// The game states, once paired and linked.
inline void renderGame(int32_t state, uint32_t nowMs, RingPixel *frame, uint8_t count) {
  using namespace TurnHubProtocol;
  const DisplayMode mode = displayMode(state);
  const uint8_t player = displayPrimaryPlayer(state);
  if (hasDisplayFlag(state, DISPLAY_FLAG_ATTENTION)) {
    const RingPixel amber = ringScale({255, 120, 0}, ringPulse(nowMs, 800));
    for (uint8_t i = 0; i < count; ++i) frame[i] = amber;
  } else if (mode == DisplayMode::GameOver && hasDisplayFlag(state, DISPLAY_FLAG_WINNER)) {
    for (uint8_t i = 0; i < count; ++i) {
      frame[i] = ringSeatColor(static_cast<uint8_t>(i + nowMs / 90), 200);
    }
  } else if (mode == DisplayMode::Running && hasDisplayFlag(state, DISPLAY_FLAG_ACTIVE)) {
    const RingPixel seat = ringSeatColor(player, 200);
    for (uint8_t i = 0; i < count; ++i) frame[i] = seat;
  } else if (mode == DisplayMode::Paused) {
    const RingPixel seat = ringSeatColor(player, static_cast<uint8_t>(10 + ringPulse(nowMs, 3000) / 6));
    for (uint8_t i = 0; i < count; i += 2) frame[i] = seat;
  } else if (mode == DisplayMode::Lobby || mode == DisplayMode::Ready || mode == DisplayMode::Starting) {
    const RingPixel seat = ringSeatColor(player, static_cast<uint8_t>(8 + ringPulse(nowMs, 2400) / 5));
    for (uint8_t i = 0; i < count; ++i) frame[i] = seat;
  }
}

// One frame: a spinner while unpaired, pairing or Atlas is lost; the seat
// colour breathing in the lobby; the full ring on this Sigil's turn; amber
// pulses on attention; a chase for the winner; otherwise dark.
// centre: LED 0 sits in the middle of the others (a 7-LED "jewel"); spinners
// and chases then run round LEDs 1.., and the centre follows the fill.
inline void renderRing(const RingInput &in, uint32_t nowMs, RingPixel *frame, uint8_t count,
    bool centre = false) {
  if (count > RING_MAX_LEDS) count = RING_MAX_LEDS;
  if (centre && count > 1) {
    renderRing(in, nowMs, frame + 1, static_cast<uint8_t>(count - 1));
    const bool spinning = in.pairing || !in.paired || in.atlasLost;
    frame[0] = spinning ? RingPixel{0, 0, 0} : frame[1];
    ringLimitCurrent(frame, count);
    return;
  }
  for (uint8_t i = 0; i < count; ++i) frame[i] = {0, 0, 0};
  if (count == 0) return;
  auto spinner = [&](RingPixel color, uint32_t stepMs) {
    const uint8_t head = static_cast<uint8_t>((nowMs / stepMs) % count);
    frame[head] = color;
    frame[(head + count - 1) % count] = ringScale(color, 64);
  };
  if (in.pairing) {
    spinner({0, 40, 120}, 60);
  } else if (!in.paired) {
    spinner({40, 40, 40}, 150);
  } else if (in.atlasLost) {
    spinner({120, 50, 0}, 120);
  } else {
    renderGame(in.state, nowMs, frame, count);
  }
  for (uint8_t i = 0; i < count; ++i) frame[i] = ringScale(frame[i], RING_BRIGHTNESS);
  ringLimitCurrent(frame, count);
}

// The board driver (sigil_ring_e32r28t.cpp).
void ringBegin();
void ringShow(const RingPixel *frame, uint8_t count);
// Serial bench commands: "ring test", "ring rgb", "ring rgbw" (RAM only).
bool ringHandleCommand(const char *line);

}  // namespace TurnHubSigil
