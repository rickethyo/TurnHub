// Replays the shared scenarios in core/scenarios/ against turnhub-core. The
// app's JVM test (CoreScenarioTest.kt) replays the same files against the
// Kotlin rules, so both engines answer every scenario the same way.

#include <dirent.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "turnhub_core.h"

using turnhub::Game;
using turnhub::Json;

namespace {

// Hands out the scenario step's "random" list in order.
class ScriptedRandom : public turnhub::Random {
 public:
  std::vector<int> draws;
  size_t next = 0;
  bool overrun = false;
  int nextInt(int bound) override {
    if (next >= draws.size()) { overrun = true; return 0; }
    const int v = draws[next++];
    if (v < 0 || v >= bound) overrun = true;
    return v;
  }
};

// "players.0.life" -> the value at that path, or Null.
const Json &at(const Json &root, const std::string &path) {
  static const Json none;
  const Json *node = &root;
  std::stringstream parts(path);
  std::string part;
  while (std::getline(parts, part, '.')) {
    if (node->isArray()) {
      char *end = nullptr;
      const long i = strtol(part.c_str(), &end, 10);
      if (*end || i < 0 || static_cast<size_t>(i) >= node->size()) return none;
      node = &(*node)[static_cast<size_t>(i)];
    } else if (node->isObject() && node->has(part)) {
      node = &node->get(part);
    } else {
      return none;
    }
  }
  return *node;
}

Json withoutRevision(const Json &state) {
  Json copy = Json::object();
  for (const auto &m : state.members()) if (m.first != "revision") copy.set(m.first, m.second);
  return copy;
}

// A seat action names the current match and revision unless the step says
// otherwise ("stale": true sends the revision before it). Revisions count
// differently in the two engines, so scenarios never spell them out.
Json withSeatDefaults(const Json &action, const Game &game) {
  if (action.get("do").asString() != "seat") return action;
  Json a = action;
  if (!a.has("match")) a.set("match", game.gameId);
  a.set("revision", game.revision - (action.get("stale").asBool() ? 1 : 0));
  return a;
}

int runFile(const std::string &path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  bool ok = false;
  const Json scenario = Json::parse(text.str(), &ok);
  if (!ok) { std::printf("FAIL %s: not valid JSON\n", path.c_str()); return 1; }
  const std::string name = scenario.get("name").asString();
  Game game;
  int failures = 0;
  const Json &steps = scenario.get("steps");
  for (size_t s = 0; s < steps.size(); ++s) {
    const Json &step = steps[s];
    ScriptedRandom random;
    const Json &draws = step.get("random");
    for (size_t i = 0; i < draws.size(); ++i) random.draws.push_back(static_cast<int>(draws[i].asInt()));
    const Game before = game;
    game = turnhub::applyAction(game, withSeatDefaults(step.get("action"), game), random);
    const Json state = turnhub::toJson(game);
    const std::string where = name + " step " + std::to_string(s + 1) + " (" + step.get("action").get("do").asString() + ")";
    if (random.overrun || random.next != random.draws.size()) {
      std::printf("FAIL %s: used %zu of %zu random draws%s\n", where.c_str(), random.next, random.draws.size(),
                  random.overrun ? " (ran out or out of range)" : "");
      ++failures;
    }
    if (step.get("unchanged").asBool()) {
      if (game.revision != before.revision || withoutRevision(state) != withoutRevision(turnhub::toJson(before))) {
        std::printf("FAIL %s: expected no change\n", where.c_str());
        ++failures;
      }
    }
    for (const auto &e : step.get("expect").members()) {
      const Json &actual = at(state, e.first);
      if (actual != e.second) {
        std::printf("FAIL %s: %s = %s, expected %s\n", where.c_str(), e.first.c_str(), actual.dump().c_str(), e.second.dump().c_str());
        ++failures;
      }
    }
    const Json &timer = step.get("timer");
    if (timer.isObject()) {
      const auto t = game.turnTimer(timer.get("nowMs").asInt());
      if (timer.has("phase") && timer.get("phase").asString() != turnhub::phaseName(t.phase)) {
        std::printf("FAIL %s: timer phase %s, expected %s\n", where.c_str(), turnhub::phaseName(t.phase), timer.get("phase").asString().c_str());
        ++failures;
      }
      if (timer.has("remainingMs")) {
        const Json actual = t.remainingMs ? Json(*t.remainingMs) : Json();
        if (actual != timer.get("remainingMs")) {
          std::printf("FAIL %s: timer remainingMs %s, expected %s\n", where.c_str(), actual.dump().c_str(), timer.get("remainingMs").dump().c_str());
          ++failures;
        }
      }
    }
  }
  // A round trip through JSON must give the same game back.
  if (turnhub::toJson(turnhub::gameFromJson(turnhub::toJson(game))) != turnhub::toJson(game)) {
    std::printf("FAIL %s: state does not survive a JSON round trip\n", name.c_str());
    ++failures;
  }
  std::printf("%s %s (%zu steps)\n", failures ? "FAIL" : "ok  ", name.c_str(), steps.size());
  return failures;
}

}  // namespace

int main(int argc, char **argv) {
  const std::string dir = argc > 1 ? argv[1] : "core/scenarios";
  std::vector<std::string> files;
  if (DIR *d = opendir(dir.c_str())) {
    while (dirent *e = readdir(d)) {
      const std::string f = e->d_name;
      if (f.size() > 5 && f.compare(f.size() - 5, 5, ".json") == 0) files.push_back(dir + "/" + f);
    }
    closedir(d);
  }
  std::sort(files.begin(), files.end());
  if (files.empty()) { std::printf("FAIL no scenarios in %s\n", dir.c_str()); return 1; }
  int failures = 0;
  for (const auto &f : files) failures += runFile(f);
  std::printf("%zu scenarios, %d failures\n", files.size(), failures);
  return failures ? 1 : 0;
}
