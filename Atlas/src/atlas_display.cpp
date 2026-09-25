// Atlas built-in display: ILI9341V 240x320 TFT driven through LovyanGFX, and
// the XPT2046 resistive touch controller read over bit-banged SPI (HSPI is
// the TFT's and VSPI is kept for the microSD card). Presentation only: it
// draws the AtlasScreen from touch_controls.cpp and feeds touches back to it.

#include "atlas_display.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"
#include "firmware_version.h"
#include "optional_preferences.h"
#include "serial_log.h"
#include "touch_calibration.h"
#include "touch_controls.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

namespace {

constexpr uint32_t SPLASH_MS = 2000;
constexpr uint32_t TOUCH_POLL_MS = 15;
constexpr uint32_t SCREEN_POLL_MS = 50;
// Holding anywhere on the lobby screen this long starts touch calibration.
constexpr uint32_t CALIBRATE_HOLD_MS = 10000;
// Samples in the first part of a calibration press are discarded while the
// finger settles.
constexpr uint32_t CALIBRATE_SETTLE_MS = 100;
constexpr uint8_t CALIBRATE_MIN_SAMPLES = 3;
// Calibration gives up after this long without a touch (e.g. a board without
// a touch panel) and keeps the calibration it had.
constexpr uint32_t CALIBRATE_IDLE_MS = 30000;
constexpr uint32_t CALIBRATE_RESULT_MS = 1500;

constexpr char TOUCH_PREF_NAMESPACE[] = "atlas-touch";
// "cal2": readings before the 2026-09-25 SPI sampling fix were scrambled, so
// a calibration saved under the old "cal" key is discarded (and removed on
// the next save) and Atlas recalibrates once.
constexpr char TOUCH_PREF_KEY[] = "cal2";
constexpr char TOUCH_PREF_LEGACY_KEY[] = "cal";

enum class DisplayMode : uint8_t { Splash, Status, Calibrate, CalibrateResult };

class AtlasPanel : public lgfx::LGFX_Device {
 public:
  AtlasPanel() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = AtlasConfig::TFT_SCLK_PIN;
      cfg.pin_mosi = AtlasConfig::TFT_MOSI_PIN;
      cfg.pin_miso = AtlasConfig::TFT_MISO_PIN;
      cfg.pin_dc = AtlasConfig::TFT_DC_PIN;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = AtlasConfig::TFT_CS_PIN;
      cfg.pin_rst = -1;  // Tied to the ESP32 EN line.
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;  // The TFT has HSPI to itself; the SD card is on VSPI.
      panel_.config(cfg);
    }
    {
      auto cfg = light_.config();
      cfg.pin_bl = AtlasConfig::TFT_BACKLIGHT_PIN;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      light_.config(cfg);
      panel_.setLight(&light_);
    }
    setPanel(&panel_);
  }

 private:
  lgfx::Bus_SPI bus_;
  lgfx::Panel_ILI9341 panel_;
  lgfx::Light_PWM light_;
};

AtlasPanel tft;
bool displayReady = false;

// Fonts: DejaVu (Bitstream Vera license) and the built-in glcd font only; the
// GNU FreeFont "Free*" fonts bundled with LovyanGFX are GPL and not used.

// Palette (RGB888; LovyanGFX converts to the panel's RGB565).
constexpr uint32_t BACKGROUND = 0x0B1020;
constexpr uint32_t RING = 0x2A3350;
constexpr uint32_t ACCENT = 0xF2A33A;
constexpr uint32_t SEAT_IDLE = 0x8A93B0;
constexpr uint32_t WORDMARK = 0xF4F6FB;
constexpr uint32_t SUBTLE = 0x8A93B0;
constexpr uint32_t DETAIL = 0xC8CEE0;

// --- Splash ------------------------------------------------------------------

// Hub emblem: a ring of four seats around the table, with the active turn's
// quarter of the ring and its seat lit.
void drawEmblem(int32_t cx, int32_t cy) {
  constexpr int32_t OUTER = 52;
  constexpr int32_t INNER = 44;
  constexpr int32_t SEAT = 9;
  tft.fillArc(cx, cy, OUTER, INNER, 0, 360, RING);
  // Screen angles run clockwise from +x; light the top-right quarter.
  tft.fillArc(cx, cy, OUTER, INNER, 270, 360, ACCENT);

  const int32_t mid = (OUTER + INNER) / 2;
  const int32_t seats[4][2] = {{0, -mid}, {mid, 0}, {0, mid}, {-mid, 0}};
  for (uint8_t i = 0; i < 4; ++i) {
    const int32_t x = cx + seats[i][0];
    const int32_t y = cy + seats[i][1];
    tft.fillCircle(x, y, SEAT + 3, BACKGROUND);
    tft.fillCircle(x, y, SEAT, i == 0 ? ACCENT : SEAT_IDLE);
  }

  // Center hub with a clockwise "next turn" arrowhead.
  tft.fillCircle(cx, cy, 20, RING);
  tft.fillTriangle(cx - 7, cy - 10, cx - 7, cy + 10, cx + 11, cy, ACCENT);
}

