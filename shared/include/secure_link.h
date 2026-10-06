#pragma once

// Secure Atlas-Sigil link: wire layouts, key derivation and the sealed
// envelope with replay protection. Planned; no firmware sends these yet.
// Design and the reasons for application-layer encryption (not ESP-NOW's
// built-in kind, which is capped at 7 encrypted peers) are in
// Documentation/engineering/PAIRING_AND_SECURE_LINK.md.
//
// The algorithms themselves come from a Crypto backend: mbedTLS on the
// ESP32s (X25519, AES-128-CCM, HMAC-SHA256), a deterministic stand-in in the
// host tests. Nothing here implements a cipher.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "protocol.h"

namespace TurnHubSecureLink {

using TurnHubProtocol::PacketType;

constexpr size_t KEY_BYTES = 16;         // Pair and session keys (AES-128).
constexpr size_t PUBLIC_KEY_BYTES = 32;  // X25519.
constexpr size_t SECRET_BYTES = 32;      // X25519 shared secret, private key.
constexpr size_t HMAC_BYTES = 32;        // HMAC-SHA256 output.
constexpr size_t NONCE_BYTES = 8;        // Session nonces in the Hello exchange.
constexpr size_t MAC_BYTES = 8;          // Truncated HMAC on handshake packets.
constexpr size_t TAG_BYTES = 8;          // AES-CCM tag on sealed frames.
constexpr size_t CCM_NONCE_BYTES = 13;
constexpr size_t ESPNOW_MAX_BYTES = 250;

// Atlas discards an unconfirmed pairing after this long.
constexpr uint32_t PAIR_CONFIRM_TIMEOUT_MS = 60000;
constexpr uint16_t PAIRING_CODE_MODULUS = 10000;  // Four digits.

enum class Direction : uint8_t { AtlasToSigil = 1, SigilToAtlas = 2 };

// --- Wire layouts (little-endian, like Packet) ------------------------------

// Sigil -> broadcast, during its pairing window.
struct __attribute__((packed)) PairRequest2Packet {
  uint8_t version;
  PacketType type;  // PairRequest2
  int32_t token;
  uint8_t publicKey[PUBLIC_KEY_BYTES];
};
static_assert(sizeof(PairRequest2Packet) == 38, "PairRequest2 layout changed");

// Atlas -> Sigil, during Atlas's pairing window.
struct __attribute__((packed)) PairAccept2Packet {
  uint8_t version;
  PacketType type;  // PairAccept2
  uint8_t sigilId;
  int32_t token;
  uint8_t publicKey[PUBLIC_KEY_BYTES];
};
static_assert(sizeof(PairAccept2Packet) == 39, "PairAccept2 layout changed");

// Atlas -> Sigil once the owner confirms or rejects the pairing code. The MAC
// proves Atlas holds the same new pair key.
struct __attribute__((packed)) PairResultPacket {
  uint8_t version;
  PacketType type;  // PairConfirmed or PairRejected
  uint8_t sigilId;
  int32_t token;
  uint8_t mac[MAC_BYTES];
};
static_assert(sizeof(PairResultPacket) == 15, "Pair result layout changed");

// Sigil -> Atlas: starts a session. info is the existing encodeHelloInfo value.
struct __attribute__((packed)) SecureHelloPacket {
  uint8_t version;
  PacketType type;  // SecureHello
  uint8_t sigilId;
  int32_t info;
  uint8_t nonce[NONCE_BYTES];
  uint8_t mac[MAC_BYTES];
};
static_assert(sizeof(SecureHelloPacket) == 23, "SecureHello layout changed");

// Atlas -> Sigil: completes the session.
struct __attribute__((packed)) SecureHelloAckPacket {
  uint8_t version;
  PacketType type;  // SecureHelloAck
  uint8_t sigilId;
  uint8_t sigilNonce[NONCE_BYTES];
  uint8_t atlasNonce[NONCE_BYTES];
  uint8_t mac[MAC_BYTES];
};
static_assert(sizeof(SecureHelloAckPacket) == 27, "SecureHelloAck layout changed");

// Sealed frame: this header (also the CCM additional data), the ciphertext of
// one complete existing packet, then the tag.
struct __attribute__((packed)) SecureHeader {
  uint8_t version;
  PacketType type;  // Secure
  uint8_t sigilId;
  uint32_t counter;
};
static_assert(sizeof(SecureHeader) == 7, "Secure header layout changed");

constexpr size_t SECURE_OVERHEAD = sizeof(SecureHeader) + TAG_BYTES;
constexpr size_t MAX_INNER_BYTES = ESPNOW_MAX_BYTES - SECURE_OVERHEAD;
static_assert(sizeof(TurnHubProtocol::GameDisplayPacket) <= MAX_INNER_BYTES,
    "The largest packet must still fit once sealed");

// --- Crypto backend ---------------------------------------------------------

struct Bytes {
  const uint8_t *data;
  size_t length;
};

class Crypto {
 public:
  virtual ~Crypto() = default;
  // A fresh X25519 key pair from the hardware RNG.
  virtual bool generateKeyPair(uint8_t privateKey[SECRET_BYTES],
      uint8_t publicKey[PUBLIC_KEY_BYTES]) = 0;
  virtual bool sharedSecret(const uint8_t privateKey[SECRET_BYTES],
      const uint8_t peerPublicKey[PUBLIC_KEY_BYTES], uint8_t secret[SECRET_BYTES]) = 0;
  // HMAC-SHA256 over the concatenation of parts.
  virtual bool hmac(const uint8_t *key, size_t keyLength, const Bytes *parts,
      size_t partCount, uint8_t out[HMAC_BYTES]) = 0;
  // AES-128-CCM with a TAG_BYTES tag. open() returns false on a bad tag and
  // must not leave usable plaintext behind in that case.
  virtual bool seal(const uint8_t key[KEY_BYTES], const uint8_t nonce[CCM_NONCE_BYTES],
      const uint8_t *aad, size_t aadLength, const uint8_t *plain, size_t length,
      uint8_t *cipher, uint8_t tag[TAG_BYTES]) = 0;
  virtual bool open(const uint8_t key[KEY_BYTES], const uint8_t nonce[CCM_NONCE_BYTES],
      const uint8_t *aad, size_t aadLength, const uint8_t *cipher, size_t length,
      const uint8_t tag[TAG_BYTES], uint8_t *plain) = 0;
  virtual void randomBytes(uint8_t *out, size_t length) = 0;
};

// Compares without an early exit, so timing doesn't reveal a partial match.
inline bool equalBytes(const uint8_t *a, const uint8_t *b, size_t length) {
  uint8_t difference = 0;
  for (size_t i = 0; i < length; ++i) difference |= a[i] ^ b[i];
  return difference == 0;
}

inline void wipe(void *data, size_t length) {
  volatile uint8_t *p = static_cast<volatile uint8_t *>(data);
  while (length--) *p++ = 0;
}

// --- Derivations ------------------------------------------------------------
// Every HMAC input starts with its own label, so no value can stand in for
// another.

struct PairingResult {
  uint8_t pairKey[KEY_BYTES];
  uint16_t code;  // 0-9999, shown with leading zeros.
};

// Both sides run this with the same arguments in the same order.
inline bool derivePairing(Crypto &crypto, const uint8_t secret[SECRET_BYTES],
    const uint8_t atlasMac[6], const uint8_t sigilMac[6],
    const uint8_t atlasPublicKey[PUBLIC_KEY_BYTES],
    const uint8_t sigilPublicKey[PUBLIC_KEY_BYTES], PairingResult &result) {
  static const uint8_t LABEL[] = "TurnHub link v1 pair";
  const Bytes parts[] = {{LABEL, sizeof(LABEL) - 1}, {atlasMac, 6}, {sigilMac, 6},
      {atlasPublicKey, PUBLIC_KEY_BYTES}, {sigilPublicKey, PUBLIC_KEY_BYTES}};
  uint8_t out[HMAC_BYTES];
  if (!crypto.hmac(secret, SECRET_BYTES, parts, 5, out)) return false;
  memcpy(result.pairKey, out, KEY_BYTES);
  const uint32_t codeBits = static_cast<uint32_t>(out[16]) |
      (static_cast<uint32_t>(out[17]) << 8) | (static_cast<uint32_t>(out[18]) << 16) |
      (static_cast<uint32_t>(out[19]) << 24);
  result.code = static_cast<uint16_t>(codeBits % PAIRING_CODE_MODULUS);
  wipe(out, sizeof(out));
  return true;
}

// "0427": always four digits plus the terminator.
inline void formatPairingCode(uint16_t code, char out[5]) {
  code %= PAIRING_CODE_MODULUS;
  for (int i = 3; i >= 0; --i) {
    out[i] = static_cast<char>('0' + code % 10);
    code /= 10;
  }
  out[4] = '\0';
}

inline bool truncatedMac(Crypto &crypto, const uint8_t pairKey[KEY_BYTES],
    const Bytes *parts, size_t count, uint8_t mac[MAC_BYTES]) {
  uint8_t out[HMAC_BYTES];
  if (!crypto.hmac(pairKey, KEY_BYTES, parts, count, out)) return false;
  memcpy(mac, out, MAC_BYTES);
  wipe(out, sizeof(out));
  return true;
}

inline bool pairResultMac(Crypto &crypto, const uint8_t pairKey[KEY_BYTES],
    const PairResultPacket &packet, uint8_t mac[MAC_BYTES]) {
  static const uint8_t LABEL[] = "TurnHub link v1 pair result";
  const Bytes parts[] = {{LABEL, sizeof(LABEL) - 1},
      {reinterpret_cast<const uint8_t *>(&packet), offsetof(PairResultPacket, mac)}};
  return truncatedMac(crypto, pairKey, parts, 2, mac);
}

inline bool helloMac(Crypto &crypto, const uint8_t pairKey[KEY_BYTES],
    const SecureHelloPacket &packet, uint8_t mac[MAC_BYTES]) {
  static const uint8_t LABEL[] = "TurnHub link v1 hello";
  const Bytes parts[] = {{LABEL, sizeof(LABEL) - 1},
      {reinterpret_cast<const uint8_t *>(&packet), offsetof(SecureHelloPacket, mac)}};
  return truncatedMac(crypto, pairKey, parts, 2, mac);
}

inline bool helloAckMac(Crypto &crypto, const uint8_t pairKey[KEY_BYTES],
    const SecureHelloAckPacket &packet, uint8_t mac[MAC_BYTES]) {
  static const uint8_t LABEL[] = "TurnHub link v1 hello ack";
  const Bytes parts[] = {{LABEL, sizeof(LABEL) - 1},
      {reinterpret_cast<const uint8_t *>(&packet), offsetof(SecureHelloAckPacket, mac)}};
  return truncatedMac(crypto, pairKey, parts, 2, mac);
}

inline bool deriveSessionKey(Crypto &crypto, const uint8_t pairKey[KEY_BYTES],
    uint8_t sigilId, const uint8_t sigilNonce[NONCE_BYTES],
    const uint8_t atlasNonce[NONCE_BYTES], uint8_t sessionKey[KEY_BYTES]) {
  static const uint8_t LABEL[] = "TurnHub link v1 session";
  const Bytes parts[] = {{LABEL, sizeof(LABEL) - 1}, {&sigilId, 1},
      {sigilNonce, NONCE_BYTES}, {atlasNonce, NONCE_BYTES}};
  uint8_t out[HMAC_BYTES];
  if (!crypto.hmac(pairKey, KEY_BYTES, parts, 4, out)) return false;
  memcpy(sessionKey, out, KEY_BYTES);
  wipe(out, sizeof(out));
  return true;
}

// --- Session channel ----------------------------------------------------------
// One per Sigil on Atlas, one on the Sigil. Counters start at 1 in each
// direction and must rise; a new session (new nonces, so a new key) resets
// them, and frames from the old session no longer open.

class Channel {
 public:
  void reset() {
    wipe(key_, sizeof(key_));
    txCounter_ = 0;
    rxCounter_ = 0;
    ready_ = false;
  }

