// Atlas built-in display: ILI9341V 240x320 TFT driven through LovyanGFX, and
// the XPT2046 resistive touch controller read over bit-banged SPI (HSPI is
// the TFT's and VSPI is kept for the microSD card). Presentation only: it
// draws the AtlasScreen from touch_controls.cpp and feeds touches back to it.

#include "atlas_display.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"
#include "firmware_version.h"
#include "serial_log.h"
#include "touch_controls.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

namespace {

constexpr uint32_t SPLASH_MS = 2000;
constexpr uint32_t TOUCH_POLL_MS = 15;
constexpr uint32_t SCREEN_POLL_MS = 50;

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

// --- Status screen -----------------------------------------------------------

constexpr int16_t TITLE_Y = 0;
constexpr int16_t DETAIL_Y = 44;
constexpr int16_t NOTICE_Y = 72;
constexpr int16_t BUTTONS_Y = 100;

void drawTextStrip(int16_t y, int16_t h, const char *text, const lgfx::IFont *font,
    uint32_t color) {
  tft.fillRect(0, y, ATLAS_SCREEN_WIDTH, h, BACKGROUND);
  if (text[0] == '\0') return;
  tft.setTextDatum(lgfx::middle_left);
  tft.setTextColor(color, BACKGROUND);
  tft.setFont(font);
  tft.drawString(text, 12, y + h / 2);
}

void drawTitle(const AtlasScreen &screen) {
  drawTextStrip(TITLE_Y, DETAIL_Y - TITLE_Y, screen.title, &fonts::DejaVu24, WORDMARK);
  tft.setTextDatum(lgfx::top_right);
  tft.setTextColor(SUBTLE, BACKGROUND);
  tft.setFont(&fonts::DejaVu9);
  tft.drawString("TurnHub Atlas", ATLAS_SCREEN_WIDTH - 8, 6);
}

// A pressed button inverts (light fill, dark label) and gains a heavier
// border, so the press does not rely on hue alone.
void drawButton(const AtlasScreen &screen, const TouchButton &button) {
  const bool pressed = screen.pressed == button.action;
  const uint32_t fill = pressed ? ACCENT : RING;
  const uint32_t text = pressed ? BACKGROUND : WORDMARK;
  tft.fillRoundRect(button.x, button.y, button.w, button.h, 8, fill);
  tft.drawRoundRect(button.x, button.y, button.w, button.h, 8, pressed ? WORDMARK : SEAT_IDLE);
  if (pressed) {
    tft.drawRoundRect(button.x + 1, button.y + 1, button.w - 2, button.h - 2, 7, WORDMARK);
    tft.drawRoundRect(button.x + 2, button.y + 2, button.w - 4, button.h - 4, 6, WORDMARK);
  }

  char label[32];
  if (pressed && button.hold && screen.holdSecondsLeft > 0) {
    snprintf(label, sizeof(label), "Keep holding: %u s",
        static_cast<unsigned>(screen.holdSecondsLeft));
  } else {
    snprintf(label, sizeof(label), "%s", button.label);
  }
  tft.setTextDatum(lgfx::middle_center);
  tft.setTextColor(text, fill);
  tft.setFont(&fonts::DejaVu18);
  tft.drawString(label, button.x + button.w / 2, button.y + button.h / 2);
}

void drawButtons(const AtlasScreen &screen) {
  tft.fillRect(0, BUTTONS_Y, ATLAS_SCREEN_WIDTH, ATLAS_SCREEN_HEIGHT - BUTTONS_Y, BACKGROUND);
  for (uint8_t i = 0; i < screen.buttonCount; ++i) drawButton(screen, screen.buttons[i]);
}

AtlasScreen shown;
bool statusDrawn = false;

void renderScreen(const AtlasScreen &screen) {
  const bool full = !statusDrawn;
  if (full) tft.fillScreen(BACKGROUND);
  if (full || strcmp(screen.title, shown.title) != 0) drawTitle(screen);
  if (full || strcmp(screen.detail, shown.detail) != 0) {
    drawTextStrip(DETAIL_Y, NOTICE_Y - DETAIL_Y, screen.detail, &fonts::DejaVu18, DETAIL);
  }
  if (full || strcmp(screen.notice, shown.notice) != 0) {
    drawTextStrip(NOTICE_Y, BUTTONS_Y - NOTICE_Y, screen.notice, &fonts::DejaVu12, ACCENT);
  }
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
  // One busy clock, 12 data bits (MSB first), then padding.
  uint16_t value = 0;
  for (uint8_t bit = 0; bit < 16; ++bit) {
    delayMicroseconds(1);
    digitalWrite(TOUCH_SCLK_PIN, HIGH);
    delayMicroseconds(1);
    digitalWrite(TOUCH_SCLK_PIN, LOW);
    value = static_cast<uint16_t>((value << 1) | (digitalRead(TOUCH_MISO_PIN) ? 1 : 0));
  }
  return static_cast<uint16_t>((value >> 3) & 0x0FFF);
}

uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
  if (a > b) { const uint16_t t = a; a = b; b = t; }
  if (b > c) { b = c; }
  return a > b ? a : b;
}

