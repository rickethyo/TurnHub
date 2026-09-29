// Secure link (shared/include/secure_link.h): pairing derivation, handshake
// MACs, sessions, sealing, tamper and replay rejection. Uses a deterministic
// stand-in for the crypto so the link logic is testable on the host; it is
// NOT cryptography. The real mbedTLS backend is checked on the device.
#include "secure_link.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace TurnHubSecureLink;
using TurnHubProtocol::PacketType;

namespace {

// Stand-in primitives: sensitive to every input byte, reproducible, not secure.
class TestCrypto : public Crypto {
 public:
  bool generateKeyPair(uint8_t priv[SECRET_BYTES], uint8_t pub[PUBLIC_KEY_BYTES]) override {
    memset(priv, 0, SECRET_BYTES);
    memset(pub, 0, PUBLIC_KEY_BYTES);
    const uint32_t exponent = 2 + next() % (P - 3);
    put32(priv, exponent);
    put32(pub, power(G, exponent));
    return true;
  }
  bool sharedSecret(const uint8_t priv[SECRET_BYTES], const uint8_t peer[PUBLIC_KEY_BYTES],
      uint8_t secret[SECRET_BYTES]) override {
    memset(secret, 0, SECRET_BYTES);
    put32(secret, power(get32(peer), get32(priv)));
    return true;
  }
  bool hmac(const uint8_t *key, size_t keyLength, const Bytes *parts, size_t count,
      uint8_t out[HMAC_BYTES]) override {
    uint64_t lanes[4] = {0x243F6A8885A308D3ull, 0x13198A2E03707344ull,
        0xA4093822299F31D0ull, 0x082EFA98EC4E6C89ull};
    absorb(lanes, key, keyLength);
    absorb(lanes, reinterpret_cast<const uint8_t *>("|"), 1);
    for (size_t i = 0; i < count; ++i) {
      absorb(lanes, parts[i].data, parts[i].length);
      absorb(lanes, reinterpret_cast<const uint8_t *>("|"), 1);
    }
    for (int i = 0; i < 32; ++i) {
      absorb(lanes, reinterpret_cast<const uint8_t *>(&i), 1);
      out[i] = static_cast<uint8_t>(lanes[i % 4] >> 24);
    }
    return true;
  }
  bool seal(const uint8_t key[KEY_BYTES], const uint8_t nonce[CCM_NONCE_BYTES],
      const uint8_t *aad, size_t aadLength, const uint8_t *plain, size_t length,
      uint8_t *cipher, uint8_t tag[TAG_BYTES]) override {
    stream(key, nonce, plain, length, cipher);
    return makeTag(key, nonce, aad, aadLength, cipher, length, tag);
  }
  bool open(const uint8_t key[KEY_BYTES], const uint8_t nonce[CCM_NONCE_BYTES],
      const uint8_t *aad, size_t aadLength, const uint8_t *cipher, size_t length,
      const uint8_t tag[TAG_BYTES], uint8_t *plain) override {
    uint8_t expected[TAG_BYTES];
    makeTag(key, nonce, aad, aadLength, cipher, length, expected);
    if (!equalBytes(expected, tag, TAG_BYTES)) return false;
    stream(key, nonce, cipher, length, plain);
    return true;
  }
  void randomBytes(uint8_t *out, size_t length) override {
    for (size_t i = 0; i < length; ++i) out[i] = static_cast<uint8_t>(next() >> 16);
  }

 private:
  static constexpr uint32_t P = 2147483647u;  // 2^31 - 1
  static constexpr uint32_t G = 16807u;
  uint32_t seed_ = 12345;

