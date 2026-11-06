#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Application-task presentation only. No credentials, card data or game authority.
namespace TurnHubActivity {
constexpr uint8_t CAPACITY = 12;
enum class Level : uint8_t { Info, Success, Warning, Error };
struct Event {
  uint32_t id = 0, atMs = 0;
  Level level = Level::Info;
  char title[32] = {}, detail[80] = {};
};
struct Journal {
  Event entries[CAPACITY]{};
  uint8_t count = 0;
  uint32_t sequence = 0, seen = 0;
};
inline Journal &journal() { static Journal value; return value; }
inline const char *levelName(Level level) {
  return level == Level::Success ? "OK" : level == Level::Warning ? "Warning" : level == Level::Error ? "Error" : "Info";
}
inline void add(Level level, const char *title, const char *detail, uint32_t nowMs) {
  Event event; event.level = level; event.atMs = nowMs;
  snprintf(event.title, sizeof(event.title), "%s", title ? title : "");
  snprintf(event.detail, sizeof(event.detail), "%s", detail ? detail : "");
  if (!event.title[0]) return;
  auto &j = journal();
  // Retries with the same outcome do not flood the shelf.
  if (j.count && j.entries[0].level == level && !strcmp(j.entries[0].title,event.title) &&
      !strcmp(j.entries[0].detail,event.detail) && nowMs-j.entries[0].atMs < 30000) return;
  for (uint8_t i = j.count < CAPACITY ? j.count : CAPACITY-1; i > 0; --i) j.entries[i] = j.entries[i-1];
  if (j.count < CAPACITY) ++j.count;
  event.id = ++j.sequence; j.entries[0] = event;
}
inline uint8_t unread() {
  auto &j=journal(); uint8_t count=0;
  for(uint8_t i=0;i<j.count;++i) if(j.entries[i].id>j.seen) ++count;
  return count;
}
inline void markSeen() { journal().seen=journal().sequence; }
inline void clear() { auto &j=journal(); j.count=0; j.seen=++j.sequence; }
}  // namespace TurnHubActivity
