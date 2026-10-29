#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "turnhub_json.h"

// Atlas's match history (plans: atlas-match-history, Invariant 12): every
// finished game as the same match record the app keeps
// (protocol/game-record-v1.schema.json), one JSON line per game in
// games.jsonl, and the same game as CSV rows in games.csv (the shared core's
// recordCsvRows, identical to the app's export). Both files sit in Atlas's
// folder on the microSD card, so a computer can read them over the USB-drive
// mode. Card only: play never waits for it, and without a card the NVS totals
// still count. A record is kept once; later copies of the same record ID are
// ignored.
namespace TurnHubHistory {

// The few file operations the history needs, on names inside Atlas's card
// folder. Firmware: sd_card.cpp (under the card lock); tests: memory.
class ArchiveFiles {
 public:
  virtual ~ArchiveFiles() = default;
  // False when the file is missing.
  virtual bool size(const char *name, size_t &size) = 0;
  virtual bool append(const char *name, const char *data, size_t size) = 0;
  // Up to capacity bytes from offset; got is 0 at the end.
  virtual bool readAt(const char *name, size_t offset, char *data, size_t capacity, size_t &got) = 0;
};

constexpr char RECORDS_FILE[] = "games.jsonl";
constexpr char CSV_FILE[] = "games.csv";
// A longer line (a damaged file) is skipped, never kept.
constexpr size_t MAX_RECORD_BYTES = 16384;

// Where the files are: the card's (sd_card.cpp) unless a test sets others.
void useFiles(ArchiveFiles *files);

enum class KeepStatus : uint8_t { Kept, AlreadyKept, NoCard, Invalid, WriteFailed };
// Keeps one finished game's record: its JSON line, then its CSV rows.
KeepStatus keep(const turnhub::Json &record);
const char *keepStatusName(KeepStatus status);

// The records a profile played in, oldest first, after skipping `skip` of
// them, at most `limit`; more is set when others follow. False without a card.
bool historyFor(const String &profileId, size_t skip, size_t limit, std::vector<turnhub::Json> &out, bool &more);

// A record from a phone (standalone import), as Atlas keeps it: the players'
// Atlas profile IDs from the import's mapping (empty: a guest), names as
// imported, no phone-local IDs. False when it isn't a record of the same game
// (record ID and player count).
bool adoptImportedRecord(const turnhub::Json &phoneRecord, const String &recordId, uint8_t players,
                         const String *names, const String *profileIds, turnhub::Json &out);

}  // namespace TurnHubHistory
