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
#include <Preferences.h>
#include <stdio.h>

#include "sigil_board.h"
#include "sigil_display_lvgl.h"
#include "sigil_touch.h"
#include "touch_calibration.h"

extern "C" const lv_font_t th_semi_16;

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
      cfg.pwm_channel = 4;  // Speaker tone uses channel 7.
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

// The calibration run, shared by loop() (sampling) and the display task
// (drawing): the target shown, or CAL_IDLE, and a message under it.
constexpr uint8_t CAL_IDLE = 0xFF;
constexpr uint8_t CAL_RESULT = TOUCH_CAL_POINTS;  // Showing calMessage only.
enum class CalMessage : uint8_t { None, Longer, Retry, Saved, NotSaved, Canceled };
volatile uint8_t calStep = CAL_IDLE;
volatile CalMessage calMessage = CalMessage::None;

const char *calMessageText(CalMessage m) {
  switch (m) {
    case CalMessage::Longer: return "Hold a little longer";
    case CalMessage::Retry: return "Those did not line up: again";
    case CalMessage::Saved: return "Touch calibrated";
    case CalMessage::NotSaved: return "Calibrated (not saved)";
    case CalMessage::Canceled: return "Calibration canceled";
    default: return nullptr;
  }
}

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

  uint32_t idleWorkDueInMs(uint32_t) const override {
    return display ? IDLE_WORK_MS : UINT32_MAX;
  }

  void idleWork(uint32_t) override {
    if (!display) return;
    const Key key = static_cast<Key>(touchedKey);
    if (key != shownKey_) {
      shownKey_ = key;
      setPressedKey(key);
    }
    drawCalibration();
    lv_timer_handler();
  }

 protected:
  void present() override {
    if (display) lv_refr_now(display);
  }

 private:
  // The calibration screen sits on LVGL's top layer, over whatever screen is
  // current, so the normal screen is back as soon as it is removed.
  void drawCalibration() {
    const uint8_t step = calStep;
    const CalMessage message = calMessage;
    if (step == drawnStep_ && message == drawnMessage_) return;
    drawnStep_ = step;
    drawnMessage_ = message;
    if (overlay_) {
      lv_obj_delete(overlay_);
      overlay_ = nullptr;
    }
    if (step == CAL_IDLE) return;
    const auto &p = TurnHubTheme::palette(theme());
    overlay_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay_);
    lv_obj_set_size(overlay_, SIGIL_SCREEN_W, SIGIL_SCREEN_H);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(p.bg), 0);
    auto line = [&](const char *t, int16_t y, uint32_t color) {
      lv_obj_t *l = lv_label_create(overlay_);
      lv_label_set_text(l, t);
      lv_obj_set_style_text_font(l, &th_semi_16, 0);
      lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_width(l, SIGIL_SCREEN_W);
      lv_obj_set_pos(l, 0, y);
    };
    const char *text = calMessageText(message);
    if (step == CAL_RESULT) {
      line(text ? text : "", SIGIL_SCREEN_H / 2 - 10, p.text);
    } else {
      char progress[40];
      snprintf(progress, sizeof(progress), "Press the cross (%u of %u)", static_cast<unsigned>(step + 1),
          static_cast<unsigned>(TOUCH_CAL_POINTS));
      line("Touch calibration", 110, p.text);
      line(progress, 136, p.textSecondary);
      if (text) line(text, 162, p.accent);
      int16_t x = 0, y = 0;
      touchCalibrationTarget(step, SIGIL_SCREEN_W, SIGIL_SCREEN_H, x, y);
      auto bar = [&](int16_t bx, int16_t by, int16_t w, int16_t h) {
        lv_obj_t *o = lv_obj_create(overlay_);
        lv_obj_remove_style_all(o);
        lv_obj_set_pos(o, bx, by);
        lv_obj_set_size(o, w, h);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, lv_color_hex(p.text), 0);
      };
      bar(x - 14, y, 29, 1);
      bar(x, y - 14, 1, 29);
      lv_obj_t *ring = lv_obj_create(overlay_);
      lv_obj_remove_style_all(ring);
      lv_obj_set_pos(ring, x - 9, y - 9);
      lv_obj_set_size(ring, 19, 19);
      lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(ring, 2, 0);
      lv_obj_set_style_border_color(ring, lv_color_hex(p.accent), 0);
    }
    lv_refr_now(display);
  }

  Key shownKey_ = Key::Count;
  uint8_t drawnStep_ = CAL_IDLE;
  CalMessage drawnMessage_ = CalMessage::None;
  lv_obj_t *overlay_ = nullptr;
};

