#pragma once

#include <stddef.h>
#include <stdint.h>

namespace TurnHubStorage {

enum class Status : uint8_t {
  Ok,
  NotFound,
  Unavailable,
  InvalidArgument,
  Corrupt,
  UnsupportedSchema,
  IoError,
};

// Atlas repositories own keys and schemas. Backends only move opaque records.
// Calls are serialized on the Atlas application task; this is not a transaction
// or a thread-safe interface. A failed write must never be reported as success.
class BlobStore {
 public:
  virtual ~BlobStore() = default;
  // Query with data == nullptr and capacity == 0. Otherwise read the entire
  // record or fail; size reports the stored length, never a truncated success.
  virtual Status read(const char *key, void *data, size_t capacity,
                      size_t &size) = 0;
  virtual Status write(const char *key, const void *data, size_t size) = 0;
};

}  // namespace TurnHubStorage