int16_t mapAxis(uint16_t raw, uint16_t rawMin, uint16_t rawMax, int16_t size, bool invert) {
  const int32_t clamped = raw < rawMin ? rawMin : raw > rawMax ? rawMax : raw;
  int32_t value = (clamped - rawMin) * (size - 1) / (rawMax - rawMin);
  if (invert) value = size - 1 - value;
  return static_cast<int16_t>(value);
}

// Reads one touch sample in screen coordinates.
bool readTouch(int16_t &x, int16_t &y, uint16_t &rawX, uint16_t &rawY) {
  using namespace AtlasConfig;
  if (digitalRead(TOUCH_IRQ_PIN) == HIGH) return false;

  digitalWrite(TOUCH_CS_PIN, LOW);
  const uint16_t z1 = touchTransfer(0xB1);
  const uint16_t z2 = touchTransfer(0xC1);
  const uint16_t a0 = touchTransfer(0x91), b0 = touchTransfer(0xD1);
  const uint16_t a1 = touchTransfer(0x91), b1 = touchTransfer(0xD1);
  const uint16_t a2 = touchTransfer(0x91), b2 = touchTransfer(0xD1);
  touchTransfer(0xD0);  // Power down with the pen interrupt enabled.
  digitalWrite(TOUCH_CS_PIN, HIGH);

  const int32_t pressure = static_cast<int32_t>(z1) + 4095 - static_cast<int32_t>(z2);
  if (pressure < TOUCH_PRESSURE_MIN) return false;

  rawX = median3(a0, a1, a2);
  rawY = median3(b0, b1, b2);
  const uint16_t alongX = TOUCH_SWAP_XY ? rawY : rawX;
  const uint16_t alongY = TOUCH_SWAP_XY ? rawX : rawY;
  x = mapAxis(alongX, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, ATLAS_SCREEN_WIDTH, TOUCH_INVERT_X);
  y = mapAxis(alongY, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, ATLAS_SCREEN_HEIGHT, TOUCH_INVERT_Y);
  return true;
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
bool wasTouched = false;

}  // namespace

void beginAtlasDisplay() {
  beginTouch();
  resetTouchControls();
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
    int16_t x = 0, y = 0;
    uint16_t rawX = 0, rawY = 0;
    const bool touched = readTouch(x, y, rawX, rawY);
    if (touched && !wasTouched) {
      char line[64];
      snprintf(line, sizeof(line), "ATLAS|TOUCH|RAW|%u|%u|SCREEN|%d|%d",
          rawX, rawY, x, y);
      serialLog.println(line);
    }
    wasTouched = touched;
    // Touches during the splash are ignored rather than acting on an unseen screen.
    const bool splash = displayReady && !statusDrawn && nowMs - displayStartedAtMs < SPLASH_MS;
    if (!splash) updateTouchControls(nowMs, touched, x, y);
  }

  if (!displayReady || nowMs - displayStartedAtMs < SPLASH_MS ||
      nowMs - lastScreenPollMs < SCREEN_POLL_MS) {
    return;
  }
  lastScreenPollMs = nowMs;
  AtlasScreen screen;
  buildAtlasScreen(nowMs, screen);
  if (!statusDrawn || !sameScreen(screen, shown)) renderScreen(screen);
}

}  // namespace TurnHubAtlas
