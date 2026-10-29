// turnhub-core rules: a line-by-line port of the app's StandaloneGame.kt
// (Magic formats, Two-Headed Giant, Star Commander, Emperor, Bag of Doom,
// Yu-Gi-Oh!, Pokémon prizes and Lorcana lore). Death May Die, Planechase and
// the Pokémon/Lorcana turn markers follow in later steps. Keep each rule in
// the same order as the Kotlin so the shared scenarios agree.

#include <cctype>
#include "turnhub_core.h"

#include <algorithm>
#include <cstdlib>

namespace turnhub {

namespace {

constexpr int SOLO_POISON = 10;
constexpr int TEAM_POISON = 15;
constexpr int COMMANDER_DAMAGE_LOSS = 21;
constexpr int DMD_DEATH_WOUNDS = 6;
constexpr int64_t TIMER_MIN_MS = 15000, TIMER_MAX_MS = 3600000;
constexpr int STAR_PLAYERS = 5;
constexpr int EMPEROR_PLAYERS = 6;
constexpr int SEAN_MODE_ORDER[STAR_PLAYERS] = {0, 2, 4, 1, 3};

bool contains(const std::vector<int> &v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }
int clampInt(int64_t v, int64_t lo, int64_t hi) { return static_cast<int>(v < lo ? lo : v > hi ? hi : v); }
int floorMod(int a, int n) { return ((a % n) + n) % n; }

// Official modes' fixed life, poison limit and player count (OfficialRules.kt).
struct Official { int life; std::optional<int> poison; std::optional<int> players; };

std::optional<Official> officialRules(Profile p, bool teams) {
  if (p == Profile::Mtg && teams) return Official{30, 15, 4};
  if (p == Profile::Mtg) return Official{20, 10, std::nullopt};
  if (p == Profile::MtgEmperor) return Official{20, 10, 6};
  if (p == Profile::MtgCommander && !teams) return Official{40, 10, std::nullopt};
  if (p == Profile::Yugioh) return Official{8000, std::nullopt, 2};
  if (p == Profile::Pokemon) return Official{6, std::nullopt, 2};
  if (p == Profile::Lorcana) return Official{20, std::nullopt, std::nullopt};
  return std::nullopt;
}

// Star Commander: neighbours are allies, the two across the star enemies.
bool starAllied(int a, int b) { return a != b && (b == (a + 1) % STAR_PLAYERS || b == (a + STAR_PLAYERS - 1) % STAR_PLAYERS); }
std::vector<int> starWinners(const std::vector<bool> &out) {
  std::vector<int> w;
  if (out.size() != STAR_PLAYERS) return w;
  for (int seat = 0; seat < STAR_PLAYERS; ++seat)
    if (!out[seat] && out[(seat + 2) % STAR_PLAYERS] && out[(seat + 3) % STAR_PLAYERS]) w.push_back(seat);
  return w;
}

// Emperor (CR 809): seats 0-2 team A, 3-5 team B, emperors at 1 and 4.
int emperorTeam(int seat) { return seat / 3; }
int emperorOf(int team) { return team * 3 + 1; }
bool emperorMates(int a, int b) { return emperorTeam(a) == emperorTeam(b); }
std::vector<int> emperorTeamSeats(int team) { return {team * 3, team * 3 + 1, team * 3 + 2}; }
std::optional<std::vector<int>> emperorWinners(const std::vector<bool> &out) {
  if (out.size() != EMPEROR_PLAYERS) return std::nullopt;
  std::vector<int> down;
  for (int t = 0; t < 2; ++t) if (out[emperorOf(t)]) down.push_back(t);
  if (down.empty()) return std::nullopt;
  if (down.size() == 1) return emperorTeamSeats(1 - down[0]);
  return std::vector<int>{};
}

// Bag of Doom (DoomRules.kt).
namespace doom {
constexpr int LAST_SURVIVORS = 2;
uint32_t bit(int player) { return player >= 1 && player <= 16 ? 1u << (player - 1) : 0; }
bool on(uint32_t living) { return __builtin_popcount(living) > LAST_SURVIVORS; }
int previous(int player, uint32_t living, int count) {
  for (int k = 1; k <= count; ++k) {
    const int p = (player - 1 - k + count) % count + 1;
    if (living & bit(p)) return p;
  }
  return 0;
}
DoomBag start(int starter, uint32_t living, int count) {
  DoomBag b;
  b.atRisk = living;
  b.boundary = previous(starter, living, count);
  b.warmup = 4;
  b.cycle = 1;
  return b;
}
DoomBag pass(DoomBag bag, int player) {
  if (bag.cycle == 0 || bag.boundary != player) return bag;
  const int left = std::max(bag.warmup - 1, 0);
  bag.warmup = left;
  bag.pending = left == 0;
  bag.pendingTurn = left == 0 ? player : 0;
  return bag;
}
DoomBag leave(DoomBag bag, uint32_t living, int count, int credited = 0) {
  if (bag.cycle == 0) return bag;
  if (!on(living)) {
    bag.atRisk = 0; bag.boundary = 0; bag.warmup = 0; bag.pending = false; bag.pendingTurn = 0; bag.selected = 0;
    return bag;
  }
  if (credited != 0) {
    bag.atRisk = living & ~bit(credited);
    bag.boundary = previous(credited, living, count);
    bag.warmup = 0; bag.pending = false; bag.pendingTurn = 0; bag.selected = 0;
    bag.cycle += 1;
    return bag;
  }
  bag.atRisk &= living;
  if (!(living & bit(bag.boundary))) bag.boundary = previous(bag.boundary, living, count);
  return bag;
}
DoomBag checked(const DoomBag &before, int victim, uint32_t living, int count) {
  DoomBag b = leave(before, living, count);
  b.atRisk = on(living) ? living : 0;
  b.cycle = before.cycle + 1;
  b.warmup = 0; b.pending = false; b.pendingTurn = 0; b.selected = 0; b.sealed = false;
  b.drawn = before.drawn | bit(victim);
  b.lastDraw = victim;
  b.lastCheck = before.cycle;
  return b;
}
}  // namespace doom

// Kotlin's trim().take(n): whitespace off both ends, then n characters.
std::string trimTake(const std::string &s, size_t n) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) --b;
  std::string t = s.substr(a, b - a);
  size_t chars = 0, i = 0;
  while (i < t.size() && chars < n) {
    const unsigned char c = t[i];
    i += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    ++chars;
  }
  return t.substr(0, std::min(i, t.size()));
}

Player freshPlayer(const Player &p, int life) {
  Player n;
  n.name = p.name; n.profileId = p.profileId; n.localId = p.localId; n.life = life;
  n.commander = p.commander; n.commanderArt = p.commanderArt; n.cardOnly = p.cardOnly;
  return n;
}

}  // namespace

bool Counters::operator==(const Counters &o) const {
  return poison == o.poison && energy == o.energy && experience == o.experience && commanderCasts == o.commanderCasts &&
      cityBlessing == o.cityBlessing && treasure == o.treasure && rad == o.rad;
}

bool Player::operator==(const Player &o) const {
  return name == o.name && profileId == o.profileId && localId == o.localId && life == o.life && eliminated == o.eliminated &&
      conceded == o.conceded && eliminatedBy == o.eliminatedBy && selfEliminated == o.selfEliminated &&
      eliminationPending == o.eliminationPending && counters == o.counters && outOrder == o.outOrder &&
      turnsCompleted == o.turnsCompleted && turnMs == o.turnMs && fastestTurnMs == o.fastestTurnMs &&
      longestTurnMs == o.longestTurnMs && commanderDamage == o.commanderDamage && demerits == o.demerits &&
      extensionsEarned == o.extensionsEarned && extensionsUsed == o.extensionsUsed && commander == o.commander &&
      commanderArt == o.commanderArt && cardOnly == o.cardOnly;
}

bool TimerRules::operator==(const TimerRules &o) const {
  return timeout == o.timeout && earnExtensions == o.earnExtensions && earnBeforePercent == o.earnBeforePercent &&
      maxEarnedPerGame == o.maxEarnedPerGame;
}

bool DoomBag::operator==(const DoomBag &o) const {
  return atRisk == o.atRisk && drawn == o.drawn && boundary == o.boundary && warmup == o.warmup && cycle == o.cycle &&
      pending == o.pending && pendingTurn == o.pendingTurn && selected == o.selected && sealed == o.sealed &&
      lastDraw == o.lastDraw && lastCheck == o.lastCheck;
}

bool Game::operator==(const Game &o) const { return toJson(*this) == toJson(o); }

// --- derived ---------------------------------------------------------------

