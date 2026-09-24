#pragma once
#include <stdint.h>
#include <string.h>

namespace TurnHub {

// Bounded, per-profile throttling shared by every PIN entry point. A full table
// fails closed until a window expires, rather than evicting a locked profile.
class LoginLimiter {
 public:
  static constexpr uint8_t MAX_ATTEMPTS = 5;
  static constexpr uint32_t WINDOW_MS = 30000;  // Matches the "wait 30 seconds" message.
  static constexpr uint8_t CAPACITY = 64;
  static constexpr size_t ID_LENGTH = 8;

  // Records an attempt; false when the profile is locked out or untracked.
  bool allow(const char *id, uint32_t now) {
    Entry *entry = find(id, now);
    if (!entry || entry->attempts >= MAX_ATTEMPTS) return false;
    ++entry->attempts;
    return true;
  }

  void success(const char *id) {
    for (auto &entry : entries_) {
      if (strcmp(entry.id, id) == 0) entry = Entry{};
    }
  }

 private:
  struct Entry {
    char id[ID_LENGTH + 1] = {};
    uint32_t started = 0;
    uint8_t attempts = 0;
  };
  Entry entries_[CAPACITY] = {};

  Entry *find(const char *id, uint32_t now) {
    if (!id || strlen(id) != ID_LENGTH) return nullptr;
    for (auto &entry : entries_) {
      if (entry.id[0] && now - entry.started >= WINDOW_MS) entry = Entry{};
      if (strcmp(entry.id, id) == 0) return &entry;
    }
    for (auto &entry : entries_) {
      if (entry.id[0]) continue;
      memcpy(entry.id, id, ID_LENGTH + 1);
      entry.started = now;
      return &entry;
    }
    return nullptr;
  }
};

}  // namespace TurnHub
