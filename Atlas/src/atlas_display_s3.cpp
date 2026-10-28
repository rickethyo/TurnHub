// The 4-inch S3 Atlas screen (ES3C40P): ST7796S over SPI and the FT6336U
// capacitive touch controller, driven by LovyanGFX, with LVGL 9.3 drawing
// the touch_controls.cpp screen model. Presentation only: touches go to
// updateTouchControls, which builds Intents. Firmware-only (env:atlas-s3);
// the other builds use atlas_board_none.cpp's display functions.
//
// The model is laid out for the panel's own 320 x 480 portrait, so every
// rectangle and touch maps 1:1; it is drawn with the S3 type and a glossy
// finish until the LVGL Atlas design replaces it.

#if defined(TURNHUB_BOARD_ES3C40P)

#include "atlas_display.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "atlas_app.h"
#include "config.h"
#include "firmware_version.h"
#include "optional_preferences.h"
#include "serial_log.h"
#include "touch_controls.h"

extern "C" {
extern const lv_font_t th_eyebrow_13;
extern const lv_font_t th_semi_16;
extern const lv_font_t th_semi_20;
extern const lv_font_t th_bold_28;
extern const lv_font_t th_cinzel_32;
extern const lv_font_t th_num_40;
extern const lv_font_t th_num_64;
}

using TurnHub::serialLog;