  void start(const uint8_t sessionKey[KEY_BYTES], Direction outgoing) {
    memcpy(key_, sessionKey, KEY_BYTES);
    outgoing_ = outgoing;
    txCounter_ = 0;
    rxCounter_ = 0;
    ready_ = true;
  }

  bool ready() const { return ready_; }

  // Seals one complete packet. Returns the frame length, or 0 (no session,
  // too large, counter exhausted or crypto failure).
  size_t seal(Crypto &crypto, uint8_t sigilId, const uint8_t *inner, size_t length,
      uint8_t *frame, size_t capacity) {
    if (!ready_ || length == 0 || length > MAX_INNER_BYTES ||
        capacity < length + SECURE_OVERHEAD || txCounter_ == UINT32_MAX) {
      return 0;
    }
    SecureHeader header{TurnHubProtocol::VERSION, PacketType::Secure, sigilId, txCounter_ + 1};
    uint8_t nonce[CCM_NONCE_BYTES];
    makeNonce(outgoing_, sigilId, header.counter, nonce);
    memcpy(frame, &header, sizeof(header));
    if (!crypto.seal(key_, nonce, frame, sizeof(header), inner, length,
            frame + sizeof(header), frame + sizeof(header) + length)) {
      return 0;
    }
    ++txCounter_;
    return length + SECURE_OVERHEAD;
  }