int64_t Game::gameElapsedMs(int64_t nowMs) const {
  return elapsedMs + (runningSinceMs ? std::max<int64_t>(nowMs - *runningSinceMs, 0) : 0);
}

std::vector<int> Game::mates(int index) const {
  if (!twoHeadedGiant) return {index};
  std::vector<int> team;
  const int first = index - index % 2;
  for (int i : {first, first + 1}) if (i >= 0 && i < static_cast<int>(players.size())) team.push_back(i);
  return team;
}

bool Game::canStart() const {
  const int n = static_cast<int>(players.size());
  const auto locked = houseRules ? std::nullopt : officialRules(profile, twoHeadedGiant);
  if (profile == Profile::DeathMayDie) return false;  // Investigators arrive with Death May Die's port.
  if (profile == Profile::MtgBagOfDoom) return n >= 5 && !twoHeadedGiant;
  if (profile == Profile::MtgStarCommander) return n == STAR_PLAYERS;
  if (ProfileRules::emperor(profile)) return n == EMPEROR_PLAYERS;
  if (locked && locked->players) return n == *locked->players;
  return n >= 2 && (!twoHeadedGiant || (n >= 4 && n % 2 == 0));
}

int Game::defaultLife(Profile p, bool teams) {
  switch (p) {
    case Profile::MtgCommander: return teams ? 60 : 40;
    case Profile::MtgStarCommander: case Profile::MtgBagOfDoom: case Profile::MtgCommanderEmperor: return 40;
    case Profile::Mtg: return teams ? 30 : 20;
    case Profile::MtgEmperor: return 20;
    case Profile::Yugioh: return 8000;
    case Profile::Pokemon: return 6;
    case Profile::Lorcana: return 20;
    case Profile::DeathMayDie: return 1;
    default: return 20;
  }
}

TurnTimer Game::turnTimer(int64_t nowMs) const {
  if (!active || !playing()) return {TimerPhase::Normal, std::nullopt};
  const int64_t turn = std::max<int64_t>(gameElapsedMs(nowMs) - turnStartedAtElapsedMs, 0);
  if (turnTimerMs <= 0) return {turn >= LONG_TURN_MS ? TimerPhase::LongTurn : TimerPhase::Normal, std::nullopt};
  const int64_t remaining = std::max<int64_t>(turnTimerMs + turnExtensionMs - turn, 0);
  const TimerPhase phase = remaining == 0 ? TimerPhase::Expired : remaining <= WARNING_MS ? TimerPhase::Warning : TimerPhase::Normal;
  return {phase, remaining};
}

std::optional<int> Game::confirmerFor(int claimant) const {
  const int n = static_cast<int>(players.size());
  for (int k = 1; k < n; ++k) {
    const int i = (claimant + k) % n;
    if (!players[i].eliminated && !winConfirmed.count(i) && !(twoHeadedGiant && i / 2 == claimant / 2) &&
        !(ProfileRules::emperor(profile) && emperorMates(i, claimant))) return i;
  }
  return std::nullopt;
}

std::optional<int> Game::openWinClaim() const {
  if (!winClaim || !playing() || *winClaim < 0 || *winClaim >= static_cast<int>(players.size()) ||
      players[*winClaim].eliminated || !confirmerFor(*winClaim)) return std::nullopt;
  return winClaim;
}

std::optional<int> Game::winConfirmer() const {
  const auto claim = openWinClaim();
  return claim ? confirmerFor(*claim) : std::nullopt;
}

// --- the lobby -------------------------------------------------------------

Game Game::addPlayer(const std::string &rawName, const std::string &profileId, const std::string &localId) const {
  const std::string name = trimTake(rawName, MAX_NAME);
  if (state != TableState::Lobby || name.empty() || static_cast<int>(players.size()) >= MAX_PLAYERS) return *this;
  for (const auto &p : players) {
    if (!profileId.empty() && p.profileId == profileId) return *this;
    if (!localId.empty() && p.localId == localId) return *this;
  }
  Game g = *this;
  Player p;
  p.name = name; p.profileId = profileId; p.localId = localId;
  g.players.push_back(p);
  return g.bump();
}

Game Game::removePlayer(int index) const {
  if (state != TableState::Lobby || index < 0 || index >= static_cast<int>(players.size())) return *this;
  Game g = *this;
  if (starter) g.starter = *starter == index ? std::nullopt : std::optional<int>(*starter > index ? *starter - 1 : *starter);
  g.players.erase(g.players.begin() + index);
  return g.bump();
}

Game Game::clearPlayers() const {
  if (state != TableState::Lobby || players.empty()) return *this;
  Game g = *this;
  g.players.clear();
  g.starter.reset();
  return g.bump();
}

Game Game::swapPlayers(int a, int b) const {
  const int n = static_cast<int>(players.size());
  if (state != TableState::Lobby || a < 0 || a >= n || b < 0 || b >= n || a == b) return *this;
  Game g = *this;
  std::swap(g.players[a], g.players[b]);
  if (starter == a) g.starter = b;
  else if (starter == b) g.starter = a;
  return g.bump();
}

Game Game::chooseStarter(std::optional<int> index) const {
  if (state != TableState::Lobby || (index && (*index < 0 || *index >= static_cast<int>(players.size())))) return *this;
  Game g = *this;
  g.starter = index == starter ? std::nullopt : index;
  return g.bump();
}

Game Game::officialValues() const {
  const auto locked = houseRules ? std::nullopt : officialRules(profile, twoHeadedGiant);
  if (!locked) return *this;
  Game g = *this;
  g.startingLife = locked->life;
  if (locked->poison) g.poisonLimit = *locked->poison;
  return g;
}

Game Game::setFormat(Profile p, std::optional<int> life) const {
  if (state != TableState::Lobby) return *this;
  const bool teams = twoHeadedGiant && ProfileRules::magic(p) && !ProfileRules::ownSeating(p);
  Game g = *this;
  g.profile = p;
  g.twoHeadedGiant = teams;
  g.poisonLimit = teams ? TEAM_POISON : (twoHeadedGiant == teams ? poisonLimit : SOLO_POISON);
  g.startingLife = clampInt(life ? *life : defaultLife(p, teams), 1, MAX_LIFE);
  return g.officialValues().bump();
}

Game Game::setHouseRules(bool on) const {
  if (state != TableState::Lobby || on == houseRules) return *this;
  Game g = *this;
  g.houseRules = on;
  return g.officialValues().bump();
}

Game Game::setTwoHeadedGiant(bool on) const {
  if (state != TableState::Lobby || on == twoHeadedGiant || (on && (!magic() || ProfileRules::ownSeating(profile)))) return *this;
  Game g = *this;
  g.twoHeadedGiant = on;
  g.startingLife = defaultLife(profile, on);
  g.poisonLimit = on ? TEAM_POISON : SOLO_POISON;
  return g.officialValues().bump();
}

Game Game::setStartingLife(int life) const {
  const auto locked = houseRules ? std::nullopt : officialRules(profile, twoHeadedGiant);
  if (state != TableState::Lobby || life < 1 || life > MAX_LIFE || locked) return *this;
  Game g = *this;
  g.startingLife = life;
  return g.bump();
}

Game Game::setTurnTimer(int64_t ms) const {
  if (state != TableState::Lobby || (ms != 0 && (ms < TIMER_MIN_MS || ms > TIMER_MAX_MS || ms % 1000 != 0))) return *this;
  Game g = *this;
  g.turnTimerMs = ms;
  return g.bump();
}

Game Game::setTimerRules(const TimerRules &rules) const {
  // The app's LocalTimerRules only allows these values.
  const bool valid = (rules.earnBeforePercent == 75 || rules.earnBeforePercent == 100) && rules.maxEarnedPerGame >= 1 &&
      rules.maxEarnedPerGame <= 3;
  if (state != TableState::Lobby || !valid || timerRules == rules) return *this;
  Game g = *this;
  g.timerRules = rules;
  return g.bump();
}

