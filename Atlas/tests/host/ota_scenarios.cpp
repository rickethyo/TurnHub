// Sigil OTA on Atlas (sigil_update_jobs.h): staging a Sigil package in flash
// with every check, re-loading it after a restart, and one update job from
// offer to done or failure. Also the web portal pack installer
// (portal_pack.h): unpacking a signed pack into a staging folder and swapping
// it live only when everything checks out. Crypto is the host stand-in shared with the
// Sigil tests; ECDSA is checked on the device.
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "portal_pack.h"
#include "sigil_update_jobs.h"
#include "../../../Sigil/tests/host/test_package_crypto.h"

using namespace TurnHubAtlas;
using namespace TurnHubFirmwarePackage;
using TurnHubTest::Sha256;
using TurnHubTest::TestPackageCrypto;

namespace {

uint8_t KEY[PUBLIC_KEY_BYTES];
const uint8_t KEY_ID_TEST[KEY_ID_BYTES] = {1, 2, 3, 4};

class RamFlash final : public PackageFlash {
 public:
  explicit RamFlash(uint32_t size) : bytes(size, 0x5A) {}
  uint32_t capacity() const override { return static_cast<uint32_t>(bytes.size()); }
  bool eraseSector(uint32_t offset) override {
    assert(offset % PACKAGE_SECTOR_BYTES == 0);
    if (offset >= bytes.size()) return false;
    ++erases;
    std::fill(bytes.begin() + offset,
        bytes.begin() + std::min<size_t>(offset + PACKAGE_SECTOR_BYTES, bytes.size()), 0xFF);
    return true;
  }
  bool write(uint32_t offset, const uint8_t *data, size_t length) override {
    if (failWrites) return false;
    for (size_t i = 0; i < length; ++i) {
      assert(bytes[offset + i] == 0xFF);  // Written only once after an erase.
      bytes[offset + i] = data[i];
    }
    return true;
  }
  bool read(uint32_t offset, uint8_t *data, size_t length) override {
    if (offset + length > bytes.size()) return false;
    memcpy(data, bytes.data() + offset, length);
    return true;
  }
  std::vector<uint8_t> bytes;
  unsigned erases = 0;
  bool failWrites = false;
};

std::vector<uint8_t> makePackage(Product product, Version version, size_t imageSize) {
  std::vector<uint8_t> img(imageSize);
  for (size_t i = 0; i < imageSize; ++i) img[i] = static_cast<uint8_t>(i * 17 + 3);
  Header h{};
  memcpy(h.magic, PACKAGE_MAGIC, sizeof(PACKAGE_MAGIC));
  h.format = HEADER_FORMAT;
  h.product = static_cast<uint8_t>(product);
  h.version = version;
  h.radioProtocol = 2;
  h.imageSize = static_cast<uint32_t>(imageSize);
  Sha256::digest(img.data(), img.size(), h.imageHash);
  memcpy(h.keyId, KEY_ID_TEST, KEY_ID_BYTES);
  uint8_t digest[HASH_BYTES];
  Sha256::digest(reinterpret_cast<const uint8_t *>(&h), SIGNED_BYTES, digest);
  TestPackageCrypto::sign(KEY, digest, h.signature);
  std::vector<uint8_t> out(reinterpret_cast<uint8_t *>(&h), reinterpret_cast<uint8_t *>(&h) + sizeof(h));
  out.insert(out.end(), img.begin(), img.end());
  return out;
}

std::vector<uint8_t> packageFor(Product product, Version version, const std::vector<uint8_t> &img) {
  Header h{};
  memcpy(h.magic, PACKAGE_MAGIC, sizeof(PACKAGE_MAGIC));
  h.format = HEADER_FORMAT;
  h.product = static_cast<uint8_t>(product);
  h.version = version;
  h.imageSize = static_cast<uint32_t>(img.size());
  Sha256::digest(img.data(), img.size(), h.imageHash);
  memcpy(h.keyId, KEY_ID_TEST, KEY_ID_BYTES);
  uint8_t digest[HASH_BYTES];
  Sha256::digest(reinterpret_cast<const uint8_t *>(&h), SIGNED_BYTES, digest);
  TestPackageCrypto::sign(KEY, digest, h.signature);
  std::vector<uint8_t> out(reinterpret_cast<uint8_t *>(&h), reinterpret_cast<uint8_t *>(&h) + sizeof(h));
  out.insert(out.end(), img.begin(), img.end());
  return out;
}

// --- Portal pack -------------------------------------------------------------

// A card in RAM: files by full path, folders as a set.
class RamCard final : public TurnHubPortal::Files {
 public:
  bool exists(const char *path) override { return dirs.count(path) || files.count(path); }
  bool makeDir(const char *path) override {
    if (failAfterWrites >= 0 && writes >= failAfterWrites) return false;
    dirs.insert(path);
    return true;
  }
  bool removeTree(const char *path) override {
    const std::string prefix = std::string(path) + "/";
    for (auto it = files.begin(); it != files.end();) {
      it = it->first.compare(0, prefix.size(), prefix) == 0 ? files.erase(it) : std::next(it);
    }
    for (auto it = dirs.begin(); it != dirs.end();) {
      it = (*it == path || it->compare(0, prefix.size(), prefix) == 0) ? dirs.erase(it) : std::next(it);
    }
    return true;
  }
  bool rename(const char *from, const char *to) override {
    if (failRename) return false;
    const std::string f(from), t(to), fp = f + "/";
    if (!dirs.count(f) || dirs.count(t)) return false;
    std::map<std::string, std::string> moved;
    for (auto it = files.begin(); it != files.end();) {
      if (it->first.compare(0, fp.size(), fp) == 0) {
        moved[t + it->first.substr(f.size())] = it->second;
        it = files.erase(it);
      } else {
        ++it;
      }
    }
    std::set<std::string> movedDirs;
    for (auto it = dirs.begin(); it != dirs.end();) {
      if (*it == f || it->compare(0, fp.size(), fp) == 0) {
        movedDirs.insert(t + it->substr(f.size()));
        it = dirs.erase(it);
      } else {
        ++it;
      }
    }
    files.insert(moved.begin(), moved.end());
    dirs.insert(movedDirs.begin(), movedDirs.end());
    return true;
  }
  bool beginFile(const char *path) override {
    assert(open.empty());
    const std::string p(path);
    assert(dirs.count(p.substr(0, p.rfind('/'))));  // Parent made first.
    open = p;
    files[open].clear();
    return true;
  }
  bool writeFile(const uint8_t *data, size_t length) override {
    assert(!open.empty());
    if (failAfterWrites >= 0 && ++writes > failAfterWrites) return false;
    files[open].append(reinterpret_cast<const char *>(data), length);
    return true;
  }
  bool endFile() override {
    open.clear();
    return true;
  }
  bool readText(const char *path, char *out, size_t capacity) override {
    auto it = files.find(path);
    if (it == files.end() || it->second.size() >= capacity) return false;
    memcpy(out, it->second.data(), it->second.size());
    out[it->second.size()] = '\0';
    return true;
  }
  std::map<std::string, std::string> files;
  std::set<std::string> dirs;
  std::string open;
  int failAfterWrites = -1;
  int writes = 0;
  bool failRename = false;
};

struct PackFile {
  std::string path;
  std::string data;
  bool gzip;
};

std::vector<uint8_t> archive(Version version, const std::vector<PackFile> &entries, uint8_t product = 4) {
  std::vector<uint8_t> a = {'T', 'H', 'F', 'W', 'D', 'S', 'C', '1', product, version.major, version.minor,
      version.patch, 0, 0, 0, 0, 'T', 'H', 'W', 'E', 'B', 'A', 'R', '1'};
  a.push_back(static_cast<uint8_t>(entries.size()));
  a.push_back(static_cast<uint8_t>(entries.size() >> 8));
  a.push_back(0);
  a.push_back(0);
  for (const PackFile &e : entries) {
    a.push_back(static_cast<uint8_t>(e.path.size()));
    a.push_back(e.gzip ? 1 : 0);
    const uint32_t n = static_cast<uint32_t>(e.data.size());
    for (int i = 0; i < 4; ++i) a.push_back(static_cast<uint8_t>(n >> (8 * i)));
    a.insert(a.end(), e.path.begin(), e.path.end());
    a.insert(a.end(), e.data.begin(), e.data.end());
  }
  return a;
}

const std::vector<PackFile> PORTAL_FILES = {
    {"index.html", "<!doctype html><title>TurnHub</title>", true},
    {"assets/app.1a2b.css", std::string(5000, 'c'), true},
    {"assets/fonts/Inter-Variable.woff2", std::string(3000, 'f'), false},
    {"empty.txt", "", false},
};

// Feeds a whole package through the reader and the installer, in chunks. The
// card argument names which card the installer writes, for readability.
bool installPack(RamCard &, const std::vector<uint8_t> &pkg, Version running,
    TurnHubPortal::Installer &installer, size_t chunk = 700) {
  TestPackageCrypto crypto;
  Reader reader;
  reader.begin(crypto, {KEY, KEY_ID_TEST, productBit(Product::Portal), running, TurnHubPortal::MAX_PACK_BYTES},
      installer);
  for (size_t at = 0; at < pkg.size(); at += chunk) {
    if (!reader.write(pkg.data() + at, std::min(chunk, pkg.size() - at))) {
      installer.abort();
      return false;
    }
  }
  if (!reader.finish() || !installer.commit()) {
    installer.abort();
    return false;
  }
  return true;
}

void portalPackInstallsAndSwaps() {
  using namespace TurnHubPortal;
  RamCard card;
  Installer installer(card);
  Version installed{};
  assert(!installedVersion(card, installed));

  const Version v1{1, 0, 0};
  for (size_t chunk : {1u, 7u, 700u, 100000u}) {
    RamCard fresh;
    Installer once(fresh);
    assert(installPack(fresh, packageFor(Product::Portal, v1, archive(v1, PORTAL_FILES)), {0, 0, 0}, once, chunk));
    assert(fresh.files.at("/turnhub/portal/live/index.html.gz") == PORTAL_FILES[0].data);
    assert(fresh.files.at("/turnhub/portal/live/assets/app.1a2b.css.gz").size() == 5000);
    assert(fresh.files.at("/turnhub/portal/live/assets/fonts/Inter-Variable.woff2").size() == 3000);
    assert(fresh.files.at("/turnhub/portal/live/empty.txt").empty());
    assert(fresh.files.at("/turnhub/portal/live/VERSION") == "1.0.0");
    assert(!fresh.exists(STAGE_DIR) && !fresh.exists(OLD_DIR));
    assert(once.filesWritten() == 4);
  }

  assert(installPack(card, packageFor(Product::Portal, v1, archive(v1, PORTAL_FILES)), {0, 0, 0}, installer));
  assert(installedVersion(card, installed) && installed.major == 1 && installed.minor == 0);

  // A newer pack replaces every file; files it no longer has are gone.
  const Version v2{1, 1, 0};
  const std::vector<PackFile> next = {{"index.html", "v2", true}, {"assets/app.9f9f.css", "x", true}};
  assert(installPack(card, packageFor(Product::Portal, v2, archive(v2, next)), v1, installer));
  assert(card.files.at("/turnhub/portal/live/index.html.gz") == "v2");
  assert(!card.files.count("/turnhub/portal/live/assets/app.1a2b.css.gz"));
  assert(installedVersion(card, installed) && installed.minor == 1);

  // Failures never touch the live portal.
  const std::string live = card.files.at("/turnhub/portal/live/index.html.gz");
  const auto stillLive = [&]() {
    assert(card.files.at("/turnhub/portal/live/index.html.gz") == live);
    assert(!card.exists(STAGE_DIR));
  };
  // Older pack: refused by the version rule before anything is staged.
  assert(!installPack(card, packageFor(Product::Portal, v1, archive(v1, PORTAL_FILES)), v2, installer));
  stillLive();
  // A firmware package is not a portal pack.
  assert(!installPack(card, packageFor(Product::Atlas, {9, 0, 0}, archive({9, 0, 0}, PORTAL_FILES)), v2, installer));
  stillLive();
  // Descriptor must say Portal and match the signed version.
  assert(!installPack(card, packageFor(Product::Portal, {1, 2, 0}, archive({1, 2, 1}, PORTAL_FILES)), v2, installer));
  assert(installer.error() == ArchiveError::BadDescriptor);
  stillLive();
  assert(!installPack(card, packageFor(Product::Portal, {1, 2, 0}, archive({1, 2, 0}, PORTAL_FILES, 1)), v2, installer));
  stillLive();
  // Unsafe paths.
  for (const char *bad : {"../x", "/abs", "a//b", "a/./b", "dir/", "sp ace.html", "VERSION", "a\\b"}) {
    const std::vector<PackFile> evil = {{"index.html", "i", true}, {bad, "x", false}};
    assert(!installPack(card, packageFor(Product::Portal, {1, 2, 0}, archive({1, 2, 0}, evil)), v2, installer));
    assert(installer.error() == ArchiveError::BadPath);
    stillLive();
  }
  // No index.html.
  const std::vector<PackFile> noIndex = {{"assets/a.css", "x", true}};
  assert(!installPack(card, packageFor(Product::Portal, {1, 2, 0}, archive({1, 2, 0}, noIndex)), v2, installer));
  assert(installer.error() == ArchiveError::NoIndex);
  stillLive();
  // Archive claims more files than it carries.
  std::vector<uint8_t> shortArchive = archive({1, 2, 0}, PORTAL_FILES);
  shortArchive[24] = 5;
  assert(!installPack(card, packageFor(Product::Portal, {1, 2, 0}, shortArchive), v2, installer));
  assert(installer.error() == ArchiveError::Truncated);
  stillLive();
  // Bytes after the last file.
  std::vector<uint8_t> trailing = archive({1, 2, 0}, PORTAL_FILES);
  trailing.push_back(0);
  assert(!installPack(card, packageFor(Product::Portal, {1, 2, 0}, trailing), v2, installer));
  assert(installer.error() == ArchiveError::TrailingData);
  stillLive();
  // Tampered archive: the hash catches it after everything was staged.
  std::vector<uint8_t> pkg = packageFor(Product::Portal, {1, 2, 0}, archive({1, 2, 0}, PORTAL_FILES));
  pkg.back() ^= 1;
  assert(!installPack(card, pkg, v2, installer));
  stillLive();
  // The card fails mid-write.
  card.failAfterWrites = card.writes + 2;
  assert(!installPack(card, packageFor(Product::Portal, {1, 2, 0}, archive({1, 2, 0}, PORTAL_FILES)), v2, installer));
  assert(installer.error() == ArchiveError::Storage);
  card.failAfterWrites = -1;
  stillLive();
  // The swap fails: the old portal stays live.
  card.failRename = true;
  assert(!installPack(card, packageFor(Product::Portal, {1, 2, 0}, archive({1, 2, 0}, PORTAL_FILES)), v2, installer));
  card.failRename = false;
  stillLive();
  // Reinstalling the same version is allowed.
  assert(installPack(card, packageFor(Product::Portal, v2, archive(v2, next)), v2, installer));

  Version v{};
  assert(parseVersion("1.12.255", v) && v.minor == 12 && v.patch == 255);
  assert(parseVersion("2.0.1\n", v) && v.major == 2);
  for (const char *bad : {"", "1.2", "1.2.3.4", "1..3", "1.2.256", "a.b.c", ".1.2", "1.2."}) assert(!parseVersion(bad, v));
  char text[16];
  formatVersion({3, 4, 5}, text, sizeof(text));
  assert(strcmp(text, "3.4.5") == 0);
}

bool upload(SigilPackageStore &store, const std::vector<uint8_t> &bytes, size_t chunk = 1460) {
  store.startUpload();
  for (size_t at = 0; at < bytes.size(); at += chunk) {
    if (!store.writeUpload(bytes.data() + at, std::min(chunk, bytes.size() - at))) return false;
  }
  return store.finishUpload();
}

void stagingChecksEverything() {
  RamFlash flash(64 * 1024);
  TestPackageCrypto crypto;
  SigilPackageStore store;
  store.begin(flash, crypto, KEY, KEY_ID_TEST);
  assert(!store.loadStaged() && !store.staged());  // Blank slot.

  const std::vector<uint8_t> oled = makePackage(Product::SigilOled, {0, 9, 0}, 20000);
  assert(upload(store, oled) && store.staged());
  assert(store.packageSize() == oled.size() && store.header().product == 3);
  assert(flash.erases == (oled.size() + PACKAGE_SECTOR_BYTES - 1) / PACKAGE_SECTOR_BYTES);
  std::vector<uint8_t> back(oled.size());
  assert(store.read(0, back.data(), back.size()) && back == oled);
  uint8_t past[2];
  assert(!store.read(static_cast<uint32_t>(oled.size()) - 1, past, 2));

  // After a restart the header is re-checked from flash.
  SigilPackageStore again;
  again.begin(flash, crypto, KEY, KEY_ID_TEST);
  assert(again.loadStaged() && again.header().version.minor == 9 &&
      again.packageSize() == oled.size());

  // Valid header but truncated/corrupt flash must not resurrect after reboot.
  flash.bytes[sizeof(Header) + 100] ^= 1;
  SigilPackageStore interrupted;
  interrupted.begin(flash, crypto, KEY, KEY_ID_TEST);
  assert(!interrupted.loadStaged());
  flash.bytes[sizeof(Header) + 100] ^= 1;

  // An e-ink package replaces it; any version is staged (each Sigil decides).
  assert(upload(store, makePackage(Product::SigilEink, {0, 1, 0}, 9000)) &&
      store.header().product == 2);

  // Refused: Atlas's own firmware, a bad signature, a damaged image, one too
  // large for the slot, a plain firmware.bin. Each leaves nothing staged.
  std::vector<uint8_t> bad = makePackage(Product::Atlas, {0, 7, 0}, 5000);
  assert(!upload(store, bad) && !store.staged() && store.error() == Error::WrongProduct);
  bad = makePackage(Product::SigilOled, {0, 9, 0}, 5000);
  bad[70] ^= 1;
  assert(!upload(store, bad) && store.error() == Error::BadSignature);
  bad = makePackage(Product::SigilOled, {0, 9, 0}, 5000);
  bad[3000] ^= 1;
  assert(!upload(store, bad) && store.error() == Error::HashMismatch && !store.staged());
  assert(!upload(store, makePackage(Product::SigilOled, {0, 9, 0}, 64 * 1024)) &&
      store.error() == Error::TooLarge);
  std::vector<uint8_t> plain(4000, 0xE9);
  assert(!upload(store, plain) && store.error() == Error::BadMagic);
  // A cut-off upload.
  const std::vector<uint8_t> good = makePackage(Product::SigilOled, {0, 9, 0}, 5000);
  store.startUpload();
  assert(store.writeUpload(good.data(), 3000) && !store.finishUpload() &&
      store.error() == Error::Truncated);
  // Flash failures.
  flash.failWrites = true;
  assert(!upload(store, good) && store.error() == Error::Storage);
  flash.failWrites = false;

  // A damaged header in flash is not staged after a restart.
  assert(upload(store, good));
  flash.bytes[10] ^= 1;
  SigilPackageStore third;
  third.begin(flash, crypto, KEY, KEY_ID_TEST);
  assert(!third.loadStaged());

  // Atlas's own update overwrites the slot.
  assert(upload(store, good));
  store.invalidate();
  assert(!store.staged() && store.packageSize() == 0);
}

const uint8_t TOKEN[TurnHubProtocol::UPDATE_TOKEN_BYTES] = {0xAB, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15};

Header target() {
  Header h{};
  h.product = 3;
  h.version = {0, 9, 0};
  return h;
}

int32_t status(TurnHubProtocol::UpdateStage stage, uint8_t progress = 0, uint8_t error = 0,
    uint8_t tag = 0xAB) {
  return TurnHubProtocol::encodeUpdateStatus(stage, progress, error, tag);
}

void jobRunsToDone() {
  using TurnHubProtocol::UpdateStage;
  SigilUpdateJobs jobs;
  const Version old{0, 8, 0};
  assert(!jobs.busy() && jobs.stage() == UpdateJobStage::Idle);
  assert(jobs.start(2, target(), TOKEN, 1000) && jobs.busy());
  assert(!jobs.start(3, target(), TOKEN, 1000));  // One at a time.

  // Offers go out at once, then once a second until answered.
  assert(jobs.tick(1000, true, old));
  assert(!jobs.tick(1500, true, old));
  assert(jobs.tick(2000, true, old));
  assert(jobs.downloadAllowed("ab0102030405060708090a0b0c0d0e0f"));
  assert(!jobs.downloadAllowed("ab0102030405060708090a0b0c0d0e00"));
  assert(!jobs.downloadAllowed("ab01"));
  assert(!jobs.downloadAllowed(nullptr));

  jobs.onStatus(3, status(UpdateStage::Accepted), 2100);  // Another Sigil: ignored.
  jobs.onStatus(2, status(UpdateStage::Accepted, 0, 0, 0x11), 2100);  // Older job's tag.
  assert(jobs.stage() == UpdateJobStage::Offering);
  jobs.onStatus(2, status(UpdateStage::Accepted), 2200);
  assert(jobs.stage() == UpdateJobStage::Accepted && !jobs.tick(3300, true, old));  // No more offers.
  jobs.onStatus(2, status(UpdateStage::Downloading, 40), 5000);
  assert(jobs.stage() == UpdateJobStage::Downloading && jobs.progress() == 40);
  jobs.onStatus(2, status(UpdateStage::Accepted), 5100);  // A late repeat doesn't go back.
  assert(jobs.stage() == UpdateJobStage::Downloading);
  jobs.onStatus(2, status(UpdateStage::Installed, 100), 9000);
  assert(jobs.stage() == UpdateJobStage::Installed && !jobs.downloadAllowed("ab0102030405060708090a0b0c0d0e0f"));
  // A Hello from before the restart is ignored for a moment...
  jobs.tick(9500, true, old);
  assert(jobs.stage() == UpdateJobStage::Installed);
  // ...then the new version's Hello finishes it.
  jobs.tick(20000, true, Version{0, 9, 0});
  assert(jobs.stage() == UpdateJobStage::Done && !jobs.busy());
  // A new job may start now.
  assert(jobs.start(2, target(), TOKEN, 30000));
}

void jobsFail() {
  using TurnHubProtocol::UpdateStage;
  using TurnHubProtocol::UpdateError;
  const Version old{0, 8, 0};
  {
    SigilUpdateJobs jobs;  // No answer at all (firmware without the updater).
    jobs.start(1, target(), TOKEN, 0);
    for (uint32_t t = 0; t <= SigilUpdateJobs::OFFER_TIMEOUT_MS + 1000; t += 500) jobs.tick(t, true, old);
    assert(jobs.stage() == UpdateJobStage::Failed && strstr(jobs.message(), "did not answer"));
  }
  {
    SigilUpdateJobs jobs;  // Refused: already newer.
    jobs.start(1, target(), TOKEN, 0);
    jobs.onStatus(1, status(UpdateStage::Refused, 0, static_cast<uint8_t>(Error::OlderVersion)), 100);
    assert(jobs.stage() == UpdateJobStage::Failed && jobs.error() == 6 &&
        strcmp(jobs.message(), "The Sigil already runs newer firmware") == 0);
  }
  {
    SigilUpdateJobs jobs;  // Failed while downloading.
    jobs.start(1, target(), TOKEN, 0);
    jobs.onStatus(1, status(UpdateStage::Downloading, 30), 100);
    jobs.onStatus(1, status(UpdateStage::Failed, 0, static_cast<uint8_t>(UpdateError::WifiJoin)), 200);
    assert(jobs.stage() == UpdateJobStage::Failed &&
        strcmp(jobs.message(), "The Sigil could not join Atlas's Wi-Fi") == 0);
  }
  {
    SigilUpdateJobs jobs;  // Went silent mid-download.
    jobs.start(1, target(), TOKEN, 0);
    jobs.onStatus(1, status(UpdateStage::Downloading, 30), 100);
    jobs.tick(100 + SigilUpdateJobs::SILENCE_TIMEOUT_MS + 1, true, old);
    assert(jobs.stage() == UpdateJobStage::Failed && strstr(jobs.message(), "stopped answering"));
  }
  {
    SigilUpdateJobs jobs;  // Rolled back.
    jobs.start(1, target(), TOKEN, 0);
    jobs.onStatus(1, status(UpdateStage::Installed, 100), 100);
    jobs.tick(100 + SigilUpdateJobs::REBOOT_GRACE_MS + 1, true, old);
    assert(jobs.stage() == UpdateJobStage::Failed && strstr(jobs.message(), "previous firmware"));
  }
  {
    SigilUpdateJobs jobs;  // Never came back.
    jobs.start(1, target(), TOKEN, 0);
    jobs.onStatus(1, status(UpdateStage::Installed, 100), 100);
    jobs.tick(100 + SigilUpdateJobs::REBOOT_TIMEOUT_MS + 1, false, old);
    assert(jobs.stage() == UpdateJobStage::Failed && strstr(jobs.message(), "did not come back"));
  }
  {
    SigilUpdateJobs jobs;  // Cancelled (Atlas's own update, a forget...).
    jobs.start(1, target(), TOKEN, 0);
    jobs.cancel("Cancelled");
    assert(jobs.stage() == UpdateJobStage::Failed && !jobs.busy());
    jobs.cancel("Again");  // A finished job keeps its result.
    assert(strcmp(jobs.message(), "Cancelled") == 0);
  }
  assert(strcmp(updateJobStageName(UpdateJobStage::Downloading), "downloading") == 0);
  assert(strcmp(updateErrorText(99), "The update failed") == 0);
}

}  // namespace

int main() {
  KEY[0] = 0x04;
  for (size_t i = 1; i < PUBLIC_KEY_BYTES; ++i) KEY[i] = static_cast<uint8_t>(i * 5 + 1);
  stagingChecksEverything();
  jobRunsToDone();
  jobsFail();
  portalPackInstallsAndSwaps();
  std::cout << "PASS Sigil OTA on Atlas: package staging and checks, reload, update jobs; portal pack install\n";
  return 0;
}
