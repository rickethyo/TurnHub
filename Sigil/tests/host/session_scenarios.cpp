// Secure sessions (shared/include/secure_session.h): the SecureHello /
// SecureHelloAck handshake, sealed traffic both ways, and everything that
// must not open: forged or stale handshakes, old sessions, wrong keys.
// Crypto is the host stand-in (test_crypto.h); mbedTLS is checked on device.
#include "secure_session.h"
#include "test_crypto.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace TurnHubSecureLink;
using TurnHubProtocol::Packet;
using TurnHubProtocol::PacketType;
using TurnHubTest::TestCrypto;

namespace {

const uint8_t PAIR_KEY[KEY_BYTES] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
const int32_t INFO = TurnHubProtocol::encodeHelloInfo(0, 9, 0, 0x60);

// One full handshake; both ends ready.
void connect(TestCrypto &crypto, SigilSession &sigil, AtlasSession &atlas, uint8_t slot) {
  SecureHelloPacket hello;
  SecureHelloAckPacket ack;
  assert(sigil.makeHello(crypto, INFO, hello) && !sigil.ready());
  assert(hello.sigilId == slot && hello.info == INFO);
  assert(atlas.acceptHello(crypto, PAIR_KEY, slot, hello, ack) && atlas.ready());
  assert(sigil.acceptAck(crypto, ack) && sigil.ready());
}

void handshakeThenSealedTrafficBothWays() {
  TestCrypto crypto;
  SigilSession sigil;
  AtlasSession atlas;
  // No key: nothing to say.
  SecureHelloPacket hello;
  assert(!sigil.makeHello(crypto, INFO, hello));
  sigil.configure(2, PAIR_KEY);
  connect(crypto, sigil, atlas, 2);

  uint8_t frame[ESPNOW_MAX_BYTES], inner[ESPNOW_MAX_BYTES];
  const Packet pass = TurnHubProtocol::makePacket(PacketType::SelectAction, 2, 0);
  size_t n = sigil.seal(crypto, &pass, sizeof(pass), frame, sizeof(frame));
  assert(n == sizeof(pass) + SECURE_OVERHEAD);
  PacketType type;
  assert(framePacketType(frame, n, type) && type == PacketType::Secure);
  assert(atlas.open(crypto, 2, frame, n, inner, sizeof(inner)) == sizeof(pass));
  assert(memcmp(inner, &pass, sizeof(pass)) == 0);

  TurnHubProtocol::GameDisplayPacket display{};
  display.version = TurnHubProtocol::VERSION;
  display.type = PacketType::GameDisplay;
  display.sigilId = 2;
  n = atlas.seal(crypto, 2, &display, sizeof(display), frame, sizeof(frame));
  assert(n == 125);
  assert(sigil.open(crypto, frame, n, inner, sizeof(inner)) == sizeof(display));
}

void forgedAndStaleHandshakesFail() {
  TestCrypto crypto;
  SigilSession sigil;
  AtlasSession atlas;
  sigil.configure(1, PAIR_KEY);
  SecureHelloPacket hello;
  SecureHelloAckPacket ack;
  assert(sigil.makeHello(crypto, INFO, hello));

  // Atlas: wrong slot, tampered info or MAC, wrong key.
  SecureHelloPacket bad = hello;
  assert(!atlas.acceptHello(crypto, PAIR_KEY, 3, bad, ack));
  bad.info ^= 1;
  assert(!atlas.acceptHello(crypto, PAIR_KEY, 1, bad, ack));
  bad = hello;
  bad.mac[0] ^= 1;
  assert(!atlas.acceptHello(crypto, PAIR_KEY, 1, bad, ack));
  uint8_t otherKey[KEY_BYTES] = {};
  assert(!atlas.acceptHello(crypto, otherKey, 1, hello, ack) && !atlas.ready());

  // Sigil: an ack must answer its latest Hello, from the same key.
  assert(atlas.acceptHello(crypto, PAIR_KEY, 1, hello, ack));
  SecureHelloAckPacket badAck = ack;
  badAck.atlasNonce[0] ^= 1;
  assert(!sigil.acceptAck(crypto, badAck));
  badAck = ack;
  badAck.sigilNonce[0] ^= 1;
  assert(!sigil.acceptAck(crypto, badAck));
  assert(!sigil.ready());
  // A newer Hello makes this ack stale.
  SecureHelloPacket newer;
  assert(sigil.makeHello(crypto, INFO, newer));
  assert(!sigil.acceptAck(crypto, ack));
  // The ack for the newer Hello works, once.
  SecureHelloAckPacket fresh;
  assert(atlas.acceptHello(crypto, PAIR_KEY, 1, newer, fresh));
  assert(sigil.acceptAck(crypto, fresh) && sigil.ready());
  assert(!sigil.acceptAck(crypto, fresh));
}

void newSessionsLeaveOldFramesBehind() {
  TestCrypto crypto;
  SigilSession sigil;
  AtlasSession atlas;
  sigil.configure(0, PAIR_KEY);
  connect(crypto, sigil, atlas, 0);
  uint8_t old[64], inner[64];
  const Packet win = TurnHubProtocol::makePacket(PacketType::LifeAdjust, 0, 0);
  const size_t n = sigil.seal(crypto, &win, sizeof(win), old, sizeof(old));

  // Reconnect (Atlas restarted, or the link dropped): the recorded win claim
  // no longer opens, and the new session works.
  connect(crypto, sigil, atlas, 0);
  assert(atlas.open(crypto, 0, old, n, inner, sizeof(inner)) == 0);
  uint8_t now[64];
  const size_t m = sigil.seal(crypto, &win, sizeof(win), now, sizeof(now));
  assert(atlas.open(crypto, 0, now, m, inner, sizeof(inner)) == sizeof(win));

  // A replayed Hello restarts Atlas's session with a new nonce: the current
  // Sigil traffic stops opening until the Sigil says Hello again.
  SecureHelloPacket hello;
  SecureHelloAckPacket ack;
  assert(sigil.makeHello(crypto, INFO, hello));
  assert(!sigil.ready());  // Waiting for Atlas; nothing is sealed meanwhile.
  assert(sigil.seal(crypto, &win, sizeof(win), now, sizeof(now)) == 0);
  assert(atlas.acceptHello(crypto, PAIR_KEY, 0, hello, ack));
  SecureHelloAckPacket replayAck;
  assert(atlas.acceptHello(crypto, PAIR_KEY, 0, hello, replayAck));  // Replayed.
  assert(memcmp(ack.atlasNonce, replayAck.atlasNonce, NONCE_BYTES) != 0);
  assert(sigil.acceptAck(crypto, ack));  // The first answer...
  const size_t k = sigil.seal(crypto, &win, sizeof(win), now, sizeof(now));
  assert(atlas.open(crypto, 0, now, k, inner, sizeof(inner)) == 0);  // ...no longer matches.

  // clear() ends everything.
  sigil.clear();
  assert(!sigil.hasKey() && !sigil.ready());
  atlas.clear();
  assert(!atlas.ready());
}

// The handshake is frozen so an out-of-date Sigil stays updatable: any
// version from MIN_UPDATABLE_VERSION up is taken (SIGIL_OTA.md), older isn't.
void handshakeToleratesNewerVersions() {
  TestCrypto crypto;
  SigilSession sigil;
  AtlasSession atlas;
  sigil.configure(4, PAIR_KEY);
  SecureHelloPacket hello;
  SecureHelloAckPacket ack;

  // A newer Sigil's Hello (re-MACed as that Sigil would) reaches this Atlas.
  assert(sigil.makeHello(crypto, INFO, hello));
  SecureHelloPacket newer = hello;
  newer.version = TurnHubProtocol::VERSION + 1;
  assert(helloMac(crypto, PAIR_KEY, newer, newer.mac));
  assert(atlas.acceptHello(crypto, PAIR_KEY, 4, newer, ack));

  // A newer Atlas's ack reaches this Sigil.
  assert(atlas.acceptHello(crypto, PAIR_KEY, 4, hello, ack));
  SecureHelloAckPacket newerAck = ack;
  newerAck.version = TurnHubProtocol::VERSION + 1;
  assert(helloAckMac(crypto, PAIR_KEY, newerAck, newerAck.mac));
  assert(sigil.acceptAck(crypto, newerAck) && sigil.ready());

  // Below MIN_UPDATABLE_VERSION: refused both ways, even correctly MACed.
  assert(sigil.makeHello(crypto, INFO, hello));
  SecureHelloPacket old = hello;
  old.version = TurnHubProtocol::MIN_UPDATABLE_VERSION - 1;
  assert(helloMac(crypto, PAIR_KEY, old, old.mac));
  assert(!atlas.acceptHello(crypto, PAIR_KEY, 4, old, ack));
  assert(atlas.acceptHello(crypto, PAIR_KEY, 4, hello, ack));
  SecureHelloAckPacket oldAck = ack;
  oldAck.version = TurnHubProtocol::MIN_UPDATABLE_VERSION - 1;
  assert(helloAckMac(crypto, PAIR_KEY, oldAck, oldAck.mac));
  assert(!sigil.acceptAck(crypto, oldAck));
}

// The update packets: offer validation and the status encoding.
void updatePackets() {
  using namespace TurnHubProtocol;
  SigilUpdateOfferPacket offer{};
  offer.version = VERSION;
  offer.type = PacketType::SigilUpdateOffer;
  offer.sigilId = 3;
  offer.product = 3;
  offer.packageSize = 900000;
  strcpy(offer.ssid, "TurnHub-Atlas");
  strcpy(offer.password, "TurnHub-Setup");
  assert(validUpdateOffer(offer));
  SigilUpdateOfferPacket bad = offer;
  bad.version = VERSION + 1;
  assert(validUpdateOffer(bad));  // A newer Atlas may update this Sigil.
  bad.version = MIN_UPDATABLE_VERSION - 1;
  assert(!validUpdateOffer(bad));
  bad = offer;
  bad.product = 1;  // Atlas firmware is never offered to a Sigil.
  assert(!validUpdateOffer(bad));
  bad = offer;
  bad.sigilId = MAX_SIGILS;
  assert(!validUpdateOffer(bad));
  bad = offer;
  bad.packageSize = 128;
  assert(!validUpdateOffer(bad));
  bad = offer;
  bad.ssid[0] = '\0';
  assert(!validUpdateOffer(bad));
  bad = offer;
  memset(bad.password, 'x', sizeof(bad.password));  // Not terminated.
  assert(!validUpdateOffer(bad));
  bad = offer;
  memset(bad.ssid, 'x', sizeof(bad.ssid));
  assert(!validUpdateOffer(bad));

  // Sealed, it fits one frame.
  assert(sizeof(offer) + SECURE_OVERHEAD <= ESPNOW_MAX_BYTES);

  UpdateStatusFields f{};
  assert(decodeUpdateStatus(encodeUpdateStatus(UpdateStage::Downloading, 40, 0, 0xAB), f));
  assert(f.stage == UpdateStage::Downloading && f.progress == 40 && f.error == 0 &&
      f.tokenTag == 0xAB);
  assert(decodeUpdateStatus(
      encodeUpdateStatus(UpdateStage::Failed, 0, static_cast<uint8_t>(UpdateError::WifiJoin), 7), f));
  assert(f.stage == UpdateStage::Failed && f.error == 20 && f.tokenTag == 7);
  assert(decodeUpdateStatus(encodeUpdateStatus(UpdateStage::Downloading, 250, 0, 0), f) &&
      f.progress == 100);
  assert(!decodeUpdateStatus(0, f));
  assert(!decodeUpdateStatus(6, f));
  assert(!decodeUpdateStatus(2 | (101 << 8), f));
}

}  // namespace

int main() {
  handshakeThenSealedTrafficBothWays();
  forgedAndStaleHandshakesFail();
  newSessionsLeaveOldFramesBehind();
  handshakeToleratesNewerVersions();
  updatePackets();
  std::cout << "PASS secure sessions: handshake, sealed traffic both ways, forged/stale handshakes, old sessions, replayed Hello, version tolerance, update packets\n";
  return 0;
}
