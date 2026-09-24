#include "serial_log.h"

#include <stdlib.h>
#include <string.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#endif

namespace TurnHub {

SerialLog serialLog;

namespace {
#if defined(ARDUINO_ARCH_ESP32)
// ESP-NOW callbacks and the TX task log from other FreeRTOS tasks, so ring
// updates are short critical sections. Nothing allocates or calls printf
// inside them.
portMUX_TYPE ringLock = portMUX_INITIALIZER_UNLOCKED;
#define SERIAL_LOG_LOCK() portENTER_CRITICAL(&ringLock)
#define SERIAL_LOG_UNLOCK() portEXIT_CRITICAL(&ringLock)
#else
#define SERIAL_LOG_LOCK()
#define SERIAL_LOG_UNLOCK()
#endif
}  // namespace

size_t SerialLog::write(uint8_t byte) {
  return write(&byte, 1);
}

size_t SerialLog::write(const uint8_t *data, size_t size) {
  if (data == nullptr || size == 0) return 0;
  Serial.write(data, size);
  const uint32_t now = millis();
  SERIAL_LOG_LOCK();
  capture(data, size, now);
  SERIAL_LOG_UNLOCK();
  return size;
}

void SerialLog::printlnRedacted(const char *serialText, const char *capturedText) {
  Serial.println(serialText ? serialText : "");
  const char *captured = capturedText ? capturedText : "";
  const uint32_t now = millis();
  SERIAL_LOG_LOCK();
  capture(reinterpret_cast<const uint8_t *>(captured), strlen(captured), now);
  captureByte('\n', now);
  SERIAL_LOG_UNLOCK();
}

void SerialLog::capture(const uint8_t *data, size_t size, uint32_t nowMs) {
  for (size_t i = 0; i < size; ++i) captureByte(static_cast<char>(data[i]), nowMs);
}

void SerialLog::captureByte(char byte, uint32_t nowMs) {
  if (byte == '\r') return;  // Serial uses CRLF; the file keeps plain LF lines.
  char stamp[STAMP_LENGTH];
  size_t stampLength = 0;
  if (lineStart_) {
    stampLength = formatStamp(nowMs, stamp);
    lineStart_ = false;
  }
  for (size_t i = 0; i <= stampLength; ++i) {
    const char next = i < stampLength ? stamp[i] : byte;
    if (wrapped_) ++dropped_;
    ring_[head_] = next;
    if (++head_ == CAPACITY) {
      head_ = 0;
      wrapped_ = true;
    }
  }
  if (byte == '\n') lineStart_ = true;
}

// "[SSSSSSS.mmm] " by hand: this runs inside the critical section, where
// newlib's printf family must not be called.
size_t SerialLog::formatStamp(uint32_t nowMs, char *out) {
  uint32_t seconds = nowMs / 1000;
  uint32_t millisPart = nowMs % 1000;
  char digits[10];
  size_t count = 0;
  do {
    digits[count++] = static_cast<char>('0' + seconds % 10);
    seconds /= 10;
  } while (seconds != 0 && count < sizeof(digits));
  size_t length = 0;
  out[length++] = '[';
  for (size_t pad = count; pad < 7; ++pad) out[length++] = ' ';
  while (count > 0) out[length++] = digits[--count];
  out[length++] = '.';
  out[length++] = static_cast<char>('0' + millisPart / 100);
  out[length++] = static_cast<char>('0' + millisPart / 10 % 10);
  out[length++] = static_cast<char>('0' + millisPart % 10);
  out[length++] = ']';
  out[length++] = ' ';
  return length;
}

String SerialLog::snapshot() const {
  // Copy under the lock, format outside it.
  char *copy = static_cast<char *>(malloc(CAPACITY));
  if (copy == nullptr) return String();
  size_t length;
  SERIAL_LOG_LOCK();
  if (wrapped_) {
    const size_t tail = CAPACITY - head_;
    memcpy(copy, ring_ + head_, tail);
    memcpy(copy + tail, ring_, head_);
    length = CAPACITY;
  } else {
    memcpy(copy, ring_, head_);
    length = head_;
  }
  const bool wrapped = wrapped_;
  SERIAL_LOG_UNLOCK();

  size_t start = 0;
  if (wrapped) {
    // The oldest line was partly overwritten; start at the next whole line.
    while (start < length && copy[start] != '\n') ++start;
    if (start < length) ++start;
  }
  String out;
  out.reserve(length - start + 1);
  for (size_t i = start; i < length; ++i) out += copy[i];
  free(copy);
  return out;
}

uint32_t SerialLog::droppedBytes() const {
  SERIAL_LOG_LOCK();
  const uint32_t dropped = dropped_;
  SERIAL_LOG_UNLOCK();
  return dropped;
}

void SerialLog::clear() {
  SERIAL_LOG_LOCK();
  head_ = 0;
  wrapped_ = false;
  lineStart_ = true;
  dropped_ = 0;
  SERIAL_LOG_UNLOCK();
}

}  // namespace TurnHub