  uint32_t next() { return seed_ = seed_ * 1103515245u + 12345u; }
  static uint32_t power(uint64_t base, uint32_t exponent) {
    uint64_t result = 1;
    base %= P;
    while (exponent) {
      if (exponent & 1) result = result * base % P;
      base = base * base % P;
      exponent >>= 1;
    }
    return static_cast<uint32_t>(result);
  }
  static void put32(uint8_t *out, uint32_t v) { memcpy(out, &v, 4); }
  static uint32_t get32(const uint8_t *in) { uint32_t v; memcpy(&v, in, 4); return v; }
  static void absorb(uint64_t lanes[4], const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
      for (int l = 0; l < 4; ++l) {
        lanes[l] = (lanes[l] ^ (data[i] + 0x9E3779B97F4A7C15ull * (l + 1))) * 0x100000001B3ull;
        lanes[l] ^= lanes[(l + 1) % 4] >> 29;
      }
    }
  }
  void stream(const uint8_t *key, const uint8_t *nonce, const uint8_t *in, size_t length,
      uint8_t *out) {
    uint8_t block[HMAC_BYTES];
    for (size_t i = 0; i < length; ++i) {
      if (i % HMAC_BYTES == 0) {
        const uint32_t index = static_cast<uint32_t>(i / HMAC_BYTES);
        const Bytes parts[] = {{nonce, CCM_NONCE_BYTES},
            {reinterpret_cast<const uint8_t *>(&index), 4}};
        hmac(key, KEY_BYTES, parts, 2, block);
      }
      out[i] = in[i] ^ block[i % HMAC_BYTES];
    }
  }
  bool makeTag(const uint8_t *key, const uint8_t *nonce, const uint8_t *aad, size_t aadLength,
      const uint8_t *cipher, size_t length, uint8_t tag[TAG_BYTES]) {
    const Bytes parts[] = {{reinterpret_cast<const uint8_t *>("tag"), 3},
        {nonce, CCM_NONCE_BYTES}, {aad, aadLength}, {cipher, length}};
    uint8_t out[HMAC_BYTES];
    hmac(key, KEY_BYTES, parts, 4, out);
    memcpy(tag, out, TAG_BYTES);
    return true;
  }
};

const uint8_t ATLAS_MAC[6] = {0x24, 0x6F, 0x28, 0x01, 0x02, 0x03};
const uint8_t SIGIL_MAC[6] = {0x24, 0x6F, 0x28, 0x0A, 0x0B, 0x0C};

struct Side {
  uint8_t priv[SECRET_BYTES];
  uint8_t pub[PUBLIC_KEY_BYTES];
};

// Runs the key agreement from both ends; returns both results.
void pairBoth(TestCrypto &crypto, PairingResult &atlas, PairingResult &sigil,
    const uint8_t *tamperedSigilPub = nullptr) {
  Side a, s;
  assert(crypto.generateKeyPair(a.priv, a.pub));
  assert(crypto.generateKeyPair(s.priv, s.pub));
  const uint8_t *sigilPubSeenByAtlas = tamperedSigilPub ? tamperedSigilPub : s.pub;
  uint8_t secretA[SECRET_BYTES], secretS[SECRET_BYTES];
  assert(crypto.sharedSecret(a.priv, sigilPubSeenByAtlas, secretA));
  assert(crypto.sharedSecret(s.priv, a.pub, secretS));
  assert(derivePairing(crypto, secretA, ATLAS_MAC, SIGIL_MAC, a.pub, sigilPubSeenByAtlas, atlas));
  assert(derivePairing(crypto, secretS, ATLAS_MAC, SIGIL_MAC, a.pub, s.pub, sigil));
}

void layoutsAreStable() {
  assert(sizeof(TurnHubProtocol::Packet) + SECURE_OVERHEAD == 22);
  assert(sizeof(TurnHubProtocol::GameDisplayPacket) + SECURE_OVERHEAD == 125);
  assert(SECURE_OVERHEAD == 15 && MAX_INNER_BYTES == 235);
}

