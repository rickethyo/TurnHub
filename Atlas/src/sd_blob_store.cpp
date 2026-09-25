#include "sd_blob_store.h"
#include <string.h>

namespace TurnHubStorage {
namespace {
const uint8_t MAGIC[4] = {'T', 'H', 'S', 'D'};
constexpr uint8_t FORMAT = 1;
constexpr size_t LENGTH_OFFSET = 8;
constexpr size_t CRC_OFFSET = 12;

// NVS key rules plus a file-name-safe character set.
bool validKey(const char *key) {
  if (key == nullptr || key[0] == '\0' || strlen(key) > 15) return false;
  for (const char *c = key; *c; ++c) {
    const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                    (*c >= '0' && *c <= '9') || *c == '_' || *c == '-';
    if (!ok) return false;
  }
  return true;
}

void putU32(uint8_t *out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t getU32(const uint8_t *in) {
  uint32_t value = 0;
  for (int i = 0; i < 4; ++i) value |= static_cast<uint32_t>(in[i]) << (8 * i);
  return value;
}
}  // namespace

uint32_t crc32(const void *data, size_t size) {
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

Status SdBlobStore::begin(FileSystem &fs, const char *directory) {
  fs_ = nullptr;
  if (directory == nullptr || directory[0] != '/' ||
      strlen(directory) >= sizeof(directory_)) {
    return Status::InvalidArgument;
  }
  if (!fs.exists(directory) && !fs.mkdir(directory)) return Status::IoError;
  strncpy(directory_, directory, sizeof(directory_) - 1);
  directory_[sizeof(directory_) - 1] = '\0';
  fs_ = &fs;
  return Status::Ok;
}

void SdBlobStore::end() { fs_ = nullptr; }

bool SdBlobStore::path(char (&out)[PATH_BYTES], const char *key,
                       const char *suffix) const {
  const size_t needed = strlen(directory_) + 1 + strlen(key) + strlen(suffix);
  if (needed >= PATH_BYTES) return false;
  strcpy(out, directory_);
  strcat(out, "/");
  strcat(out, key);
  strcat(out, suffix);
  return true;
}

// Reads and validates one record file. data == nullptr && capacity == 0 is a
// size query, matching BlobStore::read.
Status SdBlobStore::readFile(const char *filePath, void *data, size_t capacity,
                             size_t &size) {
  size_t fileSize = 0;
  if (!fs_->readFile(filePath, buffer_, sizeof(buffer_), fileSize)) {
    return Status::IoError;
  }
  if (fileSize < HEADER_BYTES || memcmp(buffer_, MAGIC, sizeof(MAGIC)) != 0) {
    return Status::Corrupt;
  }
  if (buffer_[4] != FORMAT) return Status::UnsupportedSchema;
  const uint32_t length = getU32(buffer_ + LENGTH_OFFSET);
  if (length == 0 || length > MAX_RECORD_BYTES ||
      fileSize != HEADER_BYTES + length ||
      crc32(buffer_ + HEADER_BYTES, length) != getU32(buffer_ + CRC_OFFSET)) {
    return Status::Corrupt;
  }
  size = length;
  if (data == nullptr) return Status::Ok;
  if (length > capacity) return Status::Corrupt;
  memcpy(data, buffer_ + HEADER_BYTES, length);
  return Status::Ok;
}

Status SdBlobStore::read(const char *key, void *data, size_t capacity,
                         size_t &size) {
  size = 0;
  if (fs_ == nullptr) return Status::Unavailable;
  if (!validKey(key) || (data == nullptr && capacity != 0)) {
    return Status::InvalidArgument;
  }
  char current[PATH_BYTES];
  char backup[PATH_BYTES];
  if (!path(current, key, "") || !path(backup, key, ".bak")) {
    return Status::InvalidArgument;
  }
  // A present but damaged record is Corrupt; it never falls back to an older
  // backup, which would silently roll the record back.
  if (fs_->exists(current)) return readFile(current, data, capacity, size);
  // Power was lost mid-replace: the backup is still the committed record.
  if (fs_->exists(backup)) return readFile(backup, data, capacity, size);
  return Status::NotFound;
}

Status SdBlobStore::write(const char *key, const void *data, size_t size) {
  if (fs_ == nullptr) return Status::Unavailable;
  if (!validKey(key) || data == nullptr || size == 0 ||
      size > MAX_RECORD_BYTES) {
    return Status::InvalidArgument;
  }
  char current[PATH_BYTES];
  char backup[PATH_BYTES];
  char temporary[PATH_BYTES];
  if (!path(current, key, "") || !path(backup, key, ".bak") ||
      !path(temporary, key, ".tmp")) {
    return Status::InvalidArgument;
  }

  memcpy(buffer_, MAGIC, sizeof(MAGIC));
  buffer_[4] = FORMAT;
  buffer_[5] = buffer_[6] = buffer_[7] = 0;
  putU32(buffer_ + LENGTH_OFFSET, static_cast<uint32_t>(size));
  putU32(buffer_ + CRC_OFFSET, crc32(data, size));
  memcpy(buffer_ + HEADER_BYTES, data, size);
  if (!fs_->writeFile(temporary, buffer_, HEADER_BYTES + size)) {
    return Status::IoError;
  }
  // Read the new file back before it can replace anything.
  size_t checked = 0;
  if (readFile(temporary, nullptr, 0, checked) != Status::Ok ||
      checked != size ||
      memcmp(buffer_ + HEADER_BYTES, data, size) != 0) {
    return Status::IoError;
  }

  if (fs_->exists(current)) {
    // A backup next to a current record is left over from a finished replace.
    if (fs_->exists(backup) && !fs_->remove(backup)) return Status::IoError;
    if (!fs_->rename(current, backup)) return Status::IoError;
  }
  if (!fs_->rename(temporary, current)) {
    // Put the old record back; if that fails too, reads still find the backup.
    fs_->rename(backup, current);
    return Status::IoError;
  }
  // The new record is committed. A backup left here is harmless: reads prefer
  // the current file and the next write clears it.
  fs_->remove(backup);
  return Status::Ok;
}


// Removes the record and any backup or leftover temporary copy of it.
Status SdBlobStore::remove(const char *key) {
  if (fs_ == nullptr) return Status::Unavailable;
  if (!validKey(key)) return Status::InvalidArgument;
  char current[PATH_BYTES];
  char backup[PATH_BYTES];
  char temp[PATH_BYTES];
  if (!path(current, key, "") || !path(backup, key, ".bak") || !path(temp, key, ".tmp")) {
    return Status::InvalidArgument;
  }
  bool found = false;
  const char *const files[] = {current, backup, temp};
  for (const char *file : files) {
    if (!fs_->exists(file)) continue;
    found = true;
    if (!fs_->remove(file)) return Status::IoError;
  }
  return found ? Status::Ok : Status::NotFound;
}
}  // namespace TurnHubStorage
