#include "card_cache.h"

#include <stdlib.h>
#include <string.h>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

#include "sd_card.h"
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

bool replacePlanes(const char *lines, const char *newVersion, bool &saved) {
  saved = false;
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
  TurnHubStorage::BlobStore *s = store();
  if (!s) return true;
  // Whole lines per record, so a record never splits a plane.
  uint8_t chunk = 0;
  const char *start = lines;
  while (*start && chunk < MAX_CHUNKS) {
    const char *end = start;
    const char *cut = start;
    while (*end && static_cast<size_t>(end - start) < CHUNK) {
      if (*end == '\n') cut = end + 1;
      ++end;
    }
    if (!*end) cut = end;
    if (cut == start) return true;  // A line longer than a record (can't happen for valid lines).
    char key[8];
    chunkKey(key, sizeof(key), chunk++);
    if (s->write(key, start, cut - start) != Status::Ok) return true;
    start = cut;
  }
  if (*start) return true;
  char meta[8 + VERSION_LENGTH + 1];
  const int length = snprintf(meta, sizeof(meta), "%u\t%s", static_cast<unsigned>(chunk), version);
  saved = s->write("plv", meta, static_cast<size_t>(length)) == Status::Ok;
  return true;
}

uint16_t planeCount() { return planesCount; }
const Plane *plane(uint16_t index) { return index < planesCount ? &planes[index] : nullptr; }
const Plane *findPlane(const char *id) {
  if (!id) return nullptr;
  for (uint16_t i = 0; i < planesCount; ++i) if (!strcmp(planes[i].id, id)) return &planes[i];
  return nullptr;
}
const char *planesVersion() { return version; }

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
