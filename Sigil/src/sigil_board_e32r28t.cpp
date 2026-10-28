// The touch Sigil stand-in on the Gen 1 Atlas board (E32R28T): the ILI9341
// 240 x 320 screen through LovyanGFX, drawn with LVGL (sigil_display_lvgl.cpp),
// and the XPT2046 resistive touch panel read over bit-banged SPI (the panel
// has HSPI to itself). Firmware-only (env:sigil-e32r28t). The touch reading
// and its default calibration come from legacy/gen1's atlas_display.cpp.

#if defined(TURNHUB_BOARD_E32R28T)

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "sigil_board.h"
#include "sigil_display_lvgl.h"
#include "sigil_touch.h"

namespace TurnHubSigil {
namespace {

using namespace TurnHubSigilBoard;

constexpr uint32_t BUFFER_LINES = 20;
constexpr uint8_t BACKLIGHT_ON = 200;
// LVGL timers and the pressed-key highlight.
constexpr uint32_t IDLE_WORK_MS = 30;

class Panel : public lgfx::LGFX_Device {
 public:
  Panel() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = TFT_SCLK_PIN;
      cfg.pin_mosi = TFT_MOSI_PIN;
      cfg.pin_miso = TFT_MISO_PIN;
      cfg.pin_dc = TFT_DC_PIN;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = TFT_CS_PIN;
      cfg.pin_rst = -1;  // Tied to the ESP32 EN line.
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }
    {
      auto cfg = light_.config();
      cfg.pin_bl = TFT_BACKLIGHT_PIN;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 4;  // The RGB LED has 0-2 and the buzzer 7.
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

Panel tft;
lv_display_t *display = nullptr;
volatile uint8_t touchedKey = static_cast<uint8_t>(Key::Count);

void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels) {
  const uint32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
  // LVGL renders little-endian RGB565; the panel takes it big-endian.
  lv_draw_sw_rgb565_swap(pixels, w * h);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.writePixels(reinterpret_cast<uint16_t *>(pixels), w * h);
  tft.endWrite();
  lv_display_flush_ready(disp);
}

uint32_t tick() { return millis(); }

class TouchSigilDisplay : public LvglSigilDisplay {
 public:
  void begin() override {
    if (!tft.init()) {
      Serial.println("SIGIL|DISPLAY|INIT_FAILED");
      return;
    }
    tft.setRotation(0);  // The panel's own portrait.
    tft.setBrightness(BACKLIGHT_ON);
    lv_init();
    lv_tick_set_cb(tick);
    display = lv_display_create(SIGIL_SCREEN_W, SIGIL_SCREEN_H);
    const size_t bytes = SIGIL_SCREEN_W * BUFFER_LINES * sizeof(uint16_t);
    void *a = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *b = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (display == nullptr || a == nullptr || b == nullptr) {
      Serial.println("SIGIL|DISPLAY|LVGL|NO_MEMORY");
      display = nullptr;
      return;
    }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, a, b, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    Serial.printf("SIGIL|DISPLAY|ILI9341|%dx%d|LVGL\n", tft.width(), tft.height());
  }

  uint32_t idleWorkDueInMs(uint32_t) const override { return display ? IDLE_WORK_MS : UINT32_MAX; }

  void idleWork(uint32_t) override {
    if (!display) return;
    const Key key = static_cast<Key>(touchedKey);
    if (key != shownKey_) {
      shownKey_ = key;
      setPressedKey(key);
    }
    lv_timer_handler();
  }

 protected:
  void present() override {
    if (display) lv_refr_now(display);
  }

