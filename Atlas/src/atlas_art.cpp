// Atlas touchscreen art: the Brass theme's drawing of the AtlasScreen
// (touch_controls.h) and the splash, onto any LovyanGFX target: the panel on
// the device (atlas_display.cpp), a sprite in the host preview
// (tests/host/render_atlas_screens.cpp). Presentation only; firmware and the
// preview build it, the host test runners do not (it needs LovyanGFX).

#include "atlas_art.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avatars.h"
#include "brass_fonts.h"
#include "firmware_version.h"

namespace TurnHubAtlas {

namespace {

// Fonts: LovyanGFX's DejaVu (Bitstream Vera license) and glcd fonts, and the
// Brass fonts rendered from Cinzel and Oswald (SIL OFL 1.1; brass_fonts.h,
// built by tools/fonts/make_fonts.py). The GNU FreeFont "Free*" fonts bundled
// with LovyanGFX are GPL and not used.

// --- Brass theme ---------------------------------------------------------------
//
// The portal's Brass look (WEB_PORTAL_DESIGN.md) carried onto the TFT
// (2026-09-29): a walnut ground, riveted plates, polished brass for the
// header and the main actions, an engraved serif (Cinzel) for words and
// lining figures (Oswald) for numbers. Gears, rivets and the gauge are
// decoration: every state a color marks is also written out or drawn as a
// shape (ACCESSIBILITY.md).

constexpr int16_t W = ATLAS_SCREEN_WIDTH;
constexpr int16_t PAD = 8;
constexpr int16_t CLOCK_W = 84;
constexpr int16_t HERO_H = 52;        // Title and detail lines.
constexpr int16_t BAR_Y = SCREEN_HERO_Y + HERO_H;
constexpr int16_t BAR_H = 4;
constexpr int16_t BODY_H = BUTTON_ROW_Y - 4 - SCREEN_BODY_Y;
constexpr int16_t QR_COLUMN_W = 150;  // QR screen: code on the left.
constexpr int16_t GAUGE_W = 76;
constexpr int16_t GAUGE_H = HERO_H;
// The header and the gauge are buffered in bands to save RAM (16-bit color:
// 8.3 KB and 4 KB instead of 16.6 KB and 7.9 KB).
constexpr int16_t HEADER_BAND_H = SCREEN_HEADER_H / 2;
constexpr int16_t GAUGE_BAND_H = GAUGE_H / 2;
static_assert(SCREEN_HEADER_H % HEADER_BAND_H == 0 && GAUGE_H % GAUGE_BAND_H == 0,
              "bands must tile the header and the gauge");
constexpr int16_t GAUGE_X = W - CLOCK_W + (CLOCK_W - GAUGE_W) / 2;
constexpr uint32_t GEAR_FRAME_MS = 100;  // The header gear turns while a game runs.

using Gfx = lgfx::LovyanGFX;  // The panel and its sprites alike.

// Where the art goes: the panel on the device, a sprite in the host preview.
Gfx *target = nullptr;
Gfx &tft() { return *target; }

// A Brass font, loaded once from flash. Falls back to a DejaVu size if its
// VLW data cannot be read, so text always shows.
struct BrassFont {
  lgfx::PointerWrapper data;
  lgfx::VLWfont font;
  const lgfx::IFont *fallback;
  bool ready = false;
  explicit BrassFont(const lgfx::IFont *fb) : fallback(fb) {}
  void load(const uint8_t *vlw, size_t size) {
    data.set(vlw, size);
    ready = font.loadFont(&data);
  }
  const lgfx::IFont *get() const { return ready ? static_cast<const lgfx::IFont *>(&font) : fallback; }
};

BrassFont titleFont(&fonts::DejaVu24);
BrassFont labelFont(&fonts::DejaVu18);
BrassFont nameFont(&fonts::DejaVu12);
BrassFont tagFont(&fonts::DejaVu9);
BrassFont numeralsFont(&fonts::DejaVu24);
BrassFont clockFont(&fonts::DejaVu12);
BrassFont codeFont(&fonts::DejaVu40);
BrassFont wordmarkFont(&fonts::DejaVu40);

bool loadBrassFonts() {
  titleFont.load(BrassFonts::TitleVlw, BrassFonts::TitleVlwSize);
  labelFont.load(BrassFonts::LabelVlw, BrassFonts::LabelVlwSize);
  nameFont.load(BrassFonts::NameVlw, BrassFonts::NameVlwSize);
  tagFont.load(BrassFonts::TagVlw, BrassFonts::TagVlwSize);
  numeralsFont.load(BrassFonts::NumeralsVlw, BrassFonts::NumeralsVlwSize);
  clockFont.load(BrassFonts::ClockVlw, BrassFonts::ClockVlwSize);
  codeFont.load(BrassFonts::CodeVlw, BrassFonts::CodeVlwSize);
  wordmarkFont.load(BrassFonts::WordmarkVlw, BrassFonts::WordmarkVlwSize);
  const bool all = titleFont.ready && labelFont.ready && nameFont.ready && tagFont.ready &&
      numeralsFont.ready && clockFont.ready && codeFont.ready && wordmarkFont.ready;
  return all;
}

// Off-screen buffers for the parts that change often, so they update without
// a visible clear: the header (the turning gear and the match clock) and the
// turn gauge. If one cannot be allocated, that part draws straight to the
// panel instead (flat brass, a still gear).
lgfx::LGFX_Sprite headerSprite;
lgfx::LGFX_Sprite gaugeSprite;
bool headerBuffered = false;
bool gaugeBuffered = false;
uint32_t lastGearFrameMs = 0;

bool createBuffers() {
  headerSprite.setColorDepth(16);
  gaugeSprite.setColorDepth(16);
  headerBuffered = headerSprite.createSprite(W, HEADER_BAND_H) != nullptr;
  gaugeBuffered = gaugeSprite.createSprite(GAUGE_W, GAUGE_BAND_H) != nullptr;
  return headerBuffered && gaugeBuffered;
}

uint32_t mix(uint32_t a, uint32_t b, uint16_t t256) {
  const auto ch = [&](uint8_t shift) {
    const uint32_t ca = (a >> shift) & 0xFF, cb = (b >> shift) & 0xFF;
    return ((ca * (256 - t256) + cb * t256) >> 8) << shift;
  };
  return ch(16) | ch(8) | ch(0);
}

// Brass bands: highlight at the top, shade at the bottom.
uint32_t brassRow(int16_t row, int16_t height) {
  const int32_t t = height > 1 ? row * 256 / (height - 1) : 0;
  return t < 115 ? mix(BRASS_HI, BRASS, static_cast<uint16_t>(t * 256 / 115))
                 : mix(BRASS, BRASS_LO, static_cast<uint16_t>((t - 115) * 256 / 141));
}

void rivet(Gfx &g, int16_t x, int16_t y) {
  g.fillCircle(x, y, 2, BRASS_DEEP);
  g.fillCircle(x, y, 1, BRASS);
  g.drawPixel(x - 1, y - 1, BRASS_HI);
}

// A rivet pressed into brass: a dark dot with a bright speck.
void brassRivet(Gfx &g, int16_t x, int16_t y) {
  g.fillCircle(x, y, 2, BRASS_LO);
  g.fillCircle(x, y, 1, BRASS_DEEP);
  g.drawPixel(x - 1, y - 1, BRASS_HI);
}

// A gear from plain geometry: a root disc with trapezoid teeth, turned by
// angle degrees, and an axle hole.
void gear(Gfx &g, int16_t cx, int16_t cy, int16_t r, uint8_t teeth, float angle, uint32_t color,
    int16_t hole, uint32_t holeColor) {
  const float root = r * 0.76f;
  g.fillCircle(cx, cy, static_cast<int32_t>(root + 0.5f), color);
  const float step = 360.0f / teeth;
  constexpr float RAD = 0.017453292f;
  for (uint8_t i = 0; i < teeth; ++i) {
    const float a = (angle + i * step) * RAD;
    const float base = step * 0.30f * RAD;
    const float tip = step * 0.20f * RAD;
    const int32_t x1 = cx + static_cast<int32_t>(cosf(a - base) * (root - 1));
    const int32_t y1 = cy + static_cast<int32_t>(sinf(a - base) * (root - 1));
    const int32_t x2 = cx + static_cast<int32_t>(cosf(a - tip) * r);
    const int32_t y2 = cy + static_cast<int32_t>(sinf(a - tip) * r);
    const int32_t x3 = cx + static_cast<int32_t>(cosf(a + tip) * r);
    const int32_t y3 = cy + static_cast<int32_t>(sinf(a + tip) * r);
    const int32_t x4 = cx + static_cast<int32_t>(cosf(a + base) * (root - 1));
    const int32_t y4 = cy + static_cast<int32_t>(sinf(a + base) * (root - 1));
    g.fillTriangle(x1, y1, x2, y2, x3, y3, color);
    g.fillTriangle(x1, y1, x3, y3, x4, y4, color);
  }
  if (hole > 0) g.fillCircle(cx, cy, hole, holeColor);
}

// A walnut plate with a brass frame, a top highlight and corner rivets.
void plate(Gfx &g, int16_t x, int16_t y, int16_t w, int16_t h, uint32_t fill, uint32_t frame,
    bool rivets = true) {
  g.fillRoundRect(x, y, w, h, 5, fill);
  g.drawRoundRect(x, y, w, h, 5, frame);
  g.drawFastHLine(x + 5, y + 1, w - 10, mix(fill, CREAM, 22));
  if (rivets) {
    rivet(g, x + 5, y + 5);
    rivet(g, x + w - 6, y + 5);
    rivet(g, x + 5, y + h - 6);
    rivet(g, x + w - 6, y + h - 6);
  }
}

// Polished brass: a flat face (so text can blend onto it), bright top edge,
// shaded bottom edge, a dark rim and pressed-in rivets.
void brassFace(Gfx &g, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint32_t face) {
  g.fillRoundRect(x, y, w, h, r, face);
  g.drawFastHLine(x + r, y + 1, w - 2 * r, mix(face, BRASS_HI, 200));
  g.drawFastHLine(x + r, y + 2, w - 2 * r, mix(face, BRASS_HI, 110));
  g.drawFastHLine(x + r, y + h - 3, w - 2 * r, mix(face, BRASS_LO, 120));
  g.drawFastHLine(x + r, y + h - 2, w - 2 * r, mix(face, BRASS_LO, 220));
  g.drawRoundRect(x, y, w, h, r, BRASS_DEEP);
  brassRivet(g, x + 6, y + 6);
  brassRivet(g, x + w - 7, y + 6);
  brassRivet(g, x + 6, y + h - 7);
  brassRivet(g, x + w - 7, y + h - 7);
}

// A small diamond, the Brass look's separator.
void diamond(Gfx &g, int16_t cx, int16_t cy, int16_t r, uint32_t color) {
  g.fillTriangle(cx - r, cy, cx, cy - r, cx + r, cy, color);
  g.fillTriangle(cx - r, cy, cx, cy + r, cx + r, cy, color);
}

// A centered word between two short rules ("-- TURN --").
void ruledTag(Gfx &g, const char *tag, int16_t x, int16_t w, int16_t cy, uint32_t ink, uint32_t rule,
    uint32_t bg) {
  g.setFont(tagFont.get());
  g.setTextDatum(lgfx::middle_center);
  const int16_t tw = tag[0] ? g.textWidth(tag) : 0;
  const int16_t cx = x + w / 2;
  const int16_t gap = tag[0] ? tw / 2 + 5 : 4;
  if (cx - gap - (x + 8) > 2) {
    g.drawFastHLine(x + 8, cy, cx - gap - (x + 8), rule);
    g.drawFastHLine(cx + gap, cy, x + w - 8 - (cx + gap), rule);
  }
  if (tag[0]) {
    g.setTextColor(ink, bg);
    g.drawString(tag, cx, cy + 1);
  } else {
    diamond(g, cx, cy, 2, rule);
  }
}

// Largest DejaVu size (of three) that fits the width; for plain text lines.
const lgfx::IFont *fitFont(const char *text, int16_t width) {
  static const lgfx::IFont *const FONTS[] = {&fonts::DejaVu18, &fonts::DejaVu12, &fonts::DejaVu9};
  for (const lgfx::IFont *font : FONTS) {
    tft().setFont(font);
    if (tft().textWidth(text) <= width) return font;
  }
  return &fonts::DejaVu9;
}

// Largest Brass size that fits: label, then name, then tag.
const lgfx::IFont *fitBrass(Gfx &g, const char *text, int16_t width) {
  for (const BrassFont *font : {&labelFont, &nameFont, &tagFont}) {
    g.setFont(font->get());
    if (g.textWidth(text) <= width) return font->get();
  }
  return tagFont.get();
}

// --- Splash ------------------------------------------------------------------

void drawSplash() {
  const int16_t h = tft().height();
  tft().fillScreen(WALNUT);
  // A large gear with the turn arrow in its hub, meshed with a smaller one.
  constexpr int16_t CX = 160, CY = 84;
  gear(tft(), CX + 55, CY + 36, 19, 9, 10.0f, BRASS_DEEP, 6, WALNUT);
  gear(tft(), CX + 55, CY + 36, 17, 9, 10.0f, BRASS_LO, 6, WALNUT);
  gear(tft(), CX, CY, 52, 12, 0.0f, BRASS_DEEP, 0, 0);
  gear(tft(), CX, CY, 50, 12, 0.0f, BRASS, 23, WALNUT);
  tft().drawCircle(CX, CY, 23, BRASS_HI);
  tft().drawCircle(CX, CY, 30, BRASS_LO);
  tft().fillTriangle(CX - 8, CY - 12, CX - 8, CY + 12, CX + 13, CY, BRASS_HI);

  tft().setTextDatum(lgfx::middle_center);
  tft().setTextColor(BRASS_HI, WALNUT);
  tft().setFont(wordmarkFont.get());
  tft().drawString("TURNHUB", W / 2, 172);
  tft().drawFastHLine(60, 198, 86, BRASS_LO);
  tft().drawFastHLine(174, 198, 86, BRASS_LO);
  diamond(tft(), W / 2, 198, 4, BRASS);
  tft().setTextColor(MUTED, WALNUT);
  tft().setFont(tagFont.get());
  tft().drawString("A  T  L  A  S", W / 2, 215);

  tft().setTextDatum(lgfx::bottom_right);
  tft().setTextColor(FAINT, WALNUT);
  tft().setFont(&fonts::Font0);
  tft().drawString((String("v") + TurnHubFirmware::VERSION).c_str(), W - 6, h - 4);
  rivet(tft(), 7, 7);
  rivet(tft(), W - 8, 7);
  rivet(tft(), 7, h - 8);
  rivet(tft(), W - 8, h - 8);
}

// --- Status screens ------------------------------------------------------------
//
// Regions, top to bottom: header (state badge, round and match clock, Sigil
// count, the NO SD CARD warning), hero (title, detail or action message, the
// turn gauge and its tube), body (player chips, text lines or a QR code) and
// the button row(s). Each region redraws only when its part of the
// AtlasScreen changes; the chips' turn times and the header clock update in
// place.

bool hasClock(const AtlasScreen &screen) {
  return screen.kind == ScreenKind::Status && screen.clock[0] != '\0';
}

bool gearTurning(const AtlasScreen &screen) {
  return screen.kind == ScreenKind::Status && strcmp(screen.badge, "PLAYING") == 0;
}

// The header's text is transparent (blended onto the brass) in the buffer,
// and set on flat brass when drawn straight to the panel.
void headerInk(Gfx &g, uint32_t ink, bool buffered) {
  if (buffered) g.setTextColor(ink);
  else g.setTextColor(ink, BRASS);
}

// Draws the header with its top at y = oy (negative for a lower band).
void drawHeaderTo(Gfx &g, const AtlasScreen &screen, uint32_t nowMs, bool buffered, int16_t oy) {
  constexpr int16_t H = SCREEN_HEADER_H;
  if (buffered) {
    for (int16_t y = 0; y < H - 2; ++y) g.drawFastHLine(0, oy + y, W, brassRow(y, H - 2));
  } else {
    g.fillRect(0, oy, W, H - 2, BRASS);
    g.drawFastHLine(0, oy + 1, W, BRASS_HI);
  }
  g.drawFastHLine(0, oy + H - 2, W, BRASS_DEEP);
  g.drawFastHLine(0, oy + H - 1, W, mix(BRASS_DEEP, WALNUT, 128));
  brassRivet(g, 6, oy + H / 2 - 1);
  brassRivet(g, W - 7, oy + H / 2 - 1);

  // The gear turns while a game runs (redrawn by serviceGear).
  const float angle = gearTurning(screen) && buffered ? static_cast<float>((nowMs / 50) % 360) : 0.0f;
  gear(g, 22, oy + H / 2 - 1, 8, 8, angle, BRASS_DEEP, 3, buffered ? brassRow(H / 2 - 1, H - 2) : BRASS);

  g.setFont(labelFont.get());
  g.setTextDatum(lgfx::middle_left);
  headerInk(g, INK, buffered);
  g.drawString(screen.badge, 35, oy + H / 2);
  const int16_t leftLimit = 35 + g.textWidth(screen.badge) + 12;

  int16_t right = W - 15;
  if (screen.sdMissing) {
    // Red, and written out: the card holds the luxury records.
    g.setFont(&fonts::DejaVu9);
    const int16_t w = g.textWidth("NO SD CARD") + 12;
    g.fillRoundRect(right - w, oy + 5, w, 15, 3, DANGER_DEEP);
    g.drawRoundRect(right - w, oy + 5, w, 15, 3, 0x3A0A06);
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(CREAM, DANGER_DEEP);
    g.drawString("NO SD CARD", right - w / 2, oy + 13);
    right -= w + 8;
  }

  if (screen.update != TurnHub::UpdateKind::None) {
    // Blue, and written out (the LED's blink is only the extra cue). The full
    // wording when it fits beside the badge, else just "Update".
    g.setFont(&fonts::DejaVu9);
    const char *words = TurnHub::updateKindText(screen.update);
    int16_t w = g.textWidth(words) + 12;
    const int16_t crowd = screen.round > 0 ? 110 : 0;
    if (right - w - crowd < leftLimit) {
      words = "Update";
      w = g.textWidth(words) + 12;
    }
    g.fillRoundRect(right - w, oy + 5, w, 15, 3, UPDATE_BLUE);
    g.drawRoundRect(right - w, oy + 5, w, 15, 3, UPDATE_BLUE_EDGE);
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(CREAM, UPDATE_BLUE);
    g.drawString(words, right - w / 2, oy + 13);
    right -= w + 8;
  }

  char sigils[16];
  snprintf(sigils, sizeof(sigils), "%u %s", static_cast<unsigned>(screen.sigilsOnline),
      screen.sigilsOnline == 1 ? "Sigil" : "Sigils");
  char round[12] = {};
  if (screen.round > 0) snprintf(round, sizeof(round), "Round %u", static_cast<unsigned>(screen.round));
  g.setFont(nameFont.get());
  const int16_t sigilsW = g.textWidth(sigils);
  const int16_t roundW = round[0] ? g.textWidth(round) : 0;
  g.setFont(clockFont.get());
  const int16_t clockW = screen.gameClock[0] ? g.textWidth(screen.gameClock) : 0;
  const int16_t gameW = roundW + (clockW ? clockW + 14 : 0);
  // Room for everything, else the round and clock win over the Sigil count
  // (Info lists it), else the round alone.
  bool showSigils = true, showClock = clockW > 0;
  if (right - sigilsW - (gameW ? gameW + 14 : 0) < leftLimit) showSigils = false;
  if (!showSigils && right - gameW < leftLimit) showClock = false;
  if (!round[0]) showSigils = right - sigilsW >= leftLimit;

  g.setTextDatum(lgfx::middle_right);
  if (showSigils) {
    g.setFont(nameFont.get());
    headerInk(g, INK, buffered);
    g.drawString(sigils, right, oy + H / 2);
    right -= sigilsW + 8;
    if (round[0]) {
      g.drawFastVLine(right, oy + 6, H - 14, BRASS_LO);
      right -= 8;
    }
  }
  if (round[0]) {
    if (showClock) {
      g.setFont(clockFont.get());
      headerInk(g, INK, buffered);
      g.drawString(screen.gameClock, right, oy + H / 2);
      right -= clockW + 7;
      diamond(g, right, oy + H / 2 - 1, 2, BRASS_DEEP);
      right -= 7;
    }
    if (right - roundW >= leftLimit) {
      g.setFont(nameFont.get());
      headerInk(g, INK, buffered);
      g.drawString(round, right, oy + H / 2);
    }
  }
}

// Buffered, the header is drawn band by band through one strip-sized sprite
// and each band pushed whole, so it never blinks. Half the RAM of a
// full-header buffer, for twice the drawing.
void drawHeader(const AtlasScreen &screen, uint32_t nowMs) {
  if (!headerBuffered) {
    drawHeaderTo(tft(), screen, nowMs, false, 0);
    return;
  }
  for (int16_t top = 0; top < SCREEN_HEADER_H; top += HEADER_BAND_H) {
    drawHeaderTo(headerSprite, screen, nowMs, true, static_cast<int16_t>(-top));
    headerSprite.pushSprite(target, 0, top);
  }
}

// Keeps the header gear turning while a game runs (buffered header only).
void serviceGear(const AtlasScreen &screen, uint32_t nowMs) {
  if (!headerBuffered || !gearTurning(screen) || nowMs - lastGearFrameMs < GEAR_FRAME_MS) return;
  lastGearFrameMs = nowMs;
  drawHeader(screen, nowMs);
}

// QR codes, the presence code and the pairing code share one layout: a left
// column (the QR code, if any) and the big code beside it.
bool codeLayout(const AtlasScreen &screen) {
  return screen.kind == ScreenKind::Qr || screen.kind == ScreenKind::Code ||
      screen.kind == ScreenKind::PairCode;
}

// Title (engraved serif) and the detail or action line (plain sans, for
// reading at a glance).
void drawHeroLine(const AtlasScreen &screen) {
  const bool qr = codeLayout(screen);
  const int16_t x = qr ? QR_COLUMN_W : 0;
  const int16_t w = W - x - (hasClock(screen) ? CLOCK_W : 0);
  const bool notice = screen.notice[0] != '\0';
  const char *line = notice ? screen.notice : screen.detail;
  tft().setTextDatum(lgfx::top_left);
  const lgfx::IFont *font = fitFont(line, w - 2 * PAD);
  tft().setFont(font == &fonts::DejaVu18 ? &fonts::DejaVu12 : font);
  tft().setTextColor(notice ? BRASS_HI : MUTED, WALNUT);
  tft().fillRect(x + PAD, SCREEN_HERO_Y + 32, w - 2 * PAD, 16, WALNUT);
  tft().drawString(line, x + PAD, SCREEN_HERO_Y + 32);
  // An action message is marked by a small brass diamond as well as its color.
  tft().fillRect(x, SCREEN_HERO_Y + 34, PAD, 10, WALNUT);
  if (notice) diamond(tft(), x + 3, SCREEN_HERO_Y + 39, 3, BRASS);
}

void drawHero(const AtlasScreen &screen) {
  const bool qr = codeLayout(screen);
  const int16_t x = qr ? QR_COLUMN_W : 0;
  const int16_t w = W - x - (hasClock(screen) ? CLOCK_W : 0);
  tft().fillRect(x, SCREEN_HERO_Y, w, HERO_H, WALNUT);
  tft().setTextDatum(lgfx::top_left);
  tft().setTextColor(CREAM, WALNUT);
  tft().setFont(qr ? labelFont.get() : titleFont.get());
  if (tft().textWidth(screen.title) > w - 2 * PAD) tft().setFont(labelFont.get());
  if (tft().textWidth(screen.title) > w - 2 * PAD) tft().setFont(nameFont.get());
  tft().drawString(screen.title, x + PAD, SCREEN_HERO_Y + 4);
  drawHeroLine(screen);
}

// fillArc over a span that may pass 360 degrees (screen angles run
// clockwise from +x).
void arc(Gfx &g, int16_t cx, int16_t cy, int16_t r0, int16_t r1, float a0, float a1, uint32_t color) {
  if (a1 - a0 < 0.5f) return;
  if (a1 <= 360.0f) {
    g.fillArc(cx, cy, r0, r1, a0, a1, color);
  } else if (a0 >= 360.0f) {
    g.fillArc(cx, cy, r0, r1, a0 - 360.0f, a1 - 360.0f, color);
  } else {
    g.fillArc(cx, cy, r0, r1, a0, 360.0f, color);
    g.fillArc(cx, cy, r0, r1, 0.0f, a1 - 360.0f, color);
  }
}

// The turn gauge: the portal's 270-degree pressure gauge. With a countdown
// the needle and the arc show the time left, running into the red zone;
// with the timer off the needle sweeps once a minute. The digits inside it
// carry the time; the red face near zero repeats the countdown bar's warning.
void drawGaugeTo(Gfx &g, int16_t ox, int16_t oy, const AtlasScreen &screen, bool buffered) {
  const bool warn = screen.timerWarning;
  const int16_t cx = ox + GAUGE_W / 2, cy = oy + GAUGE_H / 2 + 1;
  constexpr int16_t R = 22;
  g.fillRect(ox, oy, GAUGE_W, GAUGE_H, WALNUT);
  g.fillCircle(cx, cy, R + 3, BRASS_DEEP);
  g.fillCircle(cx, cy, R + 2, BRASS);
  g.fillArc(cx, cy, R + 2, R + 1, 190, 300, BRASS_HI);
  g.fillArc(cx, cy, R + 2, R, 10, 110, BRASS_LO);
  const uint32_t face = warn ? DANGER_DEEP : DIAL;
  const uint32_t mark = warn ? 0xF0C0A0 : DIAL_INK;
  g.fillCircle(cx, cy, R, face);
  constexpr float START = 135.0f, SPAN = 270.0f, RAD = 0.017453292f;
  for (uint8_t i = 0; i <= 10; ++i) {
    const float a = (START + SPAN * i / 10) * RAD;
    const int16_t inner = i % 5 == 0 ? R - 6 : R - 4;
    g.drawLine(cx + static_cast<int16_t>(cosf(a) * (R - 2)), cy + static_cast<int16_t>(sinf(a) * (R - 2)),
        cx + static_cast<int16_t>(cosf(a) * inner), cy + static_cast<int16_t>(sinf(a) * inner), mark);
  }
  float fraction;
  if (screen.timerPermille >= 0) {
    fraction = screen.timerPermille / 1000.0f;
    arc(g, cx, cy, R - 2, R - 4, START, START + SPAN * 0.12f, 0xC0392B);
    arc(g, cx, cy, R - 8, R - 9, START, START + SPAN * fraction, warn ? DANGER : BRASS_LO);
  } else {
    // Timer off: one sweep per elapsed minute, from the clock's seconds.
    const char *colon = strrchr(screen.clock, ':');
    const int seconds = colon != nullptr ? atoi(colon + 1) : 0;
    fraction = seconds / 60.0f;
  }
  const float a = (START + SPAN * fraction) * RAD;
  const int16_t tipX = cx + static_cast<int16_t>(cosf(a) * (R - 5));
  const int16_t tipY = cy + static_cast<int16_t>(sinf(a) * (R - 5));
  const uint32_t needle = warn ? 0xFFD2C8 : INK;
  if (buffered) {
    g.drawWideLine(cx, cy, tipX, tipY, 1.0f, needle);
  } else {
    g.drawLine(cx, cy, tipX, tipY, needle);
    g.drawLine(cx + 1, cy, tipX + 1, tipY, needle);
  }
  g.fillCircle(cx, cy, 3, BRASS_LO);
  g.drawPixel(cx - 1, cy - 1, BRASS_HI);
  // The digits sit on the face below the hub; in the buffer they blend onto
  // it, on the panel they take the face color.
  g.setFont(clockFont.get());
  g.setTextDatum(lgfx::middle_center);
  if (buffered) g.setTextColor(warn ? CREAM : INK);
  else g.setTextColor(warn ? CREAM : INK, face);
  g.drawString(screen.clock, cx, cy + 10);
}

void drawTube(const AtlasScreen &screen) {
  const bool qr = codeLayout(screen);
  const int16_t x = qr ? QR_COLUMN_W : 0;
  tft().fillRect(x, BAR_Y, W - x, BAR_H, WALNUT);
  if (!hasClock(screen) || screen.timerPermille < 0) {
    // No countdown: a plain rule under the hero, with a center diamond.
    tft().drawFastHLine(x + PAD, BAR_Y + 1, W - x - 2 * PAD, BRASS_DEEP);
    diamond(tft(), x + (W - x) / 2, BAR_Y + 1, 2, BRASS_LO);
    return;
  }
  // The countdown tube: a brass-filled glass with a tick every tenth.
  constexpr int16_t TX = 6, TW = W - 12;
  tft().drawRoundRect(TX, BAR_Y, TW, BAR_H, 2, LINE);
  tft().fillRect(TX + 1, BAR_Y + 1, TW - 2, BAR_H - 2, TUBE);
  const int16_t filled = static_cast<int16_t>((TW - 2) * screen.timerPermille / 1000);
  tft().fillRect(TX + 1, BAR_Y + 1, filled, 1, screen.timerWarning ? 0xFFB0A4 : BRASS_HI);
  tft().fillRect(TX + 1, BAR_Y + 2, filled, 1, screen.timerWarning ? DANGER : BRASS_LO);
  for (uint8_t i = 1; i < 10; ++i) tft().drawFastVLine(TX + TW * i / 10, BAR_Y + 1, BAR_H - 2, TUBE);
}

// The gauge and the tube. The gauge is drawn off-screen and pushed whole,
// so its per-second update does not blink.
void drawTimer(const AtlasScreen &screen) {
  drawTube(screen);
  if (!hasClock(screen)) return;
  if (gaugeBuffered) {
    // Band by band through a half-height sprite, like the header.
    for (int16_t top = 0; top < GAUGE_H; top += GAUGE_BAND_H) {
      drawGaugeTo(gaugeSprite, 0, static_cast<int16_t>(-top), screen, true);
      gaugeSprite.pushSprite(target, GAUGE_X, SCREEN_HERO_Y + top);
    }
  } else {
    drawGaugeTo(tft(), GAUGE_X, SCREEN_HERO_Y, screen, false);
  }
}

// A small hourglass before a player's turn time.
void hourglass(int16_t x, int16_t y, uint32_t color) {
  tft().drawFastHLine(x, y, 7, color);
  tft().fillTriangle(x + 1, y + 1, x + 5, y + 1, x + 3, y + 4, color);
  tft().drawTriangle(x + 1, y + 8, x + 5, y + 8, x + 3, y + 4, color);
  tft().drawFastHLine(x, y + 9, 7, color);
}

const char *chipTag(const ScreenPlayer &p) {
  return (p.flags & CHIP_WINNER) ? "WINNER" : (p.flags & CHIP_OUT) ? "OUT" : (p.flags & CHIP_ACTIVE) ? "TURN"
      : (p.flags & CHIP_WAITING) ? "CONFIRM"
      : (p.flags & CHIP_STARTER) ? "STARTS" : "";
}

// The chip's turn-time line, redrawn in place each second. A player's time
// only grows during a game, so the slot never needs to shrink.
void drawChipTime(const ScreenPlayer &p, int16_t x, int16_t y, int16_t w, int16_t h) {
  if (!p.turnTime[0]) return;
  const bool active = p.flags & CHIP_ACTIVE;
  const bool out = p.flags & CHIP_OUT;
  const uint32_t fill = active || (p.flags & CHIP_WINNER) ? PLATE_HI : PLATE;
  const uint32_t ink = out ? DIM : (active ? BRASS_HI : FAINT);
  const bool tall = h >= 60;
  tft().setFont(&fonts::DejaVu9);
  const int16_t tw = tft().textWidth(p.turnTime);
  const int16_t top = tall ? y + h - 27 : y + h - 14;
  int16_t groupX;
  if (tall) {
    // Room for the widest time the chip can hold ("9:59:59"), centered.
    const int16_t slotW = tft().textWidth("0:00:00") + 12;
    tft().fillRect(x + (w - slotW) / 2, top, slotW, 11, fill);
    groupX = x + (w - tw - 10) / 2;
  } else {
    // Short chips: the bottom-left slot shows the tag when there is one
    // (TURN, OUT, ...), otherwise the time.
    if (chipTag(p)[0]) return;
    groupX = x + 6;
    tft().fillRect(groupX, top, tw + 10, 11, fill);
  }
  hourglass(groupX, top + 1, ink);
  tft().setTextDatum(lgfx::top_left);
  tft().setTextColor(ink, fill);
  tft().drawString(p.turnTime, groupX + 10, top + 2);
}

// A player's chip: a riveted plate. The active player's has a triple brass
// frame and a pointer; the tag line says TURN, OUT, WINNER, STARTS or
// CONFIRM in words.
void drawChip(const ScreenPlayer &p, bool showLife, int16_t x, int16_t y, int16_t w, int16_t h) {
  const bool active = p.flags & CHIP_ACTIVE;
  const bool out = p.flags & CHIP_OUT;
  const bool winner = p.flags & CHIP_WINNER;
  const bool tall = h >= 60;
  const uint32_t fill = active || winner ? PLATE_HI : PLATE;
  tft().fillRoundRect(x, y, w, h, 5, fill);
  tft().drawFastHLine(x + 5, y + 1, w - 10, mix(fill, CREAM, 22));
  if (active || winner) {
    tft().drawRoundRect(x, y, w, h, 5, BRASS_LO);
    tft().drawRoundRect(x + 1, y + 1, w - 2, h - 2, 4, BRASS);
    tft().drawRoundRect(x + 2, y + 2, w - 4, h - 4, 3, BRASS_HI);
  } else {
    tft().drawRoundRect(x, y, w, h, 5, out ? LINE_OUT : LINE);
  }
  if (tall) {
    rivet(tft(), x + 6, y + h - 7);
    rivet(tft(), x + w - 7, y + h - 7);
  }
  const uint32_t ink = out ? DIM : CREAM;
  const char *tag = chipTag(p);
  // Short chips point at the active player before the name; tall chips
  // beside the life total, leaving the name the chip's width.
  int16_t nameX = x + 7;
  if (active && !tall) {
    tft().fillTriangle(x + 5, y + 6, x + 5, y + 16, x + 11, y + 11, BRASS);
    nameX = x + 13;
  }
  // The avatar goes before the name only when the whole name still fits.
  tft().setFont(nameFont.get());
  if (p.avatar != 0 && tft().textWidth(p.name) <= x + w - 5 - (nameX + TurnHubAvatars::AVATAR_SIZE + 3)) {
    TurnHubAvatars::drawAvatar(tft(), p.avatar, nameX, y + 4, out ? DIM : MUTED);
    nameX += TurnHubAvatars::AVATAR_SIZE + 3;
  }
  // Life total: Oswald figures, large on tall chips.
  char life[12] = {};
  if (showLife) snprintf(life, sizeof(life), "%ld", static_cast<long>(p.life));
  tft().setFont(tall ? numeralsFont.get() : clockFont.get());
  const int16_t lifeW = showLife ? tft().textWidth(life) : 0;

  tft().setFont(nameFont.get());
  char name[SCREEN_NAME_LENGTH + 1];
  snprintf(name, sizeof(name), "%s", p.name);
  const int16_t nameRoom = x + w - nameX - 5;
  for (size_t n = strlen(name); n > 1 && tft().textWidth(name) > nameRoom; --n) name[n - 1] = '\0';
  // Each text band is clipped to its own rows, so one line's background
  // never paints over another's letters.
  constexpr int16_t NAME_BAND = 20;
  tft().setClipRect(x + 3, y + 3, w - 6, NAME_BAND - 3);
  tft().setTextDatum(lgfx::top_left);
  tft().setTextColor(ink, fill);
  tft().drawString(name, nameX, y + 5);
  tft().clearClipRect();
  if (out) tft().drawFastHLine(nameX, y + 11, tft().textWidth(name), ink);

  const uint32_t lifeInk = out ? DIM : (active ? BRASS_HI : CREAM);
  const uint32_t tagInk = active || winner ? BRASS : (p.flags & CHIP_WAITING) ? BRASS_HI : MUTED;
  if (tall) {
    if (showLife) {
      tft().setFont(lifeW <= w - 8 ? numeralsFont.get() : clockFont.get());
      tft().setTextDatum(lgfx::middle_center);
      tft().setTextColor(lifeInk, fill);
      tft().drawString(life, x + w / 2, y + h / 2 - 2);
      if (active) {
        // The turn pointer, aimed at the life total.
        const int16_t px = x + (w - tft().textWidth(life)) / 2 - 11, py = y + h / 2 - 3;
        if (px >= x + 4) tft().fillTriangle(px, py - 5, px, py + 5, px + 6, py, BRASS);
      }
    } else {
      // Between games: a small gear where the life total will be.
      gear(tft(), x + w / 2, y + h / 2 - 3, 11, 10, 0.0f, out ? LINE_OUT : 0x4A3820, 4, fill);
    }
    ruledTag(tft(), tag, x, w, y + h - 10, tagInk, active || winner ? BRASS_LO : LINE, fill);
  } else {
    tft().setClipRect(x + 3, y + NAME_BAND, w - 6, h - NAME_BAND - 2);
    if (showLife) {
      tft().setFont(clockFont.get());
      tft().setTextDatum(lgfx::bottom_right);
      tft().setTextColor(lifeInk, fill);
      tft().drawString(life, x + w - 5, y + h - 2);
    }
    tft().setFont(tagFont.get());
    tft().setTextDatum(lgfx::bottom_left);
    tft().setTextColor(tagInk, fill);
    tft().drawString(tag, x + 6, y + h - 3);
    tft().clearClipRect();
  }
  drawChipTime(p, x, y, w, h);
}

// Shared with the touch adapter, so tapping a chip opens that player.
void chipCell(uint8_t index, uint8_t count, int16_t &x, int16_t &y, int16_t &w, int16_t &h) {
  screenChipCell(index, count, x, y, w, h);
}

// A QR code on white, in a brass frame (the white keeps its quiet zone).
void drawQrCode(const char *text, int16_t x, int16_t y, int16_t size) {
  tft().fillRoundRect(x, y, size, size, 8, BRASS_LO);
  tft().drawRoundRect(x, y, size, size, 8, BRASS_DEEP);
  tft().fillRoundRect(x + 3, y + 3, size - 6, size - 6, 6, QR_LIGHT);
  tft().qrcode(text, x + 7, y + 7, size - 14, 1, true);
}

void drawLines(const AtlasScreen &screen, int16_t x, int16_t y, uint32_t bg) {
  tft().setTextDatum(lgfx::top_left);
  tft().setFont(&fonts::DejaVu12);
  for (uint8_t i = 0; i < screen.lineCount; ++i) {
    // Written out and red: a missing card.
    const bool warning = strstr(screen.lines[i], "NOT INSERTED") != nullptr;
    tft().setTextColor(warning ? DANGER : MUTED, bg);
    tft().drawString(screen.lines[i], x, y + i * 16);
  }
}

// An empty lobby: how to join, on a riveted plate, with the gear train
// that fills the space the QR code used to take.
void drawJoinPlate(const AtlasScreen &screen) {
  plate(tft(), PAD, SCREEN_BODY_Y, W - 2 * PAD, BODY_H, PLATE, LINE);
  const int16_t gx = W - PAD - 44, gy = SCREEN_BODY_Y + BODY_H / 2 + 2;
  gear(tft(), gx - 34, gy + 19, 13, 9, 20.0f, 0x4A3820, 4, PLATE);
  gear(tft(), gx, gy, 28, 12, 0.0f, 0x4A3820, 9, PLATE);
  tft().drawCircle(gx, gy, 9, LINE);
  drawLines(screen, PAD + 12, SCREEN_BODY_Y + 16, PLATE);
}

void drawBody(const AtlasScreen &screen, const AtlasScreen *previous) {
  if (codeLayout(screen)) {
    tft().fillRect(0, SCREEN_HERO_Y, QR_COLUMN_W, BUTTON_ROW_Y - 4 - SCREEN_HERO_Y, WALNUT);
    tft().fillRect(QR_COLUMN_W, SCREEN_BODY_Y, W - QR_COLUMN_W, BODY_H, WALNUT);
    if (screen.code[0] != '\0') {
      // The presence code, large enough to read across the table.
      tft().setTextDatum(lgfx::top_left);
      tft().setTextColor(BRASS_HI, WALNUT);
      tft().setFont(codeFont.get());
      if (tft().textWidth(screen.code) > W - QR_COLUMN_W - 2 * PAD) tft().setFont(numeralsFont.get());
      tft().drawString(screen.code, QR_COLUMN_W + PAD, SCREEN_BODY_Y);
    }
    if (screen.qr[0] != '\0') {
      drawQrCode(screen.qr, PAD, SCREEN_HERO_Y + 2, BUTTON_ROW_Y - 8 - SCREEN_HERO_Y);
      tft().setTextDatum(lgfx::top_left);
      tft().setTextColor(FAINT, WALNUT);
      tft().setFont(&fonts::DejaVu12);
      tft().drawString(screen.qrCaption, QR_COLUMN_W + PAD, SCREEN_BODY_Y + (screen.code[0] ? 56 : 4));
    }
    // Below the code when there is one (the pairing code's instructions).
    drawLines(screen, QR_COLUMN_W + PAD, SCREEN_BODY_Y + (screen.code[0] ? 56 : 4), WALNUT);
    return;
  }
  // Their buttons use the body.
  if (screen.kind == ScreenKind::Tests || screen.kind == ScreenKind::Table ||
      screen.kind == ScreenKind::Menu || screen.kind == ScreenKind::Player ||
      screen.kind == ScreenKind::Device) return;

  // Chips alone: redraw only those that changed, and only the time line of
  // a chip whose time alone moved.
  const bool chipsOnly = previous != nullptr && previous->kind == screen.kind &&
      previous->playerCount == screen.playerCount && previous->showLife == screen.showLife &&
      previous->lineCount == 0 && screen.lineCount == 0 && screen.qr[0] == '\0' && previous->qr[0] == '\0';
  if (!chipsOnly) tft().fillRect(0, SCREEN_BODY_Y, W, BODY_H, WALNUT);
  for (uint8_t i = 0; i < screen.playerCount; ++i) {
    int16_t x, y, w, h;
    chipCell(i, screen.playerCount, x, y, w, h);
    if (chipsOnly && samePlayer(screen.players[i], previous->players[i])) {
      if (!samePlayerTime(screen.players[i], previous->players[i])) drawChipTime(screen.players[i], x, y, w, h);
      continue;
    }
    if (chipsOnly) tft().fillRect(x, y, w, h, WALNUT);
    drawChip(screen.players[i], screen.showLife, x, y, w, h);
  }
  if (screen.kind == ScreenKind::Status && screen.playerCount == 0 && screen.lineCount > 0) {
    drawJoinPlate(screen);
    return;
  }
  if (screen.qr[0] != '\0') drawQrCode(screen.qr, W - PAD - BODY_H, SCREEN_BODY_Y, BODY_H);
  drawLines(screen, PAD + 4, SCREEN_BODY_Y + 6, WALNUT);
}

// The main action on each screen is polished brass.
bool primaryAction(TouchAction action) {
  switch (action) {
    case TouchAction::StartGame:
    case TouchAction::Rematch:
    case TouchAction::Pause:
    case TouchAction::Resume:
    case TouchAction::Pair:
    case TouchAction::SetupPair:
      return true;
    default:
      return false;
  }
}

// The hold fill bar inside a pressed hold button, drawn over the old one.
void drawHoldBar(const AtlasScreen &screen, const TouchButton &button) {
  const int16_t x = button.x + 10, y = button.y + button.h - 12, w = button.w - 20;
  const int16_t filled = (w - 2) * screen.holdPermille / 1000;
  tft().drawRoundRect(x, y, w, 5, 2, BRASS_DEEP);
  tft().fillRect(x + 1, y + 1, w - 2, 3, TUBE);
  tft().fillRect(x + 1, y + 1, filled, 1, BRASS_HI);
  tft().fillRect(x + 1, y + 2, filled, 2, BRASS);
}

// Primary buttons are polished brass, the rest riveted walnut plates with a
// brass frame. A pressed button changes fill and takes a heavy cream frame,
// so the press does not rely on hue alone. A hold button says so, then
// counts down with a fill bar while held; the chosen QR code is framed and
// marked "shown".
void drawButton(const AtlasScreen &screen, const TouchButton &button) {
  const bool pressed = screen.pressed == button.action;
  const bool primary = primaryAction(button.action);
  const int16_t x = button.x, y = button.y, w = button.w, h = button.h;
  uint32_t fill, ink;
  if (pressed) {
    fill = primary ? BRASS_LO : BRASS;
    ink = primary ? CREAM : INK;
    tft().fillRoundRect(x, y, w, h, 8, fill);
    for (uint8_t i = 0; i < 3; ++i) tft().drawRoundRect(x + i, y + i, w - 2 * i, h - 2 * i, 8 - i, CREAM);
  } else if (primary) {
    fill = BRASS;
    ink = INK;
    brassFace(tft(), x, y, w, h, 8, fill);
  } else {
    fill = PLATE;
    ink = BRASS_HI;
    plate(tft(), x, y, w, h, fill, button.selected ? BRASS : LINE_HI);
    if (button.selected) {
      tft().drawRoundRect(x + 1, y + 1, w - 2, h - 2, 4, BRASS);
      tft().drawRoundRect(x + 2, y + 2, w - 4, h - 4, 3, BRASS_HI);
    }
  }

  char label[32];
  if (pressed && button.hold() && screen.holdSecondsLeft > 0) {
    snprintf(label, sizeof(label), "Hold %u s", static_cast<unsigned>(screen.holdSecondsLeft));
  } else {
    snprintf(label, sizeof(label), "%s", button.label);
  }
  const bool caption = button.hold() && !pressed;
  const bool shown = button.selected && !pressed;
  tft().setTextDatum(lgfx::middle_center);
  tft().setTextColor(ink, fill);
  tft().setFont(fitBrass(tft(), label, w - 16));
  tft().drawString(label, x + w / 2, y + h / 2 - (caption || shown ? 6 : 0));
  if (caption) ruledTag(tft(), "hold", x + 4, w - 8, y + h / 2 + 13, primary ? INK : MUTED,
      primary ? BRASS_LO : LINE, fill);
  if (shown) ruledTag(tft(), "shown", x + 4, w - 8, y + h / 2 + 13, BRASS, BRASS_LO, fill);
  if (pressed && button.hold()) drawHoldBar(screen, button);
}

bool sameButtonLayout(const AtlasScreen &a, const AtlasScreen &b) {
  if (a.buttonCount != b.buttonCount) return false;
  for (uint8_t i = 0; i < a.buttonCount; ++i) {
    const TouchButton &x = a.buttons[i];
    const TouchButton &y = b.buttons[i];
    if (x.action != y.action || x.x != y.x || x.y != y.y || x.w != y.w || x.h != y.h ||
        x.selected != y.selected || strcmp(x.label, y.label) != 0) {
      return false;
    }
  }
  return true;
}

void drawButtons(const AtlasScreen &screen) {
  const int16_t top = screen.kind == ScreenKind::Tests || screen.kind == ScreenKind::Table ||
      screen.kind == ScreenKind::Menu || screen.kind == ScreenKind::Player || screen.kind == ScreenKind::Device
      ? BUTTON_UPPER_ROW_Y : BUTTON_ROW_Y;
  tft().fillRect(0, top, W, ATLAS_SCREEN_HEIGHT - top, WALNUT);
  for (uint8_t i = 0; i < screen.buttonCount; ++i) drawButton(screen, screen.buttons[i]);
}

// Same buttons, only the press or a hold's progress changed: repaint just the
// buttons whose press changed, and for a continuing hold only its bar (and
// its label once a second), so holding a button does not blink the row.
void updateButtons(const AtlasScreen &screen, const AtlasScreen &old) {
  for (uint8_t i = 0; i < screen.buttonCount; ++i) {
    const TouchButton &button = screen.buttons[i];
    const bool pressedNow = screen.pressed == button.action;
    const bool pressedBefore = old.pressed == button.action;
    if (pressedNow != pressedBefore || (pressedNow && screen.holdSecondsLeft != old.holdSecondsLeft)) {
      drawButton(screen, button);
    } else if (pressedNow && button.hold() && screen.holdPermille != old.holdPermille) {
      drawHoldBar(screen, button);
    }
  }
}

AtlasScreen shown;
bool statusDrawn = false;

void renderScreen(const AtlasScreen &screen, uint32_t nowMs) {
  const bool full = !statusDrawn || screen.kind != shown.kind;
  if (full) tft().fillScreen(WALNUT);
  if (full || !sameHeader(screen, shown) || !sameGameClock(screen, shown)) drawHeader(screen, nowMs);
  // The title redraws the hero; a new detail or action line redraws just
  // that line in place.
  const bool heroFull = full || screen.kind != shown.kind || strcmp(screen.title, shown.title) != 0 ||
      hasClock(screen) != hasClock(shown);
  if (heroFull) {
    drawHero(screen);
  } else if (!sameHero(screen, shown)) {
    drawHeroLine(screen);
  }
  if (heroFull || !sameTimer(screen, shown)) drawTimer(screen);
  if (full || !sameBody(screen, shown)) drawBody(screen, full ? nullptr : &shown);
  if (full || !sameButtonLayout(screen, shown)) {
    drawButtons(screen);
  } else if (!sameButtons(screen, shown)) {
    updateButtons(screen, shown);
  }
  shown = screen;
  statusDrawn = true;
}

}  // namespace

AtlasArtStatus beginAtlasArt(lgfx::LovyanGFX &panel) {
  target = &panel;
  AtlasArtStatus status;
  status.fonts = loadBrassFonts();
  status.buffers = createBuffers();
  statusDrawn = false;
  return status;
}

void drawAtlasSplash() { drawSplash(); }

void renderAtlasScreen(const AtlasScreen &screen, uint32_t nowMs) {
  if (!statusDrawn || !sameScreen(screen, shown)) renderScreen(screen, nowMs);
  serviceGear(shown, nowMs);
}

void invalidateAtlasScreen() { statusDrawn = false; }

}  // namespace TurnHubAtlas
