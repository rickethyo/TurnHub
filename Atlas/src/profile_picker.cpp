// Sigil profile picker: list, pages and choices. See profile_picker.h.
#include "profile_picker.h"

#include <string.h>

#include "atlas_app.h"
#include "profile_store.h"
#include "serial_log.h"
#include "sigil_menu.h"
#include "web_api.h"

using TurnHub::serialLog;

namespace TurnHubAtlas {

using TurnHubProtocol::PickerItem;
using TurnHubProtocol::PickerKeyCode;
using TurnHubProtocol::PickerMode;
using TurnHubProtocol::PickerNotice;
using TurnHubProtocol::ProfilePickerPacket;
using TurnHubProtocol::PICKER_PAGE_ITEMS;

namespace {

constexpr size_t ID_SIZE = TurnHubProfiles::PROFILE_ID_LENGTH + 1;
constexpr size_t MAX_ENTRIES = TurnHubProfiles::MAX_LOGIN_PROFILES + 1;  // + Guest.

struct PickerCache {
  bool open = false;
  bool sent = false;           // The Sigil has the current page (or Closed).
  PickerMode mode = PickerMode::Closed;
  PickerNotice notice = PickerNotice::None;
  uint8_t page = 0;
  uint8_t pageCount = 1;
  uint8_t revision = 0;
  uint32_t lastKeyMs = 0;
  // The page on screen: profile IDs by item ("" = Guest), and what was sent.
  char ids[PICKER_PAGE_ITEMS][ID_SIZE] = {};
  PickerItem items[PICKER_PAGE_ITEMS] = {};
  uint8_t itemCount = 0;
};

PickerCache pickers[MAX_PHYSICAL_SIGILS];

struct Entry {
  char id[ID_SIZE];
  String name;
  uint8_t flags;
};

void safeName(const String &name, char (&out)[TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH + 1]) {
  size_t length = 0;
  for (size_t i = 0; i < name.length() && length < TurnHubProtocol::DISPLAY_NAME_MAX_LENGTH; ++i) {
    const uint8_t c = static_cast<uint8_t>(name[i]);
    out[length++] = c >= 0x20 && c <= 0x7E ? static_cast<char>(c) : '?';
  }
  out[length] = '\0';
}

bool lessByName(const Entry &a, const Entry &b) {
  const int byName = strcasecmp(a.name.c_str(), b.name.c_str());
  return byName != 0 ? byName < 0 : strcmp(a.id, b.id) < 0;
}

// Guest, then every profile a player could pick here, by name. Blocked
// (archived or moderated) profiles and ones already on a physical Sigil are
// left out; ones needing a phone sign-in are shown locked, so the owner
// learns why rather than wondering where their name went.
size_t buildEntries(Entry *entries) {
  size_t count = 0;
  entries[count].id[0] = '\0';
  entries[count].name = "Guest";
  entries[count].flags = TurnHubProtocol::PICKER_ITEM_GUEST;
  ++count;
  static char ids[TurnHubProfiles::MAX_LOGIN_PROFILES][ID_SIZE];
  const size_t listed = TurnHubProfiles::listProfileIds(ids, TurnHubProfiles::MAX_LOGIN_PROFILES);
  for (size_t i = 0; i < listed; ++i) {
    const String id(ids[i]);
    if (TurnHubWebApi::connectionBlocked(id)) continue;
    uint8_t controller = INVALID_ID, slot = 1;
    const bool playing = resolveProfileParticipant(id, controller, slot);
    if (playing && controller < MAX_PHYSICAL_SIGILS) continue;
    Entry &entry = entries[count];
    memcpy(entry.id, ids[i], ID_SIZE);
    entry.name = TurnHubProfiles::nameForProfile(id);
    if (!entry.name.length()) entry.name = "Profile";
    entry.flags = (playing ? TurnHubProtocol::PICKER_ITEM_PLAYING : 0) |
        (TurnHubWebApi::physicalUseAllowed(id) ? 0 : TurnHubProtocol::PICKER_ITEM_LOCKED);
    ++count;
  }
  // Insertion sort keeps Guest first; at most 64 names.
  for (size_t i = 2; i < count; ++i) {
    for (size_t j = i; j > 1 && lessByName(entries[j], entries[j - 1]); --j) {
      Entry swap = entries[j];
      entries[j] = entries[j - 1];
      entries[j - 1] = swap;
    }
  }
  return count;
}

void changed(PickerCache &cache) {
  cache.revision = static_cast<uint8_t>(cache.revision + 1);
  cache.sent = false;
}

// Rebuilds the List page (clamping it if names went away) from fresh data.
void showList(PickerCache &cache, PickerNotice notice) {
  static Entry entries[MAX_ENTRIES];
  const size_t count = buildEntries(entries);
  cache.pageCount = static_cast<uint8_t>((count + PICKER_PAGE_ITEMS - 1) / PICKER_PAGE_ITEMS);
  if (cache.page >= cache.pageCount) cache.page = cache.pageCount - 1;
  cache.mode = PickerMode::List;
  cache.notice = notice;
  cache.itemCount = 0;
  memset(cache.ids, 0, sizeof(cache.ids));
  memset(cache.items, 0, sizeof(cache.items));
  for (size_t i = static_cast<size_t>(cache.page) * PICKER_PAGE_ITEMS;
       i < count && cache.itemCount < PICKER_PAGE_ITEMS; ++i) {
    memcpy(cache.ids[cache.itemCount], entries[i].id, ID_SIZE);
    cache.items[cache.itemCount].flags = entries[i].flags;
    safeName(entries[i].name, cache.items[cache.itemCount].name);
    ++cache.itemCount;
  }
  changed(cache);
}

void showConfirm(PickerCache &cache, uint8_t item) {
  memcpy(cache.ids[0], cache.ids[item], ID_SIZE);
  cache.items[0] = cache.items[item];
  for (uint8_t i = 1; i < PICKER_PAGE_ITEMS; ++i) {
    cache.ids[i][0] = '\0';
    cache.items[i] = PickerItem{};
  }
  cache.itemCount = 1;
  cache.mode = PickerMode::Confirm;
  cache.notice = PickerNotice::None;
  changed(cache);
}

void closePicker(uint8_t sigilId, const char *reason) {
  PickerCache &cache = pickers[sigilId];
  if (!cache.open) return;
  const uint8_t revision = cache.revision;
  cache = PickerCache{};
  cache.revision = static_cast<uint8_t>(revision + 1);
  serialLog.print("ATLAS|PICKER|CLOSE|");
  serialLog.print(sigilId);
  serialLog.print("|");
  serialLog.println(reason);
}

PickerNotice noticeFor(const IntentResult &result) {
  switch (result.status) {
    case IntentStatus::Unauthorized: return PickerNotice::NeedsPhone;
    case IntentStatus::Conflict:
    case IntentStatus::InvalidActor: return PickerNotice::Unavailable;
    default: return PickerNotice::Failed;
  }
}

void finish(uint8_t sigilId, const IntentResult &result) {
  if (result.accepted()) {
    closePicker(sigilId, "JOINED");
    invalidateSigilMenu(sigilId);
    return;
  }
  serialLog.print("ATLAS|PICKER|REJECTED|");
  serialLog.print(sigilId);
  serialLog.print("|");
  serialLog.println(result.message);
  showList(pickers[sigilId], noticeFor(result));
}

void chooseItem(uint8_t sigilId, uint8_t item) {
  PickerCache &cache = pickers[sigilId];
  if (item >= cache.itemCount) return;
  if (cache.items[item].flags & TurnHubProtocol::PICKER_ITEM_GUEST) {
    serialLog.print("ATLAS|PICKER|GUEST|");
    serialLog.println(sigilId);
    finish(sigilId, dispatchModuleIntent(IntentType::Join, sigilId, 1));
    return;
  }
  if (cache.items[item].flags & TurnHubProtocol::PICKER_ITEM_LOCKED) {
    // Say why now; the handler would refuse it the same way.
    showList(cache, PickerNotice::NeedsPhone);
    return;
  }
  showConfirm(cache, item);
}

void confirmChoice(uint8_t sigilId) {
  PickerCache &cache = pickers[sigilId];
  Intent intent;
  intent.type = IntentType::PickProfile;
  intent.actor.origin = IntentOrigin::PhysicalSigil;
  intent.actor.controllerId = sigilId;
  intent.actor.slot = 1;
  strncpy(intent.payload.profileId, cache.ids[0], sizeof(intent.payload.profileId) - 1);
  serialLog.print("ATLAS|PICKER|PICK|");
  serialLog.print(sigilId);
  serialLog.print("|PROFILE|");
  serialLog.println(cache.ids[0]);
  finish(sigilId, intents.dispatch(intent));
}

}  // namespace

bool pickerSigil(uint8_t sigilId) {
  const TurnHub::SigilRecord *record = sigilBus.record(sigilId);
  if (record == nullptr || !record->helloInfoValid) return false;
  const uint8_t caps = record->capabilities;
  return (caps & TurnHubProtocol::CAPABILITY_MENU) != 0 &&
      (caps & TurnHubProtocol::CAPABILITY_HARNESS) == 0 &&
      TurnHubProtocol::pickerFirmware(record->firmwareMajor, record->firmwareMinor);
}

bool pickerOpen(uint8_t sigilId) {
  return sigilId < MAX_PHYSICAL_SIGILS && pickers[sigilId].open;
}

void openProfilePicker(uint8_t sigilId, uint32_t nowMs) {
  if (sigilId >= MAX_PHYSICAL_SIGILS || !pickerSigil(sigilId)) return;
  PickerCache &cache = pickers[sigilId];
  cache.open = true;
  cache.page = 0;
  cache.lastKeyMs = nowMs;
  showList(cache, PickerNotice::None);
  serialLog.print("ATLAS|PICKER|OPEN|");
  serialLog.print(sigilId);
  serialLog.print("|PAGES|");
  serialLog.println(cache.pageCount);
}

void handlePickerKey(uint8_t sigilId, int32_t value, uint32_t nowMs) {
  if (!pickerOpen(sigilId)) {
    invalidateProfilePicker(sigilId);  // It still shows a page: tell it Closed.
    return;
  }
  PickerCache &cache = pickers[sigilId];
  const uint8_t key = TurnHubProtocol::pickerKeyCode(value);
  if (TurnHubProtocol::pickerKeyRevision(value) != cache.revision ||
      key >= static_cast<uint8_t>(PickerKeyCode::Count)) {
    // Pressed on a page the player no longer sees: resend, never guess.
    cache.sent = false;
    return;
  }
  cache.lastKeyMs = nowMs;
  const auto code = static_cast<PickerKeyCode>(key);
  if (cache.mode == PickerMode::Confirm) {
    if (code == PickerKeyCode::Select) confirmChoice(sigilId);
    else if (code == PickerKeyCode::Left) showList(cache, PickerNotice::None);
    return;
  }
  switch (code) {
    case PickerKeyCode::Up: chooseItem(sigilId, 0); break;
    case PickerKeyCode::Right: chooseItem(sigilId, 1); break;
    case PickerKeyCode::Down: chooseItem(sigilId, 2); break;
    case PickerKeyCode::Left:
      if (cache.page == 0) {
        closePicker(sigilId, "CANCEL");
        invalidateSigilMenu(sigilId);
      } else {
        --cache.page;
        showList(cache, PickerNotice::None);
      }
      break;
    case PickerKeyCode::Select:
      if (cache.pageCount > 1) {
        cache.page = static_cast<uint8_t>((cache.page + 1) % cache.pageCount);
        showList(cache, PickerNotice::None);
      }
      break;
    default: break;
  }
}

ProfilePickerPacket profilePickerPage(uint8_t sigilId) {
  ProfilePickerPacket packet{};
  packet.version = TurnHubProtocol::VERSION;
  packet.type = TurnHubProtocol::PacketType::ProfilePicker;
  packet.sigilId = sigilId;
  packet.pageCount = 1;
  if (sigilId >= MAX_PHYSICAL_SIGILS) return packet;
  const PickerCache &cache = pickers[sigilId];
  packet.revision = cache.revision;
  packet.mode = cache.open ? cache.mode : PickerMode::Closed;
  if (!cache.open) return packet;
  packet.notice = cache.notice;
  packet.page = cache.page;
  packet.pageCount = cache.pageCount;
  packet.itemCount = cache.itemCount;
  memcpy(packet.items, cache.items, sizeof(packet.items));
  return packet;
}

void syncProfilePickers(uint32_t nowMs) {
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    PickerCache &cache = pickers[id];
    if (cache.open) {
      if (hubState != HubState::Lobby) closePicker(id, "NOT_LOBBY");
      else if (lobby.isJoined(id)) closePicker(id, "JOINED_ELSEWHERE");
      else if (!sigilBus.isOnline(id, nowMs) || !pickerSigil(id)) closePicker(id, "OFFLINE");
      else if (nowMs - cache.lastKeyMs >= PICKER_IDLE_MS) closePicker(id, "IDLE");
    }
    if (cache.sent || !sigilBus.isOnline(id, nowMs) || !pickerSigil(id)) continue;
    if (sigilBus.sendProfilePicker(profilePickerPage(id))) cache.sent = true;
  }
}

void invalidateProfilePicker(uint8_t sigilId) {
  if (sigilId < MAX_PHYSICAL_SIGILS) pickers[sigilId].sent = false;
}

void resetProfilePickers() {
  for (auto &cache : pickers) cache = PickerCache{};
}

}  // namespace TurnHubAtlas