void drawSplash() {
  const int32_t w = tft.width();
  const int32_t h = tft.height();
  tft.fillScreen(BACKGROUND);
  drawEmblem(w / 2, 90);

  tft.setTextDatum(lgfx::middle_center);
  tft.setTextColor(WORDMARK, BACKGROUND);
  tft.setFont(&fonts::DejaVu40);
  tft.drawString("TurnHub", w / 2, 180);

  tft.setTextColor(SUBTLE, BACKGROUND);
  tft.setFont(&fonts::DejaVu12);
  tft.drawString("A T L A S", w / 2, 214);

  tft.setTextDatum(lgfx::bottom_right);
  tft.setFont(&fonts::Font0);
  tft.drawString((String("v") + TurnHubFirmware::VERSION).c_str(), w - 4, h - 3);
}

// --- Status screens ------------------------------------------------------------
//
// Regions, top to bottom: header (state badge, Sigil count, the red NO SD CARD
// warning), hero (title, detail or action message, turn clock and bar), body
// (player chips, text lines or a QR code) and the button row(s). Each region
// redraws only when its part of the AtlasScreen changes. Every state that a
// color marks is also written out or drawn as a shape (ACCESSIBILITY.md).

constexpr uint32_t PANEL = 0x161D33;
constexpr uint32_t DANGER = 0xD93A3F;
constexpr uint32_t DIM = 0x4A5270;
constexpr uint32_t QR_LIGHT = 0xFFFFFF;

constexpr int16_t PAD = 8;
constexpr int16_t CLOCK_W = 84;
constexpr int16_t HERO_H = 52;        // Title and detail lines.
constexpr int16_t BAR_Y = SCREEN_HERO_Y + HERO_H;
constexpr int16_t BAR_H = 4;
constexpr int16_t BODY_H = BUTTON_ROW_Y - 4 - SCREEN_BODY_Y;
constexpr int16_t QR_COLUMN_W = 150;  // QR screen: code on the left.

// Largest font (of three) that fits the width.
const lgfx::IFont *fitFont(const char *text, int16_t width) {
  static const lgfx::IFont *const FONTS[] = {&fonts::DejaVu18, &fonts::DejaVu12, &fonts::DejaVu9};
  for (const lgfx::IFont *font : FONTS) {
    tft.setFont(font);
    if (tft.textWidth(text) <= width) return font;
  }
  return &fonts::DejaVu9;
}

void pill(int16_t x, int16_t y, const char *text, uint32_t fill, uint32_t ink, bool alignRight) {
  tft.setFont(&fonts::DejaVu12);
  const int16_t w = tft.textWidth(text) + 14;
  const int16_t left = alignRight ? x - w : x;
  tft.fillRoundRect(left, y, w, 18, 9, fill);
  tft.setTextDatum(lgfx::middle_center);
  tft.setTextColor(ink, fill);
  tft.drawString(text, left + w / 2, y + 9);
}

void drawHeader(const AtlasScreen &screen) {
  tft.fillRect(0, 0, ATLAS_SCREEN_WIDTH, SCREEN_HEADER_H, PANEL);
  pill(PAD, 4, screen.badge, RING, WORDMARK, false);
  int16_t right = ATLAS_SCREEN_WIDTH - PAD;
  if (screen.sdMissing) {
    // Red, and written out: the card holds the luxury records.
    tft.setFont(&fonts::DejaVu12);
    const int16_t w = tft.textWidth("NO SD CARD") + 14;
    pill(right, 4, "NO SD CARD", DANGER, WORDMARK, true);
    right -= w + 8;
  }
  char sigils[16];
  snprintf(sigils, sizeof(sigils), "%u %s", static_cast<unsigned>(screen.sigilsOnline),
      screen.sigilsOnline == 1 ? "Sigil" : "Sigils");
  tft.setFont(&fonts::DejaVu12);
  tft.setTextDatum(lgfx::middle_right);
  tft.setTextColor(SUBTLE, PANEL);
  tft.drawString(sigils, right, SCREEN_HEADER_H / 2);
}

bool hasClock(const AtlasScreen &screen) {
  return screen.kind == ScreenKind::Status && screen.clock[0] != '\0';
}

