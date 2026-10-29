#include "standalone_import.h"

#include "account_access.h"
#include "game_profile.h"
#include "nvs_blob_store.h"
#include "profile_statistics.h"
#include "profile_store.h"
#include "serial_log.h"

namespace TurnHubStandalone {
namespace {

using TurnHub::serialLog;
using TurnHubStorage::Status;

constexpr uint8_t IMPORTED_SCHEMA = 2;
constexpr size_t IMPORTED_HEADER = 3;
constexpr size_t IMPORTED_SIZE = IMPORTED_HEADER + IMPORTED_CAPACITY * 8;

TurnHubStorage::NvsBlobStore nvsStore;
TurnHubStorage::BlobStore *testStore = nullptr;

TurnHubStorage::BlobStore *store() {
  if (testStore != nullptr) return testStore;
  return nvsStore.begin("turnhub") == Status::Ok ? &nvsStore : nullptr;
}

struct Taken {
  uint8_t next = 0;
  uint8_t count = 0;
  uint64_t hashes[IMPORTED_CAPACITY] = {};
};

Status readTaken(TurnHubStorage::BlobStore &blobs, Taken &taken) {
  uint8_t data[IMPORTED_SIZE] = {};
  size_t size = 0;
  Status status = blobs.read(IMPORTED_KEY, data, sizeof(data), size);
  if (status == Status::NotFound) return Status::Ok;
  // A record-wide ring from before per-player receipts starts over.
  if (status == Status::Ok && size >= 1 && data[0] == 1) return Status::Ok;
  if (status != Status::Ok) return status;
  if (size < IMPORTED_HEADER || data[0] != IMPORTED_SCHEMA) return Status::Corrupt;
  taken.next = data[1];
  taken.count = data[2];
  if (taken.count > IMPORTED_CAPACITY || taken.next >= IMPORTED_CAPACITY ||
      size != IMPORTED_HEADER + taken.count * 8u) {
    return Status::Corrupt;
  }
  for (uint8_t i = 0; i < taken.count; ++i) {
    uint64_t hash = 0;
    for (uint8_t b = 0; b < 8; ++b) hash |= static_cast<uint64_t>(data[IMPORTED_HEADER + i * 8 + b]) << (8 * b);
    taken.hashes[i] = hash;
  }
  return Status::Ok;
}

Status writeTaken(TurnHubStorage::BlobStore &blobs, const Taken &taken) {
  uint8_t data[IMPORTED_SIZE] = {};
  data[0] = IMPORTED_SCHEMA;
  data[1] = taken.next;
  data[2] = taken.count;
  for (uint8_t i = 0; i < taken.count; ++i) {
    for (uint8_t b = 0; b < 8; ++b) data[IMPORTED_HEADER + i * 8 + b] = static_cast<uint8_t>(taken.hashes[i] >> (8 * b));
  }
  return blobs.write(IMPORTED_KEY, data, IMPORTED_HEADER + taken.count * 8u);
}

bool archived(const String &profileId) {
  TurnHubAccounts::Account account;
  return !TurnHubAccounts::load(profileId, account) || account.archived;
}

TurnHubProfiles::LastGameResult resultFor(const ImportedGame &game, uint8_t index) {
  using TurnHubProfiles::LastGameResult;
  if (game.winner == static_cast<int8_t>(index) || (game.winners & (1u << index)) ||
      (game.twoHeadedGiant && game.winner >= 0 && game.winner / 2 == index / 2)) return LastGameResult::Win;
  if (game.players[index].outOrder > 0) return game.players[index].conceded ? LastGameResult::Conceded : LastGameResult::Eliminated;
  if (game.winner < 0) return LastGameResult::Draw;
  return LastGameResult::Loss;
}

}  // namespace

uint64_t receiptHash(const String &recordId, const String &profileId) {
  uint64_t hash = 1469598103934665603ULL;
  auto add = [&hash](uint8_t byte) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  };
  for (size_t i = 0; i < recordId.length(); ++i) add(static_cast<uint8_t>(recordId[i]));
  add('/');
  for (size_t i = 0; i < profileId.length(); ++i) add(static_cast<uint8_t>(profileId[i]));
  return hash;
}

void useImportStore(TurnHubStorage::BlobStore *store) { testStore = store; }

bool validRecordId(const String &recordId) {
  if (recordId.length() < 8 || recordId.length() > MAX_RECORD_ID_LENGTH) return false;
  for (size_t i = 0; i < recordId.length(); ++i) {
    const char c = recordId[i];
    const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
    if (!ok) return false;
  }
  return true;
}

