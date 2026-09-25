#pragma once

#include <nvs.h>
#include "storage.h"

namespace TurnHubStorage {

// Existing internal-flash backend. Does not initialize, erase, or repair NVS.
class NvsBlobStore final : public BlobStore {
 public:
  ~NvsBlobStore() override;
  NvsBlobStore() = default;
  NvsBlobStore(const NvsBlobStore &) = delete;
  NvsBlobStore &operator=(const NvsBlobStore &) = delete;
  Status begin(const char *nameSpace);
  Status read(const char *key, void *data, size_t capacity,
              size_t &size) override;
  Status write(const char *key, const void *data, size_t size) override;
  Status remove(const char *key) override;

 private:
  nvs_handle_t handle_ = 0;
  bool ready_ = false;
};

}  // namespace TurnHubStorage
