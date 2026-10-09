// Atlas built-in display: ILI9341V 240x320 TFT driven through LovyanGFX, and
// the XPT2046 resistive touch controller read over bit-banged SPI (HSPI is
// the TFT's and VSPI is kept for the microSD card). Presentation only: it
// draws the AtlasScreen from touch_controls.cpp and feeds touches back to it.

#include "atlas_display.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "atlas_art.h"
#include "atlas_battery.h"
#include "config.h"
#include "firmware_version.h"
#include "optional_preferences.h"
#include "serial_log.h"
#include "touch_calibration.h"
#include "touch_controls.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

// A player is at the table: restarts the idle count (table_intents.cpp,
// declared in atlas_app.h).
void noteAtlasActivity(uint32_t nowMs);

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

constexpr uint8_t BACKLIGHT_ON = 200;
// The cell is a backup that keeps Atlas (the game, the radio, the portal)
// running through a pulled cable, not a power source for play: on it the
// backlight, Atlas's biggest load, is off. A touch lights the screen this
// long after the last touch, so someone can check it; that first press
// presses nothing.
constexpr uint32_t BATTERY_PEEK_MS = 15000;

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
bool backlightOn = true;
// On battery: lit by a touch until BATTERY_PEEK_MS after the last one.
bool peeking = false;
// A press that woke the dark screen; ignored until the finger lifts.
bool wakePress = false;

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
bool screenTestReleaseTouch = false;
uint32_t screenTestStartedMs = 0;
bool screenTesting = false;

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
  tft.setBrightness(BACKLIGHT_ON);
  TurnHub::OptionalPreferences themePrefs;
  if (themePrefs.begin("display", true)) {
    const String key = themePrefs.getString("theme", "graphite");
    themePrefs.end();
    for (uint8_t i = 0; i < static_cast<uint8_t>(TurnHubTheme::Id::Count); ++i) {
      const auto id = static_cast<TurnHubTheme::Id>(i);
      if (key == TurnHubTheme::palette(id).key) setAtlasArtTheme(id);
    }
  }
  const AtlasArtStatus art = beginAtlasArt(tft);
  serialLog.println(art.fonts ? "ATLAS|DISPLAY|FONTS|THEMED" : "ATLAS|DISPLAY|FONTS|FALLBACK");
  if (!art.buffers) serialLog.println("ATLAS|DISPLAY|SPRITES|UNBUFFERED");
  drawAtlasSplash();
  displayStartedAtMs = millis();
  displayReady = true;
  serialLog.println("ATLAS|DISPLAY|SPLASH");
}

// Backlight off on battery (unless a touch is peeking), on with USB.
void serviceBacklight(uint32_t nowMs) {
  const bool battery = atlasOnBattery();
  if (!battery || nowMs - lastTouchedAtMs >= BATTERY_PEEK_MS) peeking = false;
  const bool lit = !battery || peeking;
  if (lit == backlightOn) return;
  backlightOn = lit;
  if (displayReady) tft.setBrightness(lit ? BACKLIGHT_ON : 0);
  serialLog.println(lit ? "ATLAS|DISPLAY|BACKLIGHT|ON" : "ATLAS|DISPLAY|BACKLIGHT|OFF");
}