void drawHero(const AtlasScreen &screen) {
  const bool qr = screen.kind == ScreenKind::Qr || screen.kind == ScreenKind::Code;
  const int16_t x = qr ? QR_COLUMN_W : 0;
  const int16_t w = ATLAS_SCREEN_WIDTH - x - (hasClock(screen) ? CLOCK_W : 0);
  tft.fillRect(x, SCREEN_HERO_Y, w, HERO_H, BACKGROUND);
  tft.setTextDatum(lgfx::top_left);
  tft.setTextColor(WORDMARK, BACKGROUND);
  tft.setFont(qr ? &fonts::DejaVu18 : &fonts::DejaVu24);
  if (tft.textWidth(screen.title) > w - 2 * PAD) tft.setFont(&fonts::DejaVu18);
  tft.drawString(screen.title, x + PAD, SCREEN_HERO_Y + 2);
  // An action message replaces the detail line while it lasts.
  const bool notice = screen.notice[0] != '\0';
  const char *line = notice ? screen.notice : screen.detail;
  tft.setTextColor(notice ? ACCENT : DETAIL, BACKGROUND);
  tft.setFont(fitFont(line, w - 2 * PAD) == &fonts::DejaVu18 ? &fonts::DejaVu12 : fitFont(line, w - 2 * PAD));
  tft.drawString(line, x + PAD, SCREEN_HERO_Y + 32);
}

void drawTimer(const AtlasScreen &screen) {
  tft.fillRect(0, BAR_Y, ATLAS_SCREEN_WIDTH, BAR_H, BACKGROUND);
  if (!hasClock(screen)) return;
  const int16_t x = ATLAS_SCREEN_WIDTH - CLOCK_W;
  tft.fillRect(x, SCREEN_HERO_Y, CLOCK_W, HERO_H, BACKGROUND);
  tft.fillRoundRect(x, SCREEN_HERO_Y + 2, CLOCK_W - PAD, HERO_H - 6, 8,
      screen.timerWarning ? DANGER : PANEL);
  tft.setTextDatum(lgfx::middle_center);
  tft.setTextColor(WORDMARK, screen.timerWarning ? DANGER : PANEL);
  tft.setFont(&fonts::DejaVu24);
  tft.drawString(screen.clock, x + (CLOCK_W - PAD) / 2, SCREEN_HERO_Y + 2 + (HERO_H - 6) / 2);
  if (screen.timerPermille >= 0) {
    // Countdown bar under the hero; the clock says the same in numbers.
    tft.fillRect(0, BAR_Y, ATLAS_SCREEN_WIDTH, BAR_H, RING);
    tft.fillRect(0, BAR_Y, ATLAS_SCREEN_WIDTH * screen.timerPermille / 1000, BAR_H,
        screen.timerWarning ? DANGER : ACCENT);
  }
}

// A player's chip. The active player gets a thick accent frame and a
// pointer; the tag line says TURN, OUT, WINNER, HOST, STARTS or CONFIRM.
void drawChip(const ScreenPlayer &p, bool showLife, int16_t x, int16_t y, int16_t w, int16_t h) {
  const bool active = p.flags & CHIP_ACTIVE;
  const bool out = p.flags & CHIP_OUT;
  const uint32_t fill = active ? RING : PANEL;
  tft.fillRoundRect(x, y, w, h, 6, fill);
  if (active || (p.flags & CHIP_WINNER)) {
    tft.drawRoundRect(x, y, w, h, 6, ACCENT);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 5, ACCENT);
  }
  const uint32_t ink = out ? DIM : WORDMARK;
  const char *tag = (p.flags & CHIP_WINNER) ? "WINNER" : out ? "OUT" : active ? "TURN"
      : (p.flags & CHIP_WAITING) ? "CONFIRM"
      : (p.flags & CHIP_STARTER) ? "STARTS" : "";
  int16_t nameX = x + 6;
  if (active) {
    tft.fillTriangle(x + 5, y + 5, x + 5, y + 15, x + 11, y + 10, ACCENT);
    nameX = x + 14;
  }
  const bool tall = h >= 60;
  tft.setTextDatum(lgfx::top_left);
  tft.setTextColor(ink, fill);
  tft.setFont(&fonts::DejaVu12);
  char name[SCREEN_NAME_LENGTH + 1];
  snprintf(name, sizeof(name), "%s", p.name);
  // Trim the name to the chip, leaving room for the life total on short chips.
  const int16_t nameRoom = x + w - nameX - 4 - (!tall && showLife ? 34 : 0);
  for (size_t n = strlen(name); n > 1 && tft.textWidth(name) > nameRoom; --n) name[n - 1] = '\0';
  tft.drawString(name, nameX, y + 4);
  if (out) tft.drawFastHLine(nameX, y + 11, tft.textWidth(name), ink);

  char life[12] = {};
  if (showLife) snprintf(life, sizeof(life), "%ld", static_cast<long>(p.life));
  if (tall) {
    if (showLife) {
      tft.setTextDatum(lgfx::middle_center);
      tft.setTextColor(ink, fill);
      tft.setFont(&fonts::DejaVu24);
      tft.drawString(life, x + w / 2, y + h / 2 + 2);
    }
    tft.setTextDatum(lgfx::bottom_center);
    tft.setTextColor(active ? ACCENT : SUBTLE, fill);
    tft.setFont(&fonts::DejaVu9);
    tft.drawString(tag, x + w / 2, y + h - 3);
  } else {
    tft.setTextDatum(lgfx::top_right);
    tft.setTextColor(ink, fill);
    tft.setFont(&fonts::DejaVu12);
    if (showLife) tft.drawString(life, x + w - 5, y + 4);
    tft.setTextDatum(lgfx::bottom_left);
    tft.setTextColor(active ? ACCENT : SUBTLE, fill);
    tft.setFont(&fonts::DejaVu9);
    tft.drawString(tag, nameX, y + h - 2);
  }
}

