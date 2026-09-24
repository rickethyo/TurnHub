#pragma once
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace TurnHub {

// Atlas's serial log output. Everything written here goes to the USB serial
// port unchanged and is also kept, with an uptime stamp per line, in a
// fixed-size RAM ring so a connected browser can download recent output
// without a USB cable (GET /api/diagnostics/log).
//
// RAM-only by design: the ring clears on reboot and is never written to flash.
// When it fills, the oldest lines are dropped. Framework log_e()/ESP-IDF
// messages bypass Print and are not captured.
class SerialLog : public Print {
 public:
  static constexpr size_t CAPACITY = 16384;

  size_t write(uint8_t byte) override;
  size_t write(const uint8_t *data, size_t size) override;
  using Print::write;

  // Serial output that must not be downloadable (e.g. the Wi-Fi password):
  // `serialText` goes to the port only and `capturedText` replaces it in the
  // ring. Both are written as one complete line.
  void printlnRedacted(const char *serialText, const char *capturedText);

  // Captured text, oldest first, starting at a whole line.
  String snapshot() const;
  // Bytes that no longer fit and were dropped since boot.
  uint32_t droppedBytes() const;
  void clear();

 private:
  // "[" + up to 10 digits + ".mmm] "
  static constexpr size_t STAMP_LENGTH = 18;
  static size_t formatStamp(uint32_t nowMs, char *out);
  void capture(const uint8_t *data, size_t size, uint32_t nowMs);
  void captureByte(char byte, uint32_t nowMs);

  char ring_[CAPACITY] = {};
  size_t head_ = 0;          // Next write position.
  bool wrapped_ = false;
  bool lineStart_ = true;
  uint32_t dropped_ = 0;
};

extern SerialLog serialLog;

}  // namespace TurnHub
