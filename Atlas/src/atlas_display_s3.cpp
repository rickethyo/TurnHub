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
#include <math.h>
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

// The app's elevation 2: a soft drop shadow under cards and buttons.
void lift(lv_obj_t *o) {
  lv_obj_set_style_shadow_width(o, 14, 0);
  lv_obj_set_style_shadow_offset_y(o, 4, 0);
  lv_obj_set_style_shadow_spread(o, -4, 0);
  lv_obj_set_style_shadow_opa(o, pal().light ? LV_OPA_20 : LV_OPA_50, 0);
  lv_obj_set_style_shadow_color(o, lv_color_black(), 0);
}

// An app card: a flat surface, hairline frame and rounded corners. Large
// areas stay flat because RGB565 bands visibly on long gradients.
void card(lv_obj_t *o, uint32_t fill, uint32_t frame, int16_t radius = 16) {
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, rgb(fill), 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_border_color(o, rgb(frame), 0);
}

// Buttons keep a gentle top sheen (short enough not to band).
void sheen(lv_obj_t *o, uint32_t base) {
  lv_obj_set_style_bg_color(o, mixRgb(base, 0xFFFFFF, pal().light ? 18 : 26), 0);
  lv_obj_set_style_bg_grad_color(o, rgb(base), 0);
  lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
}

// One line, cut with an ellipsis.
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
  lv_obj_set_size(l, w, lv_font_get_line_height(font));
  return l;
}