Game Game::start(int64_t nowMs, const std::string &id, Random &random) const {
  if (state != TableState::Lobby && state != TableState::GameOver) return *this;
  if (!canStart()) return *this;
  const int n = static_cast<int>(players.size());
  std::optional<int> chosen;
  if (starter && state == TableState::Lobby && *starter >= 0 && *starter < n) chosen = starter;
  int first;
  if (chosen) first = twoHeadedGiant ? *chosen - *chosen % 2 : *chosen;
  else if (twoHeadedGiant) first = random.nextInt(n / 2) * 2;
  else if (ProfileRules::emperor(profile)) first = emperorOf(random.nextInt(2));  // CR 809.4.
  else first = random.nextInt(n);
  Game g = *this;
  g.gameId = id;
  g.askEliminationSource = askEliminationSource || profile == Profile::MtgBagOfDoom;
  g.state = TableState::Running;
  const bool upward = profile == Profile::Lorcana || profile == Profile::DeathMayDie;
  for (auto &p : g.players) p = freshPlayer(p, upward ? 0 : startingLife);
  g.doomBag = profile == Profile::MtgBagOfDoom ? doom::start(first + 1, (1u << n) - 1, n) : DoomBag();
  g.active = first;
  g.starter = first;
  g.winner.reset();
  g.winners.clear();
  g.monarch.reset();
  g.initiative.reset();
  g.dayNight = DayNight::Neither;
  g.turnOrderReversed = false;
  g.startedAtMs = nowMs;
  g.elapsedMs = 0;
  g.runningSinceMs = nowMs;
  g.turnStartedAtElapsedMs = 0;
  g.turnExtensionMs = 0;
  g.timeoutHandled = false;
  g.radDue.clear();
  g.winClaim.reset();
  g.winConfirmed.clear();
  return g.bump();
}

Game Game::reset() const {
  if (state == TableState::Lobby) return *this;
  Game g;
  g.profile = profile;
  g.startingLife = startingLife;
  g.autoEliminateAtZeroLife = autoEliminateAtZeroLife;
  g.askEliminationSource = askEliminationSource;
  g.autoEliminateAtPoisonLimit = autoEliminateAtPoisonLimit;
  g.autoEliminateAtCommanderDamage = autoEliminateAtCommanderDamage;
  g.poisonLimit = poisonLimit;
  g.turnTimerMs = turnTimerMs;
  g.timerRules = timerRules;
  g.twoHeadedGiant = twoHeadedGiant;
  g.houseRules = houseRules;
  for (const auto &p : players) g.players.push_back(freshPlayer(p, 0));
  g.revision = revision + 1;
  return g;
}

Game Game::backToSetup(int64_t nowMs) const {
  return !playing() || gameElapsedMs(nowMs) > UNDO_START_MS ? *this : reset();
}

// --- the table -------------------------------------------------------------

Game Game::applyTurnTimeout(int64_t nowMs) const {
  if (!active || state != TableState::Running || turnTimerMs <= 0 || timeoutHandled ||
      gameElapsedMs(nowMs) - turnStartedAtElapsedMs < turnTimerMs + turnExtensionMs) return *this;
  const auto team = mates(*active);
  const bool demerit = timerRules.timeout == TimeoutAction::Demerit || timerRules.timeout == TimeoutAction::PassAndDemerit;
  const bool autoPass = timerRules.timeout == TimeoutAction::Pass || timerRules.timeout == TimeoutAction::PassAndDemerit;
  Game g = *this;
  g.timeoutHandled = true;
  for (size_t i = 0; i < g.players.size(); ++i)
    if (demerit && contains(team, static_cast<int>(i)) && !g.players[i].eliminated) ++g.players[i].demerits;
  g = g.bump();
  return autoPass ? g.advanceTurn(nowMs, false) : g;
}

Game Game::extendTurn(int index, int64_t nowMs) const {
  if (!active) return *this;
  const auto team = mates(*active);
  const Player &holder = players[*active];
  if (state != TableState::Running || !contains(team, index) || turnTimerMs <= 0 || !timerRules.earnExtensions ||
      turnExtensionMs != 0 || timeoutHandled || holder.extensionsEarned <= holder.extensionsUsed ||
      gameElapsedMs(nowMs) - turnStartedAtElapsedMs >= turnTimerMs) return *this;
  Game g = *this;
  g.turnExtensionMs = turnTimerMs / 2;
  for (int i : team) ++g.players[i].extensionsUsed;
  return g.bump();
}

Game Game::pass(int index, int64_t nowMs) const {
  if (!active) return *this;
  const int holder = *active;
  if (state != TableState::Running || !contains(mates(holder), index)) return *this;
  const Game settled = applyTurnTimeout(nowMs);
  // A timeout may already have passed this turn; don't also pass the next one.
  if (settled.active != active || settled.players[holder].turnsCompleted != players[holder].turnsCompleted) return settled;
  return settled.advanceTurn(nowMs, true);
}

Game Game::advanceTurn(int64_t nowMs, bool reward) const {
  if (!active) return *this;
  const int holder = *active;
  const auto team = mates(holder);
  const auto next = nextAfter(team);
  if (!next) return *this;
  const int64_t now = gameElapsedMs(nowMs);
  const int64_t took = std::max<int64_t>(now - turnStartedAtElapsedMs, 0);
  const Player &h = players[holder];
  const bool earn = reward && timerRules.earnExtensions && turnTimerMs > 0 && !timeoutHandled && turnExtensionMs == 0 &&
      h.turnsCompleted >= 3 && took < turnTimerMs * timerRules.earnBeforePercent / 100 &&
      h.extensionsEarned < timerRules.maxEarnedPerGame;
  Game g = *this;
  for (size_t i = 0; i < g.players.size(); ++i) {
    Player &p = g.players[i];
    if (!contains(team, static_cast<int>(i)) || p.eliminated) continue;
    p.turnsCompleted += 1;
    p.turnMs += took;
    p.fastestTurnMs = p.fastestTurnMs == 0 ? took : std::min(p.fastestTurnMs, took);
    p.longestTurnMs = std::max(p.longestTurnMs, took);
    if (earn) p.extensionsEarned += 1;
  }
  g.active = next;
  g.turnStartedAtElapsedMs = now;
  g.turnExtensionMs = 0;
  g.timeoutHandled = false;
  g.radDue.clear();
  if (magic())
    for (int i : mates(*next)) if (players[i].counters.rad > 0 && !players[i].eliminated) g.radDue.push_back(i);
  return g.bump().doomPassed(holder + 1, nowMs);
}

Game Game::togglePause(int64_t nowMs) const {
  if (doomBag.pending) return *this;
  for (const auto &p : players) if (p.eliminationPending) return *this;
  Game g = *this;
  if (state == TableState::Running) {
    g.state = TableState::Paused;
    g.elapsedMs = gameElapsedMs(nowMs);
    g.runningSinceMs.reset();
    return g.bump();
  }
  if (state == TableState::Paused) {
    g.state = TableState::Running;
    g.runningSinceMs = nowMs;
    return g.bump();
  }
  return *this;
}

Game Game::changeLife(int index, int delta, int64_t nowMs) const {
  if (index < 0 || index >= static_cast<int>(players.size())) return *this;
  const Player &player = players[index];
  if (!playing() || player.eliminated || delta == 0) return *this;
  // Pokémon counts Prize cards left: taking the last one wins.
  if (profile == Profile::Pokemon) {
    const int prizes = clampInt(static_cast<int64_t>(player.life) + delta, 0, std::max(startingLife, player.life));
    if (prizes == player.life) return *this;
    Game g = *this;
    g.players[index].life = prizes;
    g = g.bump();
    return prizes == 0 ? g.finish({index}, nowMs) : g;
  }
  // Death May Die counts wounds up from 0 to the sixth, which kills.
  if (profile == Profile::DeathMayDie) {
    const int wounds = clampInt(static_cast<int64_t>(player.life) + delta, 0, DMD_DEATH_WOUNDS);
    if (wounds == player.life) return *this;
    Game g = *this;
    g.players[index].life = wounds;
    return g.bump();
  }
  // Lorcana counts lore up: reaching the goal wins at once.
  if (profile == Profile::Lorcana) {
    const int lore = clampInt(static_cast<int64_t>(player.life) + delta, 0, MAX_LIFE);
    if (lore == player.life) return *this;
    Game g = *this;
    g.players[index].life = lore;
    g = g.bump();
    return lore >= startingLife ? g.finish({index}, nowMs) : g;
  }
  const int life = clampInt(static_cast<int64_t>(player.life) + delta, -MAX_LIFE, MAX_LIFE);
  Game g = *this;
  for (int i : mates(index)) g.players[i].life = life;
  return g.bump().doomLifeLoss(index + 1, life - player.life).resolveEliminationRules(nowMs);
}

Game Game::commanderDamage(int index, int source, int commander, int delta, int64_t nowMs) const {
  const int n = static_cast<int>(players.size());
  if (index < 0 || index >= n) return *this;
  const Player &player = players[index];
  if (!ProfileRules::commander(profile) || !playing() || player.eliminated || source == index || source < 0 || source >= n ||
      commander < 1 || commander > 2) return *this;
  if (profile == Profile::MtgStarCommander && starAllied(index, source)) return *this;
  // Emperor teammates never attack each other (CR 809.3c).
  if (ProfileRules::emperor(profile) && emperorMates(index, source)) return *this;
  const auto found = player.commanderDamage.find(source);
  const std::array<int, 2> totals = found == player.commanderDamage.end() ? std::array<int, 2>{{0, 0}} : found->second;
  const int now = totals[commander - 1];
  const int next = clampInt(static_cast<int64_t>(now) + delta, 0, MAX_LIFE);
  const int applied = next - now;
  if (applied == 0) return *this;
  Game g = *this;
  std::array<int, 2> updated = totals;
  updated[commander - 1] = next;
  g.players[index].commanderDamage[source] = updated;
  g = g.bump();
  for (int i : mates(index)) g.players[i].life -= applied;
  return g.bump().doomLifeLoss(index + 1, -applied).resolveEliminationRules(nowMs);
}

