#pragma once

// Cards Atlas keeps for play without a search: the digital planar deck's
// catalog (sent by the app, kept on the microSD card) and each profile's saved
// commanders (also on the card). Atlas never searches for cards itself; it
// only deals and offers what was saved. Application task only.

#include <Arduino.h>
#include <stdint.h>

namespace TurnHubStorage { class BlobStore; }

namespace TurnHubCards {

// --- the planar deck catalog -------------------------------------------------

constexpr uint16_t MAX_PLANES = 192;
constexpr size_t CARD_ID_LENGTH = 36;  // Scryfall's card UUID.
constexpr size_t PLANE_NAME_LENGTH = 40;
constexpr size_t VERSION_LENGTH = 24;

struct Plane {
  char id[CARD_ID_LENGTH + 1];
  char name[PLANE_NAME_LENGTH + 1];
  bool phenomenon;
};

// The card store (the microSD card's records); tests inject their own.
void setStoreForTests(TurnHubStorage::BlobStore *store);
// Reads the catalog saved on the card into RAM; false when there is none.
bool loadPlanes();
// Replaces the catalog from lines "id<TAB>name<TAB>0|1" (1: a phenomenon).
// Kept in RAM for this boot even without a card; saved to the card when one
// is in. False (nothing changed) for a malformed or empty list.
bool replacePlanes(const char *lines, const char *version, bool &saved);
uint16_t planeCount();
const Plane *plane(uint16_t index);
const Plane *findPlane(const char *id);
const char *planesVersion();

// --- saved commanders ----------------------------------------------------------

constexpr uint8_t COMMANDER_SLOTS = 6;
constexpr size_t COMMANDER_NAME_LENGTH = 80;
constexpr size_t COMMANDER_ART_LENGTH = 48;

struct Commander {
  char name[COMMANDER_NAME_LENGTH + 1];
  char art[COMMANDER_ART_LENGTH + 1];  // A printing ID, or "" (the name only).
};
struct SavedCommanders {
  uint8_t count = 0;
  Commander slots[COMMANDER_SLOTS] = {};
};

// Valid names are printable, 1..COMMANDER_NAME_LENGTH long; art is a
// printing ID of letters, digits, '-', ':' and '.' (or empty).
bool validCommander(const String &name, const String &art);
bool loadCommanders(const String &profileId, SavedCommanders &out);
// Adds (or moves to the front) one commander; the oldest drops off when full.
// Returns the slot (1-based) it is in, or 0 when the card can't save it.
uint8_t rememberCommander(const String &profileId, const String &name, const String &art);
bool forgetCommander(const String &profileId, uint8_t slot);

}  // namespace TurnHubCards
