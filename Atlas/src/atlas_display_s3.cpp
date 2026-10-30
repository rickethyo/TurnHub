// The 4-inch S3 Atlas screen (ES3C40P): ST7796S over SPI and the FT6336U
// capacitive touch controller, driven by LovyanGFX, with LVGL 9.3 drawing
// the touch_controls.cpp screen model. Presentation only: touches go to
// updateTouchControls, which builds Intents. Firmware-only (env:atlas-s3);
// the other builds use atlas_board_none.cpp's display functions.
//
// The model is laid out for the panel's own 320 x 480 portrait, so every
// rectangle and touch maps 1:1. It is drawn in the app's style (design
// tokens, flat cards, seat-coloured player tiles), the first step towards an
// always-open app on the Atlas screen.

#if defined(TURNHUB_BOARD_ES3C40P)

#include "atlas_display.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "atlas_app.h"
#include "atlas_screen_lvgl.h"
#include "config.h"
#include "firmware_version.h"
#include "optional_preferences.h"
#include "serial_log.h"
#include "touch_controls.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {
namespace {

constexpr int16_t W = ATLAS_SCREEN_WIDTH;
constexpr int16_t H = ATLAS_SCREEN_HEIGHT;
constexpr uint32_t SPLASH_MS = 1500;
constexpr uint32_t TOUCH_POLL_MS = 15;
constexpr uint32_t SCREEN_POLL_MS = 100;
constexpr uint32_t MANA_FRAME_MS = 33;
constexpr uint8_t BACKLIGHT_ON = 255;
// Partial render buffers in internal DMA-capable RAM.
constexpr uint32_t BUFFER_LINES = 40;

class AtlasPanel : public lgfx::LGFX_Device {
 public:
  AtlasPanel() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = AtlasConfig::LCD_SCK_PIN;
      cfg.pin_mosi = AtlasConfig::LCD_MOSI_PIN;
      cfg.pin_miso = AtlasConfig::LCD_MISO_PIN;
      cfg.pin_dc = AtlasConfig::LCD_DC_PIN;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = AtlasConfig::LCD_CS_PIN;
      cfg.pin_rst = -1;  // The panel resets with the board (EN).
      cfg.pin_busy = -1;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_rotation = 0;
      cfg.readable = true;
      cfg.invert = AtlasConfig::LCD_INVERT;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }
    {
      auto cfg = light_.config();
      cfg.pin_bl = AtlasConfig::LCD_BACKLIGHT_PIN;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      light_.config(cfg);
      panel_.setLight(&light_);
    }
    {
      auto cfg = touch_.config();
      cfg.x_min = 0;
      cfg.x_max = 319;
      cfg.y_min = 0;
      cfg.y_max = 479;
      cfg.pin_int = AtlasConfig::TOUCH_INT_PIN;
      cfg.pin_rst = AtlasConfig::TOUCH_RST_PIN;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 0;
      cfg.i2c_addr = 0x38;
      cfg.pin_sda = AtlasConfig::TOUCH_SDA_PIN;
      cfg.pin_scl = AtlasConfig::TOUCH_SCL_PIN;
      // Shared later with the codec: start slow (hardware plan).
      cfg.freq = 100000;
      touch_.config(cfg);
      panel_.setTouch(&touch_);
    }
    setPanel(&panel_);
  }

 private:
  lgfx::Bus_SPI bus_;
  lgfx::Panel_ST7796 panel_;
  lgfx::Light_PWM light_;
  lgfx::Touch_FT5x06 touch_;
};

AtlasPanel tft;
lv_display_t *display = nullptr;
bool displayReady = false;
bool splashUp = false;
uint32_t startedAtMs = 0;
uint32_t lastTouchPollMs = 0;
uint32_t lastScreenPollMs = 0;
uint32_t lastTouchedAtMs = 0;
bool touchLogged = false;

TurnHubTheme::Id theme = TurnHubTheme::Id::Graphite;
bool inverted = false;
AtlasScreen shown;
bool shownValid = false;

bool screenTesting = false;
uint32_t screenTestStartedMs = 0;
bool screenTestReleaseTouch = false;

// --- LVGL glue -----------------------------------------------------------

uint32_t tick() { return millis(); }

// The SPI DMA sends one buffer while LVGL renders the next into the other:
// the buffer is handed back at once, and LovyanGFX waits for the previous
// transfer before it starts the next one, so a buffer is never overwritten
// mid-send. The bus is released (waiting out the last transfer) after the
// frame's final area.
void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels) {
  const uint32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
  // LVGL renders little-endian RGB565; the panel takes it big-endian.
  lv_draw_sw_rgb565_swap(pixels, w * h);
  if (tft.getStartCount() == 0) tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.writePixelsDMA(reinterpret_cast<const uint16_t *>(pixels), w * h, false);
  if (lv_display_flush_is_last(disp)) tft.endWrite();
  lv_display_flush_ready(disp);
}

// --- Drawing the screen model --------------------------------------------

void loadThemePreference() {
  TurnHub::OptionalPreferences prefs;
  if (!prefs.begin("display", true)) return;
  const String key = prefs.getString("theme", "graphite");
  inverted = prefs.getBool("inverted", false);
  prefs.end();
  for (uint8_t i = 0; i < static_cast<uint8_t>(TurnHubTheme::Id::Count); ++i) {
    const auto id = static_cast<TurnHubTheme::Id>(i);
    if (key == TurnHubTheme::palette(id).key) theme = id;
  }
}