void chipCell(uint8_t index, uint8_t count, int16_t &x, int16_t &y, int16_t &w, int16_t &h) {
  const uint8_t cols = count <= 4 ? count : 4;
  const uint8_t rows = count <= 4 ? 1 : 2;
  constexpr int16_t GAP = 6;
  w = (ATLAS_SCREEN_WIDTH - 2 * PAD - (cols - 1) * GAP) / cols;
  h = (BODY_H - (rows - 1) * GAP) / rows;
  x = PAD + (index % cols) * (w + GAP);
  y = SCREEN_BODY_Y + (index / cols) * (h + GAP);
}

void drawQrCode(const char *text, int16_t x, int16_t y, int16_t size) {
  tft.fillRoundRect(x, y, size, size, 6, QR_LIGHT);
  tft.qrcode(text, x + 4, y + 4, size - 8, 1, true);
}

void drawLines(const AtlasScreen &screen, int16_t x, int16_t y) {
  tft.setTextDatum(lgfx::top_left);
  tft.setTextColor(DETAIL, BACKGROUND);
  tft.setFont(&fonts::DejaVu12);
  for (uint8_t i = 0; i < screen.lineCount; ++i) {
    const bool warning = strstr(screen.lines[i], "NOT INSERTED") != nullptr;
    tft.setTextColor(warning ? DANGER : DETAIL, BACKGROUND);
    tft.drawString(screen.lines[i], x, y + i * 16);
  }
}

void drawBody(const AtlasScreen &screen, const AtlasScreen *previous) {
  if (screen.kind == ScreenKind::Qr || screen.kind == ScreenKind::Code) {
    tft.fillRect(0, SCREEN_HERO_Y, QR_COLUMN_W, BUTTON_ROW_Y - 4 - SCREEN_HERO_Y, BACKGROUND);
    tft.fillRect(QR_COLUMN_W, SCREEN_BODY_Y, ATLAS_SCREEN_WIDTH - QR_COLUMN_W, BODY_H, BACKGROUND);
    if (screen.qr[0] != '\0') {
      drawQrCode(screen.qr, PAD, SCREEN_HERO_Y + 2, BUTTON_ROW_Y - 8 - SCREEN_HERO_Y);
      tft.setTextDatum(lgfx::top_left);
      tft.setTextColor(SUBTLE, BACKGROUND);
      tft.setFont(&fonts::DejaVu12);
      tft.drawString(screen.qrCaption, QR_COLUMN_W + PAD, SCREEN_BODY_Y + (screen.code[0] ? 50 : 4));
    }
    if (screen.code[0] != '\0') {
      // The presence code, large enough to read across the table.
      tft.setTextDatum(lgfx::top_left);
      tft.setTextColor(ACCENT, BACKGROUND);
      tft.setFont(&fonts::DejaVu40);
      if (tft.textWidth(screen.code) > ATLAS_SCREEN_WIDTH - QR_COLUMN_W - 2 * PAD) tft.setFont(&fonts::DejaVu24);
      tft.drawString(screen.code, QR_COLUMN_W + PAD, SCREEN_BODY_Y + 4);
    }
    drawLines(screen, QR_COLUMN_W + PAD, SCREEN_BODY_Y + 4);
    return;
  }
  if (screen.kind == ScreenKind::Tests) return;  // Its buttons use the body.

  // Chips alone: redraw only those that changed.
  const bool chipsOnly = previous != nullptr && previous->kind == screen.kind &&
      previous->playerCount == screen.playerCount && previous->showLife == screen.showLife &&
      previous->lineCount == 0 && screen.lineCount == 0 && screen.qr[0] == '\0' && previous->qr[0] == '\0';
  if (!chipsOnly) tft.fillRect(0, SCREEN_BODY_Y, ATLAS_SCREEN_WIDTH, BODY_H, BACKGROUND);
  for (uint8_t i = 0; i < screen.playerCount; ++i) {
    if (chipsOnly && samePlayer(screen.players[i], previous->players[i])) continue;
    int16_t x, y, w, h;
    chipCell(i, screen.playerCount, x, y, w, h);
    if (chipsOnly) tft.fillRect(x, y, w, h, BACKGROUND);
    drawChip(screen.players[i], screen.showLife, x, y, w, h);
  }
  if (screen.qr[0] != '\0') drawQrCode(screen.qr, ATLAS_SCREEN_WIDTH - PAD - BODY_H, SCREEN_BODY_Y, BODY_H);
  drawLines(screen, PAD + 4, SCREEN_BODY_Y + 6);
}