// --- Touch (XPT2046, bit-banged SPI mode 0) ----------------------------------

// The fallback until a calibration is saved (Device menu > Calibrate): raw
// Y runs along screen x, raw X falls down screen y. Read from bench taps on
// the first board, 2026-10-28.
TouchCalibration defaultCalibration() {
  TouchCalibration cal;
  cal.swapXY = 1;
  cal.xAtFirst = 469;
  cal.xAtLast = 3717;
  cal.yAtFirst = 3586;
  cal.yAtLast = 293;
  return cal;
}
TouchCalibration touchCal = defaultCalibration();
constexpr char CAL_NAMESPACE[] = "th_touch";
constexpr char CAL_KEY[] = "cal";
constexpr uint32_t CAL_SETTLE_MS = 100;  // A press's first samples are dropped.
constexpr uint8_t CAL_MIN_SAMPLES = 3;
constexpr uint32_t CAL_IDLE_MS = 30000;  // Gives up without a touch.
constexpr uint32_t CAL_RESULT_MS = 1500;
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
  const bool pressed = before >= PRESSURE_MIN && after >= PRESSURE_MIN;
  // Bench check: a pen-down the pressure test rejects, at most twice a second.
  static uint32_t weakLoggedMs = 0;
  if (!pressed && millis() - weakLoggedMs >= 500) {
    weakLoggedMs = millis();
    Serial.printf("SIGIL|TOUCH|WEAK|%ld|%ld|RAW|%u|%u\n", static_cast<long>(before),
        static_cast<long>(after), x, y);
  }
  return pressed;
}

void loadCalibration() {
  Preferences prefs;
  TouchCalibration saved;
  // A board never calibrated has no namespace yet: read-only begin fails.
  bool ok = prefs.begin(CAL_NAMESPACE, true);
  if (ok) {
    ok = prefs.getBytesLength(CAL_KEY) == sizeof(saved) &&
        prefs.getBytes(CAL_KEY, &saved, sizeof(saved)) == sizeof(saved) && validTouchCalibration(saved);
    prefs.end();
  }
  if (ok) touchCal = saved;
  Serial.printf("SIGIL|TOUCH|CALIBRATION|%s|swap=%u|x=%d..%d|y=%d..%d\n", ok ? "SAVED" : "DEFAULT",
      static_cast<unsigned>(touchCal.swapXY), touchCal.xAtFirst, touchCal.xAtLast, touchCal.yAtFirst,
      touchCal.yAtLast);
}

bool saveCalibration(const TouchCalibration &cal) {
  Preferences prefs;
  if (!prefs.begin(CAL_NAMESPACE, false)) return false;
  const bool ok = prefs.putBytes(CAL_KEY, &cal, sizeof(cal)) == sizeof(cal);
  prefs.end();
  return ok;
}

// The run in progress (loop() only).
struct CalibrationRun {
  bool awaitRelease = true;  // The press that chose Calibrate must lift first.
  bool contact = false;
  uint32_t contactAtMs = 0, lastContactAtMs = 0, lastActivityAtMs = 0, resultAtMs = 0;
  uint32_t sumX = 0, sumY = 0;
  uint8_t samples = 0;
  uint16_t rawX[TOUCH_CAL_POINTS] = {}, rawY[TOUCH_CAL_POINTS] = {};
};
CalibrationRun run;

void finishCalibration(uint32_t nowMs, CalMessage message) {
  run.resultAtMs = nowMs;
  calMessage = message;
  calStep = CAL_RESULT;
}

