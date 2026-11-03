#pragma once

// Cards Atlas keeps for play without a search: the digital planar deck's
// catalog (sent by the app, kept on the microSD card) and each profile's saved
// commanders (also on the card). Atlas never searches for cards itself; it
// only deals and offers what was saved. Application task only.

#include <Arduino.h>
#include <stdint.h>

namespace TurnHubStorage { class BlobStore; }
namespace TurnHubHistory { class ArchiveFiles; }

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

// --- plane cards: text and art for the card view ---------------------------------
//
// The app sends each catalog plane's rules text, its chaos ability (apart,
// as the card sets it off), type line and artist, and its art as a 480x320
// baseline JPEG it cut from Scryfall's art crop. Kept on the microSD card as
// "pc-<id>.txt" and "pc-<id>.jpg" in Atlas's folder; nothing without a card.

constexpr size_t PLANE_TYPE_LENGTH = 47;
constexpr size_t PLANE_ARTIST_LENGTH = 47;
constexpr size_t PLANE_TEXT_LENGTH = 639;
constexpr size_t PLANE_CHAOS_LENGTH = 319;
constexpr uint16_t PLANE_ART_WIDTH = 480, PLANE_ART_HEIGHT = 320;
constexpr size_t MAX_PLANE_ART_BYTES = 96 * 1024;

struct PlaneCard {
  char id[CARD_ID_LENGTH + 1] = "";
  char type[PLANE_TYPE_LENGTH + 1] = "";
  char artist[PLANE_ARTIST_LENGTH + 1] = "";
  char text[PLANE_TEXT_LENGTH + 1] = "";
  char chaos[PLANE_CHAOS_LENGTH + 1] = "";
  bool art = false;  // Its JPEG is on the card.
};

// The files cards are kept in (the microSD card's) unless a test sets others.
void setCardFilesForTests(TurnHubHistory::ArchiveFiles *files);
// Saves one catalog plane's card (text printable ASCII; newlines allowed in
// text). jpeg may be empty (text only). False for an unknown plane, bad
// text, a JPEG that isn't one, or no card.
bool savePlaneCard(const char *id, const String &type, const String &artist, const String &text, const String &chaos,
    const uint8_t *jpeg, size_t jpegSize);
// Whether Atlas has that plane's text (and art).
bool hasPlaneCard(const char *id, bool &art);
// The card for id, read from the card once and kept for the next call;
// nullptr when it isn't saved. Application task only.
const PlaneCard *planeCard(const char *id);
// Reads up to capacity bytes of the plane's JPEG; size 0 without one.
bool readPlaneArt(const char *id, uint8_t *out, size_t capacity, size_t &size);

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
