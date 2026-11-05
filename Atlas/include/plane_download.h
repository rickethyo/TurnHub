#pragma once

// Hosted plane downloads only. Never a deck, a die, or a second game authority.
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include "turnhub_core.h"

namespace TurnHubAtlas {

inline bool planeUuid(const std::string &id) {
  if (id.size() != 36) return false;
  for (size_t i = 0; i < id.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) { if (id[i] != '-') return false; }
    else if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
  }
  return true;
}

inline std::string planeAscii(const std::string &in, size_t max, bool lines = false) {
  std::string out;
  for (unsigned char c : in) {
    if (out.size() == max) break;
    if (c >= 32 && c <= 126) out += static_cast<char>(c);
    else if (c == '\n' && lines) out += '\n';
    else if (c >= 0xC0) out += '?'; // One replacement per UTF-8 character.
  }
  return out;
}

struct DownloadedPlane {
  std::string id, name, type, artist, text, chaos, artUrl;
  bool phenomenon = false;
};

inline bool parseScryfallPlane(const std::string &body, const std::string &expected, DownloadedPlane &out) {
  if (body.empty() || body.size() > 16384 || !planeUuid(expected)) return false;
  bool ok = false;
  const auto json = turnhub::Json::parse(body, &ok);
  if (!ok || json.get("object").asString() != "card" || json.get("id").asString() != expected) return false;
  const std::string type = json.get("type_line").asString();
  const bool phenomenon = type == "Phenomenon" || type.rfind("Phenomenon ", 0) == 0;
  if (!phenomenon && type != "Plane" && type.rfind("Plane ", 0) != 0) return false;
  DownloadedPlane next;
  next.id = expected; next.name = planeAscii(json.get("name").asString(), 40);
  next.type = planeAscii(type, 47); next.artist = planeAscii(json.get("artist").asString(), 47);
  next.phenomenon = phenomenon;
  const std::string rules = json.get("oracle_text").asString();
  size_t at = 0;
  while (at < rules.size()) {
    const size_t end = rules.find('\n', at);
    const auto line = rules.substr(at, end == std::string::npos ? end : end - at);
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto &dest = line.find("{CHAOS}") != std::string::npos || lower.find("chaos ensues") != std::string::npos ? next.chaos : next.text;
    if (!dest.empty()) dest += '\n';
    dest += line;
    if (end == std::string::npos) break;
    at = end + 1;
  }
  next.text = planeAscii(next.text, 639, true); next.chaos = planeAscii(next.chaos, 319, true);
  const auto url = json.get("image_uris").get("art_crop").asString();
  // Only Scryfall's HTTPS image host. Metadata cannot turn Atlas into an arbitrary URL fetcher.
  if (url.rfind("https://cards.scryfall.io/art_crop/", 0) == 0 && url.size() <= 512 &&
      url.find_first_of("\r\n\t ") == std::string::npos) next.artUrl = url;
  if (next.name.empty()) return false;
  out = std::move(next);
  return true;
}

inline std::vector<std::string> hostedPlaneIds(const turnhub::Game &game) {
  std::vector<std::string> ids;
  if ((game.state != turnhub::TableState::Running && game.state != turnhub::TableState::Paused) ||
      !game.planechase.get("enabled").asBool()) return ids;
  auto add = [&](const turnhub::Json &card) {
    const auto id = card.get("id").asString();
    if (planeUuid(id) && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
  };
  const auto &up = game.planechase.get("faceUp");
  for (size_t i = 0; i < up.size(); ++i) add(up[i]);
  const auto &deck = game.planechase.get("deck");
  // Physical games have no face-up digital card IDs.
  if (ids.empty()) return ids;
  for (size_t i = 0; i < std::min<size_t>(3, deck.size()); ++i) add(deck[i]);
  return ids;
}

class PlaneDownloadSchedule {
 public:
  bool due(const std::string &id, uint32_t now) {
    if (id != id_) { id_ = id; since_ = now; attempted_ = false; }
    return !id.empty() && now - since_ >= 15000 && (!attempted_ || now - last_ >= 60000);
  }
  void started(uint32_t now) { last_ = now; attempted_ = true; }
 private:
  std::string id_;
  uint32_t since_ = 0, last_ = 0;
  bool attempted_ = false;
};

// Firmware-only network task, serviced from loop(); no network work on loop or the SD worker.
void serviceHostedPlaneDownload(uint32_t nowMs);

}  // namespace TurnHubAtlas