void pairingAgreesAndCodesCatchAnImpostor() {
  TestCrypto crypto;
  PairingResult atlas, sigil;
  pairBoth(crypto, atlas, sigil);
  assert(equalBytes(atlas.pairKey, sigil.pairKey, KEY_BYTES));
  assert(atlas.code == sigil.code && atlas.code < PAIRING_CODE_MODULUS);

  // An impostor swaps in its own key: Atlas ends up with a different key
  // and, almost always, a different code, which the owner sees.
  uint8_t impostorPriv[SECRET_BYTES], impostorPub[PUBLIC_KEY_BYTES];
  crypto.generateKeyPair(impostorPriv, impostorPub);
  PairingResult atlas2, sigil2;
  pairBoth(crypto, atlas2, sigil2, impostorPub);
  assert(!equalBytes(atlas2.pairKey, sigil2.pairKey, KEY_BYTES));
  assert(atlas2.code != sigil2.code);

  char text[5];
  formatPairingCode(7, text);
  assert(strcmp(text, "0007") == 0);
  formatPairingCode(9999, text);
  assert(strcmp(text, "9999") == 0);
}

void handshakeMacsRejectTampering() {
  TestCrypto crypto;
  PairingResult atlas, sigil;
  pairBoth(crypto, atlas, sigil);

  SecureHelloPacket hello{TurnHubProtocol::VERSION, PacketType::SecureHello, 3,
      TurnHubProtocol::encodeHelloInfo(0, 9, 0, 0x40), {}, {}};
  crypto.randomBytes(hello.nonce, NONCE_BYTES);
  assert(helloMac(crypto, sigil.pairKey, hello, hello.mac));
  uint8_t check[MAC_BYTES];
  assert(helloMac(crypto, atlas.pairKey, hello, check) && equalBytes(check, hello.mac, MAC_BYTES));
  SecureHelloPacket forged = hello;
  forged.sigilId = 4;
  assert(helloMac(crypto, atlas.pairKey, forged, check) && !equalBytes(check, forged.mac, MAC_BYTES));
  forged = hello;
  forged.info ^= 1;
  assert(helloMac(crypto, atlas.pairKey, forged, check) && !equalBytes(check, forged.mac, MAC_BYTES));
  uint8_t wrongKey[KEY_BYTES] = {};
  assert(helloMac(crypto, wrongKey, hello, check) && !equalBytes(check, hello.mac, MAC_BYTES));

  SecureHelloAckPacket ack{TurnHubProtocol::VERSION, PacketType::SecureHelloAck, 3, {}, {}, {}};
  memcpy(ack.sigilNonce, hello.nonce, NONCE_BYTES);
  crypto.randomBytes(ack.atlasNonce, NONCE_BYTES);
  assert(helloAckMac(crypto, atlas.pairKey, ack, ack.mac));
  assert(helloAckMac(crypto, sigil.pairKey, ack, check) && equalBytes(check, ack.mac, MAC_BYTES));
  // The same bytes can't pass as a different handshake step.
  uint8_t asHello[MAC_BYTES];
  assert(helloMac(crypto, sigil.pairKey, hello, asHello));
  assert(pairResultMac(crypto, sigil.pairKey,
      PairResultPacket{TurnHubProtocol::VERSION, PacketType::PairConfirmed, 3, 77, {}}, check));
  assert(!equalBytes(check, asHello, MAC_BYTES));
  PairResultPacket confirmed{TurnHubProtocol::VERSION, PacketType::PairConfirmed, 3, 77, {}};
  PairResultPacket rejected{TurnHubProtocol::VERSION, PacketType::PairRejected, 3, 77, {}};
  uint8_t macConfirmed[MAC_BYTES], macRejected[MAC_BYTES];
  pairResultMac(crypto, atlas.pairKey, confirmed, macConfirmed);
  pairResultMac(crypto, atlas.pairKey, rejected, macRejected);
  assert(!equalBytes(macConfirmed, macRejected, MAC_BYTES));
}

