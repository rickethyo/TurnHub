// Pairing v2 (shared/include/pairing_v2.h): key agreement, the code on both
// sides, confirm/reject/timeout, forged verdicts and several Sigils at once.
// Crypto is the host stand-in (test_crypto.h); mbedTLS is checked on device.
#include "pairing_v2.h"
#include "test_crypto.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace TurnHubSecureLink;
using TurnHubProtocol::PacketType;
using TurnHubTest::TestCrypto;

namespace {

const uint8_t ATLAS_MAC[6] = {0xB4, 0xBF, 0xE9, 0x12, 0x85, 0x74};
const uint8_t SIGIL_MAC[6] = {0x20, 0xE7, 0xC8, 0x94, 0x49, 0x80};
const uint8_t OTHER_MAC[6] = {0xF4, 0x65, 0x0B, 0xC4, 0xFF, 0x38};
const uint8_t IMPOSTOR_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

// Sigil asks, Atlas answers, Sigil derives: both now show a code.
void agree(TestCrypto &crypto, SigilPairing &sigil, AtlasPairings &atlas, const uint8_t *mac,
    int32_t token, uint8_t slot, uint32_t nowMs) {
  PairRequest2Packet request;
  PairAccept2Packet accept;
  assert(sigil.begin(crypto, token, request));
  assert(sigil.state() == SigilPairing::State::Requesting);
  assert(atlas.request(crypto, ATLAS_MAC, mac, request, slot, nowMs, accept) != nullptr);
  assert(sigil.accept(crypto, accept, ATLAS_MAC, mac, nowMs));
  assert(sigil.state() == SigilPairing::State::AwaitingConfirm);
}

void confirmStoresTheSameKeyOnBothSides() {
  TestCrypto crypto;
  SigilPairing sigil;
  AtlasPairings atlas;
  agree(crypto, sigil, atlas, SIGIL_MAC, 1111, 2, 1000);
  const PendingPairing *pending = atlas.findSlot(2);
  assert(pending && pending->code == sigil.code() && sigil.slot() == 2);
  assert(atlas.count() == 1);

  PairResultPacket result;
  uint8_t atlasKey[KEY_BYTES], atlasSaw[6], sigilKey[KEY_BYTES];
  assert(atlas.decide(crypto, 2, true, result, atlasKey, atlasSaw));
  assert(result.type == PacketType::PairConfirmed && memcmp(atlasSaw, SIGIL_MAC, 6) == 0);
  assert(atlas.count() == 0 && atlas.findSlot(2) == nullptr);
  assert(sigil.result(crypto, result, ATLAS_MAC, sigilKey) == PairVerdict::Confirmed);
  assert(memcmp(atlasKey, sigilKey, KEY_BYTES) == 0);
  assert(sigil.state() == SigilPairing::State::Idle);
  // Replaying the verdict afterwards does nothing.
  assert(sigil.result(crypto, result, ATLAS_MAC, sigilKey) == PairVerdict::None);
}

void rejectAndTimeoutStoreNothing() {
  TestCrypto crypto;
  SigilPairing sigil;
  AtlasPairings atlas;
  agree(crypto, sigil, atlas, SIGIL_MAC, 2222, 0, 5000);
  PairResultPacket result;
  uint8_t key[KEY_BYTES] = {}, mac[6];
  const uint8_t untouched[KEY_BYTES] = {};
  assert(atlas.decide(crypto, 0, false, result, key, mac));
  assert(result.type == PacketType::PairRejected && memcmp(key, untouched, KEY_BYTES) == 0);
  assert(sigil.result(crypto, result, ATLAS_MAC, key) == PairVerdict::Rejected);
  assert(memcmp(key, untouched, KEY_BYTES) == 0 && sigil.state() == SigilPairing::State::Idle);
  assert(!atlas.decide(crypto, 0, true, result, key, mac));  // Nothing pending now.

  // Atlas times out at PAIR_CONFIRM_TIMEOUT_MS and tells the Sigil.
  agree(crypto, sigil, atlas, SIGIL_MAC, 3333, 0, 10000);
  assert(!atlas.expireOne(crypto, 10000 + PAIR_CONFIRM_TIMEOUT_MS - 1, result, mac));
  assert(atlas.expireOne(crypto, 10000 + PAIR_CONFIRM_TIMEOUT_MS, result, mac));
  assert(result.type == PacketType::PairRejected && memcmp(mac, SIGIL_MAC, 6) == 0);
  assert(atlas.count() == 0);
  assert(sigil.result(crypto, result, ATLAS_MAC, key) == PairVerdict::Rejected);

  // A Sigil that never hears back gives up on its own, a little later.
  agree(crypto, sigil, atlas, SIGIL_MAC, 4444, 0, 20000);
  assert(!sigil.expired(20000 + PAIR_CONFIRM_TIMEOUT_MS));
  assert(sigil.expired(20000 + SIGIL_CONFIRM_WAIT_MS));
  sigil.cancel();
  assert(sigil.state() == SigilPairing::State::Idle && sigil.code() == 0);
}

void forgedVerdictsAreIgnored() {
  TestCrypto crypto;
  SigilPairing sigil;
  AtlasPairings atlas;
  agree(crypto, sigil, atlas, SIGIL_MAC, 5555, 1, 0);
  PairResultPacket result;
  uint8_t key[KEY_BYTES], mac[6];
  assert(atlas.decide(crypto, 1, true, result, key, mac));

  PairResultPacket forged = result;
  forged.mac[0] ^= 1;
  assert(sigil.result(crypto, forged, ATLAS_MAC, key) == PairVerdict::None);
  forged = result;
  forged.token ^= 1;
  assert(sigil.result(crypto, forged, ATLAS_MAC, key) == PairVerdict::None);
  forged = result;
  forged.sigilId = 3;
  assert(sigil.result(crypto, forged, ATLAS_MAC, key) == PairVerdict::None);
  // A confirm can't be turned into a reject (or back) without the key.
  forged = result;
  forged.type = PacketType::PairRejected;
  assert(sigil.result(crypto, forged, ATLAS_MAC, key) == PairVerdict::None);
  // Right bytes, wrong sender.
  assert(sigil.result(crypto, result, IMPOSTOR_MAC, key) == PairVerdict::None);
  // Still waiting, and the genuine verdict still works.
  assert(sigil.state() == SigilPairing::State::AwaitingConfirm);
  assert(sigil.result(crypto, result, ATLAS_MAC, key) == PairVerdict::Confirmed);
}

void acceptsMustMatchTheRequest() {
  TestCrypto crypto;
  SigilPairing sigil;
  AtlasPairings atlas;
  PairRequest2Packet request;
  PairAccept2Packet accept;
  // Nothing requested yet.
  accept.version = TurnHubProtocol::VERSION;
  accept.type = PacketType::PairAccept2;
  accept.token = 77;
  accept.sigilId = 0;
  assert(!sigil.accept(crypto, accept, ATLAS_MAC, SIGIL_MAC, 0));

  assert(sigil.begin(crypto, 77, request));
  assert(atlas.request(crypto, ATLAS_MAC, SIGIL_MAC, request, 0, 0, accept));
  PairAccept2Packet wrong = accept;
  wrong.token = 78;
  assert(!sigil.accept(crypto, wrong, ATLAS_MAC, SIGIL_MAC, 0));
  wrong = accept;
  wrong.sigilId = TurnHubProtocol::MAX_SIGILS;
  assert(!sigil.accept(crypto, wrong, ATLAS_MAC, SIGIL_MAC, 0));
  assert(sigil.accept(crypto, accept, ATLAS_MAC, SIGIL_MAC, 0));
  // Only the first answer counts.
  assert(!sigil.accept(crypto, accept, ATLAS_MAC, SIGIL_MAC, 0));

  // Atlas rejects malformed requests and bad slots.
  PairRequest2Packet bad = request;
  bad.type = PacketType::Hello;
  assert(!atlas.request(crypto, ATLAS_MAC, OTHER_MAC, bad, 1, 0, accept));
  assert(!atlas.request(crypto, ATLAS_MAC, OTHER_MAC, request, TurnHubProtocol::MAX_SIGILS, 0, accept));
}

// A spare's request (SPARE_SIGIL.md) is the same handshake under its own type,
// so Atlas can tell it apart and skip the code check.
void aSpareRequestIsMarkedAndAccepted() {
  TestCrypto crypto;
  SigilPairing sigil;
  AtlasPairings atlas;
  PairRequest2Packet request;
  PairAccept2Packet accept;
  assert(sigil.begin(crypto, 55, request, true));
  assert(request.type == PacketType::PairRequestSpare);
  assert(atlas.request(crypto, ATLAS_MAC, SIGIL_MAC, request, 2, 0, accept));
  assert(sigil.accept(crypto, accept, ATLAS_MAC, SIGIL_MAC, 0));
  assert(sigil.begin(crypto, 56, request));
  assert(request.type == PacketType::PairRequest2);
}

void repeatedRequestsGetTheSameAnswer() {
  TestCrypto crypto;
  SigilPairing sigil;
  AtlasPairings atlas;
  PairRequest2Packet request;
  PairAccept2Packet first, second;
  assert(sigil.begin(crypto, 900, request));
  const PendingPairing *a = atlas.request(crypto, ATLAS_MAC, SIGIL_MAC, request, 4, 0, first);
  const uint16_t code = a->code;
  // The Sigil repeats its broadcast every 2 s until answered.
  const PendingPairing *b = atlas.request(crypto, ATLAS_MAC, SIGIL_MAC, request, 4, 2000, second);
  assert(a == b && b->code == code && atlas.count() == 1);
  assert(memcmp(&first, &second, sizeof(first)) == 0);
  // A new press (new token) starts over with a new key.
  assert(sigil.begin(crypto, 901, request));
  const PendingPairing *c = atlas.request(crypto, ATLAS_MAC, SIGIL_MAC, request, 4, 4000, second);
  assert(c && c->token == 901 && atlas.count() == 1);
  assert(memcmp(first.publicKey, second.publicKey, PUBLIC_KEY_BYTES) != 0);
}

// Someone relays between the two with their own keys: each side ends up with
// a key shared with the impostor, and the codes the owner compares differ.
void anImpostorInTheMiddleShowsDifferentCodes() {
  TestCrypto crypto;
  SigilPairing sigil, impostorAsSigil;
  AtlasPairings atlas, impostorAsAtlas;
  PairRequest2Packet realRequest, fakeRequest;
  PairAccept2Packet realAccept, fakeAccept;
  assert(sigil.begin(crypto, 42, realRequest));
  // The impostor answers the Sigil itself...
  assert(impostorAsAtlas.request(crypto, IMPOSTOR_MAC, SIGIL_MAC, realRequest, 0, 0, fakeAccept));
  assert(sigil.accept(crypto, fakeAccept, IMPOSTOR_MAC, SIGIL_MAC, 0));
  // ...and pairs with Atlas as if it were the Sigil.
  assert(impostorAsSigil.begin(crypto, 42, fakeRequest));
  const PendingPairing *atAtlas =
      atlas.request(crypto, ATLAS_MAC, SIGIL_MAC, fakeRequest, 0, 0, realAccept);
  assert(atAtlas && atAtlas->code != sigil.code());
}

void severalSigilsWaitAtOnce() {
  TestCrypto crypto;
  SigilPairing one, two, three;
  AtlasPairings atlas;
  const uint8_t macThree[6] = {1, 2, 3, 4, 5, 6};
  agree(crypto, one, atlas, SIGIL_MAC, 1, 0, 0);
  agree(crypto, two, atlas, OTHER_MAC, 2, 1, 100);
  agree(crypto, three, atlas, macThree, 3, 2, 200);
  assert(atlas.count() == 3);
  assert(atlas.findSlot(0)->code == one.code() && atlas.findSlot(1)->code == two.code() &&
      atlas.findSlot(2)->code == three.code());

  PairResultPacket result;
  uint8_t key[KEY_BYTES], mac[6];
  assert(atlas.decide(crypto, 1, true, result, key, mac));
  assert(memcmp(mac, OTHER_MAC, 6) == 0 && atlas.count() == 2);
  // A verdict for one Sigil means nothing to another.
  assert(one.result(crypto, result, ATLAS_MAC, key) == PairVerdict::None);
  assert(two.result(crypto, result, ATLAS_MAC, key) == PairVerdict::Confirmed);

  atlas.cancelSlot(0);
  assert(atlas.count() == 1 && atlas.findSlot(0) == nullptr);
  atlas.cancelAll();
  assert(atlas.count() == 0);

  // A different Sigil asking for a slot that is pending replaces it.
  agree(crypto, one, atlas, SIGIL_MAC, 10, 5, 0);
  agree(crypto, two, atlas, OTHER_MAC, 11, 5, 0);
  assert(atlas.count() == 1 && memcmp(atlas.findSlot(5)->mac, OTHER_MAC, 6) == 0);
}

}  // namespace

int main() {
  confirmStoresTheSameKeyOnBothSides();
  rejectAndTimeoutStoreNothing();
  forgedVerdictsAreIgnored();
  acceptsMustMatchTheRequest();
  aSpareRequestIsMarkedAndAccepted();
  repeatedRequestsGetTheSameAnswer();
  anImpostorInTheMiddleShowsDifferentCodes();
  severalSigilsWaitAtOnce();
  std::cout << "PASS pairing v2: agreement, codes, confirm, reject, timeout, forged verdicts, spare requests, repeats, impostor, several pending\n";
  return 0;
}
