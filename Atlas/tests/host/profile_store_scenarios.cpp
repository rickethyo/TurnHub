#include <cassert>
#include <iostream>
#include "profile_store.h"
#include "account_access.h"
#include <nvs.h>

namespace FakeNvs {
std::map<std::string, std::vector<uint8_t>> blobs;
std::map<std::string, std::string> strings;
std::map<std::string, uint8_t> bytes;
int openError = ESP_OK, readError = ESP_OK, setError = ESP_OK, commitError = ESP_OK;
int writes = 0, commits = 0, reads = 0, failReadAt = 0;
const char *openedNamespace = nullptr;
}

using namespace TurnHubProfiles;
static String hashPin(const String &, const String &) { return String(std::string(64, 'a')); }

static void guestLookups() {
  // Exercise the actual store used by display sync, HTTP polling and gameplay.
  // Repeated reconnects used to create two durable records on each cycle.
  const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
  for (int reconnect = 0; reconnect < 100; ++reconnect) {
    assert(resetTransientSeatBindings(mac));
    for (uint8_t slot : {1, 2}) {
      assert(profileIdForSeat(mac, slot).length() == 0);
      assert(boundProfileIdForSeat(mac, slot).length() == 0);
      assert(nameForSeat(mac, slot).length() == 0);
      assert(!hasPinForSeat(mac, slot));
      ProfileStats stats;
      assert(!loadStatsForSeat(mac, slot, stats));
      assert(!setNameForSeat(mac, slot, "Unbound"));
      assert(!setPinHashForSeat(mac, slot, hashPin("", "")));
      assert(!saveStatsForSeat(mac, slot, stats));
    }
  }
  assert(FakeNvs::bytes.empty() && FakeNvs::strings.empty() && FakeNvs::blobs.empty());
  assert(FakeNvs::writes == 0);
}

static void existingAccounts() {
  const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
  const String id = createProfileWithCredentials("Owner", "1234", hashPin);
  assert(id.length() == 8);
  ProfileStats stats; stats.gamesPlayed = 3;
  assert(saveStatsForProfile(id, stats));
  assert(bindSeatToProfile(mac, 1, id));
  assert(bindSeatToProfile(mac, 2, id));
  assert(setSeatPersistent(mac, 1, true));
  assert(resetTransientSeatBindings(mac));
  assert(profileIdForSeat(mac, 1) == id && profileIdForSeat(mac, 2).length() == 0);
  assert(nameForSeat(mac, 1) == "Owner" && hasPinForSeat(mac, 1));
  assert(setSeatPersistent(mac, 1, false));
  assert(resetTransientSeatBindings(mac));
  const int writes = FakeNvs::writes;
  assert(profileIdForSeat(mac, 1).length() == 0);
  assert(profileExists(id) && nameForProfile(id) == "Owner" && hasPinForProfile(id));
  assert(loadStatsForProfile(id, stats) && stats.gamesPlayed == 3);
  assert(FakeNvs::writes == writes);
}

static void legacyPlaceholders() {
  // More empty markers than the list limit must not hide later real accounts
  // or block registration. Filtering must happen while iterating NVS.
  FakeNvs::reset();
  for (unsigned i = 0; i < MAX_LOGIN_PROFILES + 10; ++i) {
    char key[10]; snprintf(key, sizeof(key), "m%08X", i);
    FakeNvs::bytes[key] = 1;
  }
  const auto markers = FakeNvs::bytes;
  char ids[MAX_LOGIN_PROFILES][9];
  assert(listProfileIds(ids, MAX_LOGIN_PROFILES) == 0);
  assert(FakeNvs::bytes == markers && FakeNvs::writes == 0);
  const String id = createProfileWithCredentials("Real account", "1234", hashPin);
  assert(id.length() == 8 && listProfileIds(ids, MAX_LOGIN_PROFILES) == 1);
  assert(String(ids[0]) == id);
  // Preserve every kind of user data, including unreadable/future blobs.
  FakeNvs::strings["n00000000"] = "Legacy name";
  FakeNvs::strings["p00000001"] = hashPin("", "");
  FakeNvs::blobs["s00000002"] = {255};
  FakeNvs::blobs["a00000003"] = {255};
  FakeNvs::blobs["u00000004"] = {255};
  assert(listProfileIds(ids, MAX_LOGIN_PROFILES) == 6);
  assert(listProfileIds(ids, 2) == 2);
  assert(profileExists("00000005")); // Hidden is not deleted.
  // An old bound placeholder remains available for physical recovery.
  const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
  assert(bindSeatToProfile(mac, 1, "00000005"));
  assert(profileIdForSeat(mac, 1) == "00000005");
  assert(setNameForSeat(mac, 1, "Recovered"));
  assert(listProfileIds(ids, MAX_LOGIN_PROFILES) == 7);
}

int main() {
  assert(begin());
  guestLookups();
  existingAccounts();
  legacyPlaceholders();
  std::cout << "PASS: real profile store guest lookups, reconnects, saved bindings and legacy placeholder filtering\n";
}
