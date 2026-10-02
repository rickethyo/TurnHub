#pragma once
#include "Arduino.h"
#include "Wire.h"
#include "gfxfont.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <vector>

#define SH110X_BLACK 0
#define SH110X_WHITE 1
#define SH110X_DISPLAYOFF 0xAE

// Records the real renderer's output, asserts all character cells (or, for
// Adafruit GFX custom fonts, every glyph's ink box) fit, and simulates
// initialization failures. It does not simulate electrical signals.
struct DrawnLine {
  int x, y, size, color;
  std::string text;
  const GFXfont *font = nullptr;  // nullptr: the built-in 6x8 font.
  // Custom fonts: the ink box so far (x0, y0 inclusive; x1, y1 exclusive).
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool inked = false;
  int cursor = 0;
  // The rectangle the run covers, for the overlap check.
  void box(int &l, int &t, int &r, int &b) const {
    if (font) { l = x0; t = y0; r = x1; b = y1; return; }
    l = x; t = y; r = x + int(text.size()) * 6 * size; b = y + 8 * size;
  }
};
struct PanelTrace {
  int constructors = 0, begins = 0, frames = 0, shapes = 0;
  int mosi = -1, sclk = -1, dc = -1, reset = -2, cs = -1;
  int address = -1, rotation = -1;
  uint32_t clockDuring = 0, clockAfter = 0;
  bool spi = false, resetRequested = false, beginSucceeds = true;
  int lastCommand = -1;  // The last oled_command (SH110X_DISPLAYOFF on sleep).
  std::vector<DrawnLine> lines;
};
extern PanelTrace panel;

