#pragma once

// Deterministic stand-in for the secure link's Crypto backend, for host
// tests only (Atlas and Sigil). It is NOT cryptography: sensitive to every
// input byte and reproducible, so link logic is testable without mbedTLS.
// The real backend (secure_link_mbedtls.h) is checked on the device.

#include <cstring>

#include "secure_link.h"

namespace TurnHubTest {

using namespace TurnHubSecureLink;

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

}  // namespace TurnHubTest