Game Game::setDayNight(DayNight next) const {
  if (!playing() || !magic() || next == dayNight) return *this;
  Game g = *this;
  g.dayNight = next;
  return g.bump();
}

Game Game::setTurnOrderReversed(bool on) const {
  if (!playing() || ProfileRules::cooperative(profile) || on == turnOrderReversed) return *this;
  Game g = *this;
  g.turnOrderReversed = on;
  return g.bump();
}

Game Game::claimMonarch(int index) const {
  if (!playing() || index < 0 || index >= static_cast<int>(players.size()) || players[index].eliminated || !magic() ||
      monarch == index) return *this;
  Game g = *this;
  g.monarch = index;
  return g.bump();
}

Game Game::changeCounter(int index, const std::string &kind, int value, int commander, int64_t nowMs) const {
  if (index < 0 || index >= static_cast<int>(players.size())) return *this;
  // Pokémon and Lorcana turn markers and Death May Die's dashboard arrive with their ports.
  if (profile == Profile::Pokemon || profile == Profile::Lorcana || profile == Profile::DeathMayDie) return *this;
  const Player &player = players[index];
  if (!playing() || player.eliminated || !magic()) return *this;
  if (kind == "rad-resolve") return resolveRad(index, value, nowMs);
  if (kind == "initiative") {
    if (value < 0 || value > 1 || (value == 0 && initiative != index)) return *this;
    Game g = *this;
    g.initiative = value == 1 ? std::optional<int>(index) : std::nullopt;
    return g.bump();
  }
  if (kind == "city-blessing") {
    if (value < 0 || value > 1) return *this;
    Game g = *this;
    g.players[index].counters.cityBlessing = value == 1;
    return g.bump();
  }
  if (value == 0 || value < -MAX_LIFE || value > MAX_LIFE) return *this;
  const Counters &c = player.counters;
  int old;
  if (kind == "poison") old = c.poison;
  else if (kind == "energy") old = c.energy;
  else if (kind == "experience") old = c.experience;
  else if (kind == "treasure") old = c.treasure;
  else if (kind == "rad") old = c.rad;
  else if (kind == "commander-casts") {
    if (!ProfileRules::commander(profile) || commander < 1 || commander > 2) return *this;
    old = c.commanderCasts[commander - 1];
  } else return *this;
  const int next = old + value;
  if (next < 0 || next > MAX_LIFE) return *this;
  Game g = *this;
  // A team shares its poison count.
  if (kind == "poison") {
    for (int i : mates(index)) g.players[i].counters.poison = next;
    return g.bump().resolveEliminationRules(nowMs);
  }
  Counters &u = g.players[index].counters;
  if (kind == "energy") u.energy = next;
  else if (kind == "experience") u.experience = next;
  else if (kind == "treasure") u.treasure = next;
  else if (kind == "rad") u.rad = next;
  else u.commanderCasts[commander - 1] = next;
  return g.bump().resolveEliminationRules(nowMs);
}

Game Game::changeCounterForEveryone(const std::string &kind, int delta, int64_t nowMs) const {
  static const char *const everyone[] = {"poison", "energy", "experience", "treasure", "rad"};
  bool known = false;
  for (const char *k : everyone) known = known || kind == k;
  if (!known || (delta != -1 && delta != 1)) return *this;
  Game next = *this;
  std::set<int> counted;
  for (size_t i = 0; i < players.size(); ++i) {
    if (next.players[i].eliminated) continue;
    if (kind == "poison" && !counted.insert(mates(static_cast<int>(i)).front()).second) continue;
    next = next.changeCounter(static_cast<int>(i), kind, delta, 1, nowMs);
  }
  return next;
}

Game Game::resolveRad(int index, int nonlands, int64_t nowMs) const {
  const int rad = players[index].counters.rad;
  if (!contains(radDue, index) || nonlands < 0 || nonlands > rad) return *this;
  Game cleared = *this;
  cleared.radDue.erase(std::find(cleared.radDue.begin(), cleared.radDue.end(), index));
  cleared = cleared.bump();
  if (nonlands == 0) return cleared;
  cleared.players[index].counters.rad = rad - nonlands;
  return cleared.bump().changeLife(index, -nonlands, nowMs);
}

Game Game::setEliminationRule(const std::string &field, int value, int64_t nowMs) const {
  if (state == TableState::GameOver || (field != "poisonLimit" && (value < 0 || value > 1))) return *this;
  Game g = *this;
  if (field == "autoEliminateAtZeroLife") g.autoEliminateAtZeroLife = value == 1;
  else if (field == "autoEliminateAtPoisonLimit") g.autoEliminateAtPoisonLimit = value == 1;
  else if (field == "autoEliminateAtCommanderDamage") g.autoEliminateAtCommanderDamage = value == 1;
  else if (field == "poisonLimit") {
    const auto locked = houseRules ? std::nullopt : officialRules(profile, twoHeadedGiant);
    if (value < 1 || value > MAX_LIFE || locked) return *this;
    g.poisonLimit = value;
  } else return *this;
  return g.bump().resolveEliminationRules(nowMs);
}

Game Game::eliminate(int index, int64_t nowMs, bool conceded, std::optional<int> by, bool self) const {
  const int n = static_cast<int>(players.size());
  if (index < 0 || index >= n) return *this;
  if (!playing() || players[index].eliminated || (self && (conceded || by))) return *this;
  if (by && (conceded || *by < 0 || *by >= n || contains(mates(index), *by))) return *this;
  // Teammates leave together.
  std::vector<int> leaving;
  for (int i : mates(index)) if (!players[i].eliminated) leaving.push_back(i);
  int out = 1;
  for (const auto &p : players) out += p.eliminated ? 1 : 0;
  Game g = *this;
  for (int i : leaving) {
    Player &p = g.players[i];
    p.eliminated = true; p.conceded = conceded; p.outOrder = out; p.eliminatedBy = by; p.selfEliminated = self;
  }
  g = g.bump().passStatuses(leaving).doomLeft(by ? *by + 1 : 0);
  bool pending = false;
  for (const auto &p : g.players) pending = pending || p.eliminationPending;
  if (!pending) if (const auto won = g.decided()) return g.finish(*won, nowMs);
  if (active && contains(leaving, *active)) {
    const auto next = g.nextAfter(leaving);
    if (!next) return g;
    g.active = next;
    g.turnStartedAtElapsedMs = g.gameElapsedMs(nowMs);
    g.turnExtensionMs = 0;
    g.timeoutHandled = false;
  }
  return g;
}

Game Game::resolveEliminationRules(int64_t nowMs) const {
  if (!playing()) return *this;
  std::vector<int> targets;
  for (size_t i = 0; i < players.size(); ++i) {
    const Player &p = players[i];
    bool cmd = false;
    for (const auto &d : p.commanderDamage) cmd = cmd || d.second[0] >= COMMANDER_DAMAGE_LOSS || d.second[1] >= COMMANDER_DAMAGE_LOSS;
    if (!p.eliminated && ((autoEliminateAtZeroLife && ProfileRules::countsLife(profile) && p.life <= 0) ||
                          (magic() && autoEliminateAtPoisonLimit && p.counters.poison >= poisonLimit) ||
                          (ProfileRules::commander(profile) && autoEliminateAtCommanderDamage && cmd))) {
      for (int m : mates(static_cast<int>(i)))
        if (!contains(targets, m) && !players[m].eliminated) targets.push_back(m);
    }
  }
  if (targets.empty()) return *this;
  int out = 0;
  for (const auto &p : players) out += p.eliminated ? 1 : 0;
  Game g = *this;
  for (size_t i = 0; i < g.players.size(); ++i) {
    if (!contains(targets, static_cast<int>(i))) continue;
    Player &p = g.players[i];
    p.eliminated = true; p.conceded = false; p.outOrder = ++out;
    p.eliminationPending = askEliminationSource && !ProfileRules::cooperative(profile);
  }
  g = g.passStatuses(targets).bump().doomLeft();
  bool pending = false;
  for (const auto &p : g.players) pending = pending || p.eliminationPending;
  if (!pending) if (const auto won = g.decided()) return g.finish(*won, nowMs);
  if (active && contains(targets, *active)) {
    g.active = g.nextAfter(mates(*active));
    g.turnStartedAtElapsedMs = g.gameElapsedMs(nowMs);
    g.turnExtensionMs = 0;
    g.timeoutHandled = false;
  }
  if (pending) {
    g.elapsedMs = g.gameElapsedMs(nowMs);
    g.state = TableState::Paused;
    g.runningSinceMs.reset();
  }
  return g;
}

