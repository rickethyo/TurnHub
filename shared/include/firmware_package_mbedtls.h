#pragma once

// firmware_package.h's real backend for the ESP32s: SHA-256 and ECDSA P-256
// verification from mbedTLS 2.28 in the Arduino-ESP32 core. Firmware-only;
// host tests use test_package_crypto.h. Verifying takes a fraction of a
// second and runs only when a package is checked, never at boot.

#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>

#include "firmware_package.h"

namespace TurnHubFirmwarePackage {

class MbedtlsPackageCrypto final : public PackageCrypto {
 public:
  MbedtlsPackageCrypto() { mbedtls_sha256_init(&sha_); }
  ~MbedtlsPackageCrypto() override { mbedtls_sha256_free(&sha_); }
  MbedtlsPackageCrypto(const MbedtlsPackageCrypto &) = delete;
  MbedtlsPackageCrypto &operator=(const MbedtlsPackageCrypto &) = delete;

  bool hashBegin() override {
    mbedtls_sha256_free(&sha_);
    mbedtls_sha256_init(&sha_);
    return mbedtls_sha256_starts_ret(&sha_, 0) == 0;
  }
  bool hashUpdate(const uint8_t *data, size_t length) override {
    return mbedtls_sha256_update_ret(&sha_, data, length) == 0;
  }
  bool hashFinish(uint8_t out[HASH_BYTES]) override {
    return mbedtls_sha256_finish_ret(&sha_, out) == 0;
  }

  bool verify(const uint8_t publicKey[PUBLIC_KEY_BYTES], const uint8_t digest[HASH_BYTES],
      const uint8_t signature[SIGNATURE_BYTES]) override {
    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_mpi r, s;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    const bool ok = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_ecp_point_read_binary(&group, &point, publicKey, PUBLIC_KEY_BYTES) == 0 &&
        mbedtls_ecp_check_pubkey(&group, &point) == 0 &&
        mbedtls_mpi_read_binary(&r, signature, SIGNATURE_BYTES / 2) == 0 &&
        mbedtls_mpi_read_binary(&s, signature + SIGNATURE_BYTES / 2, SIGNATURE_BYTES / 2) == 0 &&
        mbedtls_ecdsa_verify(&group, digest, HASH_BYTES, &point, &r, &s) == 0;
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return ok;
  }

 private:
  mbedtls_sha256_context sha_;
};

}  // namespace TurnHubFirmwarePackage