namespace TurnHubAtlas {
namespace {

constexpr int16_t W = ATLAS_SCREEN_WIDTH;
constexpr int16_t H = ATLAS_SCREEN_HEIGHT;
constexpr uint32_t SPLASH_MS = 1500;
constexpr uint32_t TOUCH_POLL_MS = 15;
constexpr uint32_t SCREEN_POLL_MS = 100;
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

// --- Drawing the screen model --------------------------------------------

const TurnHubTheme::Palette &pal() { return TurnHubTheme::palette(theme); }

lv_color_t rgb(uint32_t c) {
  if (inverted) c = 0xFFFFFF - (c & 0xFFFFFF);
  return lv_color_hex(c);
}

lv_color_t mixRgb(uint32_t a, uint32_t b, uint8_t t) {
  return lv_color_mix(rgb(b), rgb(a), t);
}

lv_obj_t *box(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

// The glossy finish: a vertical gradient from a lifted top to the base
// colour, a hairline highlight and a soft shadow.
void gloss(lv_obj_t *o, uint32_t base, uint32_t frame, int16_t radius, bool raised) {
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, mixRgb(base, 0xFFFFFF, pal().light ? 40 : 60), 0);
  lv_obj_set_style_bg_grad_color(o, rgb(base), 0);
  lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_border_color(o, rgb(frame), 0);
  if (raised) {
    lv_obj_set_style_shadow_width(o, 10, 0);
    lv_obj_set_style_shadow_offset_y(o, 3, 0);
    lv_obj_set_style_shadow_opa(o, LV_OPA_40, 0);
    lv_obj_set_style_shadow_color(o, lv_color_black(), 0);
  }
}

lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *font, uint32_t color,
               int16_t x, int16_t y, int16_t w, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_remove_style_all(l);
  lv_label_set_text(l, s);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, rgb(color), 0);
  lv_obj_set_style_text_align(l, align, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_width(l, w);
  return l;
}

// Life totals use the digit-only numeral fonts; anything else is spelled.
bool digitsOnly(const char *s) {
  for (; *s; ++s) if ((*s < '0' || *s > '9') && *s != '-') return false;
  return true;
}

const char *chipTag(const ScreenPlayer &p) {
  return (p.flags & CHIP_WINNER) ? "WINNER" : (p.flags & CHIP_OUT) ? "OUT" : (p.flags & CHIP_ACTIVE) ? "TURN"
      : (p.flags & CHIP_WAITING) ? "CONFIRM" : (p.flags & CHIP_STARTER) ? "STARTS" : "";
}

bool codeLayout(const AtlasScreen &s) {
  return s.kind == ScreenKind::Qr || s.kind == ScreenKind::Code || s.kind == ScreenKind::PairCode;
}

bool buttonsUseBody(const AtlasScreen &s) {
  return s.kind == ScreenKind::Table || s.kind == ScreenKind::Menu || s.kind == ScreenKind::Player ||
      s.kind == ScreenKind::Device || s.kind == ScreenKind::Themes;
}

bool primaryAction(TouchAction a) {
  switch (a) {
    case TouchAction::StartGame: case TouchAction::Rematch: case TouchAction::Pause:
    case TouchAction::Resume: case TouchAction::Pair: case TouchAction::SetupPair:
    case TouchAction::PairConfirm:
      return true;
    default:
      return false;
  }
}

void drawHeader(lv_obj_t *root, const AtlasScreen &s) {
  lv_obj_t *bar = box(root, 0, 0, W, SCREEN_HEADER_H);
  gloss(bar, pal().surface2, pal().separator, 0, false);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  text(bar, s.badge, &th_eyebrow_13, pal().accent, 10, 10, 120);
  char right[64] = {};
  size_t n = 0;
  if (s.round > 0) n += snprintf(right + n, sizeof(right) - n, "Round %u  ", static_cast<unsigned>(s.round));
  if (s.gameClock[0]) n += snprintf(right + n, sizeof(right) - n, "%s  ", s.gameClock);
  if (s.sdMissing) n += snprintf(right + n, sizeof(right) - n, "NO SD CARD  ");
  if (s.update != TurnHub::UpdateKind::None) n += snprintf(right + n, sizeof(right) - n, "Update available  ");
  snprintf(right + n, sizeof(right) - n, "Sigils %u", static_cast<unsigned>(s.sigilsOnline));
  text(bar, right, &th_semi_16, s.sdMissing ? pal().warning : pal().textSecondary, 130, 8, W - 140,
       LV_TEXT_ALIGN_RIGHT);
}

void drawHero(lv_obj_t *root, const AtlasScreen &s) {
  const bool clock = s.clock[0] != '\0';
  const int16_t x = 12;
  const int16_t w = W - x - (clock ? 110 : 12);
  text(root, s.title, &th_bold_28, pal().text, x, SCREEN_HERO_Y + 2, w);
  const bool notice = s.notice[0] != '\0';
  // A notice is marked in words by its leading arrow as well as its colour.
  char line[56];
  snprintf(line, sizeof(line), notice ? "> %s" : "%s", notice ? s.notice : s.detail);
  text(root, line, &th_semi_16, notice ? pal().accent : pal().textSecondary, x, SCREEN_HERO_Y + 38, w);
  if (!clock) return;
  // The turn clock and its countdown bar.
  const uint32_t ink = s.timerWarning ? pal().warning : pal().turn;
  text(root, s.clock, &th_bold_28, ink, W - 108, SCREEN_HERO_Y + 2, 96, LV_TEXT_ALIGN_RIGHT);
  if (s.timerPermille >= 0) {
    lv_obj_t *track = box(root, W - 108, SCREEN_HERO_Y + 44, 96, 8);
    lv_obj_set_style_radius(track, 4, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(track, rgb(pal().fill), 0);
    lv_obj_t *fill = box(track, 0, 0, 96 * s.timerPermille / 1000, 8);
    lv_obj_set_style_radius(fill, 4, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, rgb(ink), 0);
  }
}

void drawChip(lv_obj_t *root, const ScreenPlayer &p, bool showLife, int16_t mx, int16_t my, int16_t mw, int16_t mh) {
  const bool active = p.flags & CHIP_ACTIVE;
  const bool out = p.flags & CHIP_OUT;
  const bool winner = p.flags & CHIP_WINNER;
  lv_obj_t *chip = box(root, mx, my, mw, mh);
  gloss(chip, active || winner ? pal().turnSoft : pal().surface3,
        active || winner ? pal().turn : pal().separatorStrong, 12, true);
  if (active || winner) lv_obj_set_style_border_width(chip, 3, 0);
  const int16_t w = mw, h = mh;
  const uint32_t ink = out ? pal().textTertiary : pal().text;
  lv_obj_t *name = text(chip, p.name, &th_semi_20, ink, 10, 6, w - 20);
  if (out) lv_obj_set_style_text_decor(name, LV_TEXT_DECOR_STRIKETHROUGH, 0);
  const bool tall = h >= 90;
  if (showLife) {
    char life[12];
    snprintf(life, sizeof(life), "%ld", static_cast<long>(p.life));
    const lv_font_t *font = tall && w >= 100 ? &th_num_64 : &th_num_40;
    const uint32_t lifeInk = out ? pal().textTertiary : (active ? pal().turn : pal().text);
    text(chip, life, font, lifeInk, 4, tall ? (h - font->line_height) / 2 : h - font->line_height - 2, w - 8,
         tall ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_RIGHT);
  }
  const char *tag = chipTag(p);
  const uint32_t tagInk = active || winner ? pal().turn : (p.flags & CHIP_WAITING) ? pal().accent : pal().textSecondary;
  char foot[24];
  if (p.turnTime[0] && (tall || !tag[0])) {
    snprintf(foot, sizeof(foot), tag[0] ? "%s  %s" : "%s%s", tag, p.turnTime);
  } else {
    snprintf(foot, sizeof(foot), "%s", tag);
  }
  text(chip, foot, &th_eyebrow_13, tagInk, 10, h - 20, w - 20, tall ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT);
}

void drawLines(lv_obj_t *root, const AtlasScreen &s, int16_t x, int16_t y, int16_t w) {
  for (uint8_t i = 0; i < s.lineCount; ++i) {
    const bool warning = strstr(s.lines[i], "NOT INSERTED") != nullptr;
    text(root, s.lines[i], &th_semi_16, warning ? pal().critical : pal().textSecondary, x, y + i * 22, w);
  }
}

void drawQr(lv_obj_t *root, const char *data, int16_t x, int16_t y, int16_t size) {
  lv_obj_t *frame = box(root, x, y, size, size);
  lv_obj_set_style_radius(frame, 10, 0);
  lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
  // Always dark on white, whatever the theme, so phones can scan it.
  lv_obj_set_style_bg_color(frame, lv_color_white(), 0);
  lv_obj_t *qr = lv_qrcode_create(frame);
  lv_qrcode_set_size(qr, size - 16);
  lv_qrcode_set_dark_color(qr, lv_color_black());
  lv_qrcode_set_light_color(qr, lv_color_white());
  lv_qrcode_update(qr, data, strlen(data));
  lv_obj_center(qr);
}

void drawBody(lv_obj_t *root, const AtlasScreen &s) {
  if (codeLayout(s)) {
    // Code on top, the QR code centred under it, then its caption and lines.
    int16_t y = SCREEN_BODY_Y;
    if (s.code[0]) {
      text(root, s.code, digitsOnly(s.code) ? &th_num_64 : &th_bold_28, pal().accent, 12, y - 6, W - 24,
           LV_TEXT_ALIGN_CENTER);
      y += 70;
    }
    if (s.qr[0]) {
      int16_t size = BUTTON_ROW_Y - 12 - y - 24 - s.lineCount * 22;
      if (size > W - 64) size = W - 64;
      drawQr(root, s.qr, (W - size) / 2, y, size);
      y += size + 4;
      text(root, s.qrCaption, &th_semi_16, pal().textTertiary, 12, y, W - 24, LV_TEXT_ALIGN_CENTER);
      y += 24;
    }
    drawLines(root, s, 16, y, W - 32);
    return;
  }
  if (buttonsUseBody(s)) return;
  for (uint8_t i = 0; i < s.playerCount; ++i) {
    int16_t x, y, w, h;
    screenChipCell(i, s.playerCount, x, y, w, h);
    drawChip(root, s.players[i], s.showLife, x, y, w, h);
  }
  const int16_t bodyH = BUTTON_ROW_Y - 4 - SCREEN_BODY_Y;
  if (s.kind == ScreenKind::Status && s.playerCount == 0 && s.lineCount > 0) {
    lv_obj_t *plate = box(root, 12, SCREEN_BODY_Y, W - 24, bodyH);
    gloss(plate, pal().surface2, pal().separator, 14, true);
    drawLines(plate, s, 16, 12, W - 56);
    return;
  }
  // Lines first, then any QR code centred in the space left under them.
  drawLines(root, s, 16, SCREEN_BODY_Y + 8, W - 32);
  if (!s.qr[0]) return;
  const int16_t top = SCREEN_BODY_Y + 16 + s.lineCount * 22;
  int16_t size = SCREEN_BODY_Y + bodyH - top;
  if (size > W - 64) size = W - 64;
  if (size >= 96) drawQr(root, s.qr, (W - size) / 2, top, size);
}

void drawButton(lv_obj_t *root, const AtlasScreen &s, const TouchButton &b) {
  const bool pressed = s.pressed == b.action;
  const bool primary = primaryAction(b.action);
  lv_obj_t *o = box(root, b.x, b.y, b.w, b.h);
  const uint32_t base = pressed ? (primary ? pal().accentPressed : pal().fillStrong)
      : primary ? pal().accent : pal().surface3;
  gloss(o, base, b.selected || pressed ? pal().focus : pal().separatorStrong, 14, !pressed);
  // Pressed is also shown by a heavier frame, not by colour alone.
  if (pressed || b.selected) lv_obj_set_style_border_width(o, 3, 0);
  const uint32_t ink = primary ? pal().onAccent : pal().text;
  const int16_t w = b.w, h = b.h;
  char label[32];
  if (pressed && b.hold() && s.holdSecondsLeft > 0) {
    snprintf(label, sizeof(label), "Hold %u s", static_cast<unsigned>(s.holdSecondsLeft));
  } else {
    snprintf(label, sizeof(label), "%s", b.label);
  }
  const char *caption = b.selected && !pressed ? "shown" : (b.hold() && !pressed ? "hold" : "");
  const lv_font_t *font = &th_semi_20;
  const int16_t textY = (h - font->line_height) / 2 - (caption[0] ? 8 : 0);
  text(o, label, font, ink, 6, textY, w - 12, LV_TEXT_ALIGN_CENTER);
  if (caption[0]) text(o, caption, &th_eyebrow_13, primary ? pal().onAccent : pal().textSecondary,
                       6, textY + font->line_height, w - 12, LV_TEXT_ALIGN_CENTER);
  if (pressed && b.hold()) {
    lv_obj_t *track = box(o, 12, h - 12, w - 24, 6);
    lv_obj_set_style_radius(track, 3, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(track, lv_color_black(), 0);
    lv_obj_t *fill = box(track, 0, 0, (w - 24) * s.holdPermille / 1000, 6);
    lv_obj_set_style_radius(fill, 3, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, rgb(pal().focus), 0);
  }
}

void render(const AtlasScreen &s) {
  lv_obj_t *root = lv_screen_active();
  lv_obj_clean(root);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(root, rgb(pal().surface1), 0);
  lv_obj_set_style_bg_grad_color(root, rgb(pal().bg), 0);
  lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  drawHeader(root, s);
  drawHero(root, s);
  drawBody(root, s);
  for (uint8_t i = 0; i < s.buttonCount; ++i) drawButton(root, s, s.buttons[i]);
}

void drawSplash() {
  lv_obj_t *root = lv_screen_active();
  lv_obj_clean(root);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(root, rgb(pal().surface2), 0);
  lv_obj_set_style_bg_grad_color(root, rgb(pal().bg), 0);
  lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
  text(root, "TurnHub", &th_cinzel_32, pal().accent, 0, H / 2 - 40, W, LV_TEXT_ALIGN_CENTER);
  char version[40];
  snprintf(version, sizeof(version), "Atlas %s", TurnHubFirmware::VERSION);
  text(root, version, &th_semi_16, pal().textSecondary, 0, H / 2 + 6, W, LV_TEXT_ALIGN_CENTER);
}

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
  resetTouchControls();
  if (!tft.init()) {
    serialLog.println("ATLAS|DISPLAY|INIT_FAILED");
    return;
  }
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
  drawSplash();
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
  if (!splashUp && nowMs - lastScreenPollMs >= SCREEN_POLL_MS) {
    lastScreenPollMs = nowMs;
    AtlasScreen screen;
    buildAtlasScreen(nowMs, screen);
    if (!shownValid || !sameScreen(screen, shown)) {
      render(screen);
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
