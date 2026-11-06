#pragma once

// Cards Atlas keeps for play without a search: the digital planar deck's
// catalog (sent by the app, kept on the microSD card) and each profile's saved
// commanders (also on the card). Direct fallback fetches only the IDs named
// by the hosting phone; Atlas never chooses a deck. Application task only.

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
// Kept in RAM for this boot even without a card. With a card in, the records
// are queued for the SD worker (queued true) and written there, never on the
// loop. False (nothing changed) for a malformed or empty list.
bool replacePlanes(const char *lines, const char *version, bool &queued);
// Adds one downloaded hosted plane without replacing the phone's catalog.
// At capacity, replaces the last catalog entry; cached card files are retained.
bool rememberHostedPlane(const char *id, const char *name, bool phenomenon);
uint16_t planeCount();
const Plane *plane(uint16_t index);
const Plane *findPlane(const char *id);
const char *planesVersion();

// --- plane cards: text and art for the card view ---------------------------------
//
// The app sends each catalog plane's rules text, its chaos ability (apart,
// as the card sets it off), type line and artist, and its art as a 480x320
// baseline JPEG normalized from Scryfall's art crop by the phone or Atlas. Kept on the microSD card in
// Atlas's "planes" folder under 8.3 names, "<hash>.txt" and "<hash>.jpg"
// (planeFileStem); nothing without a card.
//
// The loop never waits for the card here (owner, 2026-11-03: Send planes froze
// the Atlas screen for 30-110 s when every lookup and write ran on it). The SD
// worker keeps an index of which planes have text and art, built once per
// mount from one listing of the folder, and writes queued cards one at a
// time. hasPlaneCard answers from that index; savePlaneCard queues.

constexpr size_t PLANE_TYPE_LENGTH = 47;
constexpr size_t PLANE_ARTIST_LENGTH = 47;
constexpr size_t PLANE_TEXT_LENGTH = 639;
constexpr size_t PLANE_CHAOS_LENGTH = 319;
constexpr uint16_t PLANE_ART_WIDTH = 480, PLANE_ART_HEIGHT = 320;
constexpr size_t MAX_PLANE_UPLOAD_BYTES = 96 * 1024;
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

// The 8 lower-case hex digits naming a plane's files (FNV-1a of its ID).
void planeFileStem(const char *id, char (&stem)[9]);

enum class CardQueue : uint8_t {
  Queued,   // The SD worker will write it.
  Busy,     // The queue is full, or the card's index is still being read: try again shortly.
  NoCard,   // No working card.
  Invalid,  // An unknown plane, bad text or a JPEG that isn't one.
};
// Queues one catalog plane's card (text printable ASCII; newlines allowed in
// text). jpeg may be empty (text only); it is copied, so the caller's buffer
// can go. index/total (0: none) are where it falls in the app's batch, for
// the screen's progress once it is written.
CardQueue savePlaneCard(const char *id, const String &type, const String &artist, const String &text, const String &chaos,
    const uint8_t *jpeg, size_t jpegSize, uint16_t index = 0, uint16_t total = 0);
// Whether Atlas has that plane's text (and art), from the index. False before
// the index is read and without a card (planeIndexReady tells them apart).
bool hasPlaneCard(const char *id, bool &art);
// The index of this mount's plane files is read (a card is in and listed).
bool planeIndexReady();
// A card is in but its index isn't read yet: answers about it would be wrong.
bool planeIndexPending();

// --- the SD worker's side (sd_card.cpp; tests call it directly) -------------------
//
// Reads the plane folder's index after a new mount (generation changes),
// writes one queued job, then removes one file of the old layout ("pc-*" in
// Atlas's folder) per call until none are left. Returns true while there is
// more to do, so the worker calls again at once. Never on the loop.
bool servicePlaneStorage(uint32_t cardGeneration);
// A progress line the worker posted ("Plane cards: 3 of 9", "All 9 plane
// cards saved", "Couldn't save a plane card"), once; the loop shows it.
bool takePlaneProgress(char *line, size_t size);
// Jobs waiting for the worker (diagnostics and tests).
uint8_t pendingPlaneJobs();
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

}  // namespace TurnHubCards
