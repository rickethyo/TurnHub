#pragma once

// Pairing v2 (Documentation/engineering/PAIRING_AND_SECURE_LINK.md): X25519 key agreement
// at pairing, a 4-digit code shown on both the Sigil and Atlas, and nothing
// stored until the owner confirms the codes match. Pure logic over the Crypto
// interface (secure_link.h), so both firmwares share it and host tests drive
// it with a stand-in backend. The radio, NVS, screens and the PairConfirm
// Intent live in each firmware.
//
// Sigil:  Idle -> begin() -> Requesting (broadcast PairRequest2 until answered)
//              -> accept() -> AwaitingConfirm (show code)
//              -> result(): Confirmed (store key) / Rejected / expired -> Idle
// Atlas:  request() -> pending entry (show code, answer PairAccept2)
//              -> decide(confirm | reject) or expire() -> PairResultPacket

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "secure_link.h"

namespace TurnHubSecureLink {

// A Sigil keeps waiting a little longer than Atlas's own timeout, so Atlas's
// verdict (or its timeout) always arrives first.
constexpr uint32_t SIGIL_CONFIRM_WAIT_MS = PAIR_CONFIRM_TIMEOUT_MS + 5000;

enum class PairVerdict : uint8_t { None, Confirmed, Rejected };

// --- Sigil side ------------------------------------------------------------

class SigilPairing {
 public:
  enum class State : uint8_t { Idle, Requesting, AwaitingConfirm };

  // Pair pressed: a fresh key pair; `out` is the broadcast request.
  // A spare Sigil sends PairRequestSpare, which Atlas confirms without the
  // code check (FIRMWARE_UPDATES.md).
  bool begin(Crypto &crypto, int32_t token, PairRequest2Packet &out, bool spare = false) {
    cancel();
    if (!crypto.generateKeyPair(private_, public_)) return false;
    token_ = token;
    out.version = TurnHubProtocol::VERSION;
    out.type = spare ? PacketType::PairRequestSpare : PacketType::PairRequest2;
    out.token = token;
    memcpy(out.publicKey, public_, PUBLIC_KEY_BYTES);
    state_ = State::Requesting;
    return true;
  }

  // Atlas's answer. The first valid one wins; the code then goes on screen.
  // An impostor answering first gets a different code, which the owner sees.
  bool accept(Crypto &crypto, const PairAccept2Packet &packet, const uint8_t atlasMac[6],
      const uint8_t sigilMac[6], uint32_t nowMs) {
    if (state_ != State::Requesting || packet.version != TurnHubProtocol::VERSION ||
        packet.type != PacketType::PairAccept2 || packet.token != token_ ||
        packet.sigilId >= TurnHubProtocol::MAX_SIGILS) {
      return false;
    }
    uint8_t secret[SECRET_BYTES];
    PairingResult derived;
    const bool ok = crypto.sharedSecret(private_, packet.publicKey, secret) &&
        derivePairing(crypto, secret, atlasMac, sigilMac, packet.publicKey, public_, derived);
    wipe(secret, sizeof(secret));
    wipe(private_, sizeof(private_));
    if (!ok) {
      cancel();
      return false;
    }
    memcpy(pairKey_, derived.pairKey, KEY_BYTES);
    code_ = derived.code;
    wipe(&derived, sizeof(derived));
    memcpy(atlasMac_, atlasMac, 6);
    slot_ = packet.sigilId;
    acceptedMs_ = nowMs;
    state_ = State::AwaitingConfirm;
    return true;
  }

  // Atlas's verdict. Only a result from the same Atlas, for this slot and
  // token, carrying a MAC made with the new pair key counts. Confirmed hands
  // the key to the caller to store; either verdict ends pairing.
  PairVerdict result(Crypto &crypto, const PairResultPacket &packet, const uint8_t fromMac[6],
      uint8_t pairKey[KEY_BYTES]) {
    if (state_ != State::AwaitingConfirm || memcmp(fromMac, atlasMac_, 6) != 0 ||
        packet.version != TurnHubProtocol::VERSION || packet.sigilId != slot_ ||
        packet.token != token_ ||
        (packet.type != PacketType::PairConfirmed && packet.type != PacketType::PairRejected)) {
      return PairVerdict::None;
    }
    uint8_t expected[MAC_BYTES];
    if (!pairResultMac(crypto, pairKey_, packet, expected) ||
        !equalBytes(expected, packet.mac, MAC_BYTES)) {
      return PairVerdict::None;
    }
    const bool confirmed = packet.type == PacketType::PairConfirmed;
    if (confirmed) memcpy(pairKey, pairKey_, KEY_BYTES);
    cancel();
    return confirmed ? PairVerdict::Confirmed : PairVerdict::Rejected;
  }