// A pressed button inverts (light fill, dark label) and gains a heavier
// border, so the press does not rely on hue alone. A hold button says so,
// then counts down with a fill bar while held; the chosen QR code is framed.
void drawButton(const AtlasScreen &screen, const TouchButton &button) {
  const bool pressed = screen.pressed == button.action;
  const uint32_t fill = pressed ? ACCENT : RING;
  const uint32_t ink = pressed ? BACKGROUND : WORDMARK;
  tft.fillRoundRect(button.x, button.y, button.w, button.h, 10, fill);
  const uint32_t frame = pressed || button.selected ? (pressed ? WORDMARK : ACCENT) : SEAT_IDLE;
  tft.drawRoundRect(button.x, button.y, button.w, button.h, 10, frame);
  if (pressed || button.selected) {
    tft.drawRoundRect(button.x + 1, button.y + 1, button.w - 2, button.h - 2, 9, frame);
    tft.drawRoundRect(button.x + 2, button.y + 2, button.w - 4, button.h - 4, 8, frame);
  }

  char label[32];
  if (pressed && button.hold() && screen.holdSecondsLeft > 0) {
    snprintf(label, sizeof(label), "Hold %u s", static_cast<unsigned>(screen.holdSecondsLeft));
  } else {
    snprintf(label, sizeof(label), "%s", button.label);
  }
  const bool caption = button.hold() && !pressed;
  tft.setTextDatum(lgfx::middle_center);
  tft.setTextColor(ink, fill);
  tft.setFont(fitFont(label, button.w - 10));
  tft.drawString(label, button.x + button.w / 2, button.y + button.h / 2 - (caption ? 6 : 0));
  if (caption) {
    tft.setFont(&fonts::DejaVu9);
    tft.setTextColor(SUBTLE, fill);
    tft.drawString("hold", button.x + button.w / 2, button.y + button.h / 2 + 14);
  }
  if (pressed && button.hold()) {
    const int16_t barW = (button.w - 16) * screen.holdPermille / 1000;
    tft.fillRect(button.x + 8, button.y + button.h - 10, button.w - 16, 4, fill);
    tft.fillRect(button.x + 8, button.y + button.h - 10, barW, 4, BACKGROUND);
  }
  if (button.selected && !pressed) {
    tft.setFont(&fonts::DejaVu9);
    tft.setTextColor(ACCENT, fill);
    tft.drawString("shown", button.x + button.w / 2, button.y + button.h - 9);
  }
}

void drawButtons(const AtlasScreen &screen) {
  const int16_t top = screen.kind == ScreenKind::Tests ? BUTTON_UPPER_ROW_Y : BUTTON_ROW_Y;
  tft.fillRect(0, top, ATLAS_SCREEN_WIDTH, ATLAS_SCREEN_HEIGHT - top, BACKGROUND);
  for (uint8_t i = 0; i < screen.buttonCount; ++i) drawButton(screen, screen.buttons[i]);
}

AtlasScreen shown;
bool statusDrawn = false;

void renderScreen(const AtlasScreen &screen) {
  const bool full = !statusDrawn || screen.kind != shown.kind;
  if (full) tft.fillScreen(BACKGROUND);
  if (full || !sameHeader(screen, shown)) drawHeader(screen);
  const bool heroChanged = full || !sameHero(screen, shown) || hasClock(screen) != hasClock(shown);
  if (heroChanged) drawHero(screen);
  if (heroChanged || !sameTimer(screen, shown)) drawTimer(screen);
  if (full || !sameBody(screen, shown)) drawBody(screen, full ? nullptr : &shown);
  if (full || !sameButtons(screen, shown)) drawButtons(screen);
  shown = screen;
  statusDrawn = true;
}

// --- Touch (XPT2046, bit-banged SPI mode 0) ----------------------------------

