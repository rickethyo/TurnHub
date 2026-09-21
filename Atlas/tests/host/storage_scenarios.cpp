#include <cassert>
#include <cstring>
#include <iostream>
#include <type_traits>
#include "identity.h"
#include "nvs_blob_store.h"
#include "profile_stats_storage.h"

using namespace TurnHubStorage;
using namespace TurnHubProfiles;
constexpr char key[] = "sAB12CD34";

namespace FakeNvs {
std::map<std::string, std::vector<uint8_t>> blobs;
int openError = ESP_OK, readError = ESP_OK, setError = ESP_OK, commitError = ESP_OK;
int writes = 0, commits = 0;
int reads = 0, failReadAt = 0;
const char *openedNamespace = nullptr;
}

void identityContracts() {
  using namespace TurnHubIdentity;
  static_assert(!std::is_same<MatchId, ParticipantId>::value, "IDs must be distinct");
  static_assert(!std::is_convertible<ControllerId, ProfileId>::value, "No identity coercion");
  ProfileId profile;
  assert(!profile.valid());
  assert(ProfileId::parse("aB12Cd34", profile));
  assert(strcmp(profile.c_str(), "aB12Cd34") == 0);
  for (const char *invalid : {"", "123", "123456789", "ABCDEFGZ"}) {
    assert(!ProfileId::parse(invalid, profile));
    assert(strcmp(profile.c_str(), "aB12Cd34") == 0);
  }
  assert(!ProfileId::parse(nullptr, profile));
  MatchId match;
  assert(MatchId::parse("0123456789ABCDEF0123456789ABCDEF", match));
  assert(match.valid());
  assert(!MatchId::parse("AB12CD34", match));
}

void existingRecords() {
  FakeNvs::reset();
  NvsBlobStore store;
  assert(store.begin("turnhub") == Status::Ok);
  assert(strcmp(FakeNvs::openedNamespace, "turnhub") == 0);
  ProfileStats out;
  out.gamesPlayed = 91;
  assert(readStoredStats(store, key, out) == Status::NotFound);
  assert(out.gamesPlayed == 91);  // Repository never publishes partial/default reads.

  // Seed a deployed ESP32 v1 byte image independently of the struct encoder.
  std::vector<uint8_t> legacy(72, 0);
  legacy[0] = 0x78; legacy[1] = 0x56; legacy[2] = 0x34; legacy[3] = 0x12;
  legacy[16] = 17; legacy[20] = 4; legacy[64] = 1;
  legacy[66] = static_cast<uint8_t>(LastGameResult::Win);
  FakeNvs::blobs[key] = legacy;
  assert(readStoredStats(store, key, out) == Status::Ok);
  assert(out.totalTurnMs == 0x12345678ULL);
  assert(out.gamesPlayed == 17 && out.gamesWon == 4);
  assert(out.lastGameResult == LastGameResult::Win);
  assert(FakeNvs::writes == 0 && FakeNvs::blobs[key] == legacy);
  // Fault after the successful size probes, during the actual value read.
  FakeNvs::failReadAt = FakeNvs::reads + 3;
  ProfileStats unchanged;
  unchanged.gamesPlayed = 99;
  assert(readStoredStats(store, key, unchanged) == Status::IoError);
  assert(unchanged.gamesPlayed == 99 && FakeNvs::blobs[key] == legacy);
  FakeNvs::failReadAt = 0;
  ++out.gamesPlayed;
  assert(writeStoredStats(store, key, out) == Status::Ok);
  ProfileStats again;
  assert(readStoredStats(store, key, again) == Status::Ok);
  assert(again.gamesPlayed == 18 && again.gamesWon == 4);
  assert(again.totalTurnMs == out.totalTurnMs);
  assert(FakeNvs::writes == 1 && FakeNvs::commits == 1);
  assert(writeStoredStats(store, "s12345678", ProfileStats{}) == Status::Ok);
}

void protectedRecords() {
  FakeNvs::reset();
  NvsBlobStore store;
  assert(store.begin("turnhub") == Status::Ok);
  ProfileStats out;
  out.gamesPlayed = 99;
  auto reject = [&](const std::vector<uint8_t> &bytes, Status expected) {
    FakeNvs::blobs[key] = bytes;
    assert(readStoredStats(store, key, out) == expected);
    assert(out.gamesPlayed == 99);
    assert(writeStoredStats(store, key, ProfileStats{}) == expected);
    assert(FakeNvs::writes == 0 && FakeNvs::blobs[key] == bytes);
  };
  reject({}, Status::Corrupt);
  reject(std::vector<uint8_t>(71, 0), Status::Corrupt);
  reject(std::vector<uint8_t>(73, 0), Status::Corrupt);
  std::vector<uint8_t> future(72, 0);
  future[64] = 2;
  reject(future, Status::UnsupportedSchema);
  future[64] = 1; future[66] = 255;
  reject(future, Status::Corrupt);

  FakeNvs::readError = ESP_ERR_NVS_TYPE_MISMATCH;
  assert(readStoredStats(store, key, out) == Status::Corrupt);
  assert(writeStoredStats(store, key, ProfileStats{}) == Status::Corrupt);
  FakeNvs::readError = ESP_ERR_NVS_INVALID_HANDLE;
  assert(readStoredStats(store, key, out) == Status::IoError);
  assert(writeStoredStats(store, key, ProfileStats{}) == Status::IoError);
  assert(FakeNvs::writes == 0);
  FakeNvs::readError = ESP_OK;
  ProfileStats invalid;
  invalid.schemaVersion = 2;
  assert(writeStoredStats(store, "s12345678", invalid) == Status::UnsupportedSchema);
  assert(FakeNvs::writes == 0);
}

void backendFailures() {
  FakeNvs::reset();
  NvsBlobStore store;
  ProfileStats stats;
  assert(readStoredStats(store, key, stats) == Status::Unavailable);
  assert(writeStoredStats(store, key, stats) == Status::Unavailable);
  FakeNvs::openError = ESP_ERR_NVS_INVALID_HANDLE;
  assert(store.begin("turnhub") == Status::Unavailable);
  FakeNvs::openError = ESP_OK;
  assert(store.begin("turnhub") == Status::Ok);  // Retry after transient failure.
  size_t size = 123;
  assert(store.read(nullptr, nullptr, 0, size) == Status::InvalidArgument);
  assert(size == 0);
  assert(store.write("1234567890123456", &stats, sizeof(stats)) == Status::InvalidArgument);
  assert(store.write(key, nullptr, sizeof(stats)) == Status::InvalidArgument);
  FakeNvs::blobs[key] = std::vector<uint8_t>(72, 0);
  uint8_t small[2] = {};
  assert(store.read(key, small, sizeof(small), size) == Status::Corrupt);
  assert(size == 72);
  FakeNvs::blobs.clear();
  FakeNvs::setError = ESP_ERR_NVS_INVALID_HANDLE;
  assert(writeStoredStats(store, key, stats) == Status::IoError);
  assert(FakeNvs::commits == 0);
  FakeNvs::setError = ESP_OK;
  FakeNvs::commitError = ESP_ERR_NVS_INVALID_HANDLE;
  assert(writeStoredStats(store, key, stats) == Status::IoError);
  assert(FakeNvs::commits == 1);  // Commit failure is never success.
}

int main() {
  identityContracts();
  existingRecords();
  protectedRecords();
  backendFailures();
  std::cout << "PASS: identity contracts, legacy statistics, protected records, NVS failures\n";
}