void serviceCalibration(uint32_t nowMs, bool touched, uint16_t rawX, uint16_t rawY) {
  if (calStep == CAL_RESULT) {
    if (nowMs - run.resultAtMs >= CAL_RESULT_MS && !touched) {
      calMessage = CalMessage::None;
      calStep = CAL_IDLE;
    }
    return;
  }
  if (touched) {
    run.lastActivityAtMs = run.lastContactAtMs = nowMs;
    if (run.awaitRelease) return;
    if (!run.contact) {
      run.contact = true;
      run.contactAtMs = nowMs;
      run.sumX = run.sumY = 0;
      run.samples = 0;
    }
    if (nowMs - run.contactAtMs >= CAL_SETTLE_MS && run.samples < 255) {
      run.sumX += rawX;
      run.sumY += rawY;
      ++run.samples;
    }
    return;
  }
  if (nowMs - run.lastActivityAtMs >= CAL_IDLE_MS) {
    Serial.println("SIGIL|TOUCH|CALIBRATION|TIMEOUT");
    finishCalibration(nowMs, CalMessage::Canceled);
    return;
  }
  if (nowMs - run.lastContactAtMs < RELEASE_MS) return;
  if (run.awaitRelease) {
    run.awaitRelease = false;
    return;
  }
  if (!run.contact) return;
  run.contact = false;
  if (run.samples < CAL_MIN_SAMPLES) {
    calMessage = CalMessage::Longer;
    return;
  }
  const uint8_t point = calStep;
  run.rawX[point] = static_cast<uint16_t>(run.sumX / run.samples);
  run.rawY[point] = static_cast<uint16_t>(run.sumY / run.samples);
  Serial.printf("SIGIL|TOUCH|CALIBRATION|POINT|%u|%u|%u\n", static_cast<unsigned>(point), run.rawX[point],
      run.rawY[point]);
  if (point + 1 < TOUCH_CAL_POINTS) {
    calMessage = CalMessage::None;
    calStep = point + 1;
    return;
  }
  TouchCalibration solved;
  if (!solveTouchCalibration(run.rawX, run.rawY, SIGIL_SCREEN_W, SIGIL_SCREEN_H, solved)) {
    Serial.println("SIGIL|TOUCH|CALIBRATION|REJECTED");
    calMessage = CalMessage::Retry;
    calStep = 0;
    return;
  }
  touchCal = solved;
  const bool saved = saveCalibration(solved);
  Serial.printf("SIGIL|TOUCH|CALIBRATION|%s|swap=%u|x=%d..%d|y=%d..%d\n", saved ? "STORED" : "STORAGE_ERROR",
      static_cast<unsigned>(solved.swapXY), solved.xAtFirst, solved.xAtLast, solved.yAtFirst, solved.yAtLast);
  finishCalibration(nowMs, saved ? CalMessage::Saved : CalMessage::NotSaved);
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
  loadCalibration();
}

void sigilTouchStartCalibration() {
  if (calStep != CAL_IDLE) return;
  run = CalibrationRun();
  run.lastActivityAtMs = millis();
  calMessage = CalMessage::None;
  calStep = 0;
  Serial.println("SIGIL|TOUCH|CALIBRATION|START");
}

bool sigilTouchCalibrating() { return calStep != CAL_IDLE; }

Key sigilTouchRead() {
  static bool logged = false;
  static Key last = Key::Count;
  static uint32_t lastSeenMs = 0;
  uint16_t rawX = 0, rawY = 0;
  Key key = Key::Count;
  const uint32_t nowMs = millis();
  if (calStep != CAL_IDLE) {
    // Calibrating: presses aim at the crosses, never at keys.
    const bool touched = readRaw(rawX, rawY);
    serviceCalibration(nowMs, touched, rawX, rawY);
    last = Key::Count;
    touchedKey = static_cast<uint8_t>(Key::Count);
    return Key::Count;
  }
  if (readRaw(rawX, rawY)) {
    int16_t x = 0, y = 0;
    mapTouch(touchCal, rawX, rawY, SIGIL_SCREEN_W, SIGIL_SCREEN_H, x, y);
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