Game Game::resolveElimination(int victim, std::optional<int> source, bool self, int64_t nowMs) const {
  const int i = victim - 1;
  const int n = static_cast<int>(players.size());
  if (i < 0 || i >= n) return *this;
  if (!playing() || !players[i].eliminationPending || (self && source) ||
      (source && (*source < 0 || *source >= n || contains(mates(i), *source)))) return *this;
  Game next = *this;
  for (int m : mates(i)) {
    next.players[m].eliminationPending = false;
    next.players[m].selfEliminated = self;
    next.players[m].eliminatedBy = source;
  }
  next = next.bump();
  if (source) next = next.doomLeft(*source + 1);
  bool pending = false;
  for (const auto &p : next.players) pending = pending || p.eliminationPending;
  if (!pending) if (const auto won = next.decided()) return next.finish(*won, nowMs);
  return next;
}

Game Game::requestWin(int index, int64_t nowMs) const {
  if (index < 0 || index >= static_cast<int>(players.size())) return *this;
  bool pending = doomBag.pending;
  for (const auto &p : players) pending = pending || p.eliminationPending;
  if (!playing() || players[index].eliminated || openWinClaim() || pending) return *this;
  if (ProfileRules::cooperative(profile)) return claimWin(index, nowMs);
  Game asked = *this;
  asked.winClaim = index;
  asked.winConfirmed.clear();
  return asked.winConfirmer() ? asked.bump() : claimWin(index, nowMs);
}

Game Game::answerWinClaim(int index, bool accept, int64_t nowMs) const {
  const auto claimant = openWinClaim();
  if (!claimant || winConfirmer() != index) return *this;
  Game next = *this;
  if (!accept) {
    next.winClaim.reset();
    next.winConfirmed.clear();
    return next.bump();
  }
  next.winConfirmed.insert(index);
  return next.winConfirmer() ? next.bump() : next.claimWin(*claimant, nowMs);
}

Game Game::claimWin(int index, int64_t nowMs) const {
  const int n = static_cast<int>(players.size());
  if (index < 0 || index >= n) return *this;
  bool pending = doomBag.pending;
  for (const auto &p : players) pending = pending || p.eliminationPending;
  if (!playing() || players[index].eliminated || pending) return *this;
  std::vector<int> won;
  if (ProfileRules::cooperative(profile)) for (int i = 0; i < n; ++i) won.push_back(i);
  // CR 809.5a: the team wins together, its fallen generals too.
  else if (ProfileRules::emperor(profile) && n == EMPEROR_PLAYERS) won = emperorTeamSeats(emperorTeam(index));
  else won = {index};
  return finish(won, nowMs);
}

Game Game::endWithoutWinner(int64_t nowMs) const { return playing() ? finish({}, nowMs) : *this; }

Game Game::rematch(int64_t nowMs, const std::string &id, Random &random) const {
  if (state != TableState::GameOver) return *this;
  return start(nowMs, id, random);
}

Game Game::seanModeRematch(int64_t nowMs, const std::string &id, Random &random) const {
  if (state != TableState::GameOver || profile != Profile::MtgStarCommander || players.size() != STAR_PLAYERS) return *this;
  Game g = *this;
  for (int i = 0; i < STAR_PLAYERS; ++i) g.players[i] = players[SEAN_MODE_ORDER[i]];
  g.starter.reset();
  return g.start(nowMs, id, random);
}

Game Game::finish(const std::vector<int> &won, int64_t nowMs) const {
  Game g = *this;
  g.state = TableState::GameOver;
  g.winClaim.reset();
  g.winConfirmed.clear();
  g.doomBag.pending = false;
  g.doomBag.pendingTurn = 0;
  g.doomBag.selected = 0;
  g.doomBag.sealed = false;
  g.winner = won.empty() ? std::nullopt : std::optional<int>(won.front());
  g.winners = won;
  g.elapsedMs = gameElapsedMs(nowMs);
  g.runningSinceMs.reset();
  for (auto &p : g.players) p.eliminationPending = false;
  return g.bump();
}

std::optional<std::vector<int>> Game::decided() const {
  std::vector<int> standing;
  std::vector<bool> out;
  for (size_t i = 0; i < players.size(); ++i) {
    out.push_back(players[i].eliminated);
    if (!players[i].eliminated) standing.push_back(static_cast<int>(i));
  }
  if (ProfileRules::cooperative(profile)) return standing.empty() ? std::optional<std::vector<int>>(std::vector<int>{}) : std::nullopt;
  if (profile == Profile::MtgStarCommander && players.size() == STAR_PLAYERS) {
    const auto w = starWinners(out);
    if (!w.empty() || standing.empty()) return w;
    return std::nullopt;
  }
  // Emperor ends when an emperor is out: the other team wins (both out is a draw).
  if (ProfileRules::emperor(profile) && players.size() == EMPEROR_PLAYERS) return emperorWinners(out);
  std::set<int> sides;
  for (int s : standing) sides.insert(mates(s).front());
  if (sides.size() > 1) return std::nullopt;
  if (standing.empty()) return std::vector<int>{};
  return std::vector<int>{standing.front()};
}

Game Game::passStatuses(const std::vector<int> &leaving) const {
  std::optional<int> heir;
  if (active) heir = !players[*active].eliminated ? active : nextIn(*active);
  Game g = *this;
  if (monarch && contains(leaving, *monarch)) g.monarch = heir;
  if (initiative && contains(leaving, *initiative)) g.initiative = heir;
  return g;
}

std::optional<int> Game::nextIn(int index) const {
  const int n = static_cast<int>(players.size());
  for (int k = 1; k <= n; ++k) {
    const int i = (index + k) % n;
    if (!players[i].eliminated && i != index) return i;
  }
  return std::nullopt;
}

std::optional<int> Game::nextAfter(const std::vector<int> &team) const {
  if (!turnOrderReversed) return nextIn(team.back());
  const int n = static_cast<int>(players.size());
  const int from = team.front();
  for (int k = 1; k <= n; ++k) {
    const int i = floorMod(from - k, n);
    if (!players[i].eliminated && !contains(team, i)) return mates(i).front();
  }
  return std::nullopt;
}

// --- Bag of Doom -------------------------------------------------------------

uint32_t Game::doomLiving() const {
  uint32_t mask = 0;
  for (size_t i = 0; i < players.size(); ++i) if (!players[i].eliminated) mask |= doom::bit(static_cast<int>(i) + 1);
  return mask;
}

Game Game::doomLifeLoss(int target, int applied) const {
  if (profile != Profile::MtgBagOfDoom || applied >= 0 || !doom::on(doomLiving())) return *this;
  const int owner = doomBag.pending ? doomBag.pendingTurn : active ? *active + 1 : 0;
  if (owner == target || !(doomLiving() & doom::bit(owner))) return *this;
  Game g = *this;
  g.doomBag.atRisk &= ~doom::bit(owner);
  return g;
}

Game Game::doomPassed(int player, int64_t nowMs) const {
  if (profile != Profile::MtgBagOfDoom || !doom::on(doomLiving())) return *this;
  Game g = *this;
  g.doomBag = doom::pass(doomBag, player);
  if (g.doomBag.pending) {
    g.state = TableState::Paused;
    g.elapsedMs = gameElapsedMs(nowMs);
    g.runningSinceMs.reset();
  }
  return g;
}

Game Game::doomLeft(int credited) const {
  if (profile != Profile::MtgBagOfDoom) return *this;
  Game g = *this;
  g.doomBag = doom::leave(doomBag, doomLiving(), static_cast<int>(players.size()), credited);
  return g;
}

Game Game::doomEligibility(int player, bool inBag, int64_t cycle) const {
  if (!playing() || profile != Profile::MtgBagOfDoom || cycle != doomBag.cycle || doomBag.selected != 0 ||
      !doom::on(doomLiving()) || !(doomLiving() & doom::bit(player))) return *this;
  const uint32_t mask = inBag ? doomBag.atRisk | doom::bit(player) : doomBag.atRisk & ~doom::bit(player);
  if (mask == doomBag.atRisk) return *this;
  Game g = *this;
  g.doomBag.atRisk = mask;
  return g.bump();
}

