#pragma once
#include "Arduino.h"
#include "Wire.h"
#include <cassert>
#include <vector>

#define SH110X_BLACK 0
#define SH110X_WHITE 1

// Records the real renderer's output, asserts all character cells fit, and
// simulates initialization failures. It does not simulate electrical signals.
struct DrawnLine {
  int x, y, size, color;
  std::string text;
};
struct PanelTrace {
  int constructors = 0, begins = 0, frames = 0, shapes = 0;
  int mosi = -1, sclk = -1, dc = -1, reset = -2, cs = -1;
  int address = -1, rotation = -1;
  uint32_t clockDuring = 0, clockAfter = 0;
  bool spi = false, resetRequested = false, beginSucceeds = true;
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
  void setTextWrap(bool wrap) { assert(!wrap); }
  void setTextColor(int color) { color_ = color; }
  void setTextSize(int size) { size_ = size; }
  void setCursor(int x, int y) { panel.lines.push_back({x, y, size_, color_, {}}); }
  void print(char c) {
    auto &line = panel.lines.back();
    line.text += c;
    assert(line.x >= 0 && line.x + int(line.text.size()) * 6 * line.size <= w_);
    assert(line.y >= 0 && line.y + 8 * line.size <= h_);
  }
  // Every primitive must stay fully on the panel.
  void fillRect(int x, int y, int w, int h, int color) { box(x, y, w, h, color); }
  void fillRoundRect(int x, int y, int w, int h, int, int color) { box(x, y, w, h, color); }
  void drawRoundRect(int x, int y, int w, int h, int, int color) { box(x, y, w, h, color); }
  void drawFastHLine(int x, int y, int w, int color) { box(x, y, w, 1, color); }
  void drawPixel(int x, int y, int color) { box(x, y, 1, 1, color); }
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
        const auto &a = panel.lines[i], &b = panel.lines[j];
        const int aw = int(a.text.size()) * 6 * a.size, bw = int(b.text.size()) * 6 * b.size;
        assert(a.x + aw <= b.x || b.x + bw <= a.x ||
            a.y + 8 * a.size <= b.y || b.y + 8 * b.size <= a.y);
      }
    }
    ++panel.frames;
  }
  int16_t width() const { return w_; }
  int16_t height() const { return h_; }
 private:
  int16_t w_, h_;
  int size_ = 1, color_ = SH110X_WHITE;
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