uint16_t touchTransfer(uint8_t command) {
  using namespace AtlasConfig;
  for (int8_t bit = 7; bit >= 0; --bit) {
    digitalWrite(TOUCH_MOSI_PIN, (command >> bit) & 1);
    delayMicroseconds(1);
    digitalWrite(TOUCH_SCLK_PIN, HIGH);
    delayMicroseconds(1);
    digitalWrite(TOUCH_SCLK_PIN, LOW);
  }
  digitalWrite(TOUCH_MOSI_PIN, LOW);
  // One busy clock, 12 data bits (MSB first), then padding. The XPT2046
  // changes DOUT on the falling edge, so sample it while the clock is high:
  // reading just after the falling edge races the new bit and scrambles the
  // position (a doubled, wrapped value whenever the new bit wins).
  uint16_t value = 0;
  for (uint8_t bit = 0; bit < 16; ++bit) {
    delayMicroseconds(1);
    digitalWrite(TOUCH_SCLK_PIN, HIGH);
    delayMicroseconds(1);
    value = static_cast<uint16_t>((value << 1) | (digitalRead(TOUCH_MISO_PIN) ? 1 : 0));
    digitalWrite(TOUCH_SCLK_PIN, LOW);
  }
  return static_cast<uint16_t>((value >> 3) & 0x0FFF);
}

uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
  if (a > b) { const uint16_t t = a; a = b; b = t; }
  if (b > c) { b = c; }
  return a > b ? a : b;
}

TouchCalibration touchCal;

int32_t touchPressure() {
  const int32_t z1 = touchTransfer(0xB1);
  const int32_t z2 = touchTransfer(0xC1);
  return z1 + 4095 - z2;
}

// Median of three conversions on one channel. The first conversion after the
// panel drivers switch plates has not settled, so it is discarded.
uint16_t readTouchChannel(uint8_t command) {
  touchTransfer(command);
  const uint16_t a = touchTransfer(command);
  const uint16_t b = touchTransfer(command);
  const uint16_t c = touchTransfer(command);
  return median3(a, b, c);
}

// Reads one raw touch sample. Pressure is checked before and after the
// position reads, so a finger landing or lifting mid-sample (when a resistive
// panel reports positions well off the real one) is not counted as a touch.
bool readTouchRaw(uint16_t &rawX, uint16_t &rawY) {
  using namespace AtlasConfig;
  if (digitalRead(TOUCH_IRQ_PIN) == HIGH) return false;

  digitalWrite(TOUCH_CS_PIN, LOW);
  const int32_t before = touchPressure();
  const uint16_t x = readTouchChannel(0x91);
  const uint16_t y = readTouchChannel(0xD1);
  const int32_t after = touchPressure();
  touchTransfer(0xD0);  // Power down with the pen interrupt enabled.
  digitalWrite(TOUCH_CS_PIN, HIGH);

  if (before < TOUCH_PRESSURE_MIN || after < TOUCH_PRESSURE_MIN) return false;
  rawX = x;
  rawY = y;
  return true;
}

// Loads the saved calibration, falling back to the config.h values.
bool loadTouchCalibration() {
  touchCal = defaultTouchCalibration();
  TurnHub::OptionalPreferences prefs;
  if (!prefs.begin(TOUCH_PREF_NAMESPACE, true)) return false;
  TouchCalibration saved;
  const bool ok = prefs.getBytesLength(TOUCH_PREF_KEY) == sizeof(saved) &&
      prefs.getBytes(TOUCH_PREF_KEY, &saved, sizeof(saved)) == sizeof(saved) &&
      validTouchCalibration(saved);
  prefs.end();
  if (ok) touchCal = saved;
  return ok;
}

bool saveTouchCalibration(const TouchCalibration &cal) {
  TurnHub::OptionalPreferences prefs;
  if (!prefs.begin(TOUCH_PREF_NAMESPACE, false)) return false;
  const bool ok = prefs.putBytes(TOUCH_PREF_KEY, &cal, sizeof(cal)) == sizeof(cal);
  if (ok && prefs.getBytesLength(TOUCH_PREF_LEGACY_KEY) > 0) prefs.remove(TOUCH_PREF_LEGACY_KEY);
  prefs.end();
  return ok;
}

void logCalibration(const char *event, const TouchCalibration &cal) {
  char line[96];
  snprintf(line, sizeof(line), "ATLAS|TOUCH|CALIBRATION|%s|swap=%u|x=%d..%d|y=%d..%d", event,
      static_cast<unsigned>(cal.swapXY), cal.xAtFirst, cal.xAtLast, cal.yAtFirst, cal.yAtLast);
  serialLog.println(line);
}

