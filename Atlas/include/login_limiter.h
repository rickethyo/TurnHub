#pragma once
#include <stdint.h>
#include <string.h>

namespace TurnHub {
// Bounded, per-profile throttling shared by every PIN entry point. A full table
// fails closed until a window expires, rather than evicting a locked profile.
class LoginLimiter {
 public:
  bool allow(const char *id, uint32_t now) {
    auto *entry = find(id, now);
    if (!entry || entry->attempts >= 5) return false;
    ++entry->attempts;
    return true;
  }
  void success(const char *id) {
    for (auto &entry : entries_) if (strcmp(entry.id, id) == 0) entry = Entry{};
  }
 private:
  struct Entry { char id[9] = {}; uint32_t started = 0; uint8_t attempts = 0; };
  Entry entries_[64] = {};
  Entry *find(const char *id, uint32_t now) {
    if (!id || strlen(id) != 8) return nullptr;
    for (auto &entry : entries_) {
      if (entry.id[0] && now - entry.started >= 30000) entry = Entry{};
      if (strcmp(entry.id, id) == 0) return &entry;
    }
    for (auto &entry : entries_) if (!entry.id[0]) {
      memcpy(entry.id, id, 9); entry.started = now; return &entry;
    }
    return nullptr;
  }
};
}  // namespace TurnHub
