// Atlas built-in display: ILI9341V 240x320 TFT driven through LovyanGFX.
// For now it shows only the TurnHub splash (logo, firmware version).

#include "atlas_display.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"
#include "firmware_version.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

namespace {

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

// Fonts: DejaVu (Bitstream Vera license) and the built-in glcd font only; the
// GNU FreeFont "Free*" fonts bundled with LovyanGFX are GPL and not used.

// Splash palette (RGB888; LovyanGFX converts to the panel's RGB565).
constexpr uint32_t BACKGROUND = 0x0B1020;
constexpr uint32_t RING = 0x2A3350;
constexpr uint32_t ACCENT = 0xF2A33A;
constexpr uint32_t SEAT_IDLE = 0x8A93B0;
constexpr uint32_t WORDMARK = 0xF4F6FB;
constexpr uint32_t SUBTLE = 0x8A93B0;

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

}  // namespace

void beginAtlasDisplay() {
  if (!tft.init()) {
    serialLog.println("ATLAS|DISPLAY|INIT_FAILED");
    return;
  }
  tft.setRotation(1);  // Landscape, 320x240.
  tft.setBrightness(200);
  drawSplash();
  serialLog.println("ATLAS|DISPLAY|SPLASH");
}

}  // namespace TurnHubAtlas