  // No verdict in time: give up without storing anything.
  bool expired(uint32_t nowMs) const {
    return state_ == State::AwaitingConfirm && nowMs - acceptedMs_ >= SIGIL_CONFIRM_WAIT_MS;
  }

  void cancel() {
    wipe(private_, sizeof(private_));
    wipe(pairKey_, sizeof(pairKey_));
    state_ = State::Idle;
    code_ = 0;
  }

  State state() const { return state_; }
  uint16_t code() const { return code_; }
  uint8_t slot() const { return slot_; }
  const uint8_t *atlasMac() const { return atlasMac_; }

 private:
  State state_ = State::Idle;
  uint8_t private_[SECRET_BYTES] = {};
  uint8_t public_[PUBLIC_KEY_BYTES] = {};
  uint8_t pairKey_[KEY_BYTES] = {};
  uint8_t atlasMac_[6] = {};
  int32_t token_ = 0;
  uint16_t code_ = 0;
  uint8_t slot_ = 0;
  uint32_t acceptedMs_ = 0;
};

// --- Atlas side ------------------------------------------------------------

struct PendingPairing {
  bool used = false;
  uint8_t slot = 0;
  uint8_t mac[6] = {};
  int32_t token = 0;
  uint16_t code = 0;
  uint32_t startedMs = 0;
  uint8_t pairKey[KEY_BYTES] = {};
  uint8_t atlasPublicKey[PUBLIC_KEY_BYTES] = {};  // Resent on a repeated request.
};

// Several Sigils may await confirmation at once, each with its own code.
class AtlasPairings {
 public:
  static constexpr uint8_t CAPACITY = TurnHubProtocol::MAX_SIGILS;

  // A PairRequest2 during Atlas's pairing window, for the slot the caller
  // chose (the Sigil's existing slot, or a free one). A repeat of the same
  // request gets the same answer; a new token from the same Sigil starts
  // over. Returns the entry, or nullptr (full, bad packet or crypto failure).
  const PendingPairing *request(Crypto &crypto, const uint8_t atlasMac[6],
      const uint8_t sigilMac[6], const PairRequest2Packet &packet, uint8_t slot,
      uint32_t nowMs, PairAccept2Packet &accept) {
    if (packet.version != TurnHubProtocol::VERSION ||
        (packet.type != PacketType::PairRequest2 && packet.type != PacketType::PairRequestSpare) ||
        slot >= TurnHubProtocol::MAX_SIGILS) {
      return nullptr;
    }
    PendingPairing *entry = findMac(sigilMac);
    if (entry != nullptr && entry->token == packet.token && entry->slot == slot) {
      fillAccept(*entry, accept);
      return entry;
    }
    if (entry != nullptr) clear(*entry);
    // One pending pairing per slot: a different Sigil there loses its turn.
    if (PendingPairing *other = findSlotMutable(slot)) clear(*other);
    entry = freeEntry();
    if (entry == nullptr) return nullptr;

    uint8_t privateKey[SECRET_BYTES], secret[SECRET_BYTES];
    PairingResult derived;
    const bool ok = crypto.generateKeyPair(privateKey, entry->atlasPublicKey) &&
        crypto.sharedSecret(privateKey, packet.publicKey, secret) &&
        derivePairing(crypto, secret, atlasMac, sigilMac, entry->atlasPublicKey,
            packet.publicKey, derived);
    wipe(privateKey, sizeof(privateKey));
    wipe(secret, sizeof(secret));
    if (!ok) {
      clear(*entry);
      return nullptr;
    }
    entry->used = true;
    entry->slot = slot;
    memcpy(entry->mac, sigilMac, 6);
    entry->token = packet.token;
    entry->code = derived.code;
    entry->startedMs = nowMs;
    memcpy(entry->pairKey, derived.pairKey, KEY_BYTES);
    wipe(&derived, sizeof(derived));
    fillAccept(*entry, accept);
    return entry;
  }