// Both ends of one session, keyed from fresh nonces.
void startSession(TestCrypto &crypto, const PairingResult &pairing, uint8_t sigilId,
    Channel &atlas, Channel &sigil) {
  uint8_t sigilNonce[NONCE_BYTES], atlasNonce[NONCE_BYTES];
  crypto.randomBytes(sigilNonce, NONCE_BYTES);
  crypto.randomBytes(atlasNonce, NONCE_BYTES);
  uint8_t keyA[KEY_BYTES], keyS[KEY_BYTES];
  assert(deriveSessionKey(crypto, pairing.pairKey, sigilId, sigilNonce, atlasNonce, keyA));
  assert(deriveSessionKey(crypto, pairing.pairKey, sigilId, sigilNonce, atlasNonce, keyS));
  assert(equalBytes(keyA, keyS, KEY_BYTES));
  atlas.start(keyA, Direction::AtlasToSigil);
  sigil.start(keyS, Direction::SigilToAtlas);
}

void sealedFramesRoundTripAndRejectTampering() {
  TestCrypto crypto;
  PairingResult atlasPair, sigilPair;
  pairBoth(crypto, atlasPair, sigilPair);
  Channel atlas, sigil;
  uint8_t frame[ESPNOW_MAX_BYTES], inner[ESPNOW_MAX_BYTES];

  // No session yet: nothing seals or opens.
  const auto pass = TurnHubProtocol::makePacket(PacketType::Pass, 2, 0);
  assert(sigil.seal(crypto, 2, reinterpret_cast<const uint8_t *>(&pass), sizeof(pass),
      frame, sizeof(frame)) == 0);

  startSession(crypto, atlasPair, 2, atlas, sigil);

  const size_t length = sigil.seal(crypto, 2, reinterpret_cast<const uint8_t *>(&pass),
      sizeof(pass), frame, sizeof(frame));
  assert(length == sizeof(pass) + SECURE_OVERHEAD);
  // The payload is not in the clear.
  assert(memcmp(frame + sizeof(SecureHeader), &pass, sizeof(pass)) != 0);
  assert(atlas.open(crypto, 2, frame, length, inner, sizeof(inner)) == sizeof(pass));
  assert(memcmp(inner, &pass, sizeof(pass)) == 0);

  // Replay of the same frame, and any tampered byte, are dropped.
  assert(atlas.open(crypto, 2, frame, length, inner, sizeof(inner)) == 0);
  const size_t next = sigil.seal(crypto, 2, reinterpret_cast<const uint8_t *>(&pass),
      sizeof(pass), frame, sizeof(frame));
  for (size_t i = 0; i < next; ++i) {
    uint8_t bad[ESPNOW_MAX_BYTES];
    memcpy(bad, frame, next);
    bad[i] ^= 0x01;
    assert(atlas.open(crypto, 2, bad, next, inner, sizeof(inner)) == 0);
  }
  assert(atlas.open(crypto, 2, frame, next - 1, inner, sizeof(inner)) == 0);
  // Wrong Sigil slot, or Atlas's own frame reflected back at it.
  assert(atlas.open(crypto, 5, frame, next, inner, sizeof(inner)) == 0);
  assert(atlas.open(crypto, 2, frame, next, inner, sizeof(inner)) == sizeof(pass));
  const size_t out = atlas.seal(crypto, 2, reinterpret_cast<const uint8_t *>(&pass),
      sizeof(pass), frame, sizeof(frame));
  assert(atlas.open(crypto, 2, frame, out, inner, sizeof(inner)) == 0);
  assert(sigil.open(crypto, 2, frame, out, inner, sizeof(inner)) == sizeof(pass));

  // The largest packet fits; one byte past the limit doesn't.
  TurnHubProtocol::GameDisplayPacket display{};
  display.version = TurnHubProtocol::VERSION;
  display.type = PacketType::GameDisplay;
  display.sigilId = 2;
  strcpy(display.primary.name, "Ricky");
  const size_t big = atlas.seal(crypto, 2, reinterpret_cast<const uint8_t *>(&display),
      sizeof(display), frame, sizeof(frame));
  assert(big == 125);
  assert(sigil.open(crypto, 2, frame, big, inner, sizeof(inner)) == sizeof(display));
  assert(memcmp(inner, &display, sizeof(display)) == 0);
  uint8_t tooBig[MAX_INNER_BYTES + 1] = {};
  assert(atlas.seal(crypto, 2, tooBig, sizeof(tooBig), frame, sizeof(frame)) == 0);
}