void serviceScreenTest(uint32_t nowMs) {
  const uint32_t elapsed = nowMs - screenTestStartedMs;
  if (elapsed >= 9000) {
    screenTesting = false;
    shownValid = false;
    return;
  }
  static uint8_t drawn = 255;
  const uint8_t phase = elapsed / 1500;
  if (phase == drawn) return;
  drawn = phase;
  const uint16_t colors[] = {0x0000, 0xffff, 0xf800, 0x07e0, 0x001f};
  tft.fillScreen(phase < 5 ? colors[phase] : 0x0000);
  if (phase == 5)
    for (int y = 0; y < tft.height(); y += 20)
      for (int x = 0; x < tft.width(); x += 20)
        if ((x / 20 + y / 20) % 2 == 0) tft.fillRect(x, y, 20, 20, 0xffff);
  if (phase == 5) drawn = 255;
}

}  // namespace

void beginAtlasDisplay() {
  loadThemePreference();
  resetTouchControls(true);
  if (!tft.init()) {
    serialLog.println("ATLAS|DISPLAY|INIT_FAILED");
    return;
  }
  tft.initDMA();
  tft.setRotation(AtlasConfig::LCD_ROTATION);
  tft.setBrightness(BACKLIGHT_ON);
  lv_init();
  lv_tick_set_cb(tick);
  display = lv_display_create(W, H);
  const size_t bytes = W * BUFFER_LINES * sizeof(uint16_t);
  void *a = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void *b = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (display == nullptr || a == nullptr || b == nullptr) {
    serialLog.println("ATLAS|DISPLAY|LVGL|NO_MEMORY");
    return;
  }
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, a, b, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);
  renderAtlasSplash({theme, inverted}, TurnHubFirmware::VERSION);
  lv_timer_handler();
  startedAtMs = millis();
  splashUp = true;
  displayReady = true;
  char line[80];
  snprintf(line, sizeof(line), "ATLAS|DISPLAY|ST7796|%dx%d|ROTATION|%u|INVERT|%u", tft.width(), tft.height(),
           static_cast<unsigned>(AtlasConfig::LCD_ROTATION), AtlasConfig::LCD_INVERT ? 1u : 0u);
  serialLog.println(line);
}

void serviceAtlasDisplay(uint32_t nowMs) {
  if (!displayReady) return;
  if (screenTesting) {
    serviceScreenTest(nowMs);
    return;  // Touches must not activate controls hidden by the test.
  }
  if (nowMs - lastTouchPollMs >= TOUCH_POLL_MS) {
    lastTouchPollMs = nowMs;
    lgfx::touch_point_t point;
    bool touched = tft.getTouch(&point, 1) > 0;
    if (screenTestReleaseTouch) {
      if (!touched) screenTestReleaseTouch = false;
      touched = false;
    }
    // Panel pixels are model pixels.
    const int16_t x = touched ? point.x : 0;
    const int16_t y = touched ? point.y : 0;
    if (touched) {
      if (!touchLogged) {
        // One line per press, for checking the touch orientation.
        char line[64];
        snprintf(line, sizeof(line), "ATLAS|TOUCH|PANEL|%d|%d|SCREEN|%d|%d", point.x, point.y, x, y);
        serialLog.println(line);
        touchLogged = true;
      }
      lastTouchedAtMs = nowMs;
      noteAtlasActivity(nowMs);
    } else if (nowMs - lastTouchedAtMs >= TOUCH_RELEASE_MS) {
      touchLogged = false;
    }
    // The splash ignores touches rather than acting on an unseen screen.
    if (!splashUp) updateTouchControls(nowMs, touched, x, y);
  }
  if (splashUp && nowMs - startedAtMs >= SPLASH_MS) splashUp = false;
  // Mana Tap's orbs move at about 30 frames a second while it plays.
  const bool manaPlaying = shownValid && shown.kind == ScreenKind::ManaTap &&
      shown.mana.phase() == TurnHub::ManaPhase::Playing;
  if (!splashUp && nowMs - lastScreenPollMs >= (manaPlaying ? MANA_FRAME_MS : SCREEN_POLL_MS)) {
    lastScreenPollMs = nowMs;
    AtlasScreen screen;
    buildAtlasScreen(nowMs, screen);
    if (!shownValid || !sameScreen(screen, shown)) {
      // A ticking clock and Mana Tap's orbs update in place; anything else rebuilds.
      if (!shownValid || !sameScreenButTimes(screen, shown) || !updateAtlasScreenTimes(screen) ||
          !updateAtlasScreenMana(screen))
        renderAtlasScreen(screen, {theme, inverted});
      shown = screen;
      shownValid = true;
    }
  }
  lv_timer_handler();
}

bool startAtlasScreenTest() {
  if (!displayReady || screenTesting) return false;
  screenTestStartedMs = millis();
  screenTesting = true;
  screenTestReleaseTouch = true;
  noteAtlasActivity(screenTestStartedMs);
  return true;
}

TurnHubTheme::Id atlasDisplayTheme() { return theme; }

bool chooseAtlasDisplayTheme(TurnHubTheme::Id id) {
  if (!TurnHubTheme::valid(static_cast<uint8_t>(id))) return false;
  TurnHub::OptionalPreferences prefs;
  if (!prefs.begin("display", false)) return false;
  const String key(TurnHubTheme::palette(id).key);
  const bool saved = prefs.putString("theme", key) == key.length();
  prefs.end();
  if (saved) {
    theme = id;
    shownValid = false;
  }
  return saved;
}

bool atlasDisplayInverted() { return inverted; }

bool chooseAtlasDisplayInverted(bool value) {
  TurnHub::OptionalPreferences prefs;
  if (!prefs.begin("display", false)) return false;
  const bool saved = prefs.putBool("inverted", value) == 1;
  prefs.end();
  if (saved) {
    inverted = value;
    shownValid = false;
  }
  return saved;
}

}  // namespace TurnHubAtlas

#endif  // TURNHUB_BOARD_ES3C40P
