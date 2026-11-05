#include "card_cache.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include "match_archive.h"
#include "sd_card.h"
#include "serial_log.h"
#include "storage.h"

namespace TurnHubCards {
namespace {

using TurnHubStorage::Status;

TurnHubStorage::BlobStore *injected = nullptr;
bool injectedSet = false;
TurnHubStorage::BlobStore *store() { return injectedSet ? injected : TurnHubAtlas::sdBlobStore(); }

// About 15 KB, taken (from PSRAM where there is some) only once a catalog arrives.
Plane *planes = nullptr;
bool reserve() {
  if (planes) return true;
#if defined(ESP_PLATFORM)
  planes = static_cast<Plane *>(heap_caps_malloc(sizeof(Plane) * MAX_PLANES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
  if (!planes) planes = static_cast<Plane *>(malloc(sizeof(Plane) * MAX_PLANES));
  return planes != nullptr;
}
uint16_t planesCount = 0;
char version[VERSION_LENGTH + 1] = "";

// The catalog is saved as text in records of up to CHUNK bytes (the card's
// record limit is 1 KB): "pl0".."pl<n-1>", then "plv" = "<n>\t<version>".
constexpr size_t CHUNK = 960;
constexpr uint8_t MAX_CHUNKS = 24;

bool idChar(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}

// Parses catalog lines onto [out] after [count] entries; false for any malformed line.
bool parsePlanes(const char *text, Plane *out, uint16_t &count) {
  const char *line = text;
  while (*line) {
    const char *end = strchr(line, '\n');
    if (!end) end = line + strlen(line);
    if (end != line) {
      if (count >= MAX_PLANES) return false;
      const char *tab1 = static_cast<const char *>(memchr(line, '\t', end - line));
      const char *tab2 = tab1 ? static_cast<const char *>(memchr(tab1 + 1, '\t', end - tab1 - 1)) : nullptr;
      if (!tab2 || tab1 - line != static_cast<long>(CARD_ID_LENGTH) || tab2 - tab1 - 1 < 1 ||
          tab2 - tab1 - 1 > static_cast<long>(PLANE_NAME_LENGTH) || end - tab2 != 2 || (tab2[1] != '0' && tab2[1] != '1'))
        return false;
      Plane &p = out[count];
      for (size_t i = 0; i < CARD_ID_LENGTH; ++i) {
        if (!idChar(line[i])) return false;
        p.id[i] = line[i];
      }
      p.id[CARD_ID_LENGTH] = '\0';
      const size_t nameLength = tab2 - tab1 - 1;
      for (size_t i = 0; i < nameLength; ++i) {
        const char c = tab1[1 + i];
        p.name[i] = c >= 32 && c <= 126 ? c : '?';
      }
      p.name[nameLength] = '\0';
      p.phenomenon = tab2[1] == '1';
      for (uint16_t i = 0; i < count; ++i) if (!strcmp(out[i].id, p.id)) return false;
      ++count;
    }
    line = *end ? end + 1 : end;
  }
  return true;
}

void chunkKey(char *key, size_t size, uint8_t index) { snprintf(key, size, "pl%u", static_cast<unsigned>(index)); }

String commanderKey(const String &profileId) { return String("d") + profileId; }

bool printable(const String &text, size_t max) {
  if (!text.length() || text.length() > max) return false;
  for (size_t i = 0; i < text.length(); ++i) if (text[i] < 32 || text[i] > 126 || text[i] == '\t') return false;
  return true;
}

}  // namespace

// Hands the catalog's lines to the SD worker (the plane cards section below).
bool queueCatalog(const char *lines, const char *catalogVersion);

void setStoreForTests(TurnHubStorage::BlobStore *s) {
  injected = s;
  injectedSet = true;
  planesCount = 0;
  version[0] = '\0';
}

bool loadPlanes() {
  TurnHubStorage::BlobStore *s = store();
  if (!s) return false;
  char meta[8 + VERSION_LENGTH + 1] = {};
  size_t size = 0;
  if (s->read("plv", meta, sizeof(meta) - 1, size) != Status::Ok) return false;
  meta[size] = '\0';
  const char *tab = strchr(meta, '\t');
  const int chunks = atoi(meta);
  if (!tab || chunks < 1 || chunks > MAX_CHUNKS || strlen(tab + 1) > VERSION_LENGTH) return false;
  // Each record holds whole lines: parse one at a time, straight into the catalog.
  if (!reserve()) return false;
  planesCount = 0;
  uint16_t count = 0;
  for (int i = 0; i < chunks; ++i) {
    char key[8], text[CHUNK + 1];
    chunkKey(key, sizeof(key), static_cast<uint8_t>(i));
    if (s->read(key, text, CHUNK, size) != Status::Ok) return false;
    text[size] = '\0';
    if (!parsePlanes(text, planes, count)) return false;
  }
  if (!count) return false;
  planesCount = count;
  snprintf(version, sizeof(version), "%s", tab + 1);
  return true;
}

bool replacePlanes(const char *lines, const char *newVersion, bool &queued) {
  queued = false;
  if (!lines || !newVersion || !newVersion[0] || strlen(newVersion) > VERSION_LENGTH) return false;
  // Parsed in place: a malformed list puts the saved catalog back.
  if (!reserve()) return false;
  uint16_t count = 0;
  planesCount = 0;
  if (!parsePlanes(lines, planes, count) || !count) {
    loadPlanes();
    return false;
  }
  planesCount = count;
  snprintf(version, sizeof(version), "%s", newVersion);
  // The records go to the card from the SD worker (writeCatalog).
  if (store()) queued = queueCatalog(lines, version);
  return true;
}

uint16_t planeCount() { return planesCount; }
const Plane *plane(uint16_t index) { return index < planesCount ? &planes[index] : nullptr; }
const Plane *findPlane(const char *id) {
  if (!id) return nullptr;
  for (uint16_t i = 0; i < planesCount; ++i) if (!strcmp(planes[i].id, id)) return &planes[i];
  return nullptr;
}
bool rememberHostedPlane(const char *id, const char *name, bool phenomenon) {
  if (!id || !name || strlen(id) != CARD_ID_LENGTH || !name[0] || strlen(name) > PLANE_NAME_LENGTH) return false;
  for (size_t i = 0; i < CARD_ID_LENGTH; ++i) if (!idChar(id[i])) return false;
  for (const char *c = name; *c; ++c) if (*c < 32 || *c > 126) return false;
  if (findPlane(id)) return true;
  String row = String(id) + "\t" + name + "\t" + (phenomenon ? "1\n" : "0\n");
  String lines;
  if (!lines.reserve((planesCount + 1) * (CARD_ID_LENGTH + PLANE_NAME_LENGTH + 4))) return false;
  const uint16_t keep = planesCount < MAX_PLANES ? planesCount : MAX_PLANES - 1;
  for (uint16_t i = 0; i < keep; ++i)
    lines += String(planes[i].id) + "\t" + planes[i].name + "\t" + (planes[i].phenomenon ? "1\n" : "0\n");
  lines += row;
  char nextVersion[VERSION_LENGTH + 1];
  snprintf(nextVersion, sizeof(nextVersion), "direct-%.8s", id);
  bool queued = false;
  return replacePlanes(lines.c_str(), nextVersion, queued);
}

const char *planesVersion() { return version; }

// --- plane cards ---------------------------------------------------------------

namespace {
TurnHubHistory::ArchiveFiles *injectedFiles = nullptr;
bool injectedFilesSet = false;
TurnHubHistory::ArchiveFiles *files() { return injectedFilesSet ? injectedFiles : TurnHubAtlas::sdArchiveFiles(); }

// The loop queues and reads; the SD worker writes. Every field below that both
// touch is read or changed inside a Guard, which never waits for the card.
#if defined(ESP_PLATFORM)
portMUX_TYPE cardsMux = portMUX_INITIALIZER_UNLOCKED;
struct Guard {
  Guard() { portENTER_CRITICAL(&cardsMux); }
  ~Guard() { portEXIT_CRITICAL(&cardsMux); }
  Guard(const Guard &) = delete;
  Guard &operator=(const Guard &) = delete;
};
#else
struct Guard {};
#endif

// The SD worker gives the card lock back after every write; without a pause it
// takes it again before the waiting loop runs, so a 20 s catalog save held the
// loop's own card reads for 15 s on the bench (2026-11-03). A tick lets the
// loop in between. Worker only.
void letLoopIn() {
#if defined(ESP_PLATFORM)
  vTaskDelay(1);
#endif
}

void *bigAlloc(size_t size) {
#if defined(ESP_PLATFORM)
  if (void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return p;
#endif
  return malloc(size);
}

constexpr char PLANE_FOLDER[] = "planes";

// Which planes have text and art on this mount, by file stem (planeFileStem).
// One listing of the planes folder fills it; the worker updates it after each
// write. In PSRAM: two entries per catalog plane is room for every file.
struct IndexEntry {
  uint32_t key;
  bool text, art;
};
constexpr uint16_t INDEX_CAPACITY = MAX_PLANES * 2;
IndexEntry *indexEntries = nullptr;
uint16_t indexCount = 0;
bool indexReady = false;
bool indexBuilt = false;  // indexGeneration is set.
uint32_t indexGeneration = 0;

uint32_t stemKey(const char *id) {
  uint32_t hash = 2166136261u;  // FNV-1a
  for (const char *c = id; *c; ++c) hash = (hash ^ static_cast<uint8_t>(*c)) * 16777619u;
  return hash;
}

// Inside a Guard.
IndexEntry *findEntry(uint32_t key) {
  for (uint16_t i = 0; i < indexCount; ++i) if (indexEntries[i].key == key) return &indexEntries[i];
  return nullptr;
}
void noteFile(uint32_t key, bool text, bool art, bool present) {
  Guard guard;
  if (!indexEntries) return;
  IndexEntry *e = findEntry(key);
  if (!e) {
    if (!present || indexCount >= INDEX_CAPACITY) return;
    e = &indexEntries[indexCount++];
    *e = IndexEntry{key, false, false};
  }
  if (text) e->text = present;
  if (art) e->art = present;
}

// "1a2b3c4d.txt" or ".jpg" -> its key and kind; false for anything else.
bool parseStem(const char *name, uint32_t &key, bool &text) {
  if (strlen(name) != 12 || name[8] != '.') return false;
  key = 0;
  for (int i = 0; i < 8; ++i) {
    const char c = name[i];
    const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (v < 0) return false;
    key = (key << 4) | static_cast<uint32_t>(v);
  }
  const char *ext = name + 9;
  if (!strcasecmp(ext, "txt")) text = true;
  else if (!strcasecmp(ext, "jpg")) text = false;
  else return false;
  return true;
}

// A JPEG is never written empty (text-only cards remove it), so a listed
// file is present.
void listed(const char *name, void *) {
  uint32_t key = 0;
  bool text = false;
  if (parseStem(name, key, text)) noteFile(key, text, !text, true);
}

String planePath(const char *id, const char *ext) {
  char stem[9];
  planeFileStem(id, stem);
  return String(PLANE_FOLDER) + "/" + stem + ext;
}

PlaneCard cached;
bool cachedValid = false;
uint32_t cardsWritten = 0;  // The worker bumps it; a cached card older than that is read again.
uint32_t cachedAt = 0;

bool cardText(const String &text, size_t max, bool lines) {
  if (text.length() > max) return false;
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    if ((c < 32 || c > 126) && !(lines && c == '\n')) return false;
  }
  return true;
}

bool writeFile(TurnHubHistory::ArchiveFiles *f, const String &name, const char *data, size_t size) {
  if (!f->remove(name.c_str())) return false;
  // In pieces, each under the card lock on its own, so the log and the record
  // store get the card between them.
  for (size_t at = 0; at < size; at += 4096) {
    const size_t n = size - at < 4096 ? size - at : 4096;
    if (!f->append(name.c_str(), data + at, n)) return false;
    letLoopIn();
  }
  return true;
}

// --- the queue: the loop fills a slot, the SD worker empties it ----------------

enum class JobState : uint8_t { Free, Filling, Ready, Writing };
enum class JobKind : uint8_t { Card, Catalog };
struct Job {
  JobState state = JobState::Free;
  JobKind kind = JobKind::Card;
  uint32_t sequence = 0;
  char id[CARD_ID_LENGTH + 1] = "";
  char version[VERSION_LENGTH + 1] = "";
  char *text = nullptr;  // The card's text file, or the catalog's lines.
  size_t textSize = 0;
  uint8_t *jpeg = nullptr;
  size_t jpegSize = 0;
  uint16_t index = 0, total = 0;
};
// A card's art is up to 46 KB from the app; three in flight keep the worker
// busy while the next upload arrives. The buffers are PSRAM, freed when written.
constexpr uint8_t JOB_SLOTS = 3;
Job jobs[JOB_SLOTS];
uint32_t nextSequence = 1;

char progressLine[48] = "";
bool progressWaiting = false;

void postProgress(const char *line) {
  Guard guard;
  snprintf(progressLine, sizeof(progressLine), "%s", line);
  progressWaiting = true;
}

Job *reserveJob() {
  Guard guard;
  for (Job &job : jobs) {
    if (job.state != JobState::Free) continue;
    job.state = JobState::Filling;
    return &job;
  }
  return nullptr;
}
void releaseJob(Job &job) {
  free(job.text);
  free(job.jpeg);
  Guard guard;
  job = Job{};
}
void publishJob(Job &job) {
  {
    Guard guard;
    job.sequence = nextSequence++;
    job.state = JobState::Ready;
  }
  TurnHubAtlas::wakeSdWorker();
}
Job *takeJob() {
  Guard guard;
  Job *first = nullptr;
  for (Job &job : jobs)
    if (job.state == JobState::Ready && (!first || job.sequence < first->sequence)) first = &job;
  if (first) first->state = JobState::Writing;
  return first;
}

// The old layout ("pc-<id>.txt/.jpg" beside the records, long names): the
// worker removes them, one per pass, after listing them once per mount.
constexpr uint16_t LEGACY_CAPACITY = 512;
constexpr size_t LEGACY_NAME = 48;
char (*legacyNames)[LEGACY_NAME] = nullptr;
uint16_t legacyCount = 0, legacyNext = 0, legacyRemoved = 0;

void listedLegacy(const char *name, void *) {
  if (strncmp(name, "pc-", 3) || strlen(name) >= LEGACY_NAME || legacyCount >= LEGACY_CAPACITY) return;
  snprintf(legacyNames[legacyCount++], LEGACY_NAME, "%s", name);
}

// From the worker, for a new mount: list the planes folder once, and note
// the old layout's files. Logs how long the card took, to compare a listing
// with single lookups (one lookup took about 320 ms in the old folder).
void buildIndex(TurnHubHistory::ArchiveFiles *f) {
  if (!indexEntries) indexEntries = static_cast<IndexEntry *>(bigAlloc(sizeof(IndexEntry) * INDEX_CAPACITY));
  {
    Guard guard;
    indexReady = false;
    indexCount = 0;
  }
  if (!indexEntries) return;
  const uint32_t started = millis();
  f->makeFolder(PLANE_FOLDER);
  const bool listedFolder = f->list(PLANE_FOLDER, listed, nullptr);
  const uint32_t listMs = millis() - started;
  unsigned text = 0, art = 0;
  char sample[13] = "";
  {
    Guard guard;
    for (uint16_t i = 0; i < indexCount; ++i) {
      text += indexEntries[i].text;
      art += indexEntries[i].art;
      if (!sample[0] && indexEntries[i].text)
        snprintf(sample, sizeof(sample), "%08lx.txt", static_cast<unsigned long>(indexEntries[i].key));
    }
    indexReady = listedFolder;
  }
  // The old layout, listed once; one lookup there and one in the planes
  // folder are timed for the record.
  if (!legacyNames) legacyNames = static_cast<char (*)[LEGACY_NAME]>(bigAlloc(LEGACY_CAPACITY * LEGACY_NAME));
  legacyCount = legacyNext = legacyRemoved = 0;
  uint32_t folderMs = 0, legacyStatMs = 0, statMs = 0;
  size_t size = 0;
  if (legacyNames) {
    const uint32_t at = millis();
    f->list("", listedLegacy, nullptr);
    folderMs = millis() - at;
    if (legacyCount) {
      const uint32_t statAt = millis();
      f->size(legacyNames[0], size);
      legacyStatMs = millis() - statAt;
    }
  }
  if (sample[0]) {
    const uint32_t statAt = millis();
    f->size((String(PLANE_FOLDER) + "/" + sample).c_str(), size);
    statMs = millis() - statAt;
  }
  TurnHub::serialLog.printf(
      "ATLAS|PLANES|INDEX|%s|text=%u|art=%u|listMs=%lu|statMs=%lu|legacy=%u|folderListMs=%lu|legacyStatMs=%lu\n",
      listedFolder ? "READY" : "NO_FOLDER", text, art, static_cast<unsigned long>(listMs),
      static_cast<unsigned long>(statMs), static_cast<unsigned>(legacyCount), static_cast<unsigned long>(folderMs),
      static_cast<unsigned long>(legacyStatMs));
}

void writeCard(TurnHubHistory::ArchiveFiles *f, Job &job) {
  const uint32_t key = stemKey(job.id);
  const uint32_t started = millis();
  bool ok;
  if (job.jpegSize) ok = writeFile(f, planePath(job.id, ".jpg"), reinterpret_cast<const char *>(job.jpeg), job.jpegSize);
  else ok = f->remove(planePath(job.id, ".jpg").c_str());
  if (ok) noteFile(key, false, true, job.jpegSize > 0);
  if (ok) ok = writeFile(f, planePath(job.id, ".txt"), job.text, job.textSize);
  noteFile(key, true, false, ok);
  {
    Guard guard;
    ++cardsWritten;
  }
  char line[48];
  if (!ok) snprintf(line, sizeof(line), "Couldn't save a plane card");
  else if (job.total > 0 && job.index >= job.total) snprintf(line, sizeof(line), "All %u plane cards saved", job.total);
  else if (job.total > 0) snprintf(line, sizeof(line), "Plane cards: %u of %u", job.index, job.total);
  else snprintf(line, sizeof(line), "Plane card saved");
  postProgress(line);
  TurnHub::serialLog.printf("ATLAS|PLANES|CARD|%s|%u/%u|art=%u|ms=%lu\n", ok ? "SAVED" : "FAILED", job.index, job.total,
      static_cast<unsigned>(job.jpegSize), static_cast<unsigned long>(millis() - started));
}

// The catalog as text in records of up to CHUNK bytes: "pl0".."pl<n-1>",
// then "plv" = "<n>\t<version>" (loadPlanes reads them back).
void writeCatalog(Job &job) {
  TurnHubStorage::BlobStore *s = store();
  const uint32_t started = millis();
  bool saved = false;
  if (s) {
    uint8_t chunk = 0;
    const char *start = job.text;
    bool ok = true;
    // Whole lines per record, so a record never splits a plane.
    while (ok && *start && chunk < MAX_CHUNKS) {
      const char *end = start;
      const char *cut = start;
      while (*end && static_cast<size_t>(end - start) < CHUNK) {
        if (*end == '\n') cut = end + 1;
        ++end;
      }
      if (!*end) cut = end;
      if (cut == start) {  // A line longer than a record (can't happen for valid lines).
        ok = false;
        break;
      }
      char key[8];
      chunkKey(key, sizeof(key), chunk++);
      ok = s->write(key, start, cut - start) == Status::Ok;
      letLoopIn();
      start = cut;
    }
    if (ok && !*start) {
      char meta[8 + VERSION_LENGTH + 1];
      const int length = snprintf(meta, sizeof(meta), "%u\t%s", static_cast<unsigned>(chunk), job.version);
      saved = s->write("plv", meta, static_cast<size_t>(length)) == Status::Ok;
    }
  }
  TurnHub::serialLog.printf("ATLAS|PLANES|CATALOG|%s|ms=%lu\n", saved ? "SAVED" : "FAILED",
      static_cast<unsigned long>(millis() - started));
}
}  // namespace

bool queueCatalog(const char *lines, const char *catalogVersion) {
  Job *job = reserveJob();
  if (!job) return false;
  const size_t size = strlen(lines);
  job->kind = JobKind::Catalog;
  job->text = static_cast<char *>(bigAlloc(size + 1));
  if (!job->text) {
    releaseJob(*job);
    return false;
  }
  memcpy(job->text, lines, size + 1);
  job->textSize = size;
  snprintf(job->version, sizeof(job->version), "%s", catalogVersion);
  publishJob(*job);
  return true;
}

void planeFileStem(const char *id, char (&stem)[9]) {
  snprintf(stem, sizeof(stem), "%08lx", static_cast<unsigned long>(stemKey(id ? id : "")));
}

void setCardFilesForTests(TurnHubHistory::ArchiveFiles *f) {
  injectedFiles = f;
  injectedFilesSet = true;
  cachedValid = false;
  for (Job &job : jobs) if (job.state != JobState::Free) releaseJob(job);
  legacyCount = legacyNext = legacyRemoved = 0;
  Guard guard;
  indexReady = indexBuilt = false;
  indexCount = 0;
  progressWaiting = false;
}

CardQueue savePlaneCard(const char *id, const String &type, const String &artist, const String &text, const String &chaos,
    const uint8_t *jpeg, size_t jpegSize, uint16_t index, uint16_t total) {
  if (!id || !findPlane(id) || !cardText(type, PLANE_TYPE_LENGTH, false) || !cardText(artist, PLANE_ARTIST_LENGTH, false) ||
      !cardText(text, PLANE_TEXT_LENGTH, true) || !cardText(chaos, PLANE_CHAOS_LENGTH, true) || jpegSize > MAX_PLANE_ART_BYTES)
    return CardQueue::Invalid;
  // A baseline JPEG starts FF D8 and ends FF D9.
  if (jpegSize && (jpegSize < 4 || !jpeg || jpeg[0] != 0xFF || jpeg[1] != 0xD8 || jpeg[jpegSize - 2] != 0xFF ||
                   jpeg[jpegSize - 1] != 0xD9))
    return CardQueue::Invalid;
  if (!files()) return CardQueue::NoCard;
  if (!planeIndexReady()) return CardQueue::Busy;
  Job *job = reserveJob();
  if (!job) return CardQueue::Busy;
  // type, artist, chaos (newlines as tabs), then the text.
  String chaosLine = chaos;
  chaosLine.replace("\n", "\t");
  const String body = type + "\n" + artist + "\n" + chaosLine + "\n" + text;
  job->kind = JobKind::Card;
  snprintf(job->id, sizeof(job->id), "%s", id);
  job->text = static_cast<char *>(bigAlloc(body.length() + 1));
  job->jpeg = jpegSize ? static_cast<uint8_t *>(bigAlloc(jpegSize)) : nullptr;
  if (!job->text || (jpegSize && !job->jpeg)) {
    releaseJob(*job);
    return CardQueue::Busy;
  }
  memcpy(job->text, body.c_str(), body.length() + 1);
  job->textSize = body.length();
  if (jpegSize) memcpy(job->jpeg, jpeg, jpegSize);
  job->jpegSize = jpegSize;
  job->index = index;
  job->total = total;
  publishJob(*job);
  return CardQueue::Queued;
}

bool planeIndexReady() {
  Guard guard;
  return indexReady;
}

bool planeIndexPending() { return files() && !planeIndexReady(); }

bool hasPlaneCard(const char *id, bool &art) {
  art = false;
  if (!id) return false;
  const uint32_t key = stemKey(id);
  Guard guard;
  if (!indexReady) return false;
  const IndexEntry *e = findEntry(key);
  if (!e || !e->text) return false;
  art = e->art;
  return true;
}

bool servicePlaneStorage(uint32_t cardGeneration) {
  TurnHubHistory::ArchiveFiles *f = files();
  if (!f) {
    Guard guard;
    indexReady = indexBuilt = false;
  } else if (!indexBuilt || indexGeneration != cardGeneration) {
    buildIndex(f);
    indexBuilt = true;
    indexGeneration = cardGeneration;
    return true;
  }
  if (Job *job = takeJob()) {
    // The catalog needs only the record store; a card the files too.
    if (job->kind == JobKind::Catalog) writeCatalog(*job);
    else if (f) writeCard(f, *job);
    else postProgress("Couldn't save a plane card");
    releaseJob(*job);
    return true;
  }
  if (f && legacyNames && legacyNext < legacyCount) {
    if (f->remove(legacyNames[legacyNext])) ++legacyRemoved;
    letLoopIn();
    if (++legacyNext == legacyCount)
      TurnHub::serialLog.printf("ATLAS|PLANES|LEGACY|REMOVED|%u\n", static_cast<unsigned>(legacyRemoved));
    return true;
  }
  return false;
}

bool takePlaneProgress(char *line, size_t size) {
  Guard guard;
  if (!progressWaiting) return false;
  progressWaiting = false;
  snprintf(line, size, "%s", progressLine);
  return true;
}

uint8_t pendingPlaneJobs() {
  Guard guard;
  uint8_t pending = 0;
  for (const Job &job : jobs) pending += job.state != JobState::Free;
  return pending;
}

const PlaneCard *planeCard(const char *id) {
  if (!id || !id[0]) return nullptr;
  bool art = false;
  if (!hasPlaneCard(id, art)) return nullptr;
  uint32_t written;
  {
    Guard guard;
    written = cardsWritten;
  }
  if (cachedValid && cachedAt == written && !strcmp(cached.id, id)) return &cached;
  TurnHubHistory::ArchiveFiles *f = files();
  if (!f) return nullptr;
  char body[PLANE_TYPE_LENGTH + PLANE_ARTIST_LENGTH + PLANE_CHAOS_LENGTH + PLANE_TEXT_LENGTH + 8];
  size_t got = 0;
  if (!f->readAt(planePath(id, ".txt").c_str(), 0, body, sizeof(body) - 1, got) || !got) return nullptr;
  body[got] = '\0';
  PlaneCard card;
  snprintf(card.id, sizeof(card.id), "%s", id);
  char *fields[4] = {body, nullptr, nullptr, nullptr};
  for (int i = 1; i < 4; ++i) {
    char *nl = strchr(fields[i - 1], '\n');
    if (!nl) return nullptr;
    *nl = '\0';
    fields[i] = nl + 1;
  }
  for (char *c = fields[2]; *c; ++c) if (*c == '\t') *c = '\n';
  snprintf(card.type, sizeof(card.type), "%s", fields[0]);
  snprintf(card.artist, sizeof(card.artist), "%s", fields[1]);
  snprintf(card.chaos, sizeof(card.chaos), "%s", fields[2]);
  snprintf(card.text, sizeof(card.text), "%s", fields[3]);
  card.art = art;
  cached = card;
  cachedValid = true;
  cachedAt = written;
  return &cached;
}

bool readPlaneArt(const char *id, uint8_t *out, size_t capacity, size_t &size) {
  size = 0;
  bool art = false;
  if (!id || !hasPlaneCard(id, art) || !art) return false;
  TurnHubHistory::ArchiveFiles *f = files();
  const String name = planePath(id, ".jpg");
  size_t total = 0;
  if (!f || !f->size(name.c_str(), total) || !total || total > capacity) return false;
  while (size < total) {
    size_t got = 0;
    if (!f->readAt(name.c_str(), size, reinterpret_cast<char *>(out) + size, total - size, got) || !got)
      return false;
    size += got;
  }
  return true;
}

bool validCommander(const String &name, const String &art) {
  if (!printable(name, COMMANDER_NAME_LENGTH) || art.length() > COMMANDER_ART_LENGTH) return false;
  for (size_t i = 0; i < art.length(); ++i) {
    const char c = art[i];
    if (!idChar(c) && c != ':' && c != '.' && c != '_') return false;
  }
  return true;
}

// "d<profileId>": lines "name<TAB>art", newest first.
bool loadCommanders(const String &profileId, SavedCommanders &out) {
  out = SavedCommanders{};
  TurnHubStorage::BlobStore *s = store();
  if (!s || profileId.length() != 8) return false;
  char text[COMMANDER_SLOTS * (COMMANDER_NAME_LENGTH + COMMANDER_ART_LENGTH + 2) + 1];
  size_t size = 0;
  const Status status = s->read(commanderKey(profileId).c_str(), text, sizeof(text) - 1, size);
  if (status == Status::NotFound) return true;
  if (status != Status::Ok) return false;
  text[size] = '\0';
  char *line = text;
  while (*line && out.count < COMMANDER_SLOTS) {
    char *end = strchr(line, '\n');
    if (end) *end = '\0';
    char *tab = strchr(line, '\t');
    if (tab) {
      *tab = '\0';
      if (validCommander(String(line), String(tab + 1))) {
        Commander &c = out.slots[out.count++];
        snprintf(c.name, sizeof(c.name), "%s", line);
        snprintf(c.art, sizeof(c.art), "%s", tab + 1);
      }
    }
    if (!end) break;
    line = end + 1;
  }
  return true;
}

namespace {
bool saveCommanders(const String &profileId, const SavedCommanders &saved) {
  TurnHubStorage::BlobStore *s = store();
  if (!s) return false;
  String text;
  for (uint8_t i = 0; i < saved.count; ++i) {
    text += saved.slots[i].name; text += '\t'; text += saved.slots[i].art; text += '\n';
  }
  if (!text.length()) {
    const Status removed = s->remove(commanderKey(profileId).c_str());
    return removed == Status::Ok || removed == Status::NotFound;
  }
  return s->write(commanderKey(profileId).c_str(), text.c_str(), text.length()) == Status::Ok;
}
}  // namespace

uint8_t rememberCommander(const String &profileId, const String &name, const String &art) {
  SavedCommanders saved;
  if (!validCommander(name, art) || !loadCommanders(profileId, saved)) return 0;
  SavedCommanders next;
  Commander &first = next.slots[next.count++];
  snprintf(first.name, sizeof(first.name), "%s", name.c_str());
  snprintf(first.art, sizeof(first.art), "%s", art.c_str());
  for (uint8_t i = 0; i < saved.count && next.count < COMMANDER_SLOTS; ++i)
    if (!String(saved.slots[i].name).equalsIgnoreCase(name)) next.slots[next.count++] = saved.slots[i];
  return saveCommanders(profileId, next) ? 1 : 0;
}

bool forgetCommander(const String &profileId, uint8_t slot) {
  SavedCommanders saved;
  if (!loadCommanders(profileId, saved) || slot < 1 || slot > saved.count) return false;
  for (uint8_t i = slot; i < saved.count; ++i) saved.slots[i - 1] = saved.slots[i];
  --saved.count;
  return saveCommanders(profileId, saved);
}

}  // namespace TurnHubCards
