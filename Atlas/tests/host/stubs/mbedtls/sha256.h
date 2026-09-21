#pragma once
#include <cstdint>
#include <cstddef>
// Deterministic credential test double, NOT cryptography. Production links the
// framework's mbedTLS; these tests exercise authentication/authorization flow.
struct mbedtls_sha256_context { uint32_t state=2166136261UL; };
inline void mbedtls_sha256_init(mbedtls_sha256_context *c) { c->state=2166136261UL; }
inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context *,int) { return 0; }
inline int mbedtls_sha256_update_ret(mbedtls_sha256_context *c,const unsigned char *data,size_t length) {
  for(size_t i=0;i<length;++i) c->state=(c->state^data[i])*16777619UL;
  return 0;
}
inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context *c,unsigned char *digest) {
  for(size_t i=0;i<32;++i) { c->state=c->state*1664525UL+1013904223UL; digest[i]=static_cast<unsigned char>(c->state>>24); }
  return 0;
}
inline void mbedtls_sha256_free(mbedtls_sha256_context *) {}