Game Game::doomCredit(int victim, int source, int64_t cycle) const {
  const int n = static_cast<int>(players.size());
  if (victim - 1 < 0 || victim - 1 >= n) return *this;
  const Player &t = players[victim - 1];
  if (!playing() || profile != Profile::MtgBagOfDoom || cycle != doomBag.cycle || doomBag.selected != 0 || !t.eliminated ||
      t.conceded || t.selfEliminated || t.eliminationPending || t.eliminatedBy || source - 1 < 0 || source - 1 >= n ||
      source == victim || (doomBag.drawn & doom::bit(victim))) return *this;
  Game g = *this;
  g.players[victim - 1].eliminatedBy = source - 1;
  return g.bump().doomLeft(source);
}

Game Game::drawDoom(int64_t cycle, int64_t nowMs, Random &random) const {
  bool pending = false;
  for (const auto &p : players) pending = pending || p.eliminationPending;
  if (state != TableState::Paused || profile != Profile::MtgBagOfDoom || !doomBag.pending || cycle != doomBag.cycle || pending) return *this;
  std::vector<int> candidates;
  for (size_t i = 0; i < players.size(); ++i)
    if (!players[i].eliminated && doomBag.contains(static_cast<int>(i) + 1)) candidates.push_back(static_cast<int>(i));
  const int victim = doomBag.selected != 0 ? doomBag.selected
      : candidates.empty() ? 0 : candidates[random.nextInt(static_cast<int>(candidates.size()))] + 1;
  Game next = *this;
  if (victim != 0) {
    next.doomBag.selected = 0;
    next = next.eliminate(victim - 1, nowMs);
  }
  next.doomBag = doom::checked(doomBag, victim, next.doomLiving(), static_cast<int>(players.size()));
  return next.bump();
}

// --- names -------------------------------------------------------------------

namespace {
struct ProfileNameEntry { Profile p; const char *name; };
const ProfileNameEntry PROFILE_NAMES[] = {
    {Profile::Generic, "GENERIC"}, {Profile::Mtg, "MTG"}, {Profile::MtgCommander, "MTG_COMMANDER"},
    {Profile::MtgStarCommander, "MTG_STAR_COMMANDER"}, {Profile::MtgBagOfDoom, "MTG_BAG_OF_DOOM"},
    {Profile::MtgEmperor, "MTG_EMPEROR"}, {Profile::MtgCommanderEmperor, "MTG_COMMANDER_EMPEROR"},
    {Profile::Yugioh, "YUGIOH"}, {Profile::Pokemon, "POKEMON"}, {Profile::Lorcana, "LORCANA"},
    {Profile::DeathMayDie, "DEATH_MAY_DIE"},
};
const char *const STATE_NAMES[] = {"LOBBY", "STARTING", "RUNNING", "PAUSED", "GAME_OVER"};
const char *const DAY_NIGHT_NAMES[] = {"NEITHER", "DAY", "NIGHT"};
const char *const TIMEOUT_NAMES[] = {"OVERTIME", "PASS", "DEMERIT", "PASS_AND_DEMERIT"};
const char *const PHASE_NAMES[] = {"NORMAL", "WARNING", "EXPIRED", "LONG_TURN"};

template <size_t N>
int indexOf(const char *const (&names)[N], const std::string &s) {
  for (size_t i = 0; i < N; ++i) if (s == names[i]) return static_cast<int>(i);
  return -1;
}
}  // namespace

const char *profileName(Profile p) {
  for (const auto &e : PROFILE_NAMES) if (e.p == p) return e.name;
  return "GENERIC";
}

bool profileFromName(const std::string &name, Profile &out) {
  for (const auto &e : PROFILE_NAMES) if (name == e.name) { out = e.p; return true; }
  return false;
}

const char *stateName(TableState s) { return STATE_NAMES[static_cast<int>(s)]; }
const char *phaseName(TimerPhase p) { return PHASE_NAMES[static_cast<int>(p)]; }

// --- JSON --------------------------------------------------------------------

namespace {

Json opt(const std::optional<int> &v) { return v ? Json(*v) : Json(); }
Json opt64(const std::optional<int64_t> &v) { return v ? Json(*v) : Json(); }
Json optString(const std::string &s) { return s.empty() ? Json() : Json(s); }
std::optional<int> readOpt(const Json &j) { return j.isNumber() ? std::optional<int>(static_cast<int>(j.asInt())) : std::nullopt; }
Json ints(const std::vector<int> &v) { Json a = Json::array(); for (int x : v) a.push(x); return a; }
std::vector<int> readInts(const Json &j) { std::vector<int> v; for (size_t i = 0; i < j.size(); ++i) v.push_back(static_cast<int>(j[i].asInt())); return v; }

Json counterJson(const Counters &c) {
  Json j = Json::object();
  j.set("poison", c.poison);
  j.set("energy", c.energy);
  j.set("experience", c.experience);
  Json casts = Json::array();
  casts.push(c.commanderCasts[0]);
  casts.push(c.commanderCasts[1]);
  j.set("commanderCasts", casts);
  j.set("cityBlessing", c.cityBlessing);
  j.set("treasure", c.treasure);
  j.set("rad", c.rad);
  return j;
}

Json playerJson(const Player &p) {
  Json j = Json::object();
  j.set("name", p.name);
  j.set("profileId", optString(p.profileId));
  j.set("localId", optString(p.localId));
  j.set("life", p.life);
  j.set("eliminated", p.eliminated);
  j.set("conceded", p.conceded);
  j.set("eliminatedBy", opt(p.eliminatedBy));
  j.set("selfEliminated", p.selfEliminated);
  j.set("eliminationPending", p.eliminationPending);
  j.set("counters", counterJson(p.counters));
  j.set("outOrder", opt(p.outOrder));
  j.set("turnsCompleted", p.turnsCompleted);
  j.set("turnMs", p.turnMs);
  j.set("fastestTurnMs", p.fastestTurnMs);
  j.set("longestTurnMs", p.longestTurnMs);
  Json damage = Json::object();
  for (const auto &d : p.commanderDamage) {
    Json pair = Json::array();
    pair.push(d.second[0]);
    pair.push(d.second[1]);
    damage.set(std::to_string(d.first), pair);
  }
  j.set("commanderDamage", damage);
  j.set("demerits", p.demerits);
  j.set("extensionsEarned", p.extensionsEarned);
  j.set("extensionsUsed", p.extensionsUsed);
  j.set("commander", optString(p.commander));
  j.set("commanderArt", optString(p.commanderArt));
  j.set("cardOnly", p.cardOnly);
  return j;
}

Player playerFromJson(const Json &j) {
  Player p;
  p.name = j.get("name").asString();
  p.profileId = j.get("profileId").asString();
  p.localId = j.get("localId").asString();
  p.life = static_cast<int>(j.get("life").asInt());
  p.eliminated = j.get("eliminated").asBool();
  p.conceded = j.get("conceded").asBool();
  p.eliminatedBy = readOpt(j.get("eliminatedBy"));
  p.selfEliminated = j.get("selfEliminated").asBool();
  p.eliminationPending = j.get("eliminationPending").asBool();
  const Json &c = j.get("counters");
  p.counters.poison = static_cast<int>(c.get("poison").asInt());
  p.counters.energy = static_cast<int>(c.get("energy").asInt());
  p.counters.experience = static_cast<int>(c.get("experience").asInt());
  p.counters.commanderCasts = {{static_cast<int>(c.get("commanderCasts")[0].asInt()), static_cast<int>(c.get("commanderCasts")[1].asInt())}};
  p.counters.cityBlessing = c.get("cityBlessing").asBool();
  p.counters.treasure = static_cast<int>(c.get("treasure").asInt());
  p.counters.rad = static_cast<int>(c.get("rad").asInt());
  p.outOrder = readOpt(j.get("outOrder"));
  p.turnsCompleted = static_cast<int>(j.get("turnsCompleted").asInt());
  p.turnMs = j.get("turnMs").asInt();
  p.fastestTurnMs = j.get("fastestTurnMs").asInt();
  p.longestTurnMs = j.get("longestTurnMs").asInt();
  for (const auto &m : j.get("commanderDamage").members())
    p.commanderDamage[atoi(m.first.c_str())] = {{static_cast<int>(m.second[0].asInt()), static_cast<int>(m.second[1].asInt())}};
  p.demerits = static_cast<int>(j.get("demerits").asInt());
  p.extensionsEarned = static_cast<int>(j.get("extensionsEarned").asInt());
  p.extensionsUsed = static_cast<int>(j.get("extensionsUsed").asInt());
  p.commander = j.get("commander").asString();
  p.commanderArt = j.get("commanderArt").asString();
  p.cardOnly = j.get("cardOnly").asBool();
  return p;
}

Json doomJson(const DoomBag &b) {
  Json j = Json::object();
  j.set("atRisk", static_cast<int64_t>(b.atRisk));
  j.set("drawn", static_cast<int64_t>(b.drawn));
  j.set("boundary", b.boundary);
  j.set("warmup", b.warmup);
  j.set("cycle", b.cycle);
  j.set("pending", b.pending);
  j.set("pendingTurn", b.pendingTurn);
  j.set("selected", b.selected);
  j.set("sealed", b.sealed);
  j.set("lastDraw", b.lastDraw);
  j.set("lastCheck", b.lastCheck);
  return j;
}

DoomBag doomFromJson(const Json &j) {
  DoomBag b;
  b.atRisk = static_cast<uint32_t>(j.get("atRisk").asInt());
  b.drawn = static_cast<uint32_t>(j.get("drawn").asInt());
  b.boundary = static_cast<int>(j.get("boundary").asInt());
  b.warmup = static_cast<int>(j.get("warmup").asInt());
  b.cycle = j.get("cycle").asInt();
  b.pending = j.get("pending").asBool();
  b.pendingTurn = static_cast<int>(j.get("pendingTurn").asInt());
  b.selected = static_cast<int>(j.get("selected").asInt());
  b.sealed = j.get("sealed").asBool();
  b.lastDraw = static_cast<int>(j.get("lastDraw").asInt());
  b.lastCheck = j.get("lastCheck").asInt();
  return b;
}

TimerRules timerRulesFromJson(const Json &j, const TimerRules &fallback) {
  TimerRules r = fallback;
  const int t = indexOf(TIMEOUT_NAMES, j.get("timeout").asString());
  if (t >= 0) r.timeout = static_cast<TimeoutAction>(t);
  if (j.has("earnExtensions")) r.earnExtensions = j.get("earnExtensions").asBool();
  if (j.has("earnBeforePercent")) r.earnBeforePercent = static_cast<int>(j.get("earnBeforePercent").asInt());
  if (j.has("maxEarnedPerGame")) r.maxEarnedPerGame = static_cast<int>(j.get("maxEarnedPerGame").asInt());
  return r;
}

}  // namespace