  // Opens a sealed frame from the other side. Returns the inner packet
  // length, or 0 for anything that must be dropped: no session, wrong Sigil,
  // bad length or tag, or a counter not above the last accepted one.
  size_t open(Crypto &crypto, uint8_t sigilId, const uint8_t *frame, size_t length,
      uint8_t *inner, size_t capacity) {
    if (!ready_ || length <= SECURE_OVERHEAD || length > ESPNOW_MAX_BYTES) return 0;
    const size_t innerLength = length - SECURE_OVERHEAD;
    if (capacity < innerLength) return 0;
    SecureHeader header;
    memcpy(&header, frame, sizeof(header));
    if (header.version != TurnHubProtocol::VERSION || header.type != PacketType::Secure ||
        header.sigilId != sigilId || header.counter <= rxCounter_) {
      return 0;
    }
    uint8_t nonce[CCM_NONCE_BYTES];
    makeNonce(incoming(), sigilId, header.counter, nonce);
    if (!crypto.open(key_, nonce, frame, sizeof(header), frame + sizeof(header),
            innerLength, frame + sizeof(header) + innerLength, inner)) {
      return 0;
    }
    rxCounter_ = header.counter;
    return innerLength;
  }

 private:
  Direction incoming() const {
    return outgoing_ == Direction::AtlasToSigil ? Direction::SigilToAtlas
                                                : Direction::AtlasToSigil;
  }

