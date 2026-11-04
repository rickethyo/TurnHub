// Atlas's own endpoints: the v1 info and state contract (Atlas's state, not
// a game: games run on the hub phone, web_hosted_api.cpp), the planar
// catalog and plane cards the app keeps on Atlas's microSD card, and a
// profile's saved commanders.

#include <stdlib.h>
#include <vector>

#include "firmware_version.h"
#include "home_tls.h"
#include "home_wifi.h"
#include "protocol.h"
#include "web_api_internal.h"
#include "card_cache.h"
#include "game_profile.h"
#include "touch_controls.h"

namespace TurnHubWebApi {
namespace internal {

// Parses a decimal number within +/-LIFE_LIMIT.
bool parseLifeInteger(const String &text, int32_t &value, bool negativeAllowed) {
  if (!text.length() || text.length() > 8) return false;
  const bool negative = text[0] == '-';
  size_t i = negative ? 1 : 0;
  if ((negative && !negativeAllowed) || i == text.length()) return false;
  uint32_t number = 0;
  for (; i < text.length(); ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    number = number * 10 + static_cast<uint32_t>(text[i] - '0');
    if (number > static_cast<uint32_t>(TurnHub::LIFE_LIMIT)) return false;
  }
  value = negative ? -static_cast<int32_t>(number) : static_cast<int32_t>(number);
  return true;
}

// A positive decimal within [min, max].
bool parseBoundedNumber(const String &text, int32_t min, int32_t max, int32_t &value) {
  return parseLifeInteger(text, value, false) && value >= min && value <= max;
}

namespace {
// Sends 401 and returns null when the request has no valid session.
WebSession *requireSession(WebServer &server) {
  WebSession *session = sessionForRequest(server);
  if (!session) sendError(server, 401, "Sign in first");
  return session;
}

}  // namespace

void handleInfo(WebServer &server) {
  String json;
  json.reserve(640);
  json = "{\"product\":\"TurnHub\",\"deviceType\":\"Atlas\",\"atlasId\":\"";
  json += atlasHardwareId();
  json += "\",\"firmwareVersion\":\"";
  json += TurnHubFirmware::VERSION;
  json += "\",\"apiVersion\":\"1\",\"protocolVersion\":\"1\",\"radioProtocolVersion\":";
  json += String(TurnHubProtocol::VERSION);
  json += ",\"bootId\":\""; json += bootId;
  json += "\",\"revision\":"; json += String(readClientRevision ? readClientRevision() : 0);
  json += ",\"capabilities\":{\"stateSnapshot\":";
  json += jsonBool(readClientState && readClientRevision);
  json += ",\"sessionControls\":false,\"intentEnvelope\":false,\"events\":false,\"requestDeduplication\":false}";
  // Where phones reach Atlas on the home network over HTTPS (home_tls.h):
  // the app learns the pin only from Atlas's own hotspot, then uses it.
  const char *address = TurnHubAtlas::homeWifiAddress();
  const char *pin = TurnHubAtlas::homeTlsPin();
  json += ",\"home\":";
  if (address[0] && pin[0]) {
    json += "{\"address\":\""; json += address;
    json += "\",\"tlsPort\":"; json += String(TurnHub::HOME_TLS_PORT);
    json += ",\"tlsPin\":\""; json += pin; json += "\"}";
  } else {
    json += "null";
  }
  json += "}";
  sendJson(server, 200, json);
}

void handleState(WebServer &server) {
  if (!readClientState || !readClientRevision) {
    sendError(server, 503, "State snapshot is not configured");
    return;
  }
  sendJson(server, 200, readClientState(atlasHardwareId(), bootId));
}


// It also lists the planes whose card (text, and art) Atlas keeps, so the
// app sends only the missing ones. That list is the SD worker's index, never
// a card lookup (card_cache.h); 503 while the index of a new card is read.
void handlePlanesCatalogInfo(WebServer &server) {
  if (TurnHubCards::planeIndexPending()) {
    sendError(server, 503, "Atlas is still reading its microSD card. Try again in a moment");
    return;
  }
  String body = String("{\"version\":\"") + jsonEscape(TurnHubCards::planesVersion()) +
      "\",\"count\":" + String(TurnHubCards::planeCount()) + ",\"cards\":[";
  String art = ",\"art\":[";
  bool firstCard = true, firstArt = true;
  for (uint16_t i = 0; i < TurnHubCards::planeCount(); ++i) {
    const auto *plane = TurnHubCards::plane(i);
    bool hasArt = false;
    if (!TurnHubCards::hasPlaneCard(plane->id, hasArt)) continue;
    body += firstCard ? "\"" : ",\""; body += plane->id; body += '"';
    firstCard = false;
    if (!hasArt) continue;
    art += firstArt ? "\"" : ",\""; art += plane->id; art += '"';
    firstArt = false;
  }
  sendJson(server, 200, body + "]" + art + "]}");
}

namespace {
// Standard base64 (RFC 4648, padding optional); false for any other character.
bool decodeBase64(const String &in, std::vector<uint8_t> &out) {
  out.clear();
  out.reserve(in.length() * 3 / 4);
  uint32_t bits = 0;
  int count = 0;
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62;
    else if (c == '/') v = 63;
    else if (c == '=') break;
    else return false;
    bits = (bits << 6) | static_cast<uint32_t>(v);
    if (++count == 4) {
      out.push_back(static_cast<uint8_t>(bits >> 16)); out.push_back(static_cast<uint8_t>(bits >> 8));
      out.push_back(static_cast<uint8_t>(bits));
      bits = 0; count = 0;
    }
  }
  if (count == 1) return false;
  if (count == 2) out.push_back(static_cast<uint8_t>(bits >> 4));
  if (count == 3) { out.push_back(static_cast<uint8_t>(bits >> 10)); out.push_back(static_cast<uint8_t>(bits >> 2)); }
  return true;
}
}  // namespace

// POST (any signed-in browser or tablet): one catalog plane's card for the
// Atlas card view: id, type, artist, text and chaos in the query, and the art
// (a base64 480x320 baseline JPEG, or empty) as the text/plain body, which
// WebServer keeps as arg("plain") without URL-decoding it (decoding a big
// form field one character at a time froze Atlas for about 30 s). index and
// total (optional) are where this card falls in the app's batch, for the
// screen. 202 once queued: the SD worker writes it to the microSD card and
// the Atlas screen shows the progress then; 503 while the queue is full (the
// app sends the same card again shortly).
void handlePlaneCard(WebServer &server) {
  if (!requireSession(server)) return;
  std::vector<uint8_t> jpeg;
  if (!decodeBase64(server.arg("plain"), jpeg)) {
    sendError(server, 400, "Send the art as base64");
    return;
  }
  const long index = server.arg("index").toInt(), total = server.arg("total").toInt();
  const auto queued = TurnHubCards::savePlaneCard(server.arg("id").c_str(), server.arg("type"), server.arg("artist"),
      server.arg("text"), server.arg("chaos"), jpeg.data(), jpeg.size(),
      static_cast<uint16_t>(index > 0 && index < 65536 ? index : 0), static_cast<uint16_t>(total > 0 && total < 65536 ? total : 0));
  switch (queued) {
    case TurnHubCards::CardQueue::Queued:
      sendJson(server, 202, String("{\"ok\":true,\"queued\":true,\"art\":") + jsonBool(!jpeg.empty()) + "}");
      return;
    case TurnHubCards::CardQueue::Busy:
      sendError(server, 503, "Atlas is still saving the last plane cards. Try again in a moment");
      return;
    case TurnHubCards::CardQueue::NoCard:
    case TurnHubCards::CardQueue::Invalid:
      TurnHubAtlas::showSyncStatus(millis(), "Couldn't save a plane card");
      sendError(server, 409, "Atlas couldn't keep that plane: it needs a catalog plane, plain text and a microSD card");
      return;
  }
}

// GET (any signed-in browser or app) or POST view=game|table|planes|card:
// what the Atlas screen shows during Game 1's match. Any signed-in player
// seated in that game may set it from a phone (owner, 2026-11-02): the app
// names the seat it acts for (module, slot) as with /api/tablet/*, and a
// browser uses its own seat. Sigil seats and guests (no profile) can't.
// Answers the open view and the views available now.
void handlePlanesCatalog(WebServer &server) {
  if (!requireSession(server)) return;
  // In RAM at once; the SD worker writes the records (queued).
  bool queued = false;
  if (!TurnHubCards::replacePlanes(server.arg("plain").c_str(), server.arg("version").c_str(), queued)) {
    sendError(server, 400, "Send the planes as id, name and phenomenon lines with a version");
    return;
  }
  char line[48];
  snprintf(line, sizeof(line), queued ? "Got %u planes from the app" : "%u planes, not on the microSD card",
      static_cast<unsigned>(TurnHubCards::planeCount()));
  TurnHubAtlas::showSyncStatus(millis(), line);
  sendJson(server, 200, String("{\"ok\":true,\"count\":") + String(TurnHubCards::planeCount()) +
      ",\"queued\":" + jsonBool(queued) + "}");
}

// GET ?profileId= (a tablet, for a seated player) or the session's own profile.
void handleSavedCommanders(WebServer &server) {
  WebSession *session = requireSession(server);
  if (!session) return;
  const String profile = server.hasArg("profileId") ? server.arg("profileId") : String(session->profileId);
  TurnHubCards::SavedCommanders saved;
  if (!TurnHubProfiles::profileExists(profile) || !TurnHubCards::loadCommanders(profile, saved)) {
    sendError(server, 409, "Saved commanders need this Atlas's microSD card");
    return;
  }
  String body = "{\"profileId\":\"" + profile + "\",\"commanders\":[";
  for (uint8_t i = 0; i < saved.count; ++i) {
    if (i) body += ',';
    body += "{\"slot\":"; body += String(i + 1);
    body += ",\"name\":\""; body += jsonEscape(saved.slots[i].name);
    body += "\",\"art\":"; body += saved.slots[i].art[0] ? "\"" + jsonEscape(saved.slots[i].art) + "\"" : String("null");
    body += '}';
  }
  body += "]}";
  sendJson(server, 200, body);
}

// slot (1-based saved commander, 0 for none), or name (+ art) to save it to

}  // namespace internal
}  // namespace TurnHubWebApi