class Adafruit_SH1106G {
 public:
  Adafruit_SH1106G(uint16_t w, uint16_t h, TwoWire *, int16_t reset,
      uint32_t before, uint32_t after) : w_(w), h_(h) {
    ++panel.constructors; panel.spi = false; panel.reset = reset;
    panel.clockDuring = before; panel.clockAfter = after;
  }
  Adafruit_SH1106G(uint16_t w, uint16_t h, int16_t mosi, int16_t sclk,
      int16_t dc, int16_t reset, int16_t cs) : w_(w), h_(h) {
    ++panel.constructors; panel.spi = true;
    panel.mosi = mosi; panel.sclk = sclk; panel.dc = dc;
    panel.reset = reset; panel.cs = cs;
  }
  bool begin(uint8_t address, bool reset) {
    ++panel.begins; panel.address = address; panel.resetRequested = reset;
    return panel.beginSucceeds;
  }
  void setRotation(int r) { panel.rotation = r; }
  void oled_command(uint8_t command) { panel.lastCommand = command; }
  void setTextWrap(bool wrap) { assert(!wrap); }
  void setTextColor(int color) { color_ = color; }
  void setTextSize(int size) { size_ = size; }
  void setFont(const GFXfont *font) { font_ = font; }
  void setCursor(int x, int y) {
    DrawnLine line{x, y, size_, color_, {}};
    line.font = font_;
    line.cursor = x;
    panel.lines.push_back(line);
  }
  void print(char c) {
    auto &line = panel.lines.back();
    line.text += c;
    if (!line.font) {
      assert(line.x >= 0 && line.x + int(line.text.size()) * 6 * line.size <= w_);
      assert(line.y >= 0 && line.y + 8 * line.size <= h_);
      return;
    }
    // Custom font: the cursor is the baseline; each glyph's ink must be on
    // the panel.
    const uint8_t code = static_cast<uint8_t>(c);
    assert(code >= line.font->first && code <= line.font->last);
    const GFXglyph &g = line.font->glyph[code - line.font->first];
    if (g.width && g.height) {
      const int gx = line.cursor + g.xOffset, gy = line.y + g.yOffset;
      assert(gx >= 0 && gy >= 0 && gx + g.width <= w_ && gy + g.height <= h_);
      if (!line.inked) { line.x0 = gx; line.y0 = gy; line.x1 = gx + g.width; line.y1 = gy + g.height; }
      line.x0 = std::min(line.x0, gx); line.y0 = std::min(line.y0, gy);
      line.x1 = std::max(line.x1, gx + g.width); line.y1 = std::max(line.y1, gy + g.height);
      line.inked = true;
    }
    line.cursor += g.xAdvance;
  }
  // Adafruit GFX's measurement: the ink box of the text drawn from (x, y).
  void getTextBounds(const char *str, int16_t x, int16_t y, int16_t *x1, int16_t *y1,
      uint16_t *w, uint16_t *h) {
    if (!font_) {
      *x1 = x; *y1 = y; *w = static_cast<uint16_t>(strlen(str) * 6 * size_); *h = static_cast<uint16_t>(8 * size_);
      return;
    }
    int minX = 32767, minY = 32767, maxX = -32768, maxY = -32768, cursor = x;
    for (const char *p = str; *p; ++p) {
      const uint8_t code = static_cast<uint8_t>(*p);
      if (code < font_->first || code > font_->last) continue;
      const GFXglyph &g = font_->glyph[code - font_->first];
      if (g.width && g.height) {
        minX = std::min(minX, cursor + g.xOffset); minY = std::min(minY, y + g.yOffset);
        maxX = std::max(maxX, cursor + g.xOffset + g.width - 1);
        maxY = std::max(maxY, y + g.yOffset + g.height - 1);
      }
      cursor += g.xAdvance;
    }
    if (maxX < minX) { *x1 = x; *y1 = y; *w = 0; *h = 0; return; }
    *x1 = static_cast<int16_t>(minX); *y1 = static_cast<int16_t>(minY);
    *w = static_cast<uint16_t>(maxX - minX + 1); *h = static_cast<uint16_t>(maxY - minY + 1);
  }
  // Every primitive must stay fully on the panel.
  // A black fill erases the text runs it covers completely (the OLED legend
  // redraws its row in place).
  void fillRect(int x, int y, int w, int h, int color) {
    box(x, y, w, h, color);
    if (color != SH110X_BLACK) return;
    panel.lines.erase(std::remove_if(panel.lines.begin(), panel.lines.end(), [&](const DrawnLine &line) {
      int l, t, r, b;
      line.box(l, t, r, b);
      return l >= x && t >= y && r <= x + w && b <= y + h;
    }), panel.lines.end());
  }
  void fillRoundRect(int x, int y, int w, int h, int, int color) { box(x, y, w, h, color); }
  void drawRoundRect(int x, int y, int w, int h, int, int color) { box(x, y, w, h, color); }
  void drawFastHLine(int x, int y, int w, int color) { box(x, y, w, 1, color); }
  void drawPixel(int x, int y, int color) { box(x, y, 1, 1, color); }
  void drawLine(int x0, int y0, int x1, int y1, int color) {
    box(std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0) + 1, std::abs(y1 - y0) + 1, color);
  }
  void fillCircle(int x, int y, int r, int color) { box(x - r, y - r, 2 * r + 1, 2 * r + 1, color); }
  void drawCircle(int x, int y, int r, int color) { box(x - r, y - r, 2 * r + 1, 2 * r + 1, color); }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, int color) {
    triangle(x0, y0, x1, y1, x2, y2, color);
  }
  void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, int color) {
    triangle(x0, y0, x1, y1, x2, y2, color);
  }
  void clearDisplay() { panel.lines.clear(); panel.shapes = 0; }
  void display() {
    // Text runs must never overlap each other, even when a number grows.
    for (size_t i = 0; i < panel.lines.size(); ++i) {
      for (size_t j = 0; j < i; ++j) {
        int al, at, ar, ab, bl, bt, br, bb;
        panel.lines[i].box(al, at, ar, ab);
        panel.lines[j].box(bl, bt, br, bb);
        assert(ar <= bl || br <= al || ab <= bt || bb <= at);
      }
    }
    ++panel.frames;
  }
  int16_t width() const { return w_; }
  int16_t height() const { return h_; }
 private:
  int16_t w_, h_;
  int size_ = 1, color_ = SH110X_WHITE;
  const GFXfont *font_ = nullptr;
  void box(int x, int y, int w, int h, int color) {
    assert(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= w_ && y + h <= h_);
    assert(color == SH110X_WHITE || color == SH110X_BLACK); ++panel.shapes;
  }
  void triangle(int x0, int y0, int x1, int y1, int x2, int y2, int color) {
    for (int x : {x0, x1, x2}) assert(x >= 0 && x < w_);
    for (int y : {y0, y1, y2}) assert(y >= 0 && y < h_);
    assert(color == SH110X_WHITE || color == SH110X_BLACK); ++panel.shapes;
  }
};