ImportResult importGame(const ImportedGame &game) {
  ImportResult result;
  if (!validRecordId(game.recordId) || game.playerCount < 2 || game.playerCount > MAX_IMPORT_PLAYERS ||
      game.gameProfile >= static_cast<uint8_t>(TurnHub::GameProfile::Count) ||
      (game.gameProfile == static_cast<uint8_t>(TurnHub::GameProfile::DoomCommander) && game.playerCount < 5) ||
      (game.twoHeadedGiant && (game.playerCount < 4 || game.playerCount % 2 ||
          !TurnHub::teamsAllowed(static_cast<TurnHub::GameProfile>(game.gameProfile)))) ||
      game.winner >= static_cast<int8_t>(game.playerCount) || game.starter >= static_cast<int8_t>(game.playerCount) ||
      // The winner set lists real players and starts at the first winner.
      (game.winners && (game.winner < 0 || (game.winners >> game.playerCount) ||
          !(game.winners & (1u << game.winner)) || (game.winners & ((1u << game.winner) - 1u))))) {
    return result;
  }
  TurnHubStorage::BlobStore *blobs = store();
  Taken taken;
  if (blobs == nullptr || readTaken(*blobs, taken) != Status::Ok) {
    result.status = ImportStatus::StorageError;
    return result;
  }
  auto seen = [&taken](uint64_t hash) {
    for (uint8_t i = 0; i < taken.count; ++i)
      if (taken.hashes[i] == hash) return true;
    return false;
  };
  if (!TurnHubProfiles::ready() && !TurnHubProfiles::begin()) {
    result.status = ImportStatus::StorageError;
    return result;
  }
  // Exact identities only. Validate the whole mapping before taking a receipt
  // or changing any statistics. Names remain display labels; an empty ID is a
  // guest. A player this record already credited is skipped, even if their
  // profile has since been archived.
  bool fresh[MAX_IMPORT_PLAYERS] = {};
  // A profile an Admin merged into another is credited as that one.
  String resolved[MAX_IMPORT_PLAYERS];
  for (uint8_t i = 0; i < game.playerCount; ++i)
    if (game.players[i].profileId.length()) resolved[i] = TurnHubProfiles::mergedProfileFor(game.players[i].profileId);
  uint8_t mapped = 0;
  for (uint8_t i = 0; i < game.playerCount; ++i) {
    const auto &p = game.players[i];
    if ((p.selfEliminated && (!p.outOrder || p.conceded || p.eliminatedBy >= 0 || p.bagDrawn)) ||
        (p.bagDrawn && (game.gameProfile != static_cast<uint8_t>(TurnHub::GameProfile::DoomCommander) ||
          !p.outOrder || p.conceded || p.eliminatedBy >= 0)) ||
        (p.conceded && !p.outOrder) || p.eliminatedBy < -1 ||
        (p.eliminatedBy >= 0 && (!p.outOrder || p.conceded || p.eliminatedBy == i || p.eliminatedBy >= game.playerCount ||
            (game.twoHeadedGiant && p.eliminatedBy / 2 == i / 2)))) return result;
    const String &id = resolved[i];
    if (id.length() == 0) continue;
    ++mapped;
    bool bad = false;
    for (uint8_t j = 0; j < i; ++j) bad = bad || resolved[j] == id;
    // Credited before a merge under the old ID counts as credited.
    if (!bad && (seen(receiptHash(game.recordId, id)) || seen(receiptHash(game.recordId, game.players[i].profileId)))) {
      ++result.already;
    } else {
      bad = bad || !TurnHubProfiles::profileExists(id) || archived(id);
      fresh[i] = true;
    }
    if (bad) {
      result.status = ImportStatus::BadMapping;
      return result;
    }
  }
  if (mapped == 0) {
    result.status = ImportStatus::BadMapping;
    return result;
  }
  result.guests = static_cast<uint8_t>(game.playerCount - mapped);
  if (result.already == mapped) {
    result.status = ImportStatus::Duplicate;
    return result;
  }
  // Remembered first: a cut after this loses the statistics, never doubles them.
  for (uint8_t i = 0; i < game.playerCount; ++i) {
    if (!fresh[i]) continue;
    taken.hashes[taken.next] = receiptHash(game.recordId, resolved[i]);
    taken.next = static_cast<uint8_t>((taken.next + 1) % IMPORTED_CAPACITY);
    if (taken.count < IMPORTED_CAPACITY) ++taken.count;
  }
  if (writeTaken(*blobs, taken) != Status::Ok) {
    result.status = ImportStatus::StorageError;
    return result;
  }
  for (uint8_t i = 0; i < game.playerCount; ++i) {
    if (!fresh[i]) continue;
    const ImportedPlayer &player = game.players[i];
    const String profileId = resolved[i];
    TurnHubProfiles::ProfileStats stats{};
    if (!TurnHubProfiles::loadStatsForProfile(profileId, stats)) {
      result.unmatched[result.unmatchedCount++] = player.name;
      continue;
    }
    TurnHubProfileStats::GameResult played;
    played.gameProfile = game.gameProfile;
    played.durationMs = game.durationMs;
    played.result = resultFor(game, i);
    played.started = game.starter == static_cast<int8_t>(i);
    for (uint8_t victim = 0; victim < game.playerCount; ++victim)
      if (game.players[victim].eliminatedBy == i) ++played.eliminationsMade;
    played.turns = player.turnsCompleted;
    played.turnMs = player.turnMs;
    played.fastestTurnMs = player.fastestTurnMs;
    played.longestTurnMs = player.longestTurnMs;
    TurnHubProfileStats::applyGameResult(stats, played);
    if (TurnHubProfiles::saveStatsForProfile(profileId, stats)) {
      ++result.credited;
    } else {
      result.unmatched[result.unmatchedCount++] = player.name;
    }
  }
  result.status = ImportStatus::Imported;
  serialLog.print("ATLAS|STANDALONE|IMPORT|players=");
  serialLog.print(game.playerCount);
  serialLog.print("|credited=");
  serialLog.print(result.credited);
  serialLog.print("|guests=");
  serialLog.println(result.guests);
  return result;
}

}  // namespace TurnHubStandalone
