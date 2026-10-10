#pragma once

// Bounded artwork over the existing checksummed, locked SD BlobStore. Images
// are immutable chunks; publishing one small manifest is the commit point.
#include <stdio.h>
#include <string.h>
#include "storage.h"

namespace TurnHubArtwork {
using TurnHubStorage::BlobStore;
using TurnHubStorage::Status;
constexpr size_t CHUNK = 768;
constexpr size_t MAX_BYTES = 48 * 1024;
constexpr uint16_t MAX_SIDE = 512;
struct Image { uint32_t revision = 0; uint32_t size = 0; };
inline void chunkKey(char *key, uint32_t revision, size_t index) {
  snprintf(key, 16, "i%08lx%02x", static_cast<unsigned long>(revision), static_cast<unsigned>(index));
}
inline bool valid(const Image &image) {
  return image.revision != 0 && image.size >= 32 && image.size <= MAX_BYTES;
}
inline Status metadata(BlobStore &store, const char *key, Image &image) {
  uint8_t record[9] = {}; size_t size = 0; image = {};
  Status status = store.read(key, record, sizeof(record), size);
  if (status != Status::Ok) return status;
  if (size != sizeof(record) || record[0] != 1) return Status::Corrupt;
  for (size_t i = 0; i < 4; ++i) {
    image.revision |= uint32_t(record[1 + i]) << (8 * i);
    image.size |= uint32_t(record[5 + i]) << (8 * i);
  }
  return valid(image) ? Status::Ok : Status::Corrupt;
}
inline Status publish(BlobStore &store, const char *key, const Image &image) {
  if (!valid(image)) return Status::InvalidArgument;
  uint8_t record[9] = {1};
  for (size_t i = 0; i < 4; ++i) {
    record[1 + i] = image.revision >> (8 * i);
    record[5 + i] = image.size >> (8 * i);
  }
  return store.write(key, record, sizeof(record));
}
inline Status readChunk(BlobStore &store, const Image &image, size_t index,
                        uint8_t *bytes, size_t &size) {
  if (!valid(image) || index >= (image.size + CHUNK - 1) / CHUNK) return Status::InvalidArgument;
  char key[16]; chunkKey(key, image.revision, index);
  Status status = store.read(key, bytes, CHUNK, size);
  const size_t expected = image.size - index * CHUNK < CHUNK ? image.size - index * CHUNK : CHUNK;
  return status == Status::Ok && size != expected ? Status::Corrupt : status;
}
inline void thumbnailKey(char *key, uint32_t revision) {
  snprintf(key, 16, "r%08lx", static_cast<unsigned long>(revision));
}
inline void discard(BlobStore &store, const Image &image) {
  if (!valid(image)) return;
  char thumb[16]; thumbnailKey(thumb, image.revision); store.remove(thumb);
  for (size_t i = 0; i < (image.size + CHUNK - 1) / CHUNK; ++i) {
    char key[16]; chunkKey(key, image.revision, i); store.remove(key);
  }
}
// Streams a baseline JPEG's marker envelope without allocating the image.
// Never trusts the app's declared dimensions or MIME type. Hardware decoder
// errors still require a rendering fallback: envelope validation is not decoding.
class Reader {
 public:
  Reader(BlobStore &store, const Image &image) : store_(store), image_(image) {}
  int next() {
    if (position_ >= image_.size) return -1;
    if (position_ % CHUNK == 0) {
      size_t size = 0;
      if (readChunk(store_, image_, position_ / CHUNK, buffer_, size) != Status::Ok) return -1;
    }
    return buffer_[position_++ % CHUNK];
  }
  size_t position() const { return position_; }
 private:
  BlobStore &store_; Image image_; size_t position_ = 0; uint8_t buffer_[CHUNK] = {};
};
inline bool validateJpeg(BlobStore &store, const Image &image) {
  if (!valid(image)) return false;
  Reader r(store, image);
  if (r.next() != 0xff || r.next() != 0xd8) return false;
  bool frame = false, scan = false;
  for (;;) {
    if (r.next() != 0xff) return false;
    int marker; do { marker = r.next(); } while (marker == 0xff);
    if (marker == 0xd9) return frame && scan && r.position() == image.size;
    if (marker < 0 || marker == 0 || marker == 0xd8 || (marker >= 0xd0 && marker <= 0xd7)) return false;
    int hi = r.next(), lo = r.next();
    if (hi < 0 || lo < 0) return false;
    int length = (hi << 8 | lo) - 2;
    if (length < 0 || size_t(length) > image.size - r.position()) return false;
    if (marker == 0xc0) {
      if (frame || length < 6 || r.next() != 8) return false;
      int h1 = r.next(), h2 = r.next(), w1 = r.next(), w2 = r.next(), components = r.next();
      if (h1 < 0 || h2 < 0 || w1 < 0 || w2 < 0 || components < 0) return false;
      int height = h1 << 8 | h2, width = w1 << 8 | w2;
      if (width < 32 || width > MAX_SIDE || height != width || components != 3 || length != 6 + 3 * components) return false;
      length -= 6; frame = true;
    } else if ((marker >= 0xc1 && marker <= 0xcf) && marker != 0xc4) {
      return false; // Progressive/arithmetic/lossless are outside this contract.
    }
    for (int i = 0; i < length; ++i) if (r.next() < 0) return false;
    if (marker == 0xda) {
      if (!frame || scan) return false;
      scan = true;
      for (;;) {
        int byte = r.next(); if (byte < 0) return false;
        if (byte != 0xff) continue;
        int end; do { end = r.next(); } while (end == 0xff);
        if (end == 0 || (end >= 0xd0 && end <= 0xd7)) continue;
        return end == 0xd9 && r.position() == image.size;
      }
    }
  }
}
} // namespace TurnHubArtwork