void beginTouch() {
  using namespace AtlasConfig;
  pinMode(TOUCH_CS_PIN, OUTPUT);
  digitalWrite(TOUCH_CS_PIN, HIGH);
  pinMode(TOUCH_SCLK_PIN, OUTPUT);
  digitalWrite(TOUCH_SCLK_PIN, LOW);
  pinMode(TOUCH_MOSI_PIN, OUTPUT);
  pinMode(TOUCH_MISO_PIN, INPUT);
  pinMode(TOUCH_IRQ_PIN, INPUT);
  // Arm the pen interrupt.
  digitalWrite(TOUCH_CS_PIN, LOW);
  touchTransfer(0xD0);
  digitalWrite(TOUCH_CS_PIN, HIGH);
}

uint32_t displayStartedAtMs = 0;
uint32_t lastTouchPollMs = 0;
uint32_t lastScreenPollMs = 0;
uint32_t lastTouchedAtMs = 0;
uint32_t touchHeldSinceMs = 0;
DisplayMode mode = DisplayMode::Splash;
bool calibrationSaved = false;

// --- Touch calibration screen ------------------------------------------------

struct CalibrationRun {
  uint8_t point = 0;
  bool awaitRelease = true;  // A finger still down from the trigger must lift first.
  bool contact = false;
  uint32_t contactAtMs = 0;
  uint32_t lastContactAtMs = 0;
  uint32_t lastActivityAtMs = 0;
  uint32_t sumX = 0;
  uint32_t sumY = 0;
  uint8_t samples = 0;
  uint16_t rawX[TOUCH_CAL_POINTS] = {};
  uint16_t rawY[TOUCH_CAL_POINTS] = {};
  uint32_t resultAtMs = 0;
};
CalibrationRun cal;

void drawCalibrationPoint(const char *message) {
  tft.fillScreen(BACKGROUND);
  tft.setTextDatum(lgfx::middle_center);
  tft.setTextColor(WORDMARK, BACKGROUND);
  tft.setFont(&fonts::DejaVu18);
  tft.drawString("Touch calibration", ATLAS_SCREEN_WIDTH / 2, 84);
  char line[48];
  snprintf(line, sizeof(line), "Press and release the cross (%u of %u)",
      static_cast<unsigned>(cal.point + 1), static_cast<unsigned>(TOUCH_CAL_POINTS));
  tft.setTextColor(DETAIL, BACKGROUND);
  tft.setFont(&fonts::DejaVu12);
  tft.drawString(line, ATLAS_SCREEN_WIDTH / 2, 116);
  if (message != nullptr) {
    tft.setTextColor(ACCENT, BACKGROUND);
    tft.drawString(message, ATLAS_SCREEN_WIDTH / 2, 140);
  }
  int16_t x = 0, y = 0;
  touchCalibrationTarget(cal.point, ATLAS_SCREEN_WIDTH, ATLAS_SCREEN_HEIGHT, x, y);
  tft.drawFastHLine(x - 14, y, 29, WORDMARK);
  tft.drawFastVLine(x, y - 14, 29, WORDMARK);
  tft.drawCircle(x, y, 8, ACCENT);
  tft.drawCircle(x, y, 9, ACCENT);
}

void startCalibration(uint32_t nowMs) {
  cal = CalibrationRun();
  cal.lastActivityAtMs = nowMs;
  mode = DisplayMode::Calibrate;
  resetTouchControls();
  serialLog.println("ATLAS|TOUCH|CALIBRATION|START");
  drawCalibrationPoint(nullptr);
}

void finishCalibrationMessage(uint32_t nowMs, const char *message) {
  tft.fillScreen(BACKGROUND);
  tft.setTextDatum(lgfx::middle_center);
  tft.setTextColor(WORDMARK, BACKGROUND);
  tft.setFont(&fonts::DejaVu18);
  tft.drawString(message, ATLAS_SCREEN_WIDTH / 2, ATLAS_SCREEN_HEIGHT / 2);
  cal.resultAtMs = nowMs;
  mode = DisplayMode::CalibrateResult;
}

