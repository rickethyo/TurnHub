#pragma once
#include <Arduino.h>
#include <string.h>
#include "json_text.h"
#include "config.h"
#include "serial_log.h"
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace TurnHub {
namespace Diagnostics {
// The newest events only (40 before 2026-09-30); the serial log and its SD
// copy keep the full history.
constexpr uint8_t ACTIVITY_CAPACITY = 16;
constexpr size_t ACTIVITY_KIND_LENGTH = 16;
constexpr size_t ACTIVITY_MESSAGE_LENGTH = 96;

struct ActivityEvent {
  uint32_t atMs = 0;
  char kind[ACTIVITY_KIND_LENGTH] = {};
  char message[ACTIVITY_MESSAGE_LENGTH] = {};
};

struct ActivityLog {
  ActivityEvent entries[ACTIVITY_CAPACITY] = {};
  uint8_t next = 0;
  uint8_t count = 0;
};

inline ActivityLog &activityLog() {
  static ActivityLog log;
  return log;
}

inline void recordActivity(const char *kind, const String &message) {
  ActivityLog &log = activityLog();
  ActivityEvent &entry = log.entries[log.next];
  entry.atMs = millis();
  strncpy(entry.kind, kind ? kind : "event", ACTIVITY_KIND_LENGTH - 1);
  entry.kind[ACTIVITY_KIND_LENGTH - 1] = '\0';
  strncpy(entry.message, message.c_str(), ACTIVITY_MESSAGE_LENGTH - 1);
  entry.message[ACTIVITY_MESSAGE_LENGTH - 1] = '\0';
  log.next = static_cast<uint8_t>((log.next + 1) % ACTIVITY_CAPACITY);
  if (log.count < ACTIVITY_CAPACITY) ++log.count;
}

inline String activityJson() {
  const ActivityLog &log = activityLog();
  String json = "{\"events\":[";
  json.reserve(ACTIVITY_CAPACITY * 170);
  const uint8_t count = log.count;
  const uint8_t newest = log.next == 0 ? ACTIVITY_CAPACITY - 1 : log.next - 1;
  for (uint8_t offset = 0; offset < count; ++offset) {
    const uint8_t index = static_cast<uint8_t>(
        (newest + ACTIVITY_CAPACITY - offset) % ACTIVITY_CAPACITY);
    const ActivityEvent &entry = log.entries[index];
    if (offset) json += ',';
    json += "{\"atMs\":";
    json += String(entry.atMs);
    json += ",\"ageMs\":";
    json += String(millis() - entry.atMs);
    json += ",\"kind\":\"";
    json += jsonEscape(entry.kind);
    json += "\",\"message\":\"";
    json += jsonEscape(entry.message);
    json += "\"}";
  }
  json += "]}";
  return json;
}
}  // namespace Diagnostics

using Diagnostics::activityJson;
using Diagnostics::recordActivity;

inline const char *resetReason() {
#if defined(ARDUINO_ARCH_ESP32)
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_EXT: return "external";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    default: return "unknown";
  }
#else
  return "host_test";
#endif
}
// ESP-IDF's FreeRTOS returns BYTES, unlike upstream FreeRTOS's words.
// This is the historical minimum unused stack, never current free stack.
inline uint32_t taskStackMinimumFreeBytes() {
#if defined(ARDUINO_ARCH_ESP32)
  return uxTaskGetStackHighWaterMark(nullptr);
#else
  return 0;  // Not measured by the host runner.
#endif
}

inline size_t loopStackSizeBytes() {
#if defined(ARDUINO_ARCH_ESP32)
  return getArduinoLoopTaskStackSize();
#else
  return AtlasConfig::LOOP_TASK_STACK_BYTES;
#endif
}

struct LowStackWarning {
  bool reported = false;
  uint32_t lastMs = 0;
  bool due(uint32_t minimumBytes, uint32_t nowMs) {
    if (minimumBytes >= AtlasConfig::LOOP_STACK_WARNING_BYTES ||
        (reported && nowMs - lastMs < 30000)) return false;
    reported = true;
    lastMs = nowMs;
    return true;
  }
};

// Called only from loopTask (HTTP completion and periodic health sampling).
inline void warnLowLoopStack(uint32_t minimumBytes, uint32_t nowMs) {
#if defined(ARDUINO_ARCH_ESP32)
  static LowStackWarning warning;
  if (!warning.due(minimumBytes, nowMs)) return;
  serialLog.print("ATLAS|STACK|LOW|minimumFreeBytes=");
  serialLog.print(minimumBytes);
  serialLog.print("|targetBytes=");
  serialLog.println(AtlasConfig::LOOP_STACK_WARNING_BYTES);
#else
  (void)minimumBytes;
  (void)nowMs;
#endif
}

inline String runtimeDiagnosticsJson() {
  uint32_t freeHeap=0, minimumHeap=0, largestBlock=0;
#if defined(ARDUINO_ARCH_ESP32)
  freeHeap = esp_get_free_heap_size();
  minimumHeap = esp_get_minimum_free_heap_size();
  largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#endif
  return String("{\"uptimeMs\":")+String(millis())+
      ",\"resetReason\":\""+resetReason()+"\",\"freeHeap\":"+String(freeHeap)+
      ",\"minimumFreeHeap\":"+String(minimumHeap)+",\"largestFreeBlock\":"+String(largestBlock)+
      ",\"loopStackSizeBytes\":"+String(loopStackSizeBytes())+
      ",\"loopStackMinimumFreeBytes\":"+String(taskStackMinimumFreeBytes())+"}";
}
} // namespace TurnHub
