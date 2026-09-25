#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "storage.h"

namespace TurnHubStorage {

// File operations for bounded append-only diagnostics, separate from BlobStore.
// The firmware adapter flushes and closes every append. One task owns this API.
class DiagnosticFileSystem {
 public:
  virtual ~DiagnosticFileSystem() = default;
  virtual bool exists(const char *path) = 0;
  virtual bool fileSize(const char *path, size_t &size) = 0;
  virtual bool appendFile(const char *path, const void *data, size_t size) = 0;
  virtual bool rename(const char *from, const char *to) = 0;
  virtual bool remove(const char *path) = 0;
};

inline bool sdStorageReady(Status store, Status selfTest) {
  return store == Status::Ok && selfTest == Status::Ok;
}

// Disposable text: current + three archives, never an authoritative database.
// Failure stops writes until begin() is called again. Rotation is not atomic
// on FAT; a power failure may lose log data or damage filesystem metadata.
class DiagnosticLog {
 public:
  static constexpr size_t FILE_LIMIT = 256 * 1024;
  static constexpr unsigned ARCHIVES = 3;

  bool begin(DiagnosticFileSystem &fs, size_t fileLimit = FILE_LIMIT) {
    fs_ = &fs;
    ready_ = false;
    limit_ = fileLimit;
    size_ = 0;
    if (!limit_) return false;
    if (fs_->exists(current()) && !fs_->fileSize(current(), size_)) return false;
    // Preserve unexpected/oversized existing data; do not grow or archive it.
    if (size_ > limit_) return false;
    ready_ = true;
    return true;
  }

  bool append(const void *data, size_t size) {
    if (!ready_ || (size && !data) || size > limit_) return false;
    if (!size) return true;
    if (size > limit_ - size_ && !rotate()) return false;
    if (!fs_->appendFile(current(), data, size)) return ready_ = false;
    size_ += size;
    return true;
  }

  bool ready() const { return ready_; }
  static const char *current() { return "/turnhub/diagnostics.log"; }

 private:
  bool rotate() {
    char from[48], to[48];
    snprintf(to, sizeof(to), "%s.%u", current(), ARCHIVES);
    if (fs_->exists(to) && !fs_->remove(to)) return ready_ = false;
    for (unsigned n = ARCHIVES; n > 1; --n) {
      snprintf(from, sizeof(from), "%s.%u", current(), n - 1);
      snprintf(to, sizeof(to), "%s.%u", current(), n);
      if (fs_->exists(from) && !fs_->rename(from, to)) return ready_ = false;
    }
    snprintf(to, sizeof(to), "%s.1", current());
    if (fs_->exists(current()) && !fs_->rename(current(), to)) return ready_ = false;
    size_ = 0;
    return true;
  }

  DiagnosticFileSystem *fs_ = nullptr;
  size_t limit_ = FILE_LIMIT;
  size_t size_ = 0;
  bool ready_ = false;
};

}  // namespace TurnHubStorage
