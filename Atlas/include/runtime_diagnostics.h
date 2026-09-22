#pragma once
#include <Arduino.h>
#include <string.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace TurnHub {
namespace Diagnostics {
constexpr uint8_t ACTIVITY_CAPACITY = 40;
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

inline String jsonEscape(const char *text) {
  String escaped;
  if (text == nullptr) return escaped;
  for (const char *p = text; *p; ++p) {
    switch (*p) {
      case '\\': escaped += "\\\\"; break;
      case '"': escaped += "\\\""; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      default: escaped += *p; break;
    }
  }
  return escaped;
}

inline String activityJson() {
  const ActivityLog &log = activityLog();
  String json = "{\"events\":[";
  json.reserve(5200);
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
inline String runtimeDiagnosticsJson() {
  uint32_t freeHeap=0, minimumHeap=0, largestBlock=0, stackFree=0;
#if defined(ARDUINO_ARCH_ESP32)
  freeHeap = esp_get_free_heap_size();
  minimumHeap = esp_get_minimum_free_heap_size();
  largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  stackFree = uxTaskGetStackHighWaterMark(nullptr);
#endif
  return String("{\"uptimeMs\":")+String(millis())+
      ",\"resetReason\":\""+resetReason()+"\",\"freeHeap\":"+String(freeHeap)+
      ",\"minimumFreeHeap\":"+String(minimumHeap)+",\"largestFreeBlock\":"+String(largestBlock)+
      ",\"loopStackFreeBytes\":"+String(stackFree)+"}";
}
} // namespace TurnHub