void serviceCalibration(uint32_t nowMs, bool touched, uint16_t rawX, uint16_t rawY) {
  if (touched) {
    cal.lastActivityAtMs = nowMs;
    cal.lastContactAtMs = nowMs;
    if (cal.awaitRelease) return;
    if (!cal.contact) {
      cal.contact = true;
      cal.contactAtMs = nowMs;
      cal.sumX = cal.sumY = 0;
      cal.samples = 0;
    }
    if (nowMs - cal.contactAtMs >= CALIBRATE_SETTLE_MS && cal.samples < 255) {
      cal.sumX += rawX;
      cal.sumY += rawY;
      ++cal.samples;
    }
    return;
  }

  if (nowMs - cal.lastActivityAtMs >= CALIBRATE_IDLE_MS) {
    serialLog.println("ATLAS|TOUCH|CALIBRATION|TIMEOUT");
    finishCalibrationMessage(nowMs, "Calibration cancelled");
    return;
  }
  if (nowMs - cal.lastContactAtMs < TOUCH_RELEASE_MS) return;
  if (cal.awaitRelease) {
    cal.awaitRelease = false;
    return;
  }
  if (!cal.contact) return;
  cal.contact = false;
  if (cal.samples < CALIBRATE_MIN_SAMPLES) {
    drawCalibrationPoint("Hold a little longer");
    return;
  }
  cal.rawX[cal.point] = static_cast<uint16_t>(cal.sumX / cal.samples);
  cal.rawY[cal.point] = static_cast<uint16_t>(cal.sumY / cal.samples);
  if (++cal.point < TOUCH_CAL_POINTS) {
    drawCalibrationPoint(nullptr);
    return;
  }

  TouchCalibration solved;
  if (!solveTouchCalibration(cal.rawX, cal.rawY, ATLAS_SCREEN_WIDTH, ATLAS_SCREEN_HEIGHT,
          solved)) {
    serialLog.println("ATLAS|TOUCH|CALIBRATION|REJECTED");
    cal.point = 0;
    drawCalibrationPoint("Those presses did not line up; try again");
    return;
  }
  touchCal = solved;
  calibrationSaved = saveTouchCalibration(solved);
  logCalibration(calibrationSaved ? "SAVED" : "STORAGE_ERROR", solved);
  finishCalibrationMessage(nowMs, calibrationSaved ? "Touch calibrated" :
      "Calibrated (not saved)");
}

}  // namespace

void beginAtlasDisplay() {
  beginTouch();
  resetTouchControls();
  calibrationSaved = loadTouchCalibration();
  logCalibration(calibrationSaved ? "LOADED" : "DEFAULT", touchCal);
  if (!tft.init()) {
    serialLog.println("ATLAS|DISPLAY|INIT_FAILED");
    return;
  }
  tft.setRotation(AtlasConfig::TFT_ROTATION);
  tft.setBrightness(200);
  drawSplash();
  displayStartedAtMs = millis();
  displayReady = true;
  serialLog.println("ATLAS|DISPLAY|SPLASH");
}

void serviceAtlasDisplay(uint32_t nowMs) {
  if (nowMs - lastTouchPollMs >= TOUCH_POLL_MS) {
    lastTouchPollMs = nowMs;
    uint16_t rawX = 0, rawY = 0;
    const bool touched = readTouchRaw(rawX, rawY);
    int16_t x = 0, y = 0;
    if (touched) mapTouch(touchCal, rawX, rawY, ATLAS_SCREEN_WIDTH, ATLAS_SCREEN_HEIGHT, x, y);
    if (touched) {
      // A new press, not a resistive drop-out within one.
      if (nowMs - lastTouchedAtMs >= TOUCH_RELEASE_MS) {
        touchHeldSinceMs = nowMs;
        char line[64];
        snprintf(line, sizeof(line), "ATLAS|TOUCH|RAW|%u|%u|SCREEN|%d|%d", rawX, rawY, x, y);
        serialLog.println(line);
      }
      lastTouchedAtMs = nowMs;
    }

    switch (mode) {
      case DisplayMode::Calibrate:
        serviceCalibration(nowMs, touched, rawX, rawY);
        break;
      case DisplayMode::Status:
        // A long hold anywhere, only while the table is in the lobby, recalibrates.
        if (touched && nowMs - touchHeldSinceMs >= CALIBRATE_HOLD_MS &&
            touchCalibrationAllowed()) {
          startCalibration(nowMs);
          break;
        }
        updateTouchControls(nowMs, touched, x, y);
        break;
      case DisplayMode::Splash:
      case DisplayMode::CalibrateResult:
        // Touches here are ignored rather than acting on an unseen screen.
        break;
    }
  }

  if (!displayReady) return;
  if (mode == DisplayMode::Splash) {
    if (nowMs - displayStartedAtMs < SPLASH_MS) return;
    // Without a saved calibration, calibrate before offering any buttons.
    if (!calibrationSaved) {
      startCalibration(nowMs);
      return;
    }
    mode = DisplayMode::Status;
  }
  if (mode == DisplayMode::CalibrateResult) {
    if (nowMs - cal.resultAtMs < CALIBRATE_RESULT_MS) return;
    mode = DisplayMode::Status;
    statusDrawn = false;
  }
  if (mode != DisplayMode::Status || nowMs - lastScreenPollMs < SCREEN_POLL_MS) return;
  lastScreenPollMs = nowMs;
  AtlasScreen screen;
  buildAtlasScreen(nowMs, screen);
  if (!statusDrawn || !sameScreen(screen, shown)) renderScreen(screen);
}

}  // namespace TurnHubAtlas
