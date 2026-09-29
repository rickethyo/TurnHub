#pragma once

// The secure link's real crypto backend (secure_link.h) for the ESP32s:
// mbedTLS 2.28 from the Arduino-ESP32 core (X25519, AES-128-CCM with the
// hardware AES, HMAC-SHA256) and the hardware RNG. Firmware-only: host tests
// can't link mbedTLS and use their stand-in backend instead. Check it at boot
// with knownAnswerTest() (secure_link.h) before trusting it.

#include <esp_system.h>
#include <mbedtls/ccm.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecp.h>
#include <mbedtls/md.h>

#include "secure_link.h"

namespace TurnHubSecureLink {

class MbedtlsCrypto final : public Crypto {
 public:
  bool generateKeyPair(uint8_t privateKey[SECRET_BYTES],
      uint8_t publicKey[PUBLIC_KEY_BYTES]) override {
    esp_fill_random(privateKey, SECRET_BYTES);
    // X25519 of the base point (u = 9) is the public key (RFC 7748).
    uint8_t basePoint[PUBLIC_KEY_BYTES] = {9};
    return x25519(privateKey, basePoint, publicKey);
  }

  bool sharedSecret(const uint8_t privateKey[SECRET_BYTES],
      const uint8_t peerPublicKey[PUBLIC_KEY_BYTES], uint8_t secret[SECRET_BYTES]) override {
    if (!x25519(privateKey, peerPublicKey, secret)) return false;
    // An all-zero result means a low-order peer key (RFC 7748 section 6.1).
    uint8_t any = 0;
    for (size_t i = 0; i < SECRET_BYTES; ++i) any |= secret[i];
    return any != 0;
  }

  bool hmac(const uint8_t *key, size_t keyLength, const Bytes *parts, size_t partCount,
      uint8_t out[HMAC_BYTES]) override {
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    bool ok = info != nullptr && mbedtls_md_setup(&ctx, info, 1) == 0 &&
        mbedtls_md_hmac_starts(&ctx, key, keyLength) == 0;
    for (size_t i = 0; ok && i < partCount; ++i) {
      ok = parts[i].length == 0 || mbedtls_md_hmac_update(&ctx, parts[i].data, parts[i].length) == 0;
    }
    ok = ok && mbedtls_md_hmac_finish(&ctx, out) == 0;
    mbedtls_md_free(&ctx);
    return ok;
  }

  bool seal(const uint8_t key[KEY_BYTES], const uint8_t nonce[CCM_NONCE_BYTES],
      const uint8_t *aad, size_t aadLength, const uint8_t *plain, size_t length,
      uint8_t *cipher, uint8_t tag[TAG_BYTES]) override {
    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);
    const bool ok = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, KEY_BYTES * 8) == 0 &&
        mbedtls_ccm_encrypt_and_tag(&ctx, length, nonce, CCM_NONCE_BYTES, aad, aadLength,
            plain, cipher, tag, TAG_BYTES) == 0;
    mbedtls_ccm_free(&ctx);
    return ok;
  }

  bool open(const uint8_t key[KEY_BYTES], const uint8_t nonce[CCM_NONCE_BYTES],
      const uint8_t *aad, size_t aadLength, const uint8_t *cipher, size_t length,
      const uint8_t tag[TAG_BYTES], uint8_t *plain) override {
    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);
    // A bad tag returns an error and mbedTLS zeroes the output.
    const bool ok = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, KEY_BYTES * 8) == 0 &&
        mbedtls_ccm_auth_decrypt(&ctx, length, nonce, CCM_NONCE_BYTES, aad, aadLength,
            cipher, plain, tag, TAG_BYTES) == 0;
    mbedtls_ccm_free(&ctx);
    return ok;
  }

  void randomBytes(uint8_t *out, size_t length) override { esp_fill_random(out, length); }

 private:
  static int rng(void *, unsigned char *out, size_t length) {
    esp_fill_random(out, length);
    return 0;
  }

  // RFC 7748 X25519: clamps the scalar and masks the u-coordinate's top bit,
  // then lets mbedTLS do the Montgomery ladder (it also rejects known
  // low-order points). All values little-endian, 32 bytes.
  static bool x25519(const uint8_t scalar[SECRET_BYTES], const uint8_t u[PUBLIC_KEY_BYTES],
      uint8_t out[PUBLIC_KEY_BYTES]) {
    uint8_t k[SECRET_BYTES];
    memcpy(k, scalar, SECRET_BYTES);
    k[0] &= 248;
    k[31] &= 127;
    k[31] |= 64;
    uint8_t peer[PUBLIC_KEY_BYTES];
    memcpy(peer, u, PUBLIC_KEY_BYTES);
    peer[31] &= 127;

    mbedtls_ecp_group grp;
    mbedtls_mpi d, z;
    mbedtls_ecp_point q;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&z);
    mbedtls_ecp_point_init(&q);
    const bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) == 0 &&
        mbedtls_mpi_read_binary_le(&d, k, SECRET_BYTES) == 0 &&
        mbedtls_mpi_read_binary_le(&q.X, peer, PUBLIC_KEY_BYTES) == 0 &&
        mbedtls_mpi_lset(&q.Z, 1) == 0 &&
        mbedtls_ecdh_compute_shared(&grp, &z, &q, &d, rng, nullptr) == 0 &&
        mbedtls_mpi_write_binary_le(&z, out, PUBLIC_KEY_BYTES) == 0;
    mbedtls_ecp_point_free(&q);
    mbedtls_mpi_free(&z);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
    wipe(k, sizeof(k));
    return ok;
  }
};

}  // namespace TurnHubSecureLink
