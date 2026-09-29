#pragma once

// Secure sessions (SECURE_LINK.md): each time a Sigil connects, a signed
// SecureHello / SecureHelloAck exchange agrees a fresh session key from the
// pair key and both sides' random nonces, then every packet travels sealed in
// a Channel (secure_link.h). A packet recorded in an earlier session never
// opens in a later one. Pure logic over the Crypto interface, shared by both
// firmwares, the TestHarness and the Wokwi fake Atlas; host-tested.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "secure_link.h"

namespace TurnHubSecureLink {

// --- Sigil end -----------------------------------------------------------------

class SigilSession {
 public:
  // The saved pair key and slot (paired with Atlas). Ends any session.
  void configure(uint8_t sigilId, const uint8_t pairKey[KEY_BYTES]) {
    clear();
    memcpy(pairKey_, pairKey, KEY_BYTES);
    sigilId_ = sigilId;
    hasKey_ = true;
  }
  void clear() {
    wipe(pairKey_, sizeof(pairKey_));
    wipe(nonce_, sizeof(nonce_));
    channel_.reset();
    hasKey_ = false;
    helloPending_ = false;
  }
  bool hasKey() const { return hasKey_; }
  bool ready() const { return channel_.ready(); }

  // A fresh SecureHello (info: encodeHelloInfo). Ends the current session:
  // nothing is sealed or opened until Atlas answers this Hello.
  bool makeHello(Crypto &crypto, int32_t info, SecureHelloPacket &out) {
    if (!hasKey_) return false;
    channel_.reset();
    crypto.randomBytes(nonce_, NONCE_BYTES);
    out.version = TurnHubProtocol::VERSION;
    out.type = PacketType::SecureHello;
    out.sigilId = sigilId_;
    out.info = info;
    memcpy(out.nonce, nonce_, NONCE_BYTES);
    if (!helloMac(crypto, pairKey_, out, out.mac)) return false;
    helloPending_ = true;
    return true;
  }

  // Atlas's answer to the latest Hello (and only that one) starts the session.
  // Any updatable protocol version is taken, so a newer Atlas can still update
  // this Sigil (MIN_UPDATABLE_VERSION); the caller decides what else to accept.
  bool acceptAck(Crypto &crypto, const SecureHelloAckPacket &ack) {
    if (!hasKey_ || !helloPending_ || !TurnHubProtocol::updatableVersion(ack.version) ||
        ack.type != PacketType::SecureHelloAck || ack.sigilId != sigilId_ ||
        !equalBytes(ack.sigilNonce, nonce_, NONCE_BYTES)) {
      return false;
    }
    uint8_t expected[MAC_BYTES];
    if (!helloAckMac(crypto, pairKey_, ack, expected) ||
        !equalBytes(expected, ack.mac, MAC_BYTES)) {
      return false;
    }
    uint8_t sessionKey[KEY_BYTES];
    if (!deriveSessionKey(crypto, pairKey_, sigilId_, nonce_, ack.atlasNonce, sessionKey)) {
      return false;
    }
    channel_.start(sessionKey, Direction::SigilToAtlas);
    wipe(sessionKey, sizeof(sessionKey));
    helloPending_ = false;
    return true;
  }

  size_t seal(Crypto &crypto, const void *inner, size_t length, uint8_t *frame, size_t capacity) {
    return channel_.seal(crypto, sigilId_, static_cast<const uint8_t *>(inner), length, frame,
        capacity);
  }
  size_t open(Crypto &crypto, const uint8_t *frame, size_t length, uint8_t *inner,
      size_t capacity) {
    return channel_.open(crypto, sigilId_, frame, length, inner, capacity);
  }
  // A new slot from Atlas (it keeps a Sigil's slot, so this is rare).
  uint8_t sigilId() const { return sigilId_; }

 private:
  uint8_t pairKey_[KEY_BYTES] = {};
  uint8_t nonce_[NONCE_BYTES] = {};
  uint8_t sigilId_ = 0;
  bool hasKey_ = false;
  bool helloPending_ = false;
  Channel channel_;
};

// --- Atlas end, one per paired Sigil -------------------------------------------

class AtlasSession {
 public:
  void clear() { channel_.reset(); }
  bool ready() const { return channel_.ready(); }

  // A SecureHello from the Sigil in `sigilId` with its pair key. A valid one
  // starts a new session (replacing any old one) and fills the answer. A
  // replayed Hello only restarts the session with a fresh Atlas nonce, so the
  // replayer learns nothing; the real Sigil reconnects on its next Hello.
  // Any updatable protocol version is taken (MIN_UPDATABLE_VERSION): hello.version
  // tells the caller whether the Sigil needs an update.
  bool acceptHello(Crypto &crypto, const uint8_t pairKey[KEY_BYTES], uint8_t sigilId,
      const SecureHelloPacket &hello, SecureHelloAckPacket &ack) {
    if (!TurnHubProtocol::updatableVersion(hello.version) || hello.type != PacketType::SecureHello ||
        hello.sigilId != sigilId) {
      return false;
    }
    uint8_t expected[MAC_BYTES];
    if (!helloMac(crypto, pairKey, hello, expected) ||
        !equalBytes(expected, hello.mac, MAC_BYTES)) {
      return false;
    }
    ack.version = TurnHubProtocol::VERSION;
    ack.type = PacketType::SecureHelloAck;
    ack.sigilId = sigilId;
    memcpy(ack.sigilNonce, hello.nonce, NONCE_BYTES);
    crypto.randomBytes(ack.atlasNonce, NONCE_BYTES);
    uint8_t sessionKey[KEY_BYTES];
    if (!helloAckMac(crypto, pairKey, ack, ack.mac) ||
        !deriveSessionKey(crypto, pairKey, sigilId, hello.nonce, ack.atlasNonce, sessionKey)) {
      return false;
    }
    channel_.start(sessionKey, Direction::AtlasToSigil);
    wipe(sessionKey, sizeof(sessionKey));
    return true;
  }

  size_t seal(Crypto &crypto, uint8_t sigilId, const void *inner, size_t length, uint8_t *frame,
      size_t capacity) {
    return channel_.seal(crypto, sigilId, static_cast<const uint8_t *>(inner), length, frame,
        capacity);
  }
  size_t open(Crypto &crypto, uint8_t sigilId, const uint8_t *frame, size_t length,
      uint8_t *inner, size_t capacity) {
    return channel_.open(crypto, sigilId, frame, length, inner, capacity);
  }

 private:
  Channel channel_;
};

// A received frame's packet type (byte 1 in every layout), without trusting it.
inline bool framePacketType(const uint8_t *frame, size_t length, PacketType &type) {
  if (frame == nullptr || length < 2) return false;
  type = static_cast<PacketType>(frame[1]);
  return true;
}

}  // namespace TurnHubSecureLink
