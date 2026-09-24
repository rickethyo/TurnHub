#pragma once

// Account permissions and private moderation counters, stored per profile.
// See Documentation/engineering/ACCOUNTS_AND_MODERATION.md.

#include <Arduino.h>
#include "storage.h"

namespace TurnHubAccounts {

enum Permission : uint8_t {
  Admin = 1,
  GameMaster = 2,
  Developer = 4,
  // Moderation abilities; each requires GameMaster.
  ResetConnections = 8,
  RemovePlayer = 16,
};
constexpr uint8_t ALL_PERMISSIONS = Admin | GameMaster | Developer | ResetConnections | RemovePlayer;
constexpr uint8_t MODERATION_PERMISSIONS = ResetConnections | RemovePlayer;

struct Account {
  uint8_t permissions = 0;
  bool nudgeMuted = false;
  bool archived = false;
  // Set by moderation; the owner must sign in again before reconnecting.
  bool reconnectRequired = false;
  // Moderation counts written by firmware before 2026-09-24. They now live in
  // the profile's moderation statistics (profile_store.h); these fields exist
  // only so TurnHubAccounts::load() can migrate them, after which they are 0.
  uint32_t legacyConnectionResets = 0;
  uint32_t legacyGameRemovals = 0;
};

// Record layout (12 bytes, little-endian counters):
//   [0] schema (1 or 2)   [1] permissions   [2] flags   [3] reconnectRequired
//   [4..7] legacy connectionResets          [8..11] legacy gameRemovals
// Schema 1 flags hold only nudgeMuted (bit 0); schema 2 adds archived (bit 1).
constexpr size_t ACCOUNT_RECORD_SIZE = 12;
constexpr uint8_t ACCOUNT_SCHEMA = 2;

inline TurnHubStorage::Status read(TurnHubStorage::BlobStore &store, const char *key, Account &account) {
  using TurnHubStorage::Status;
  uint8_t b[ACCOUNT_RECORD_SIZE] = {};
  size_t n = 0;
  Status status = store.read(key, nullptr, 0, n);
  if (status != Status::Ok) return status;
  if (n != sizeof(b)) return Status::Corrupt;
  status = store.read(key, b, sizeof(b), n);
  if (status != Status::Ok) return status;
  if (n != sizeof(b)) return Status::Corrupt;
  if (b[0] != 1 && b[0] != 2) return Status::UnsupportedSchema;
  const uint8_t maxFlags = b[0] == 1 ? 1 : 3;
  if (b[1] > ALL_PERMISSIONS || b[2] > maxFlags || b[3] > 1) return Status::Corrupt;

  account = Account{};
  account.permissions = b[1];
  account.nudgeMuted = b[2] & 1;
  account.archived = b[2] & 2;
  account.reconnectRequired = b[3];
  for (unsigned i = 0; i < 4; ++i) {
    account.legacyConnectionResets |= uint32_t(b[4 + i]) << (i * 8);
    account.legacyGameRemovals |= uint32_t(b[8 + i]) << (i * 8);
  }
  return Status::Ok;
}

// Writes the current schema. Refuses to overwrite a record it cannot read,
// so an unknown future schema is never clobbered.
inline TurnHubStorage::Status write(TurnHubStorage::BlobStore &store, const char *key, const Account &account) {
  using TurnHubStorage::Status;
  Account existing;
  const Status status = read(store, key, existing);
  if (status != Status::Ok && status != Status::NotFound) return status;
  if (account.permissions > ALL_PERMISSIONS) return Status::Corrupt;

  uint8_t b[ACCOUNT_RECORD_SIZE] = {
      ACCOUNT_SCHEMA,
      account.permissions,
      uint8_t((account.nudgeMuted ? 1 : 0) | (account.archived ? 2 : 0)),
      uint8_t(account.reconnectRequired),
  };
  for (unsigned i = 0; i < 4; ++i) {
    b[4 + i] = uint8_t(account.legacyConnectionResets >> (i * 8));
    b[8 + i] = uint8_t(account.legacyGameRemovals >> (i * 8));
  }
  return store.write(key, b, sizeof(b));
}

bool load(const String &id, Account &account);
bool save(const String &id, const Account &account);
// Missing bootstrap record is distinct from unreadable storage.
bool primaryAdmin(String &id);
bool establishAdmin(const String &id);

// True when the account exists, is not archived and holds every bit in permission.
inline bool has(const String &id, uint8_t permission) {
  Account account;
  return load(id, account) && !account.archived && (account.permissions & permission) == permission;
}

}  // namespace TurnHubAccounts
