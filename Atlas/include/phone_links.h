#pragma once

// Paired phones (owner decision 2026-11-05: Atlas is an accessory and holds no
// accounts). A phone pairs once, the way a Sigil does: it asks, the Atlas
// screen shows "Allow <phone>?" with a code the phone shows too, and a tap on
// Allow gives the phone a device key. That key replaces signing in for hosting
// a game (protocol/hosted-v1.md). The first phone paired is the owner.
//
// Atlas keeps each phone's ID, name, owner flag and a hash of its key in NVS
// ("phones"); the key itself only leaves Atlas once, in the answer to the
// phone that asked. Application task only.

#include <stddef.h>
#include <stdint.h>

#include "storage.h"

namespace TurnHubPhones {

constexpr uint8_t MAX_PHONES = 8;
constexpr size_t PHONE_ID_LENGTH = 8;    // hex
constexpr size_t KEY_LENGTH = 32;        // hex, sent to the phone once
constexpr size_t KEY_HASH_LENGTH = 32;   // hex, kept
constexpr size_t NAME_LENGTH = 32;
constexpr size_t REQUEST_ID_LENGTH = 16; // hex
// How long a request waits for Allow on the Atlas screen.
constexpr uint32_t REQUEST_MS = 60000;
// "phone:<id>:<key>" in the X-TurnHub-Token header.
constexpr char CREDENTIAL_PREFIX[] = "phone:";

struct Phone {
  char id[PHONE_ID_LENGTH + 1] = {};
  char name[NAME_LENGTH + 1] = {};
  char keyHash[KEY_HASH_LENGTH + 1] = {};
  bool owner = false;
};

enum class RequestState : uint8_t { Waiting, Allowed, Refused, Expired, Unknown };

struct Request {
  bool open = false;
  char id[REQUEST_ID_LENGTH + 1] = {};
  char name[NAME_LENGTH + 1] = {};
  uint32_t code = 0;          // six digits, shown on both screens
  char homeCode[15] = {};     // Home Wi-Fi: TLS fingerprint, shown during the same approval.
  uint32_t startedMs = 0;
  RequestState state = RequestState::Waiting;
  char phoneId[PHONE_ID_LENGTH + 1] = {};
  char key[KEY_LENGTH + 1] = {};  // Allowed: handed to the phone once, then wiped.
};

// Reads the saved phones (boot); none without a record.
void load();
uint8_t count();
const Phone *phone(uint8_t index);

// A phone asks to pair. Refused (nullptr) while another request waits or the
// list is full; name is cleaned to printable ASCII.
const Request *ask(const char *name, uint32_t nowMs, const char *tlsPin = nullptr);
// The request waiting for Allow on the screen, or nullptr.
const Request *waiting(uint32_t nowMs);
// Allow (saves the phone, issues its key) or refuse the waiting request.
bool decide(bool allow, uint32_t nowMs);
bool cancel(const char *requestId, uint32_t nowMs);
// The phone's poll: its request's state; an Allowed request hands over the
// phone ID and key once (later polls see Unknown).
RequestState collect(const char *requestId, uint32_t nowMs, char (&phoneId)[PHONE_ID_LENGTH + 1],
    char (&key)[KEY_LENGTH + 1]);

// The paired phone a "phone:<id>:<key>" credential names, or nullptr.
const Phone *verify(const char *credential);
// Forgets every phone (Settings > Device > Forget phones).
bool forgetAll();

// For tests: replaces the storage and forgets RAM.
void setStoreForTests(TurnHubStorage::BlobStore *store);
void resetForTests();

}  // namespace TurnHubPhones
