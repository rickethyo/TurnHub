#include "match_archive.h"

#include <string>

#include "sd_card.h"
#include "serial_log.h"
#include "turnhub_core.h"

namespace TurnHubHistory {

namespace {

using turnhub::Json;
using TurnHub::serialLog;

ArchiveFiles *injected = nullptr;

ArchiveFiles *files() { return injected ? injected : TurnHubAtlas::sdArchiveFiles(); }

// Calls each complete line of RECORDS_FILE in order until visit returns false.
// A line longer than MAX_RECORD_BYTES (damage) is skipped.
template <typename Visit>
bool forEachLine(ArchiveFiles &fs, Visit visit) {
  size_t total = 0;
  if (!fs.size(RECORDS_FILE, total)) return true;  // No games yet.
  std::string line;
  bool tooLong = false;
  char chunk[512];
  for (size_t offset = 0; offset < total;) {
    size_t got = 0;
    if (!fs.readAt(RECORDS_FILE, offset, chunk, sizeof(chunk), got)) return false;
    if (got == 0) break;
    offset += got;
    for (size_t i = 0; i < got; ++i) {
      if (chunk[i] != '\n') {
        if (line.size() < MAX_RECORD_BYTES) line += chunk[i]; else tooLong = true;
        continue;
      }
      const bool keepGoing = tooLong || line.empty() || visit(line);
      line.clear();
      tooLong = false;
      if (!keepGoing) return true;
    }
  }
  return true;  // A torn last line (no newline) is not a record.
}

// The record's ID as it appears in a line written by Json::dump.
std::string idNeedle(const std::string &recordId) { return "\"recordId\":" + Json(recordId).dump() + ","; }

bool validRecord(const Json &r) {
  const std::string &id = r.get("recordId").asString();
  const Json &players = r.get("players");
  if (!r.isObject() || id.empty() || id.size() > 48 || !r.get("gameProfile").isString() || !players.isArray() ||
      players.size() < 2 || players.size() > 10 || !r.get("winners").isArray())
    return false;
  for (size_t i = 0; i < players.size(); ++i) {
    const Json &p = players[i];
    if (!p.isObject() || !p.get("name").isString() || !p.get("counters").isObject() ||
        p.get("counters").get("commanderCasts").size() != 2)
      return false;
  }
  return true;
}

}  // namespace

void useFiles(ArchiveFiles *files) { injected = files; }

const char *keepStatusName(KeepStatus status) {
  switch (status) {
    case KeepStatus::Kept: return "kept";
    case KeepStatus::AlreadyKept: return "already_kept";
    case KeepStatus::NoCard: return "no_card";
    case KeepStatus::Invalid: return "invalid";
    case KeepStatus::WriteFailed: return "write_failed";
  }
  return "unknown";
}

KeepStatus keep(const Json &record) {
  if (!validRecord(record)) return KeepStatus::Invalid;
  ArchiveFiles *fs = files();
  if (!fs) return KeepStatus::NoCard;
  const std::string needle = idNeedle(record.get("recordId").asString());
  bool seen = false;
  if (!forEachLine(*fs, [&](const std::string &line) { seen = line.find(needle) != std::string::npos; return !seen; }))
    return KeepStatus::WriteFailed;
  if (seen) return KeepStatus::AlreadyKept;
  // After a torn write (power cut) the file ends mid-line: start a new one.
  size_t total = 0;
  char last = '\n';
  size_t got = 0;
  if (fs->size(RECORDS_FILE, total) && total > 0 && !fs->readAt(RECORDS_FILE, total - 1, &last, 1, got)) {
    return KeepStatus::WriteFailed;
  }
  const std::string line = (last == '\n' ? "" : "\n") + record.dump() + "\n";
  if (line.size() > MAX_RECORD_BYTES || !fs->append(RECORDS_FILE, line.data(), line.size())) {
    serialLog.println("ATLAS|HISTORY|RECORD_WRITE_FAILED");
    return KeepStatus::WriteFailed;
  }
  // The CSV follows the records file; a new or emptied file gets its header first.
  size_t csvSize = 0;
  std::string csv = (!fs->size(CSV_FILE, csvSize) || csvSize == 0) ? std::string(turnhub::RECORD_CSV_HEADER) : std::string();
  csv += turnhub::recordCsvRows(record);
  if (!fs->append(CSV_FILE, csv.data(), csv.size())) serialLog.println("ATLAS|HISTORY|CSV_WRITE_FAILED");
  serialLog.println("ATLAS|HISTORY|KEPT");
  return KeepStatus::Kept;
}

bool historyFor(const String &profileId, size_t skip, size_t limit, std::vector<Json> &out, bool &more) {
  out.clear();
  more = false;
  ArchiveFiles *fs = files();
  if (!fs || profileId.length() == 0) return fs != nullptr;
  const std::string needle = "\"profileId\":" + Json(std::string(profileId.c_str())).dump();
  return forEachLine(*fs, [&](const std::string &line) {
    if (line.find(needle) == std::string::npos) return true;
    bool ok = false;
    Json record = Json::parse(line, &ok);
    if (!ok || !validRecord(record)) return true;
    bool played = false;
    const Json &players = record.get("players");
    for (size_t i = 0; i < players.size() && !played; ++i) played = players[i].get("profileId").asString() == profileId.c_str();
    if (!played) return true;
    if (skip > 0) { --skip; return true; }
    if (out.size() == limit) { more = true; return false; }
    out.push_back(std::move(record));
    return true;
  });
}

bool adoptImportedRecord(const Json &phone, const String &recordId, uint8_t players, const String *names,
                         const String *profileIds, Json &out) {
  if (!phone.isObject() || phone.get("recordId").asString() != recordId.c_str() ||
      phone.get("players").size() != players)
    return false;
  out = phone;
  Json seats = Json::array();
  for (uint8_t i = 0; i < players; ++i) {
    Json p = phone.get("players")[i];
    if (!p.isObject()) return false;
    p.set("name", std::string(names[i].c_str()));
    p.set("profileId", profileIds[i].length() ? Json(std::string(profileIds[i].c_str())) : Json());
    p.set("localId", Json());
    seats.push(std::move(p));
  }
  out.set("players", std::move(seats));
  return validRecord(out);
}

}  // namespace TurnHubHistory
