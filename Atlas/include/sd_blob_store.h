#pragma once

#include <stddef.h>
#include <stdint.h>
#include "storage.h"

namespace TurnHubStorage {

// The few file operations SdBlobStore needs. Firmware wraps the Arduino SD
// library (sd_card.cpp); host tests use an in-memory fake with fault injection.
class FileSystem {
 public:
  virtual ~FileSystem() = default;
  virtual bool exists(const char *path) = 0;
  // Whole-file read. Fails if the file is missing, unreadable or larger than
  // capacity; size is the number of bytes read.
  virtual bool readFile(const char *path, void *data, size_t capacity,
                        size_t &size) = 0;
  // Create or truncate, write everything, flush and close.
  virtual bool writeFile(const char *path, const void *data, size_t size) = 0;
  // Fails if `to` already exists (FAT semantics).
  virtual bool rename(const char *from, const char *to) = 0;
  virtual bool remove(const char *path) = 0;
  virtual bool mkdir(const char *path) = 0;
};

// Opaque records as files under one directory on removable media.
//
// Each file is a 16-byte header (magic "THSD", format 1, three reserved bytes,
// little-endian payload length, little-endian CRC-32 of the payload) followed
// by the payload. A torn or bit-flipped file reads as Corrupt, never as data.
//
// A write goes to <key>.tmp, is read back and checked, and only then replaces
// <key>: the old record is renamed to <key>.bak, the new one renamed into
// place, and the backup removed. If power is lost between those renames, the
// next read can fall back to <key>.bak. A leftover .tmp is never read. This
// protects against interrupted application operations when the filesystem is
// intact; FAT metadata and the card itself are not power-loss transactional.
class SdBlobStore final : public BlobStore {
 public:
  static constexpr size_t MAX_RECORD_BYTES = 4096;
  static constexpr size_t HEADER_BYTES = 16;

  SdBlobStore() = default;
  SdBlobStore(const SdBlobStore &) = delete;
  SdBlobStore &operator=(const SdBlobStore &) = delete;

  // `directory` is an absolute path such as "/turnhub"; it is created if
  // missing. Never formats or erases anything.
  Status begin(FileSystem &fs, const char *directory);
  // Forget the file system (for example after the card is removed).
  void end();
  bool ready() const { return fs_ != nullptr; }

  Status read(const char *key, void *data, size_t capacity,
              size_t &size) override;
  Status write(const char *key, const void *data, size_t size) override;
  Status remove(const char *key) override;

 private:
  static constexpr size_t PATH_BYTES = 48;
  bool path(char (&out)[PATH_BYTES], const char *key, const char *suffix) const;
  Status readFile(const char *filePath, void *data, size_t capacity,
                  size_t &size);

  FileSystem *fs_ = nullptr;
  char directory_[24] = {};
  uint8_t buffer_[HEADER_BYTES + MAX_RECORD_BYTES] = {};
};

uint32_t crc32(const void *data, size_t size);

}  // namespace TurnHubStorage
