#pragma once

// Signed firmware packages (.thfw) for Atlas and Sigil updates: the embedded
// build descriptor, the 128-byte signed header, the version rule, and a
// streaming reader that checks a package while it is written to flash.
// Design: Documentation/engineering/SIGIL_OTA.md ("Firmware package").
//
// The algorithms come from a PackageCrypto backend: mbedTLS SHA-256 and
// ECDSA P-256 on the ESP32s, a stand-in in the host tests. The packaging tool
// (tools/firmware/thfw.py) writes the same layout.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace TurnHubFirmwarePackage {

constexpr size_t HASH_BYTES = 32;        // SHA-256.
constexpr size_t SIGNATURE_BYTES = 64;   // ECDSA P-256, r || s.
constexpr size_t PUBLIC_KEY_BYTES = 65;  // Uncompressed P-256 point (0x04 || x || y).
constexpr size_t KEY_ID_BYTES = 4;
constexpr size_t BUILD_ID_BYTES = 8;
constexpr uint8_t HEADER_FORMAT = 1;
constexpr size_t SIGNED_BYTES = 64;      // The header bytes the signature covers.

// Portal is the Atlas web portal pack (Atlas/web): not a firmware image but a
// signed file archive that Atlas unpacks onto its SD card (PORTAL_PACK.md).
enum class Product : uint8_t { Atlas = 1, SigilEink = 2, SigilOled = 3, Portal = 4 };

inline bool knownProduct(uint8_t product) {
  return product >= static_cast<uint8_t>(Product::Atlas) &&
      product <= static_cast<uint8_t>(Product::Portal);
}

struct Version {
  uint8_t major;
  uint8_t minor;
  uint8_t patch;
};

// <0, 0 or >0, like strcmp.
inline int compareVersions(const Version &a, const Version &b) {
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
  return 0;
}

// A device takes the same version (reinstall) or newer, never older: a
// downgrade needs USB.
inline bool versionAllowed(const Version &running, const Version &offered) {
  return compareVersions(offered, running) >= 0;
}

// --- Embedded descriptor ------------------------------------------------------
// Every Atlas and Sigil image carries exactly one, so the packaging tool reads
// the product and version from the image itself. Its magic appears nowhere
// else in firmware (nothing on the device searches for it).
struct __attribute__((packed)) Descriptor {
  char magic[8];  // "THFWDSC1", not NUL-terminated.
  uint8_t product;
  Version version;
  uint8_t radioProtocol;
  uint8_t reserved[3];
};
static_assert(sizeof(Descriptor) == 16, "Firmware descriptor layout changed");

// Defines the image's descriptor. Use once per firmware, at namespace scope,
// and read it through the returned reference so the linker keeps it.
#define TURNHUB_FIRMWARE_DESCRIPTOR(name, product, major, minor, patch, radio)        \
  extern "C" __attribute__((used)) const ::TurnHubFirmwarePackage::Descriptor name = { \
      {'T', 'H', 'F', 'W', 'D', 'S', 'C', '1'},                                    \
      static_cast<uint8_t>(product),                                               \
      {major, minor, patch},                                                       \
      radio,                                                                       \
      {0, 0, 0}}

// --- Package header -----------------------------------------------------------
struct __attribute__((packed)) Header {
  char magic[8];  // "THFWPKG1"
  uint8_t format;
  uint8_t product;
  Version version;
  uint8_t radioProtocol;
  uint16_t flags;  // 0; reserved.
  uint32_t imageSize;
  uint8_t imageHash[HASH_BYTES];
  uint8_t buildId[BUILD_ID_BYTES];
  uint8_t keyId[KEY_ID_BYTES];
  uint8_t signature[SIGNATURE_BYTES];  // Over SHA-256 of bytes 0-63.
};
static_assert(sizeof(Header) == 128, "Package header layout changed");
static_assert(offsetof(Header, signature) == SIGNED_BYTES, "Signature must follow the signed bytes");
static_assert(offsetof(Header, imageSize) == 16 && offsetof(Header, imageHash) == 20 &&
        offsetof(Header, buildId) == 52 && offsetof(Header, keyId) == 60,
    "Package header offsets changed (tools/firmware/thfw.py must match)");

constexpr char PACKAGE_MAGIC[8] = {'T', 'H', 'F', 'W', 'P', 'K', 'G', '1'};

