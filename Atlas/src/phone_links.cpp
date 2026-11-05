// Paired phones: see phone_links.h.

#include "phone_links.h"

#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <string.h>

#include "nvs_blob_store.h"
#include "serial_log.h"

namespace TurnHubPhones {

namespace {

// "phones" record, schema 1: {1, count, then count x Phone fields}.
constexpr char KEY[] = "phones";
constexpr size_t RECORD_SIZE = PHONE_ID_LENGTH + NAME_LENGTH + KEY_HASH_LENGTH + 1;
constexpr size_t BLOB_SIZE = 2 + MAX_PHONES * RECORD_SIZE;

Phone phones[MAX_PHONES];
uint8_t phoneCount = 0;
Request pending;

TurnHubStorage::NvsBlobStore nvs;
TurnHubStorage::BlobStore *injected = nullptr;

TurnHubStorage::BlobStore *store() {
  if (injected) return injected;
  return nvs.begin("turnhub") == TurnHubStorage::Status::Ok ? &nvs : nullptr;
}

void randomHex(char *out, size_t length) {
  static const char DIGITS[] = "0123456789abcdef";  // HEX is an Arduino macro
  for (size_t i = 0; i < length; ++i) out[i] = DIGITS[esp_random() & 15];
  out[length] = '\0';
}

void keyHash(const char *phoneId, const char *key, char (&out)[KEY_HASH_LENGTH + 1]) {
  char material[64];
  snprintf(material, sizeof(material), "TurnHubPhone:%s:%s", phoneId, key);
  uint8_t digest[32] = {};
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  mbedtls_sha256_starts_ret(&context, 0);
  mbedtls_sha256_update_ret(&context, reinterpret_cast<const unsigned char *>(material), strlen(material));
  mbedtls_sha256_finish_ret(&context, digest);
  mbedtls_sha256_free(&context);
  for (size_t i = 0; i < KEY_HASH_LENGTH / 2; ++i) snprintf(out + 2 * i, 3, "%02x", digest[i]);
  out[KEY_HASH_LENGTH] = '\0';
}

// Constant time, so a wrong key's prefix doesn't show in the timing.
bool sameText(const char *a, const char *b, size_t length) {
  uint8_t diff = 0;
  for (size_t i = 0; i < length; ++i) diff |= static_cast<uint8_t>(a[i] ^ b[i]);
  return diff == 0;
}

bool save() {
  TurnHubStorage::BlobStore *s = store();
  if (!s) return false;
  if (!phoneCount) {
    const auto status = s->remove(KEY);
    return status == TurnHubStorage::Status::Ok || status == TurnHubStorage::Status::NotFound;
  }
  uint8_t data[BLOB_SIZE] = {};
  data[0] = 1;
  data[1] = phoneCount;
  for (uint8_t i = 0; i < phoneCount; ++i) {
    uint8_t *r = data + 2 + i * RECORD_SIZE;
    memcpy(r, phones[i].id, PHONE_ID_LENGTH);
    memcpy(r + PHONE_ID_LENGTH, phones[i].name, NAME_LENGTH);
    memcpy(r + PHONE_ID_LENGTH + NAME_LENGTH, phones[i].keyHash, KEY_HASH_LENGTH);
    r[RECORD_SIZE - 1] = phones[i].owner ? 1 : 0;
  }
  return s->write(KEY, data, 2 + phoneCount * RECORD_SIZE) == TurnHubStorage::Status::Ok;
}

void cleanName(const char *raw, char (&out)[NAME_LENGTH + 1]) {
  size_t n = 0;
  for (const char *c = raw ? raw : ""; *c && n < NAME_LENGTH; ++c) {
    if (*c >= 32 && *c <= 126) out[n++] = *c;
  }
  while (n && out[n - 1] == ' ') --n;
  out[n] = '\0';
  size_t start = 0;
  while (out[start] == ' ') ++start;
  if (start) memmove(out, out + start, n - start + 1);
  if (!out[0]) snprintf(out, sizeof(out), "A phone");
}

void expire(uint32_t nowMs) {
  if (pending.open && pending.state == RequestState::Waiting && nowMs - pending.startedMs >= REQUEST_MS)
    pending.state = RequestState::Expired;
  // A decided request is kept a little longer for the phone's poll.
  if (pending.open && pending.state != RequestState::Waiting && nowMs - pending.startedMs >= 2 * REQUEST_MS) pending = Request();
}

}  // namespace

void load() {
  phoneCount = 0;
  pending = Request();
  TurnHubStorage::BlobStore *s = store();
  if (!s) return;
  uint8_t data[BLOB_SIZE] = {};
  size_t size = 0;
  if (s->read(KEY, data, sizeof(data), size) != TurnHubStorage::Status::Ok || size < 2 || data[0] != 1 ||
      data[1] > MAX_PHONES || size != 2 + data[1] * RECORD_SIZE) return;
  for (uint8_t i = 0; i < data[1]; ++i) {
    const uint8_t *r = data + 2 + i * RECORD_SIZE;
    Phone &p = phones[i];
    p = Phone();
    memcpy(p.id, r, PHONE_ID_LENGTH);
    memcpy(p.name, r + PHONE_ID_LENGTH, NAME_LENGTH);
    memcpy(p.keyHash, r + PHONE_ID_LENGTH + NAME_LENGTH, KEY_HASH_LENGTH);
    p.owner = r[RECORD_SIZE - 1] == 1;
  }
  phoneCount = data[1];
}

uint8_t count() { return phoneCount; }
const Phone *phone(uint8_t index) { return index < phoneCount ? &phones[index] : nullptr; }

const Request *ask(const char *name, uint32_t nowMs, const char *tlsPin) {
  expire(nowMs);
  if ((pending.open && pending.state == RequestState::Waiting) || phoneCount >= MAX_PHONES) return nullptr;
  pending = Request();
  pending.open = true;
  randomHex(pending.id, REQUEST_ID_LENGTH);
  cleanName(name, pending.name);
  pending.code = esp_random() % 1000000;
  if (tlsPin && strlen(tlsPin) == 64) {
    snprintf(pending.homeCode, sizeof(pending.homeCode), "%.4s %.4s %.4s", tlsPin, tlsPin + 4, tlsPin + 8);
    for (char *c = pending.homeCode; *c; ++c) if (*c >= 'a' && *c <= 'f') *c -= 'a' - 'A';
  }
  pending.startedMs = nowMs;
  TurnHub::serialLog.print("ATLAS|PHONE|ASK|");
  TurnHub::serialLog.println(pending.name);
  return &pending;
}

const Request *waiting(uint32_t nowMs) {
  expire(nowMs);
  return pending.open && pending.state == RequestState::Waiting ? &pending : nullptr;
}

bool cancel(const char *requestId, uint32_t nowMs) {
  expire(nowMs);
  if (!requestId || strlen(requestId) != REQUEST_ID_LENGTH || !pending.open || pending.state != RequestState::Waiting ||
      !sameText(requestId,pending.id,REQUEST_ID_LENGTH)) return false;
  pending.state = RequestState::Refused;
  return true;
}

bool decide(bool allow, uint32_t nowMs) {
  if (!waiting(nowMs)) return false;
  if (!allow) {
    pending.state = RequestState::Refused;
    TurnHub::serialLog.println("ATLAS|PHONE|REFUSED");
    return true;
  }
  if (phoneCount >= MAX_PHONES) return false;
  Phone &p = phones[phoneCount];
  p = Phone();
  randomHex(p.id, PHONE_ID_LENGTH);
  snprintf(p.name, sizeof(p.name), "%s", pending.name);
  p.owner = phoneCount == 0;
  randomHex(pending.key, KEY_LENGTH);
  keyHash(p.id, pending.key, p.keyHash);
  ++phoneCount;
  if (!save()) {
    --phoneCount;
    memset(pending.key, 0, sizeof(pending.key));
    return false;
  }
  snprintf(pending.phoneId, sizeof(pending.phoneId), "%s", p.id);
  pending.state = RequestState::Allowed;
  TurnHub::serialLog.print("ATLAS|PHONE|PAIRED|");
  TurnHub::serialLog.println(p.owner ? "OWNER" : "PLAYER");
  return true;
}

RequestState collect(const char *requestId, uint32_t nowMs, char (&phoneId)[PHONE_ID_LENGTH + 1],
    char (&key)[KEY_LENGTH + 1]) {
  phoneId[0] = key[0] = '\0';
  expire(nowMs);
  if (!pending.open || !requestId || strlen(requestId) != REQUEST_ID_LENGTH ||
      !sameText(requestId, pending.id, REQUEST_ID_LENGTH)) return RequestState::Unknown;
  const RequestState state = pending.state;
  if (state == RequestState::Allowed) {
    snprintf(phoneId, sizeof(phoneId), "%s", pending.phoneId);
    snprintf(key, sizeof(key), "%s", pending.key);
    pending = Request();  // The key leaves Atlas once.
  } else if (state != RequestState::Waiting) {
    pending = Request();
  }
  return state;
}

const Phone *verify(const char *credential) {
  const size_t prefix = sizeof(CREDENTIAL_PREFIX) - 1;
  if (!credential || strncmp(credential, CREDENTIAL_PREFIX, prefix) != 0 ||
      strlen(credential) != prefix + PHONE_ID_LENGTH + 1 + KEY_LENGTH || credential[prefix + PHONE_ID_LENGTH] != ':') return nullptr;
  char id[PHONE_ID_LENGTH + 1], key[KEY_LENGTH + 1], hash[KEY_HASH_LENGTH + 1];
  memcpy(id, credential + prefix, PHONE_ID_LENGTH); id[PHONE_ID_LENGTH] = '\0';
  memcpy(key, credential + prefix + PHONE_ID_LENGTH + 1, KEY_LENGTH); key[KEY_LENGTH] = '\0';
  keyHash(id, key, hash);
  for (uint8_t i = 0; i < phoneCount; ++i)
    if (!strcmp(phones[i].id, id) && sameText(phones[i].keyHash, hash, KEY_HASH_LENGTH)) return &phones[i];
  return nullptr;
}

bool forgetAll() {
  const uint8_t before = phoneCount;
  phoneCount = 0;
  if (!save()) { phoneCount = before; return false; }
  TurnHub::serialLog.println("ATLAS|PHONE|FORGOT_ALL");
  return true;
}

void setStoreForTests(TurnHubStorage::BlobStore *s) { injected = s; resetForTests(); }
void resetForTests() { phoneCount = 0; pending = Request(); }

}  // namespace TurnHubPhones