int32_t textWidth(const char *s, const lv_font_t *font) {
  lv_point_t size;
  lv_text_get_size(&size, s, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}

// Life totals use the digit-only numeral fonts; anything else is spelled.
bool digitsOnly(const char *s) {
  for (; *s; ++s) if ((*s < '0' || *s > '9') && *s != '-') return false;
  return true;
}

// Seat hues follow the app's avatar colours (Palette.avatarColor).
uint32_t hsl(float h, float s, float l) {
  const float c = (1 - fabsf(2 * l - 1)) * s, hp = h / 60, x = c * (1 - fabsf(fmodf(hp, 2.0f) - 1)), m = l - c / 2;
  float r = 0, g = 0, b = 0;
  if (hp < 1) { r = c; g = x; } else if (hp < 2) { r = x; g = c; } else if (hp < 3) { g = c; b = x; }
  else if (hp < 4) { g = x; b = c; } else if (hp < 5) { r = x; b = c; } else { r = c; b = x; }
  auto to = [&](float v) { return static_cast<uint32_t>(lroundf((v + m) * 255)); };
  return to(r) << 16 | to(g) << 8 | to(b);
}

uint32_t seatColor(uint8_t number) {
  static const float hues[] = {38, 168, 12, 205, 95, 280, 330, 60};
  const bool contrast = theme == TurnHubTheme::Id::Contrast;
  const float sat = contrast ? 0.0f : pal().ornament ? 0.42f : 0.5f;
  const float light = contrast ? 0.18f : pal().ornament ? 0.34f : pal().light ? 0.44f : 0.38f;
  return hsl(hues[(number + 7) % 8], sat, light);
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

// Actions that end, remove or erase something: spelled in the critical colour
// (and they are holds or ask twice, so colour is never the only warning).
bool dangerAction(TouchAction a) {
  switch (a) {
    case TouchAction::EndMatch: case TouchAction::ClearLobby: case TouchAction::RemoveSeat:
    case TouchAction::Concede: case TouchAction::ConfirmConcede: case TouchAction::UnpairSigils:
    case TouchAction::FactoryResetAtlas: case TouchAction::PairReject:
      return true;
    default:
      return false;
  }
}

// A rounded tag, like the app's status pills.
lv_obj_t *pill(lv_obj_t *parent, const char *s, uint32_t fill, uint32_t ink) {
  lv_obj_t *o = box(parent, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(o, 8, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, rgb(fill), 0);
  lv_obj_set_style_pad_hor(o, 8, 0);
  lv_obj_set_style_pad_ver(o, 3, 0);
  lv_obj_t *l = lv_label_create(o);
  lv_obj_remove_style_all(l);
  lv_label_set_text(l, s);
  lv_obj_set_style_text_font(l, &th_eyebrow_13, 0);
  lv_obj_set_style_text_color(l, rgb(ink), 0);
  return o;
}

// A filled disc in the seat's hue holding the player's initial.
void seatToken(lv_obj_t *parent, const ScreenPlayer &p, int16_t x, int16_t y, int16_t side, bool ring) {
  lv_obj_t *disc = box(parent, x, y, side, side);
  lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(disc, rgb(seatColor(p.number)), 0);
  if (ring) {
    lv_obj_set_style_border_width(disc, 2, 0);
    lv_obj_set_style_border_color(disc, rgb(pal().turn), 0);
  }
  char initial[2] = {p.name[0] ? p.name[0] : '?', 0};
  lv_obj_t *l = lv_label_create(disc);
  lv_obj_remove_style_all(l);
  lv_label_set_text(l, initial);
  lv_obj_set_style_text_font(l, side >= 36 ? &th_semi_20 : &th_semi_16, 0);
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_obj_center(l);
}

void drawHeader(lv_obj_t *root, const AtlasScreen &s) {
  lv_obj_t *bar = box(root, 0, 0, W, SCREEN_HEADER_H);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_color(bar, rgb(pal().separator), 0);
  text(bar, s.badge, &th_eyebrow_13, pal().accent, 14, 10, 110);
  char right[64] = {};
  size_t n = 0;
  if (s.round > 0) n += snprintf(right + n, sizeof(right) - n, "Round %u  ", static_cast<unsigned>(s.round));
  if (s.gameClock[0]) n += snprintf(right + n, sizeof(right) - n, "%s  ", s.gameClock);
  if (s.sdMissing) n += snprintf(right + n, sizeof(right) - n, "NO SD  ");
  if (s.update != TurnHub::UpdateKind::None) n += snprintf(right + n, sizeof(right) - n, "Update  ");
  snprintf(right + n, sizeof(right) - n, "Sigils %u", static_cast<unsigned>(s.sigilsOnline));
  text(bar, right, &th_semi_16, s.sdMissing ? pal().warning : pal().textSecondary, 110, 7, W - 124,
       LV_TEXT_ALIGN_RIGHT);
}

void drawHero(lv_obj_t *root, const AtlasScreen &s) {
  const bool clock = s.clock[0] != '\0';
  const int16_t x = 14;
  const int16_t w = W - x - (clock ? 110 : 14);
  text(root, s.title, pal().ornament ? &th_cinzel_32 : &th_bold_28, pal().text, x, SCREEN_HERO_Y + 4, w);
  const bool notice = s.notice[0] != '\0';
  // A notice is marked in words by its leading arrow as well as its colour.
  char line[56];
  snprintf(line, sizeof(line), notice ? "> %s" : "%s", notice ? s.notice : s.detail);
  text(root, line, &th_semi_16, notice ? pal().accent : pal().textSecondary, x, SCREEN_HERO_Y + 40, w);
  if (!clock) return;
  // The turn clock and its countdown bar.
  const uint32_t ink = s.timerWarning ? pal().warning : pal().turn;
  text(root, s.clock, &th_bold_28, ink, W - 110, SCREEN_HERO_Y + 4, 96, LV_TEXT_ALIGN_RIGHT);
  if (s.timerPermille >= 0) {
    lv_obj_t *track = box(root, W - 110, SCREEN_HERO_Y + 44, 96, 8);
    lv_obj_set_style_radius(track, 4, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(track, rgb(pal().fill), 0);
    lv_obj_t *fill = box(track, 0, 0, 96 * s.timerPermille / 1000, 8);
    lv_obj_set_style_radius(fill, 4, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, rgb(ink), 0);
  }
}

// A player tile, like the app's seat cards: token and name, the life total
// large, and a status pill. The turn is the one thing that glows.
void drawChip(lv_obj_t *root, const ScreenPlayer &p, bool showLife, int16_t x, int16_t y, int16_t w, int16_t h) {
  const bool active = p.flags & CHIP_ACTIVE;
  const bool out = p.flags & CHIP_OUT;
  const bool winner = p.flags & CHIP_WINNER;
  const bool glow = active || winner;
  lv_obj_t *tile = box(root, x, y, w, h);
  card(tile, glow ? pal().turnSoft : pal().surface2, glow ? pal().turn : pal().separator);
  if (glow) lv_obj_set_style_border_width(tile, 2, 0);
  lift(tile);
  const uint32_t ink = out ? pal().textTertiary : pal().text;
  const bool tall = h >= 90;
  const int16_t side = tall ? 36 : 28;
  seatToken(tile, p, 10, tall ? 10 : (h - side) / 2, side, glow);
  const int16_t nameX = 10 + side + 8;
  lv_obj_t *name = text(tile, p.name, &th_semi_20, ink, nameX, tall ? 14 : 6, w - nameX - 10);
  if (out) lv_obj_set_style_text_decor(name, LV_TEXT_DECOR_STRIKETHROUGH, 0);
  const char *tag = chipTag(p);
  // The pill and turn time sit under the name (short tiles) or at the foot.
  lv_obj_t *foot = box(tile, tall ? 10 : nameX, tall ? h - 30 : h - 26, LV_SIZE_CONTENT, 22);
  lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(foot, 8, 0);
  if (tag[0]) {
    const bool waiting = p.flags & CHIP_WAITING;
    pill(foot, tag, glow ? pal().turn : waiting ? pal().accent : pal().surface3,
         glow ? pal().onTurn : waiting ? pal().onAccent : pal().textSecondary);
  }
  if (p.turnTime[0] && (tall || !tag[0])) {
    lv_obj_t *t = lv_label_create(foot);
    lv_obj_remove_style_all(t);
    lv_label_set_text(t, p.turnTime);
    lv_obj_set_style_text_font(t, &th_semi_16, 0);
    lv_obj_set_style_text_color(t, rgb(pal().textSecondary), 0);
  }
  if (!showLife) return;
  char life[12];
  snprintf(life, sizeof(life), "%ld", static_cast<long>(p.life));
  const lv_font_t *font = tall && h >= 120 ? &th_num_64 : &th_num_40;
  const uint32_t lifeInk = out ? pal().textTertiary : (active ? pal().turn : pal().text);
  const int16_t lh = lv_font_get_line_height(font);
  text(tile, life, font, lifeInk, 8, tall ? (h - lh) / 2 + 4 : (h - lh) / 2, w - 18, LV_TEXT_ALIGN_RIGHT);
}

// Body lines in a column: each wraps to the width and pushes the next down.
void drawLines(lv_obj_t *parent, const AtlasScreen &s, int16_t x, int16_t y, int16_t w) {
  if (s.lineCount == 0) return;
  lv_obj_t *col = box(parent, x, y, w, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 6, 0);
  for (uint8_t i = 0; i < s.lineCount; ++i) {
    const bool warning = strstr(s.lines[i], "NOT INSERTED") != nullptr;
    lv_obj_t *l = lv_label_create(col);
    lv_obj_remove_style_all(l);
    lv_label_set_text(l, s.lines[i]);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_font(l, &th_semi_16, 0);
    lv_obj_set_style_text_line_space(l, 2, 0);
    lv_obj_set_style_text_color(l, rgb(warning ? pal().critical : pal().text), 0);
  }
}

void drawQr(lv_obj_t *root, const char *data, int16_t x, int16_t y, int16_t size) {
  lv_obj_t *frame = box(root, x, y, size, size);
  lv_obj_set_style_radius(frame, 12, 0);
  lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
  // Always dark on white, whatever the theme, so phones can scan it.
  lv_obj_set_style_bg_color(frame, lv_color_white(), 0);
  lift(frame);
  lv_obj_t *qr = lv_qrcode_create(frame);
  lv_qrcode_set_size(qr, size - 20);
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
      y += 76;
    }
    if (s.qr[0]) {
      int16_t size = BUTTON_ROW_Y - 12 - y - 26 - s.lineCount * 24;
      if (size > W - 80) size = W - 80;
      drawQr(root, s.qr, (W - size) / 2, y, size);
      y += size + 6;
      text(root, s.qrCaption, &th_semi_16, pal().textSecondary, 12, y, W - 24, LV_TEXT_ALIGN_CENTER);
      y += 26;
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
  if (s.playerCount > 0) return;
  const int16_t bodyH = BUTTON_ROW_Y - 8 - SCREEN_BODY_Y;
  if (s.lineCount == 0 && !s.qr[0]) return;
  // A card holding the lines, and any QR code centred under them.
  lv_obj_t *plate = box(root, 8, SCREEN_BODY_Y, W - 16, s.qr[0] ? bodyH : LV_SIZE_CONTENT);
  card(plate, pal().surface1, pal().separator);
  lv_obj_set_style_pad_all(plate, 14, 0);
  lv_obj_set_style_pad_bottom(plate, 16, 0);
  drawLines(plate, s, 0, 0, W - 16 - 28);
  if (!s.qr[0]) return;
  const int16_t top = 16 + s.lineCount * 24;
  int16_t size = bodyH - 28 - top;
  if (size > W - 96) size = W - 96;
  if (size >= 96) drawQr(plate, s.qr, (W - 16 - 28 - size) / 2, top, size);
}

void drawButton(lv_obj_t *root, const AtlasScreen &s, const TouchButton &b) {
  const bool pressed = s.pressed == b.action;
  const bool primary = primaryAction(b.action);
  const bool danger = dangerAction(b.action);
  lv_obj_t *o = box(root, b.x, b.y, b.w, b.h);
  const uint32_t base = pressed ? (primary ? pal().accentPressed : pal().fillStrong)
      : primary ? pal().accent : pal().surface3;
  card(o, base, b.selected || pressed ? pal().focus : danger ? pal().critical : pal().separatorStrong, 14);
  if (!primary || pressed) lv_obj_set_style_border_width(o, 1, 0); else lv_obj_set_style_border_width(o, 0, 0);
  if (!pressed) {
    sheen(o, base);
    lift(o);
  }
  // Pressed and the shown choice are also marked by a heavier frame.
  if (pressed || b.selected) lv_obj_set_style_border_width(o, 3, 0);
  const uint32_t ink = primary ? pal().onAccent : danger ? pal().critical : pal().text;
  char label[32];
  if (pressed && b.hold() && s.holdSecondsLeft > 0) {
    snprintf(label, sizeof(label), "Hold %u s", static_cast<unsigned>(s.holdSecondsLeft));
  } else {
    snprintf(label, sizeof(label), "%s", b.label);
  }
  const char *caption = b.selected && !pressed ? "shown" : (b.hold() && !pressed ? "hold" : "");
  // The largest type that fits on one line; two lines of the small one otherwise.
  const int16_t room = b.w - 16;
  const lv_font_t *font = textWidth(label, &th_semi_20) <= room ? &th_semi_20 : &th_semi_16;
  lv_obj_t *col = box(o, 0, 0, b.w, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *l = lv_label_create(col);
  lv_obj_remove_style_all(l);
  lv_label_set_text(l, label);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, rgb(ink), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  if (textWidth(label, font) > room) {
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, room);
  }
  if (caption[0]) {
    lv_obj_t *c = lv_label_create(col);
    lv_obj_remove_style_all(c);
    lv_label_set_text(c, caption);
    lv_obj_set_style_text_font(c, &th_eyebrow_13, 0);
    lv_obj_set_style_text_color(c, rgb(primary ? pal().onAccent : pal().textSecondary), 0);
  }
  lv_obj_align(col, LV_ALIGN_CENTER, 0, 0);
  if (pressed && b.hold()) {
    lv_obj_t *track = box(o, 12, b.h - 12, b.w - 24, 6);
    lv_obj_set_style_radius(track, 3, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(track, lv_color_black(), 0);
    lv_obj_t *fill = box(track, 0, 0, (b.w - 24) * s.holdPermille / 1000, 6);
    lv_obj_set_style_radius(fill, 3, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, rgb(pal().focus), 0);
  }
}

void render(const AtlasScreen &s) {
  lv_obj_t *root = lv_screen_active();
  lv_obj_clean(root);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(root, rgb(pal().bg), 0);
  lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_NONE, 0);
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
  lv_obj_set_style_bg_color(root, rgb(pal().bg), 0);
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