// --- Backend ------------------------------------------------------------------
class PackageCrypto {
 public:
  virtual ~PackageCrypto() = default;
  // One running SHA-256 at a time.
  virtual bool hashBegin() = 0;
  virtual bool hashUpdate(const uint8_t *data, size_t length) = 0;
  virtual bool hashFinish(uint8_t out[HASH_BYTES]) = 0;
  // ECDSA P-256 over a SHA-256 digest.
  virtual bool verify(const uint8_t publicKey[PUBLIC_KEY_BYTES], const uint8_t digest[HASH_BYTES],
      const uint8_t signature[SIGNATURE_BYTES]) = 0;
};

constexpr uint8_t productBit(Product product) {
  return static_cast<uint8_t>(1u << static_cast<uint8_t>(product));
}
constexpr uint8_t ANY_SIGIL = productBit(Product::SigilEink) | productBit(Product::SigilOled);

// The trusted key and what the device will accept.
struct Policy {
  const uint8_t *publicKey;  // PUBLIC_KEY_BYTES
  const uint8_t *keyId;      // KEY_ID_BYTES
  uint8_t products;          // productBit()s: a device takes its own; Atlas stages any Sigil's.
  Version running;
  uint32_t maxImageSize;
};

enum class Error : uint8_t {
  None = 0,
  BadMagic = 1,
  BadFormat = 2,
  UnknownKey = 3,
  BadSignature = 4,
  WrongProduct = 5,
  OlderVersion = 6,
  TooLarge = 7,
  Truncated = 8,
  TooLong = 9,
  HashMismatch = 10,
  Storage = 11,  // The sink refused a write.
  Crypto = 12,
};

inline const char *errorName(Error error) {
  switch (error) {
    case Error::None: return "none";
    case Error::BadMagic: return "notPackage";
    case Error::BadFormat: return "badFormat";
    case Error::UnknownKey: return "unknownKey";
    case Error::BadSignature: return "badSignature";
    case Error::WrongProduct: return "wrongProduct";
    case Error::OlderVersion: return "olderVersion";
    case Error::TooLarge: return "tooLarge";
    case Error::Truncated: return "truncated";
    case Error::TooLong: return "tooLong";
    case Error::HashMismatch: return "hashMismatch";
    case Error::Storage: return "storage";
    case Error::Crypto: return "crypto";
  }
  return "unknown";
}

// The same outcome as a sentence for the person uploading (portal pages);
// errorName() stays the log and wire spelling.
inline const char *errorMessage(Error error) {
  switch (error) {
    case Error::None: return "No error";
    case Error::BadMagic: return "That file is not a TurnHub firmware package (.thfw)";
    case Error::BadFormat: return "The package is damaged or from an unknown format";
    case Error::UnknownKey: return "The package is signed with a key this device does not trust";
    case Error::BadSignature: return "The package signature is not valid";
    case Error::WrongProduct: return "The package is for a different kind of device";
    case Error::OlderVersion: return "Only the same or a newer version can be installed; a downgrade needs USB";
    case Error::TooLarge: return "The firmware image is too large for this device";
    case Error::Truncated: return "The upload ended early; try again";
    case Error::TooLong: return "The upload has extra data after the package";
    case Error::HashMismatch: return "The firmware image does not match its signature";
    case Error::Storage: return "The device could not write the firmware to flash";
    case Error::Crypto: return "The device could not check the package";
  }
  return "The firmware update failed";
}

// Where the checked bytes go: Atlas stages the whole package, a Sigil writes
// only the image into its idle slot. header() comes once, after the header
// has passed every check; image() only after that.
class Sink {
 public:
  virtual ~Sink() = default;
  virtual bool header(const Header &header) = 0;
  virtual bool image(const uint8_t *data, size_t length) = 0;
};