Json toJson(const Game &g) {
  Json j = Json::object();
  j.set("gameId", g.gameId);
  j.set("state", stateName(g.state));
  j.set("profile", profileName(g.profile));
  j.set("startingLife", g.startingLife);
  j.set("autoEliminateAtZeroLife", g.autoEliminateAtZeroLife);
  j.set("askEliminationSource", g.askEliminationSource);
  j.set("autoEliminateAtPoisonLimit", g.autoEliminateAtPoisonLimit);
  j.set("autoEliminateAtCommanderDamage", g.autoEliminateAtCommanderDamage);
  j.set("poisonLimit", g.poisonLimit);
  j.set("turnTimerMs", g.turnTimerMs);
  Json rules = Json::object();
  rules.set("timeout", TIMEOUT_NAMES[static_cast<int>(g.timerRules.timeout)]);
  rules.set("earnExtensions", g.timerRules.earnExtensions);
  rules.set("earnBeforePercent", g.timerRules.earnBeforePercent);
  rules.set("maxEarnedPerGame", g.timerRules.maxEarnedPerGame);
  j.set("timerRules", rules);
  j.set("turnExtensionMs", g.turnExtensionMs);
  j.set("timeoutHandled", g.timeoutHandled);
  j.set("twoHeadedGiant", g.twoHeadedGiant);
  j.set("houseRules", g.houseRules);
  Json players = Json::array();
  for (const auto &p : g.players) players.push(playerJson(p));
  j.set("players", players);
  j.set("active", opt(g.active));
  j.set("starter", opt(g.starter));
  j.set("winner", opt(g.winner));
  j.set("winners", ints(g.winners));
  j.set("monarch", opt(g.monarch));
  j.set("initiative", opt(g.initiative));
  j.set("dayNight", DAY_NIGHT_NAMES[static_cast<int>(g.dayNight)]);
  j.set("turnOrderReversed", g.turnOrderReversed);
  j.set("winClaim", opt(g.winClaim));
  j.set("winConfirmed", ints(std::vector<int>(g.winConfirmed.begin(), g.winConfirmed.end())));
  j.set("startedAtMs", g.startedAtMs);
  j.set("elapsedMs", g.elapsedMs);
  j.set("runningSinceMs", opt64(g.runningSinceMs));
  j.set("turnStartedAtElapsedMs", g.turnStartedAtElapsedMs);
  j.set("doomBag", doomJson(g.doomBag));
  j.set("radDue", ints(g.radDue));
  j.set("revision", g.revision);
  return j;
}

Json gameRecord(const Game &g) {
  if (g.state != TableState::GameOver || g.gameId.empty()) return Json();
  std::string wire = profileName(g.profile);
  for (char &c : wire) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  Json r = Json::object();
  if (g.twoHeadedGiant) r.set("twoHeadedGiant", true);
  r.set("recordId", g.gameId);
  r.set("gameProfile", wire);
  r.set("startingLife", g.startingLife);
  r.set("startedAtMs", g.startedAtMs);
  r.set("durationMs", g.elapsedMs);
  r.set("starter", opt(g.starter));
  r.set("winner", opt(g.winner));
  r.set("winners", ints(g.winners));
  Json players = Json::array();
  for (size_t i = 0; i < g.players.size(); ++i) {
    const Player &p = g.players[i];
    Json j = Json::object();
    j.set("name", p.name);
    j.set("profileId", optString(p.profileId));
    j.set("localId", optString(p.localId));
    j.set("finalLife", p.life);
    j.set("turnsCompleted", p.turnsCompleted);
    j.set("demerits", p.demerits);
    j.set("extensionsUsed", p.extensionsUsed);
    j.set("turnMs", p.turnMs);
    j.set("fastestTurnMs", p.fastestTurnMs);
    j.set("longestTurnMs", p.longestTurnMs);
    j.set("outOrder", opt(p.outOrder));
    j.set("conceded", p.conceded);
    j.set("eliminatedBy", opt(p.eliminatedBy));
    j.set("bagDrawn", (g.doomBag.drawn & (1u << i)) != 0);
    j.set("selfEliminated", p.selfEliminated);
    int received = 0;
    for (const auto &d : p.commanderDamage) received += d.second[0] + d.second[1];
    j.set("commanderDamageReceived", received);
    // The record keeps the counters that matter after the game: not treasure or rad.
    Json counters = Json::object();
    counters.set("poison", p.counters.poison);
    counters.set("energy", p.counters.energy);
    counters.set("experience", p.counters.experience);
    Json casts = Json::array();
    casts.push(p.counters.commanderCasts[0]);
    casts.push(p.counters.commanderCasts[1]);
    counters.set("commanderCasts", casts);
    counters.set("cityBlessing", p.counters.cityBlessing);
    j.set("counters", counters);
    Json bySource = Json::object();
    for (const auto &d : p.commanderDamage) {
      Json pair = Json::array();
      pair.push(d.second[0]);
      pair.push(d.second[1]);
      bySource.set(std::to_string(d.first), pair);
    }
    j.set("commanderBySource", bySource);
    players.push(j);
  }
  r.set("players", players);
  return r;
}

