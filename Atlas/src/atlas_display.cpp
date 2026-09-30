// Atlas built-in display: ILI9341V 240x320 TFT driven through LovyanGFX, and
// the XPT2046 resistive touch controller read over bit-banged SPI (HSPI is
// the TFT's and VSPI is kept for the microSD card). Presentation only: it
// draws the AtlasScreen from touch_controls.cpp and feeds touches back to it.

#include "atlas_display.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "atlas_art.h"
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
  tft.fillScreen(WALNUT);
  tft.setTextDatum(lgfx::middle_center);
  tft.setTextColor(CREAM, WALNUT);
  tft.setFont(&fonts::DejaVu18);
  tft.drawString("Touch calibration", ATLAS_SCREEN_WIDTH / 2, 84);
  char line[48];
  snprintf(line, sizeof(line), "Press and release the cross (%u of %u)",
      static_cast<unsigned>(cal.point + 1), static_cast<unsigned>(TOUCH_CAL_POINTS));
  tft.setTextColor(MUTED, WALNUT);
  tft.setFont(&fonts::DejaVu12);
  tft.drawString(line, ATLAS_SCREEN_WIDTH / 2, 116);
  if (message != nullptr) {
    tft.setTextColor(BRASS, WALNUT);
    tft.drawString(message, ATLAS_SCREEN_WIDTH / 2, 140);
  }
  int16_t x = 0, y = 0;
  touchCalibrationTarget(cal.point, ATLAS_SCREEN_WIDTH, ATLAS_SCREEN_HEIGHT, x, y);
  tft.drawFastHLine(x - 14, y, 29, CREAM);
  tft.drawFastVLine(x, y - 14, 29, CREAM);
  tft.drawCircle(x, y, 8, BRASS);
  tft.drawCircle(x, y, 9, BRASS);
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
  tft.fillScreen(WALNUT);
  tft.setTextDatum(lgfx::middle_center);
  tft.setTextColor(CREAM, WALNUT);
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
    finishCalibrationMessage(nowMs, "Calibration canceled");
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
  const AtlasArtStatus art = beginAtlasArt(tft);
  serialLog.println(art.fonts ? "ATLAS|DISPLAY|FONTS|BRASS" : "ATLAS|DISPLAY|FONTS|FALLBACK");
  if (!art.buffers) serialLog.println("ATLAS|DISPLAY|SPRITES|UNBUFFERED");
  drawAtlasSplash();
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
    invalidateAtlasScreen();
  }
  if (mode != DisplayMode::Status || nowMs - lastScreenPollMs < SCREEN_POLL_MS) return;
  lastScreenPollMs = nowMs;
  AtlasScreen screen;
  buildAtlasScreen(nowMs, screen);
  renderAtlasScreen(screen, nowMs);
}

}  // namespace TurnHubAtlas