// Checks a header on its own (magic, format, key, signature, product,
// version, size). Uses the crypto's hash.
inline Error checkHeader(PackageCrypto &crypto, const Policy &policy, const Header &header) {
  if (memcmp(header.magic, PACKAGE_MAGIC, sizeof(PACKAGE_MAGIC)) != 0) return Error::BadMagic;
  if (header.format != HEADER_FORMAT || header.flags != 0) return Error::BadFormat;
  if (memcmp(header.keyId, policy.keyId, KEY_ID_BYTES) != 0) return Error::UnknownKey;
  uint8_t digest[HASH_BYTES];
  if (!crypto.hashBegin() ||
      !crypto.hashUpdate(reinterpret_cast<const uint8_t *>(&header), SIGNED_BYTES) ||
      !crypto.hashFinish(digest)) {
    return Error::Crypto;
  }
  if (!crypto.verify(policy.publicKey, digest, header.signature)) return Error::BadSignature;
  // Only a signed header's fields are worth reading.
  if (header.product > 7 || (policy.products & (1u << header.product)) == 0) {
    return Error::WrongProduct;
  }
  if (!versionAllowed(policy.running, header.version)) return Error::OlderVersion;
  if (header.imageSize == 0 || header.imageSize > policy.maxImageSize) return Error::TooLarge;
  return Error::None;
}

// Streams a package: collects and checks the header, then passes image bytes
// to the sink while hashing them. The first error sticks.
class Reader {
 public:
  void begin(PackageCrypto &crypto, const Policy &policy, Sink &sink) {
    crypto_ = &crypto;
    policy_ = policy;
    sink_ = &sink;
    headerFill_ = 0;
    imageWritten_ = 0;
    error_ = Error::None;
    headerOk_ = false;
    finished_ = false;
  }

  bool write(const uint8_t *data, size_t length) {
    if (error_ != Error::None || crypto_ == nullptr || finished_) return false;
    while (length > 0 && !headerOk_) {
      const size_t take = min(length, sizeof(Header) - headerFill_);
      memcpy(reinterpret_cast<uint8_t *>(&header_) + headerFill_, data, take);
      headerFill_ += take;
      data += take;
      length -= take;
      if (headerFill_ < sizeof(Header)) return true;
      const Error headerError = checkHeader(*crypto_, policy_, header_);
      if (headerError != Error::None) return fail(headerError);
      if (!crypto_->hashBegin()) return fail(Error::Crypto);
      if (!sink_->header(header_)) return fail(Error::Storage);
      headerOk_ = true;
    }
    if (length == 0) return true;
    if (length > header_.imageSize - imageWritten_) return fail(Error::TooLong);
    if (!crypto_->hashUpdate(data, length)) return fail(Error::Crypto);
    if (!sink_->image(data, length)) return fail(Error::Storage);
    imageWritten_ += static_cast<uint32_t>(length);
    return true;
  }

  // After the last write: the whole image arrived and matches the signed hash.
  bool finish() {
    if (error_ != Error::None || crypto_ == nullptr || finished_) return false;
    if (!headerOk_ || imageWritten_ != header_.imageSize) return fail(Error::Truncated);
    uint8_t digest[HASH_BYTES];
    if (!crypto_->hashFinish(digest)) return fail(Error::Crypto);
    uint8_t difference = 0;
    for (size_t i = 0; i < HASH_BYTES; ++i) difference |= digest[i] ^ header_.imageHash[i];
    if (difference != 0) return fail(Error::HashMismatch);
    finished_ = true;
    return true;
  }

  Error error() const { return error_; }
  bool headerOk() const { return headerOk_; }
  bool finished() const { return finished_; }
  const Header &header() const { return header_; }
  uint32_t imageWritten() const { return imageWritten_; }
  // Total package bytes a complete package has (0 until the header is in).
  uint32_t packageSize() const {
    return headerOk_ ? static_cast<uint32_t>(sizeof(Header)) + header_.imageSize : 0;
  }

 private:
  static size_t min(size_t a, size_t b) { return a < b ? a : b; }
  bool fail(Error error) {
    error_ = error;
    return false;
  }

  PackageCrypto *crypto_ = nullptr;
  Policy policy_ = {};
  Sink *sink_ = nullptr;
  Header header_ = {};
  size_t headerFill_ = 0;
  uint32_t imageWritten_ = 0;
  Error error_ = Error::None;
  bool headerOk_ = false;
  bool finished_ = false;
};

// The Sigil build's product, from the same flags that pick its display.
inline Product productForSigilCapabilities(uint8_t capabilities, uint8_t oledCapabilityBit) {
  return (capabilities & oledCapabilityBit) != 0 ? Product::SigilOled : Product::SigilEink;
}

}  // namespace TurnHubFirmwarePackage