 private:
  Key shownKey_ = Key::Count;
};

// --- Touch (XPT2046, bit-banged SPI mode 0) ----------------------------------

// Raw-to-screen mapping for the portrait screen: the gen1 landscape defaults
// (rotation 3) turned a quarter. Resistive panels vary; adjust from the
// SIGIL|TOUCH lines if presses land off their keys.
constexpr int16_t RAW_X_MIN = 200, RAW_X_MAX = 3700;
constexpr int16_t RAW_Y_MIN = 240, RAW_Y_MAX = 3800;
constexpr bool SWAP_XY = true;   // Screen x follows the raw Y channel.
constexpr bool INVERT_X = true;  // ... falling as x grows.
constexpr bool INVERT_Y = false;
constexpr int32_t PRESSURE_MIN = 400;
// A resistive panel drops out for a sample or two under a steady finger;
// a press holds this long past its last good reading so holds survive.
constexpr uint32_t RELEASE_MS = 60;

uint16_t transfer(uint8_t command) {
  for (int8_t bit = 7; bit >= 0; --bit) {
    digitalWrite(TOUCH_MOSI_PIN, (command >> bit) & 1);
    delayMicroseconds(1);
    digitalWrite(TOUCH_SCLK_PIN, HIGH);
    delayMicroseconds(1);
    digitalWrite(TOUCH_SCLK_PIN, LOW);
  }
  digitalWrite(TOUCH_MOSI_PIN, LOW);
  // DOUT changes on the falling edge: sample while the clock is high.
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
  if (b > c) b = c;
  return a > b ? a : b;
}

int32_t pressure() { return static_cast<int32_t>(transfer(0xB1)) + 4095 - transfer(0xC1); }

// The first conversion after the plates switch has not settled.
uint16_t channel(uint8_t command) {
  transfer(command);
  const uint16_t a = transfer(command), b = transfer(command), c = transfer(command);
  return median3(a, b, c);
}

bool readRaw(uint16_t &x, uint16_t &y) {
  if (digitalRead(TOUCH_IRQ_PIN) == HIGH) return false;
  digitalWrite(TOUCH_CS_PIN, LOW);
  const int32_t before = pressure();
  x = channel(0x91);
  y = channel(0xD1);
  const int32_t after = pressure();
  transfer(0xD0);  // Power down with the pen interrupt enabled.
  digitalWrite(TOUCH_CS_PIN, HIGH);
  return before >= PRESSURE_MIN && after >= PRESSURE_MIN;
}

int16_t scale(int32_t raw, int32_t lo, int32_t hi, bool invert, int16_t size) {
  int32_t v = (raw - lo) * size / (hi - lo);
  if (invert) v = size - 1 - v;
  return static_cast<int16_t>(v < 0 ? 0 : v >= size ? size - 1 : v);
}

}  // namespace

void sigilTouchBegin() {
  pinMode(TOUCH_CS_PIN, OUTPUT);
  digitalWrite(TOUCH_CS_PIN, HIGH);
  pinMode(TOUCH_SCLK_PIN, OUTPUT);
  digitalWrite(TOUCH_SCLK_PIN, LOW);
  pinMode(TOUCH_MOSI_PIN, OUTPUT);
  pinMode(TOUCH_MISO_PIN, INPUT);
  pinMode(TOUCH_IRQ_PIN, INPUT);
  digitalWrite(TOUCH_CS_PIN, LOW);
  transfer(0xD0);  // Arm the pen interrupt.
  digitalWrite(TOUCH_CS_PIN, HIGH);
}

Key sigilTouchRead() {
  static bool logged = false;
  static Key last = Key::Count;
  static uint32_t lastSeenMs = 0;
  uint16_t rawX = 0, rawY = 0;
  Key key = Key::Count;
  const uint32_t nowMs = millis();
  if (readRaw(rawX, rawY)) {
    const uint16_t a = SWAP_XY ? rawY : rawX, b = SWAP_XY ? rawX : rawY;
    const int16_t x = SWAP_XY ? scale(a, RAW_Y_MIN, RAW_Y_MAX, INVERT_X, SIGIL_SCREEN_W)
                              : scale(a, RAW_X_MIN, RAW_X_MAX, INVERT_X, SIGIL_SCREEN_W);
    const int16_t y = SWAP_XY ? scale(b, RAW_X_MIN, RAW_X_MAX, INVERT_Y, SIGIL_SCREEN_H)
                              : scale(b, RAW_Y_MIN, RAW_Y_MAX, INVERT_Y, SIGIL_SCREEN_H);
    key = padKeyAt(x, y);
    if (!logged) {
      // One line per press, for checking the calibration.
      Serial.printf("SIGIL|TOUCH|RAW|%u|%u|SCREEN|%d|%d|KEY|%u\n", rawX, rawY, x, y,
          static_cast<unsigned>(key));
      logged = true;
    }
    last = key;
    lastSeenMs = nowMs;
  } else if (nowMs - lastSeenMs < RELEASE_MS) {
    key = last;
  } else {
    logged = false;
    last = Key::Count;
  }
  touchedKey = static_cast<uint8_t>(key);
  return key;
}

SigilDisplay &getSigilDisplay() {
  static TouchSigilDisplay display;
  return display;
}

}  // namespace TurnHubSigil

#endif  // TURNHUB_BOARD_E32R28T