  // Unique per key: the direction and counter never repeat within a session.
  static void makeNonce(Direction direction, uint8_t sigilId, uint32_t counter,
      uint8_t nonce[CCM_NONCE_BYTES]) {
    memset(nonce, 0, CCM_NONCE_BYTES);
    nonce[0] = static_cast<uint8_t>(direction);
    nonce[1] = sigilId;
    for (int i = 0; i < 4; ++i) nonce[2 + i] = static_cast<uint8_t>(counter >> (8 * i));
  }

  uint8_t key_[KEY_BYTES] = {};
  Direction outgoing_ = Direction::AtlasToSigil;
  uint32_t txCounter_ = 0;
  uint32_t rxCounter_ = 0;
  bool ready_ = false;
};

// --- Known-answer self-test ---------------------------------------------------
// Checks a backend against published vectors before any key is trusted:
// X25519 (RFC 7748 section 6.1), HMAC-SHA256 (RFC 4231 test case 2) and
// AES-128-CCM with an 8-byte tag (RFC 3610 packet vector 1). The expected
// bytes were confirmed with an independent library (Python cryptography,
// 2026-09-28). Then a fresh key agreement and one sealed round trip.
// Returns 0 on success, otherwise the number of the first failed check.

enum class SelfTestStep : uint8_t {
  Pass = 0, X25519Public, X25519Shared, Hmac, CcmSeal, CcmOpen, CcmTamper, Envelope,
};

inline SelfTestStep knownAnswerTest(Crypto &crypto) {
  static const uint8_t alicePrivate[32] = {
      0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d, 0x3c, 0x16, 0xc1, 0x72, 0x51, 0xb2, 0x66, 0x45,
      0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0, 0x99, 0x2a, 0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9, 0x2c, 0x2a};
  static const uint8_t alicePublic[32] = {
      0x85, 0x20, 0xf0, 0x09, 0x89, 0x30, 0xa7, 0x54, 0x74, 0x8b, 0x7d, 0xdc, 0xb4, 0x3e, 0xf7, 0x5a,
      0x0d, 0xbf, 0x3a, 0x0d, 0x26, 0x38, 0x1a, 0xf4, 0xeb, 0xa4, 0xa9, 0x8e, 0xaa, 0x9b, 0x4e, 0x6a};
  static const uint8_t bobPublic[32] = {
      0xde, 0x9e, 0xdb, 0x7d, 0x7b, 0x7d, 0xc1, 0xb4, 0xd3, 0x5b, 0x61, 0xc2, 0xec, 0xe4, 0x35, 0x37,
      0x3f, 0x83, 0x43, 0xc8, 0x5b, 0x78, 0x67, 0x4d, 0xad, 0xfc, 0x7e, 0x14, 0x6f, 0x88, 0x2b, 0x4f};
  static const uint8_t shared[32] = {
      0x4a, 0x5d, 0x9d, 0x5b, 0xa4, 0xce, 0x2d, 0xe1, 0x72, 0x8e, 0x3b, 0xf4, 0x80, 0x35, 0x0f, 0x25,
      0xe0, 0x7e, 0x21, 0xc9, 0x47, 0xd1, 0x9e, 0x33, 0x76, 0xf0, 0x9b, 0x3c, 0x1e, 0x16, 0x17, 0x42};
  static const uint8_t hmacExpected[32] = {
      0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e, 0x6a, 0x04, 0x24, 0x26, 0x08, 0x95, 0x75, 0xc7,
      0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27, 0x39, 0x83, 0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43};
  static const uint8_t ccmCipher[23] = {
      0x58, 0x8c, 0x97, 0x9a, 0x61, 0xc6, 0x63, 0xd2, 0xf0, 0x66, 0xd0, 0xc2, 0xc0, 0xf9, 0x89, 0x80,
      0x6d, 0x5f, 0x6b, 0x61, 0xda, 0xc3, 0x84};
  static const uint8_t ccmTag[8] = {0x17, 0xe8, 0xd1, 0x2c, 0xfd, 0xf9, 0x26, 0xe0};
  static const uint8_t ccmNonce[13] = {
      0x00, 0x00, 0x00, 0x03, 0x02, 0x01, 0x00, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5};

  // X25519 is the slow part on the ESP32, so the check runs only two scalar
  // multiplications from RFC 7748 section 6.1: a public key and a shared
  // secret.
  uint8_t out[32];
  const uint8_t basePoint[32] = {9};
  if (!crypto.sharedSecret(alicePrivate, basePoint, out) || !equalBytes(out, alicePublic, 32)) {
    return SelfTestStep::X25519Public;
  }
  if (!crypto.sharedSecret(alicePrivate, bobPublic, out) || !equalBytes(out, shared, 32)) {
    return SelfTestStep::X25519Shared;
  }

  static const uint8_t hmacKey[] = "Jefe";
  static const uint8_t hmacData[] = "what do ya want for nothing?";
  const Bytes hmacParts[] = {{hmacData, 10}, {hmacData + 10, sizeof(hmacData) - 1 - 10}};
  if (!crypto.hmac(hmacKey, 4, hmacParts, 2, out) || !equalBytes(out, hmacExpected, 32)) {
    return SelfTestStep::Hmac;
  }

  uint8_t key[16], aad[8], plain[23], cipher[23], tag[8], back[23];
  for (uint8_t i = 0; i < 16; ++i) key[i] = static_cast<uint8_t>(0xc0 + i);
  for (uint8_t i = 0; i < 8; ++i) aad[i] = i;
  for (uint8_t i = 0; i < 23; ++i) plain[i] = static_cast<uint8_t>(8 + i);
  if (!crypto.seal(key, ccmNonce, aad, 8, plain, 23, cipher, tag) ||
      !equalBytes(cipher, ccmCipher, 23) || !equalBytes(tag, ccmTag, 8)) {
    return SelfTestStep::CcmSeal;
  }
  if (!crypto.open(key, ccmNonce, aad, 8, cipher, 23, tag, back) || !equalBytes(back, plain, 23)) {
    return SelfTestStep::CcmOpen;
  }
  tag[0] ^= 0x01;
  if (crypto.open(key, ccmNonce, aad, 8, cipher, 23, tag, back)) return SelfTestStep::CcmTamper;

  // A pair key derived from the known shared secret seals and opens one packet.
  const uint8_t macA[6] = {1, 2, 3, 4, 5, 6}, macB[6] = {6, 5, 4, 3, 2, 1};
  PairingResult pairing;
  uint8_t session[KEY_BYTES], nonceA[NONCE_BYTES] = {1}, nonceB[NONCE_BYTES] = {2};
  uint8_t frame[ESPNOW_MAX_BYTES], inner[ESPNOW_MAX_BYTES];
  const TurnHubProtocol::Packet packet = TurnHubProtocol::makePacket(PacketType::SelectAction, 1, 42);
  Channel sender, receiver;
  bool ok = derivePairing(crypto, shared, macA, macB, alicePublic, bobPublic, pairing) &&
      deriveSessionKey(crypto, pairing.pairKey, 1, nonceA, nonceB, session);
  if (ok) {
    sender.start(session, Direction::SigilToAtlas);
    receiver.start(session, Direction::AtlasToSigil);
    const size_t length = sender.seal(crypto, 1, reinterpret_cast<const uint8_t *>(&packet),
        sizeof(packet), frame, sizeof(frame));
    ok = length != 0 &&
        receiver.open(crypto, 1, frame, length, inner, sizeof(inner)) == sizeof(packet) &&
        memcmp(inner, &packet, sizeof(packet)) == 0;
  }
  wipe(&pairing, sizeof(pairing));
  wipe(session, sizeof(session));
  sender.reset();
  receiver.reset();
  return ok ? SelfTestStep::Pass : SelfTestStep::Envelope;
}

}  // namespace TurnHubSecureLink