void outOfOrderAndOldSessionsAreDropped() {
  TestCrypto crypto;
  PairingResult atlasPair, sigilPair;
  pairBoth(crypto, atlasPair, sigilPair);
  Channel atlas, sigil;
  startSession(crypto, atlasPair, 1, atlas, sigil);
  const auto packet = TurnHubProtocol::makePacket(PacketType::ActionWin, 1, 0);
  const uint8_t *raw = reinterpret_cast<const uint8_t *>(&packet);
  uint8_t first[64], second[64], inner[64];
  const size_t n1 = sigil.seal(crypto, 1, raw, sizeof(packet), first, sizeof(first));
  const size_t n2 = sigil.seal(crypto, 1, raw, sizeof(packet), second, sizeof(second));
  // A newer frame arriving first makes the older one stale.
  assert(atlas.open(crypto, 1, second, n2, inner, sizeof(inner)) == sizeof(packet));
  assert(atlas.open(crypto, 1, first, n1, inner, sizeof(inner)) == 0);

  // A win claim recorded in an earlier session can't be replayed later.
  Channel atlas2, sigil2;
  startSession(crypto, atlasPair, 1, atlas2, sigil2);
  assert(atlas2.open(crypto, 1, second, n2, inner, sizeof(inner)) == 0);
  const size_t fresh = sigil2.seal(crypto, 1, raw, sizeof(packet), first, sizeof(first));
  assert(atlas2.open(crypto, 1, first, fresh, inner, sizeof(inner)) == sizeof(packet));

  // reset() ends the session.
  atlas2.reset();
  assert(!atlas2.ready());
  assert(atlas2.open(crypto, 1, first, fresh, inner, sizeof(inner)) == 0);
}

void eightSigilsHaveIndependentSessions() {
  TestCrypto crypto;
  Channel atlas[8], sigil[8];
  PairingResult pairs[8];
  for (uint8_t id = 0; id < 8; ++id) {
    PairingResult sigilSide;
    pairBoth(crypto, pairs[id], sigilSide);
    startSession(crypto, pairs[id], id, atlas[id], sigil[id]);
  }
  const auto pass = TurnHubProtocol::makePacket(PacketType::Pass, 0, 0);
  uint8_t frame[64], inner[64];
  for (uint8_t id = 0; id < 8; ++id) {
    const size_t n = sigil[id].seal(crypto, id, reinterpret_cast<const uint8_t *>(&pass),
        sizeof(pass), frame, sizeof(frame));
    for (uint8_t other = 0; other < 8; ++other) {
      // Only that Sigil's own session on Atlas opens its frame.
      const size_t opened = atlas[other].open(crypto, other, frame, n, inner, sizeof(inner));
      assert((opened != 0) == (other == id));
    }
  }
}

}  // namespace

int main() {
  layoutsAreStable();
  pairingAgreesAndCodesCatchAnImpostor();
  handshakeMacsRejectTampering();
  sealedFramesRoundTripAndRejectTampering();
  outOfOrderAndOldSessionsAreDropped();
  eightSigilsHaveIndependentSessions();
  // The boot self-test rejects a backend that isn't the real algorithms: the
  // stand-in fails the first published vector. (mbedTLS passes on the device.)
  {
    TestCrypto crypto;
    assert(knownAnswerTest(crypto) == SelfTestStep::X25519Public);
  }
  std::cout << "PASS secure link: layouts, pairing codes, handshake MACs, sealing, tamper, replay, 8 sessions\n";
  return 0;
}
