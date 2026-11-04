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
  // Deletes one record. NotFound when there was none; never touches others.
  virtual Status remove(const char *key) = 0;
};

}  // namespace TurnHubStorage

namespace TurnHub {

// Shared, greppable spelling for diagnostic logging, so a serial console tells
// NotFound, Corrupt, UnsupportedSchema, IoError and a clean Ok apart.
inline const char *storageStatusName(TurnHubStorage::Status status) {
  using TurnHubStorage::Status;
  switch (status) {
    case Status::Ok: return "OK";
    case Status::NotFound: return "NOT_FOUND";
    case Status::Unavailable: return "UNAVAILABLE";
    case Status::InvalidArgument: return "INVALID_ARGUMENT";
    case Status::Corrupt: return "CORRUPT";
    case Status::UnsupportedSchema: return "UNSUPPORTED_SCHEMA";
    case Status::IoError: return "IO_ERROR";
    default: return "UNKNOWN";
  }
}

}  // namespace TurnHub
