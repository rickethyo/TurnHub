// Secure link (shared/include/secure_link.h): pairing derivation, handshake
// MACs, sessions, sealing, tamper and replay rejection. Uses a deterministic
// stand-in for the crypto so the link logic is testable on the host; it is
// NOT cryptography. The real mbedTLS backend is checked on the device.
#include "secure_link.h"
#include "test_crypto.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace TurnHubSecureLink;
using TurnHubProtocol::PacketType;
using TurnHubTest::TestCrypto;

namespace {

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
  const auto pass = TurnHubProtocol::makePacket(PacketType::SelectAction, 2, 0);
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
  const auto packet = TurnHubProtocol::makePacket(PacketType::LifeAdjust, 1, 0);
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
  const auto pass = TurnHubProtocol::makePacket(PacketType::SelectAction, 0, 0);
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
