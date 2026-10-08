#pragma once

#include <Arduino.h>

#include "storage.h"
#include "turnhub_types.h"

// Finished standalone tablet games (Android/README.md "The one exception"):
// a game the app ran with no Atlas at the table reaches Atlas only as one of
// these records, imported once. Atlas credits each player that matches one of
// its profiles by explicit ID (never by name)
// and ignores a record ID it has already taken. The ID is remembered before
// any statistics are written, so a cut can lose a record's statistics but
// a replay is not counted twice while its receipt remains in the 64-ID ring.
namespace TurnHubStandalone {

constexpr uint8_t MAX_IMPORT_PLAYERS = 8;
constexpr size_t MAX_RECORD_ID_LENGTH = 48;

struct ImportedPlayer {
  String name;
  // Required explicit profile ID on the destination Atlas.
  String profileId;
  uint32_t turnsCompleted = 0;
  uint32_t turnMs = 0;
  uint32_t fastestTurnMs = 0;
  uint32_t longestTurnMs = 0;
  // 1 for the first player out; 0 for anyone still in at the end.
  uint8_t outOrder = 0;
};

struct ImportedGame {
  String recordId;
  uint8_t gameProfile = 0;
  uint32_t durationMs = 0;
  // Indices into players; -1 for none (a game with no winner).
  int8_t starter = -1;
  int8_t winner = -1;
  uint8_t playerCount = 0;
  ImportedPlayer players[MAX_IMPORT_PLAYERS];
};

enum class ImportStatus : uint8_t { Imported, Duplicate, Invalid, BadMapping, StorageError };

struct ImportResult {
  ImportStatus status = ImportStatus::Invalid;
  uint8_t credited = 0;
  // Players whose statistics could not be credited (e.g. a storage write failed).
  uint8_t unmatchedCount = 0;
  String unmatched[MAX_IMPORT_PLAYERS];
};

// The record IDs already taken: schema 1, next slot, count, then up to
// IMPORTED_CAPACITY 64-bit FNV-1a hashes, little-endian, under "sgimport".
constexpr char IMPORTED_KEY[] = "sgimport";
constexpr uint8_t IMPORTED_CAPACITY = 64;
uint64_t recordHash(const String &recordId);

// Where taken record IDs are kept: NVS ("turnhub") unless a test sets another.
void useImportStore(TurnHubStorage::BlobStore *store);

bool validRecordId(const String &recordId);
ImportResult importGame(const ImportedGame &game);

}  // namespace TurnHubStandalone