  // The owner's PairConfirm (confirm or reject) for a slot. Fills the result
  // packet to send; on confirm also copies out the key and MAC to store.
  // The entry is gone either way. False if nothing is pending there.
  bool decide(Crypto &crypto, uint8_t slot, bool confirm, PairResultPacket &out,
      uint8_t pairKey[KEY_BYTES], uint8_t sigilMac[6]) {
    PendingPairing *entry = findSlotMutable(slot);
    if (entry == nullptr) return false;
    const bool ok = makeResult(crypto, *entry, confirm, out);
    if (ok && confirm) {
      memcpy(pairKey, entry->pairKey, KEY_BYTES);
      memcpy(sigilMac, entry->mac, 6);
    }
    clear(*entry);
    return ok;
  }

  // Drops one pairing left unconfirmed for PAIR_CONFIRM_TIMEOUT_MS, filling
  // the rejection to send (so the Sigil stops showing its code). Call until
  // it returns false.
  bool expireOne(Crypto &crypto, uint32_t nowMs, PairResultPacket &out, uint8_t sigilMac[6]) {
    for (PendingPairing &entry : entries_) {
      if (!entry.used || nowMs - entry.startedMs < PAIR_CONFIRM_TIMEOUT_MS) continue;
      memcpy(sigilMac, entry.mac, 6);
      const bool ok = makeResult(crypto, entry, false, out);
      clear(entry);
      return ok;
    }
    return false;
  }

  // Forget, factory reset or leaving the lobby: drop without a verdict.
  void cancelSlot(uint8_t slot) {
    if (PendingPairing *entry = findSlotMutable(slot)) clear(*entry);
  }
  void cancelAll() {
    for (PendingPairing &entry : entries_) clear(entry);
  }

  const PendingPairing *findSlot(uint8_t slot) const {
    for (const PendingPairing &entry : entries_) {
      if (entry.used && entry.slot == slot) return &entry;
    }
    return nullptr;
  }
  // For screens: entries in slot order, `used` false for empty ones.
  const PendingPairing &at(uint8_t index) const { return entries_[index]; }
  uint8_t count() const {
    uint8_t n = 0;
    for (const PendingPairing &entry : entries_) n += entry.used ? 1 : 0;
    return n;
  }

 private:
  PendingPairing entries_[CAPACITY];

  PendingPairing *findMac(const uint8_t mac[6]) {
    for (PendingPairing &entry : entries_) {
      if (entry.used && memcmp(entry.mac, mac, 6) == 0) return &entry;
    }
    return nullptr;
  }
  PendingPairing *findSlotMutable(uint8_t slot) {
    for (PendingPairing &entry : entries_) {
      if (entry.used && entry.slot == slot) return &entry;
    }
    return nullptr;
  }
  PendingPairing *freeEntry() {
    for (PendingPairing &entry : entries_) {
      if (!entry.used) return &entry;
    }
    return nullptr;
  }
  static void clear(PendingPairing &entry) {
    wipe(&entry, sizeof(entry));
    entry = PendingPairing{};
  }
  static void fillAccept(const PendingPairing &entry, PairAccept2Packet &accept) {
    accept.version = TurnHubProtocol::VERSION;
    accept.type = PacketType::PairAccept2;
    accept.sigilId = entry.slot;
    accept.token = entry.token;
    memcpy(accept.publicKey, entry.atlasPublicKey, PUBLIC_KEY_BYTES);
  }
  static bool makeResult(Crypto &crypto, const PendingPairing &entry, bool confirm,
      PairResultPacket &out) {
    out.version = TurnHubProtocol::VERSION;
    out.type = confirm ? PacketType::PairConfirmed : PacketType::PairRejected;
    out.sigilId = entry.slot;
    out.token = entry.token;
    return pairResultMac(crypto, entry.pairKey, out, out.mac);
  }
};

}  // namespace TurnHubSecureLink