Game gameFromJson(const Json &j) {
  Game g;
  g.gameId = j.get("gameId").asString();
  const int s = indexOf(STATE_NAMES, j.get("state").asString());
  if (s >= 0) g.state = static_cast<TableState>(s);
  profileFromName(j.get("profile").asString(), g.profile);
  g.startingLife = static_cast<int>(j.get("startingLife").asInt(g.startingLife));
  g.autoEliminateAtZeroLife = j.get("autoEliminateAtZeroLife").asBool(g.autoEliminateAtZeroLife);
  g.askEliminationSource = j.get("askEliminationSource").asBool(g.askEliminationSource);
  g.autoEliminateAtPoisonLimit = j.get("autoEliminateAtPoisonLimit").asBool(g.autoEliminateAtPoisonLimit);
  g.autoEliminateAtCommanderDamage = j.get("autoEliminateAtCommanderDamage").asBool(g.autoEliminateAtCommanderDamage);
  g.poisonLimit = static_cast<int>(j.get("poisonLimit").asInt(g.poisonLimit));
  g.turnTimerMs = j.get("turnTimerMs").asInt();
  g.timerRules = timerRulesFromJson(j.get("timerRules"), g.timerRules);
  g.turnExtensionMs = j.get("turnExtensionMs").asInt();
  g.timeoutHandled = j.get("timeoutHandled").asBool();
  g.twoHeadedGiant = j.get("twoHeadedGiant").asBool();
  g.houseRules = j.get("houseRules").asBool();
  const Json &players = j.get("players");
  for (size_t i = 0; i < players.size(); ++i) g.players.push_back(playerFromJson(players[i]));
  g.active = readOpt(j.get("active"));
  g.starter = readOpt(j.get("starter"));
  g.winner = readOpt(j.get("winner"));
  g.winners = readInts(j.get("winners"));
  g.monarch = readOpt(j.get("monarch"));
  g.initiative = readOpt(j.get("initiative"));
  const int d = indexOf(DAY_NIGHT_NAMES, j.get("dayNight").asString());
  if (d >= 0) g.dayNight = static_cast<DayNight>(d);
  g.turnOrderReversed = j.get("turnOrderReversed").asBool();
  g.winClaim = readOpt(j.get("winClaim"));
  for (int x : readInts(j.get("winConfirmed"))) g.winConfirmed.insert(x);
  g.startedAtMs = j.get("startedAtMs").asInt();
  g.elapsedMs = j.get("elapsedMs").asInt();
  if (j.get("runningSinceMs").isNumber()) g.runningSinceMs = j.get("runningSinceMs").asInt();
  g.turnStartedAtElapsedMs = j.get("turnStartedAtElapsedMs").asInt();
  g.doomBag = doomFromJson(j.get("doomBag"));
  g.radDue = readInts(j.get("radDue"));
  g.revision = j.get("revision").asInt();
  return g;
}

Game applySeatAction(const Game &game, const SeatAction &a, int64_t now) {
  const int n = static_cast<int>(game.players.size());
  // Planechase and Death May Die seats arrive with their ports.
  if (a.kind.rfind("planar-", 0) == 0 || game.profile == Profile::DeathMayDie) return game;
  if (game.gameId != a.match || game.revision != a.revision || a.player < 1 || a.player > n ||
      (game.state != TableState::Running && game.state != TableState::Paused)) return game;
  const bool cards = ProfileRules::magic(game.profile) || game.profile == Profile::Pokemon ||
      game.profile == Profile::Lorcana || game.profile == Profile::Yugioh;
  if (!cards) return game;
  const int index = a.player - 1;
  const bool simple = a.value == 0 && a.slot == 0 && a.source == 0;
  const std::string &k = a.kind;
  if (k == "life" && a.slot == 0 && a.source == 0 && a.value != 0 && a.value >= -10000 && a.value <= 10000)
    return game.changeLife(index, a.value, now);
  if (k == "commander" && a.slot >= 1 && a.slot <= 2 && a.source >= 1 && a.source <= n && (a.value == -1 || a.value == 1))
    return game.commanderDamage(index, a.source - 1, a.slot, a.value, now);
  if (simple) {
    if (k == "pass") return game.pass(index, now);
    if (k == "extend-turn") return game.extendTurn(index, now);
    // A claim waits on the other players, as a Sigil's does.
    if (k == "win") return game.requestWin(index, now);
    if (k == "confirm") return game.answerWinClaim(index, true, now);
    if (k == "deny") return game.answerWinClaim(index, false, now);
    if (k == "concede") return game.concede(index, now);
    if (k == "eliminate") return game.eliminate(index, now);
    if (k == "monarch") return game.claimMonarch(index);
  }
  // 1 pauses and 0 resumes, so a retried or crossed command never undoes another player's tap.
  if (k == "pause" && a.slot == 0 && a.source == 0 && (a.value == 0 || a.value == 1))
    return (game.state == TableState::Running) == (a.value == 1) ? game.togglePause(now) : game;
  if (k == "rad-resolve" && a.slot == 0 && a.source == 0 && a.value >= 0 && a.value <= Game::MAX_LIFE)
    return game.changeCounter(index, k, a.value, 1, now);
  static const char *const counters[] = {"poison", "energy", "experience", "treasure", "rad", "commander-casts", "city-blessing", "initiative"};
  if (a.source == 0 && a.slot >= 0 && a.slot <= 2 && a.value >= -1 && a.value <= 1)
    for (const char *c : counters) if (k == c) return game.changeCounter(index, k, a.value, std::max(a.slot, 1), now);
  // Turn actions and Pokémon Special Conditions arrive with their port.
  return game;
}

Game applyAction(const Game &g, const Json &a, Random &random) {
  const std::string &d = a.get("do").asString();
  const int64_t now = a.get("nowMs").asInt();
  const auto i = [&](const char *key) { return static_cast<int>(a.get(key).asInt()); };
  if (d == "addPlayer") return g.addPlayer(a.get("name").asString(), a.get("profileId").asString(), a.get("localId").asString());
  if (d == "removePlayer") return g.removePlayer(i("index"));
  if (d == "clearPlayers") return g.clearPlayers();
  if (d == "movePlayer") return g.movePlayer(i("index"), i("by"));
  if (d == "swapPlayers") return g.swapPlayers(i("a"), i("b"));
  if (d == "chooseStarter") return g.chooseStarter(readOpt(a.get("index")));
  if (d == "setFormat") {
    Profile p;
    if (!profileFromName(a.get("profile").asString(), p)) return g;
    return g.setFormat(p, readOpt(a.get("life")));
  }
  if (d == "setHouseRules") return g.setHouseRules(a.get("on").asBool());
  if (d == "setTwoHeadedGiant") return g.setTwoHeadedGiant(a.get("on").asBool());
  if (d == "setStartingLife") return g.setStartingLife(i("life"));
  if (d == "setTurnTimer") return g.setTurnTimer(a.get("ms").asInt());
  if (d == "setTimerRules") return g.setTimerRules(timerRulesFromJson(a, TimerRules()));
  if (d == "start") return g.start(now, a.get("gameId").asString(), random);
  if (d == "reset") return g.reset();
  if (d == "backToSetup") return g.backToSetup(now);
  if (d == "applyTurnTimeout") return g.applyTurnTimeout(now);
  if (d == "extendTurn") return g.extendTurn(i("index"), now);
  if (d == "pass") return g.pass(i("index"), now);
  if (d == "togglePause") return g.togglePause(now);
  if (d == "changeLife") return g.changeLife(i("index"), i("delta"), now);
  if (d == "commanderDamage") return g.commanderDamage(i("index"), i("source"), i("commander"), i("delta"), now);
  if (d == "setDayNight") {
    const int v = indexOf(DAY_NIGHT_NAMES, a.get("value").asString());
    return v < 0 ? g : g.setDayNight(static_cast<DayNight>(v));
  }
  if (d == "setTurnOrderReversed") return g.setTurnOrderReversed(a.get("on").asBool());
  if (d == "claimMonarch") return g.claimMonarch(i("index"));
  if (d == "changeCounter")
    return g.changeCounter(i("index"), a.get("kind").asString(), i("value"), a.has("commander") ? i("commander") : 1, now);
  if (d == "changeCounterForEveryone") return g.changeCounterForEveryone(a.get("kind").asString(), i("delta"), now);
  if (d == "setEliminationRule") return g.setEliminationRule(a.get("field").asString(), i("value"), now);
  if (d == "concede") return g.concede(i("index"), now);
  if (d == "eliminate") return g.eliminate(i("index"), now, a.get("conceded").asBool(), readOpt(a.get("by")), a.get("self").asBool());
  if (d == "resolveElimination") return g.resolveElimination(i("victim"), readOpt(a.get("source")), a.get("self").asBool(), now);
  if (d == "requestWin") return g.requestWin(i("index"), now);
  if (d == "answerWinClaim") return g.answerWinClaim(i("index"), a.get("accept").asBool(), now);
  if (d == "claimWin") return g.claimWin(i("index"), now);
  if (d == "endWithoutWinner") return g.endWithoutWinner(now);
  if (d == "rematch") return g.rematch(now, a.get("gameId").asString(), random);
  if (d == "seanModeRematch") return g.seanModeRematch(now, a.get("gameId").asString(), random);
  if (d == "doomEligibility") return g.doomEligibility(i("player"), a.get("inBag").asBool(), a.get("cycle").asInt());
  if (d == "doomCredit") return g.doomCredit(i("victim"), i("source"), a.get("cycle").asInt());
  if (d == "drawDoom") return g.drawDoom(a.get("cycle").asInt(), now, random);
  if (d == "seat") {
    SeatAction seat;
    seat.match = a.get("match").asString();
    seat.revision = a.get("revision").asInt();
    seat.player = i("player");
    seat.kind = a.get("kind").asString();
    seat.value = i("value");
    seat.slot = i("slot");
    seat.source = i("source");
    return applySeatAction(g, seat, now);
  }
  return g;
}

}  // namespace turnhub