void serviceAtlasDisplay(uint32_t nowMs) {
  if (screenTesting) {
    const uint32_t elapsed = nowMs - screenTestStartedMs;
    if (elapsed < 9000) {
      static uint8_t drawn = 255;
      const uint8_t phase = elapsed / 1500;
      if (phase != drawn) {
        drawn = phase;
        const uint16_t colors[] = {0x0000, 0xffff, 0xf800, 0x07e0, 0x001f};
        tft.fillScreen(phase < 5 ? colors[phase] : 0x0000);
        if (phase == 5)
          for (int y = 0; y < tft.height(); y += 20)
            for (int x = 0; x < tft.width(); x += 20)
              if ((x / 20 + y / 20) % 2 == 0) tft.fillRect(x, y, 20, 20, 0xffff);
      }
      return;  // Touches must not activate controls hidden by the test.
    }
    screenTesting = false;
    invalidateAtlasScreen();
    serviceBacklight(nowMs);
  }
  if (nowMs - lastTouchPollMs >= TOUCH_POLL_MS) {
    lastTouchPollMs = nowMs;
    uint16_t rawX = 0, rawY = 0;
    bool touched = readTouchRaw(rawX, rawY);
    if (screenTestReleaseTouch) {
      if (!touched) screenTestReleaseTouch = false;
      touched = false;
    }
    int16_t x = 0, y = 0;
    if (touched) mapTouch(touchCal, rawX, rawY, ATLAS_SCREEN_WIDTH, ATLAS_SCREEN_HEIGHT, x, y);
    if (touched) {
      // A new press, not a resistive drop-out within one.
      if (nowMs - lastTouchedAtMs >= TOUCH_RELEASE_MS) {
        touchHeldSinceMs = nowMs;
        wakePress = !backlightOn;
        char line[64];
        snprintf(line, sizeof(line), "ATLAS|TOUCH|RAW|%u|%u|SCREEN|%d|%d", rawX, rawY, x, y);
        serialLog.println(line);
      }
      lastTouchedAtMs = nowMs;
      noteAtlasActivity(nowMs);
      if (atlasOnBattery()) peeking = true;
    } else if (nowMs - lastTouchedAtMs >= TOUCH_RELEASE_MS) {
      wakePress = false;
    }
    // The press that lit a dark screen acts on nothing.
    if (wakePress) touched = false;

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

  serviceBacklight(nowMs);
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

TurnHubTheme::Id atlasDisplayTheme() { return atlasArtTheme(); }
bool chooseAtlasDisplayTheme(TurnHubTheme::Id theme) {
  if (!TurnHubTheme::valid(static_cast<uint8_t>(theme))) return false;
  TurnHub::OptionalPreferences prefs;
  if (!prefs.begin("display", false)) return false;
  const String key(TurnHubTheme::palette(theme).key);
  const bool saved = prefs.putString("theme", key) == key.length();
  prefs.end();
  if (saved) setAtlasArtTheme(theme);
  return saved;
}

bool startAtlasScreenTest() {
  if (!displayReady || mode != DisplayMode::Status || screenTesting) return false;
  screenTestStartedMs = millis();
  screenTesting = true;
  screenTestReleaseTouch = true;
  tft.setBrightness(BACKLIGHT_ON);
  backlightOn = true;
  noteAtlasActivity(screenTestStartedMs);
  return true;
}

namespace {
// Drives a pin to a level that lasts through deep sleep (outputs otherwise
// float once the chip sleeps). The reset takes the backlight off its PWM.
void holdPin(int8_t pin, int level) {
  const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
  gpio_reset_pin(gpio);
  gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
  gpio_set_level(gpio, level);
  gpio_hold_en(gpio);
}

constexpr gpio_num_t WAKE_TOUCH_PIN = static_cast<gpio_num_t>(AtlasConfig::TOUCH_IRQ_PIN);
constexpr gpio_num_t WAKE_BOOT_PIN = static_cast<gpio_num_t>(AtlasConfig::BOOT_BUTTON_PIN);
// The wake is level-triggered, so a finger still on the panel would wake
// Atlas at once; wait this long at most for it to lift.
constexpr uint32_t SLEEP_RELEASE_WAIT_MS = 3000;

// Asleep on the cell, the timer wakes Atlas this often to look for USB. The
// first check comes soon after the load is gone, to read the resting cell
// before USB could come back.
constexpr uint64_t USB_CHECK_INTERVAL_US = 60ULL * 1000000ULL;
constexpr uint64_t USB_FIRST_CHECK_US = 5ULL * 1000000ULL;
// The board has no USB-sense pin. Asleep, the cell rests with no load, so
// the charger pushing it up this far over the last check (or to near full)
// means USB is back. *Needs verification* on the 502030: awake, plugging in
// stepped the reading 200-220 mV, but that included Atlas's own load.
constexpr uint16_t USB_STEP_MV = 80;
constexpr uint16_t USB_FULL_MV = 4150;
constexpr uint8_t USB_CHECK_READS = 32;

// Kept through deep sleep (RTC slow memory); cleared by a cold start.
RTC_DATA_ATTR bool sleptOnBattery = false;
RTC_DATA_ATTR bool sleptEmpty = false;
RTC_DATA_ATTR uint16_t restingCellMv = 0;

uint16_t readRestingCellMv() {
  analogSetPinAttenuation(AtlasConfig::BATTERY_ADC_PIN, ADC_11db);
  uint32_t total = 0;
  for (uint8_t i = 0; i < USB_CHECK_READS; ++i) total += analogReadMilliVolts(AtlasConfig::BATTERY_ADC_PIN);
  return static_cast<uint16_t>(total / USB_CHECK_READS * AtlasConfig::BATTERY_DIVIDER_NUMERATOR /
      AtlasConfig::BATTERY_DIVIDER_DENOMINATOR);
}

// The outputs are already held; arms the wake sources and sleeps.
void armWakeAndSleep() {
  gpio_deep_sleep_hold_en();
  // The pen interrupt line has its pull-up on the board (GPIO36 has no
  // internal one); BOOT's moves to the RTC domain, which ext0 keeps on.
  rtc_gpio_pullup_en(WAKE_BOOT_PIN);
  rtc_gpio_pulldown_dis(WAKE_BOOT_PIN);
  esp_sleep_enable_ext0_wakeup(WAKE_TOUCH_PIN, 0);
  esp_sleep_enable_ext1_wakeup(1ULL << WAKE_BOOT_PIN, ESP_EXT1_WAKEUP_ALL_LOW);
  if (sleptOnBattery) esp_sleep_enable_timer_wakeup(restingCellMv == 0 ? USB_FIRST_CHECK_US : USB_CHECK_INTERVAL_US);
  esp_deep_sleep_start();
}
}  // namespace

void sleepAtlas(bool batteryEmpty) {
  using namespace AtlasConfig;
  const uint32_t startedMs = millis();
  while ((digitalRead(TOUCH_IRQ_PIN) == LOW || digitalRead(BOOT_BUTTON_PIN) == LOW) &&
         millis() - startedMs < SLEEP_RELEASE_WAIT_MS) {
    delay(10);
  }
  if (displayReady) {
    tft.setBrightness(0);
    tft.sleep();
  }
  // Power the touch controller down with its pen interrupt armed: that line
  // is the wake signal.
  digitalWrite(TOUCH_CS_PIN, LOW);
  touchTransfer(0xD0);
  digitalWrite(TOUCH_CS_PIN, HIGH);
  serialLog.println("ATLAS|SLEEP|POWER_DOWN");
  Serial.flush();
  holdPin(TFT_BACKLIGHT_PIN, LOW);
  holdPin(TOUCH_CS_PIN, HIGH);
  holdPin(RGB_RED_PIN, HIGH);  // Common anode: high is off.
  holdPin(RGB_GREEN_PIN, HIGH);
  holdPin(RGB_BLUE_PIN, HIGH);
  holdPin(AUDIO_ENABLE_PIN, HIGH);  // Amplifier off.
  // On USB, only a touch or BOOT wakes Atlas; on the cell, so does USB.
  sleptOnBattery = atlasOnBattery();
  sleptEmpty = batteryEmpty;
  restingCellMv = 0;
  armWakeAndSleep();
}

void stayAsleepWithoutUsb() {
  const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
  const bool timer = wake == ESP_SLEEP_WAKEUP_TIMER;
  const bool pressed = wake == ESP_SLEEP_WAKEUP_EXT0 || wake == ESP_SLEEP_WAKEUP_EXT1;
  // A touch or BOOT starts Atlas, unless the cell is flat.
  if (!sleptOnBattery || !(timer || (pressed && sleptEmpty))) {
    sleptOnBattery = false;
    sleptEmpty = false;
    return;
  }
  const uint16_t mv = readRestingCellMv();
  if (mv >= USB_FULL_MV || (restingCellMv != 0 && mv >= restingCellMv + USB_STEP_MV)) {
    sleptOnBattery = false;
    sleptEmpty = false;
    return;
  }
  // The resting cell creeps up after the load goes; follow it.
  restingCellMv = mv;
  armWakeAndSleep();
}

void releaseSleepWakePins() {
  const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
  if (wake == ESP_SLEEP_WAKEUP_EXT0 || wake == ESP_SLEEP_WAKEUP_EXT1 || wake == ESP_SLEEP_WAKEUP_TIMER) {
    rtc_gpio_deinit(WAKE_TOUCH_PIN);
    rtc_gpio_deinit(WAKE_BOOT_PIN);
  }
  using namespace AtlasConfig;
  gpio_deep_sleep_hold_dis();
  const int8_t held[] = {TFT_BACKLIGHT_PIN, TOUCH_CS_PIN, static_cast<int8_t>(RGB_RED_PIN),
      static_cast<int8_t>(RGB_GREEN_PIN), static_cast<int8_t>(RGB_BLUE_PIN),
      static_cast<int8_t>(AUDIO_ENABLE_PIN)};
  for (const int8_t pin : held) gpio_hold_dis(static_cast<gpio_num_t>(pin));
}

}  // namespace TurnHubAtlas
