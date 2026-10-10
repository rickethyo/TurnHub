// Signed firmware packages (shared/include/firmware_package.h): the version
// rule, header checks, and the streaming reader, including every way a
// package must be refused. Crypto is the host stand-in (test_package_crypto.h);
// ECDSA is checked on the device.
#include "firmware_package.h"
#include "test_package_crypto.h"
#include "update_offer.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using namespace TurnHubFirmwarePackage;
using TurnHubTest::Sha256;
using TurnHubTest::TestPackageCrypto;

TURNHUB_FIRMWARE_DESCRIPTOR(testDescriptor, Product::SigilOled, 0, 8, 1, 2);

namespace {

uint8_t KEY[PUBLIC_KEY_BYTES];
uint8_t OTHER_KEY[PUBLIC_KEY_BYTES];
const uint8_t KEY_ID[KEY_ID_BYTES] = {0xA1, 0xB2, 0xC3, 0xD4};

void makeKeys() {
  KEY[0] = OTHER_KEY[0] = 0x04;
  for (size_t i = 1; i < PUBLIC_KEY_BYTES; ++i) {
    KEY[i] = static_cast<uint8_t>(i * 7);
    OTHER_KEY[i] = static_cast<uint8_t>(i * 11 + 3);
  }
}

std::vector<uint8_t> image(size_t size) {
  std::vector<uint8_t> bytes(size);
  for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>(i * 31 + 7);
  bytes[0] = 0xE9;  // ESP32 image magic, for realism.
  return bytes;
}

Header headerFor(const std::vector<uint8_t> &img, Product product, Version version,
    const uint8_t *signingKey = KEY) {
  Header h{};
  memcpy(h.magic, PACKAGE_MAGIC, sizeof(PACKAGE_MAGIC));
  h.format = HEADER_FORMAT;
  h.product = static_cast<uint8_t>(product);
  h.version = version;
  h.radioProtocol = 2;
  h.imageSize = static_cast<uint32_t>(img.size());
  Sha256::digest(img.data(), img.size(), h.imageHash);
  memcpy(h.buildId, "1df68ca0", BUILD_ID_BYTES);
  memcpy(h.keyId, KEY_ID, KEY_ID_BYTES);
  uint8_t digest[HASH_BYTES];
  Sha256::digest(reinterpret_cast<const uint8_t *>(&h), SIGNED_BYTES, digest);
  TestPackageCrypto::sign(signingKey, digest, h.signature);
  return h;
}

std::vector<uint8_t> package(const Header &h, const std::vector<uint8_t> &img) {
  std::vector<uint8_t> bytes(reinterpret_cast<const uint8_t *>(&h),
      reinterpret_cast<const uint8_t *>(&h) + sizeof(h));
  bytes.insert(bytes.end(), img.begin(), img.end());
  return bytes;
}

Policy sigilPolicy() {
  return Policy{KEY, KEY_ID, productBit(Product::SigilOled), Version{0, 8, 0}, 1310720};
}

struct RecordingSink : Sink {
  bool sawHeader = false;
  std::vector<uint8_t> written;
  size_t failAfter = SIZE_MAX;
  bool header(const Header &) override {
    assert(!sawHeader && written.empty());
    sawHeader = true;
    return true;
  }
  bool image(const uint8_t *data, size_t length) override {
    assert(sawHeader);
    if (written.size() + length > failAfter) return false;
    written.insert(written.end(), data, data + length);
    return true;
  }
};

// Feeds in uneven chunks (so the header straddles writes) and finishes.
Error feed(const std::vector<uint8_t> &bytes, const Policy &policy, RecordingSink &sink,
    size_t chunk = 37) {
  TestPackageCrypto crypto;
  Reader reader;
  reader.begin(crypto, policy, sink);
  for (size_t at = 0; at < bytes.size(); at += chunk) {
    const size_t n = std::min(chunk, bytes.size() - at);
    if (!reader.write(bytes.data() + at, n)) return reader.error();
  }
  reader.finish();
  return reader.error();
}

void versionRule() {
  assert(compareVersions({0, 8, 0}, {0, 8, 0}) == 0);
  assert(compareVersions({0, 8, 1}, {0, 8, 0}) > 0);
  assert(compareVersions({0, 9, 0}, {0, 8, 9}) > 0);
  assert(compareVersions({1, 0, 0}, {0, 99, 99}) > 0);
  assert(versionAllowed({0, 8, 0}, {0, 8, 0}));   // Reinstall.
  assert(versionAllowed({0, 8, 0}, {0, 9, 0}));   // Upgrade.
  assert(!versionAllowed({0, 8, 0}, {0, 7, 9}));  // Downgrade: USB only.
}

void descriptorLayout() {
  const uint8_t *raw = reinterpret_cast<const uint8_t *>(&testDescriptor);
  assert(memcmp(raw, "THFWDSC1", 8) == 0);
  assert(raw[8] == 3 && raw[9] == 0 && raw[10] == 8 && raw[11] == 1 && raw[12] == 2);
  assert(raw[13] == 0 && raw[14] == 0 && raw[15] == 0);
}

void goodPackageStreamsThrough() {
  const std::vector<uint8_t> img = image(5000);
  const std::vector<uint8_t> bytes = package(headerFor(img, Product::SigilOled, {0, 9, 0}), img);
  for (size_t chunk : {size_t(1), size_t(37), size_t(128), size_t(129), bytes.size()}) {
    RecordingSink sink;
    assert(feed(bytes, sigilPolicy(), sink, chunk) == Error::None);
    assert(sink.sawHeader && sink.written == img);
  }
  // The same version reinstalls.
  RecordingSink sink;
  const std::vector<uint8_t> same = package(headerFor(img, Product::SigilOled, {0, 8, 0}), img);
  assert(feed(same, sigilPolicy(), sink) == Error::None);
}

void badHeadersAreRefusedBeforeAnyWrite() {
  const std::vector<uint8_t> img = image(3000);
  const Header good = headerFor(img, Product::SigilOled, {0, 9, 0});
  struct Case {
    Header header;
    Error expected;
  };
  std::vector<Case> cases;
  Header h = good;
  h.magic[0] = 'X';
  cases.push_back({h, Error::BadMagic});
  h = good;
  h.format = 2;
  cases.push_back({h, Error::BadFormat});
  h = good;
  h.keyId[0] ^= 1;
  cases.push_back({h, Error::UnknownKey});
  cases.push_back({headerFor(img, Product::SigilOled, {0, 9, 0}, OTHER_KEY), Error::BadSignature});
  h = good;
  h.version.minor = 10;  // Edited after signing.
  cases.push_back({h, Error::BadSignature});
  h = good;
  h.signature[63] ^= 1;
  cases.push_back({h, Error::BadSignature});
  cases.push_back({headerFor(img, Product::SigilEink, {0, 9, 0}), Error::WrongProduct});
  cases.push_back({headerFor(img, Product::Atlas, {0, 9, 0}), Error::WrongProduct});
  cases.push_back({headerFor(img, Product::SigilOled, {0, 7, 5}), Error::OlderVersion});

  for (const Case &c : cases) {
    RecordingSink sink;
    assert(feed(package(c.header, img), sigilPolicy(), sink) == c.expected);
    assert(!sink.sawHeader && sink.written.empty());
  }

  // Too large for the slot.
  Policy tight = sigilPolicy();
  tight.maxImageSize = 2999;
  RecordingSink sink;
  assert(feed(package(good, img), tight, sink) == Error::TooLarge && !sink.sawHeader);

  // Atlas stages either Sigil's package, never its own.
  Policy staging = sigilPolicy();
  staging.products = ANY_SIGIL;
  staging.running = Version{0, 0, 0};
  RecordingSink eink;
  assert(feed(package(headerFor(img, Product::SigilEink, {0, 1, 0}), img), staging, eink) == Error::None);
  RecordingSink atlas;
  assert(feed(package(headerFor(img, Product::Atlas, {9, 0, 0}), img), staging, atlas) ==
      Error::WrongProduct);

  // Not a package at all (a plain firmware.bin).
  RecordingSink plain;
  assert(feed(image(4000), sigilPolicy(), plain) == Error::BadMagic && !plain.sawHeader);
}

void badImagesFailAtTheEnd() {
  const std::vector<uint8_t> img = image(3000);
  const std::vector<uint8_t> good = package(headerFor(img, Product::SigilOled, {0, 9, 0}), img);

  std::vector<uint8_t> flipped = good;
  flipped[sizeof(Header) + 1234] ^= 0x40;
  RecordingSink sink;
  assert(feed(flipped, sigilPolicy(), sink) == Error::HashMismatch);

  std::vector<uint8_t> shorter(good.begin(), good.end() - 1);
  RecordingSink cut;
  assert(feed(shorter, sigilPolicy(), cut) == Error::Truncated);

  std::vector<uint8_t> headerOnly(good.begin(), good.begin() + 100);
  RecordingSink partial;
  assert(feed(headerOnly, sigilPolicy(), partial) == Error::Truncated && !partial.sawHeader);

  std::vector<uint8_t> longer = good;
  longer.push_back(0);
  RecordingSink extra;
  assert(feed(longer, sigilPolicy(), extra) == Error::TooLong);

  RecordingSink full;
  full.failAfter = 1000;
  assert(feed(good, sigilPolicy(), full) == Error::Storage);

  // Errors stick, and a finished reader takes nothing more.
  TestPackageCrypto crypto;
  RecordingSink sink2;
  Reader reader;
  reader.begin(crypto, sigilPolicy(), sink2);
  assert(reader.write(good.data(), good.size()) && reader.finish() && reader.finished());
  assert(reader.packageSize() == good.size());
  assert(!reader.write(good.data(), 1) && !reader.finish());
}

void errorNames() {
  assert(strcmp(errorName(Error::BadSignature), "badSignature") == 0);
  assert(strcmp(errorName(Error::OlderVersion), "olderVersion") == 0);
  assert(knownProduct(1) && knownProduct(3) && !knownProduct(4) && !knownProduct(0) && !knownProduct(5));
  assert(productForSigilCapabilities(0x10, 0x10) == Product::SigilOled);
  assert(productForSigilCapabilities(0x07, 0x10) == Product::SigilEink);
}

void sha256KnownAnswer() {
  uint8_t out[32];
  Sha256::digest(reinterpret_cast<const uint8_t *>("abc"), 3, out);
  const uint8_t expected[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
      0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10,
      0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  assert(memcmp(out, expected, 32) == 0);
}

void updateOffers() {
  using namespace TurnHubProtocol;
  using TurnHubSigil::OfferDecision;
  TurnHubProtocol::SigilUpdateOfferPacket offer{};
  offer.version = VERSION;
  offer.type = PacketType::SigilUpdateOffer;
  offer.sigilId = 2;
  offer.product = static_cast<uint8_t>(Product::SigilOled);
  offer.major = 0;
  offer.minor = 9;
  offer.patch = 0;
  offer.packageSize = 900000;
  for (uint8_t i = 0; i < UPDATE_TOKEN_BYTES; ++i) offer.token[i] = static_cast<uint8_t>(0xF0 + i);
  strcpy(offer.ssid, "TurnHub-Atlas");
  strcpy(offer.password, "secret");
  const Version running{0, 8, 0};
  TurnHubSigil::UpdaterMemory memory;
  const auto check = [&](const SigilUpdateOfferPacket &o) {
    return TurnHubSigil::checkUpdateOffer(o, 2, Product::SigilOled, running, memory);
  };
  assert(check(offer).decision == OfferDecision::Accept);
  SigilUpdateOfferPacket other = offer;
  other.sigilId = 3;
  assert(check(other).decision == OfferDecision::Ignore);  // Another Sigil's.
  other = offer;
  other.ssid[0] = '\0';
  assert(check(other).decision == OfferDecision::Ignore);  // Malformed.
  other = offer;
  other.product = static_cast<uint8_t>(Product::SigilEink);
  assert(check(other).decision == OfferDecision::Refuse &&
      check(other).error == static_cast<uint8_t>(Error::WrongProduct));
  other = offer;
  other.minor = 7;
  assert(check(other).decision == OfferDecision::Refuse &&
      check(other).error == static_cast<uint8_t>(Error::OlderVersion));
  other.minor = 8;  // Reinstall.
  assert(check(other).decision == OfferDecision::Accept);

  // Once taken, the same token is a repeat (never a restart), and another
  // job is refused while one runs.
  memory.hasJob = true;
  memory.running = true;
  memcpy(memory.token, offer.token, UPDATE_TOKEN_BYTES);
  assert(check(offer).decision == OfferDecision::Repeat);
  other = offer;
  other.token[0] ^= 1;
  assert(check(other).decision == OfferDecision::Refuse &&
      check(other).error == static_cast<uint8_t>(UpdateError::Busy));
  memory.running = false;  // Failed earlier: a new job may start.
  assert(check(other).decision == OfferDecision::Accept);
  assert(check(offer).decision == OfferDecision::Repeat);

  char hex[2 * UPDATE_TOKEN_BYTES + 1];
  TurnHubSigil::updateTokenHex(offer.token, hex);
  assert(strcmp(hex, "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff") == 0);
}

}  // namespace

int main() {
  makeKeys();
  sha256KnownAnswer();
  versionRule();
  descriptorLayout();
  goodPackageStreamsThrough();
  badHeadersAreRefusedBeforeAnyWrite();
  badImagesFailAtTheEnd();
  errorNames();
  updateOffers();
  std::cout << "PASS firmware packages: version rule, descriptor, streaming, refused headers and images, update offers\n";
  return 0;
}
