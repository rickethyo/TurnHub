#include "led_renderer.h"
#include "hosted_game.h"

#include <cstring>
#include <algorithm>

namespace TurnHub {

LedRenderer::LedRenderer(SigilBus &bus)
    : bus_(bus) {
  invalidateAll();
}

void LedRenderer::invalidate(uint8_t sigilId) {
  if (sigilId < MAX_PHYSICAL_SIGILS) {
    cache_[sigilId].ledStateValid = false;
    cache_[sigilId].clockValid = false;
    cache_[sigilId].displayValid = false;
  }
}

void LedRenderer::invalidateAll() {
  for (auto &entry : cache_) {
    entry.ledStateValid = false;
    entry.clockValid = false;
    entry.displayValid = false;
  }
}

void LedRenderer::sendLedState(uint8_t sigilId, const SigilLedState &cue, uint32_t nowMs) {
  TurnHubProtocol::LedStateFields fields;
  fields.cue = cue.cue;
  fields.overlays = cue.overlays;
  fields.playerNumber = cue.playerNumber < 15 ? cue.playerNumber : 15;
  fields.seatSlot = cue.seatSlot;
  fields.sharedSeat = cue.sharedSeat;

  fields.anchorAgeMs = cue.anchorMs != 0 ? nowMs - cue.anchorMs : 0;

  const int32_t value = TurnHubProtocol::encodeLedState(fields);
  const uint32_t key = TurnHubProtocol::ledStateKey(value);
  Cache &cache = cache_[sigilId];
  // Atlas's clock paces every Sigil's looping patterns (TableClock).
  if ((!cache.clockValid || nowMs - cache.lastClockTxMs >= TurnHubProtocol::TABLE_CLOCK_INTERVAL_MS) &&
      bus_.send(sigilId, TurnHubProtocol::PacketType::TableClock, static_cast<int32_t>(nowMs))) {
    cache.clockValid = true;
    cache.lastClockTxMs = nowMs;
  }
  if (cache.ledStateValid && cache.ledStateKey == key) return;
  if (bus_.send(sigilId, TurnHubProtocol::PacketType::LedState, value)) {
    cache.ledStateValid = true;
    cache.ledStateKey = key;
  }
}

void LedRenderer::renderHosted(const turnhub::Game &game, uint32_t nowMs, uint16_t sigilMask) {
  using namespace TurnHubProtocol;
  using turnhub::TableState;
  const bool inGame = game.state != TableState::Lobby && game.state != TableState::Starting;
  const DisplayMode mode = game.state == TableState::Lobby ? DisplayMode::Lobby : game.state == TableState::Starting ?
      DisplayMode::Starting : game.state == TableState::Running ? DisplayMode::Running :
      game.state == TableState::Paused ? DisplayMode::Paused : DisplayMode::GameOver;
  const int n = static_cast<int>(game.players.size());
  auto copyName = [](const std::string &from, char *out, int number) {
    if (from.empty()) { snprintf(out, DISPLAY_NAME_MAX_LENGTH + 1, "Player %d", number); return; }
    for (size_t c = 0; c < from.size() && c < DISPLAY_NAME_MAX_LENGTH; ++c)
      out[c] = from[c] >= 32 && from[c] <= 126 ? from[c] : '?';
  };
  auto isWinner = [&](int i) {
    return game.state == TableState::GameOver && ((game.winner && *game.winner == i) ||
        std::find(game.winners.begin(), game.winners.end(), i) != game.winners.end());
  };
  auto hasTurn = [&](int i) {
    if (!game.active || game.state == TableState::GameOver) return false;
    if (*game.active == i) return true;
    const std::vector<int> mates = game.mates(*game.active);  // Two-Headed Giant: the team's turn.
    return std::find(mates.begin(), mates.end(), i) != mates.end();
  };

  // The table roster: rebuilt at most once a second.
  TableCache &roster = tableCache_;
  if (!roster.valid || nowMs - roster.builtAt >= 1000) {
    TableDisplayPacket pages[2] = {};
    const uint8_t count = static_cast<uint8_t>(std::min<int>(n, TABLE_MAX_PLAYERS));
    for (uint8_t page = 0; page < 2; ++page) {
      auto &p = pages[page];
      p.version = VERSION; p.type = PacketType::TableDisplay; p.mode = static_cast<uint8_t>(mode);
      p.game = 1; p.page = page; p.total = count; p.round = 0;
      for (uint8_t i = 0; i < TABLE_PAGE_ROWS && page * TABLE_PAGE_ROWS + i < count; ++i) {
        const int index = page * TABLE_PAGE_ROWS + i;
        auto &row = p.rows[i];
        row.player = static_cast<uint8_t>(index + 1);
        row.life = game.players[index].life;
        if (inGame && game.state != TableState::GameOver && hasTurn(index)) row.flags |= TABLE_ACTIVE;
        if (game.players[index].eliminated) row.flags |= TABLE_OUT;
        if (isWinner(index)) row.flags |= TABLE_WINNER;
        if (!inGame && game.starter && *game.starter == index) row.flags |= TABLE_STARTER;
        copyName(game.players[index].name, row.name, index + 1);
      }
    }
    const uint32_t previous = roster.pages[0].revision;
    for (auto &p : pages) p.revision = previous;
    if (!roster.valid || memcmp(pages, roster.pages, sizeof(pages))) {
      for (auto &p : pages) p.revision = previous + 1;
      memcpy(roster.pages, pages, sizeof(pages));
    }
    roster.valid = true; roster.builtAt = nowMs;
  }

  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if ((sigilMask & (1u << id)) == 0) continue;
    if (!bus_.isOnline(id, nowMs)) { invalidate(id); continue; }
    Cache &cache = cache_[id];
    if (!cache.displayValid || cache.tableRevision != roster.pages[0].revision || nowMs - cache.lastTableTxMs >= 5000) {
      bool sent = true;
      for (uint8_t page = 0; page < (roster.pages[0].total > TABLE_PAGE_ROWS ? 2 : 1); ++page) {
        auto packet = roster.pages[page]; packet.sigilId = id;
        if (!bus_.sendTableDisplay(packet)) sent = false;
      }
      if (sent) { cache.tableRevision = roster.pages[0].revision; cache.lastTableTxMs = nowMs; }
    }

    int seats[2] = {-1, -1};
    const uint8_t count = TurnHubHosted::sigilPlayers(game, id, seats);
    int primary = count > 0 ? seats[0] : -1, secondary = count > 1 ? seats[1] : -1;
    uint8_t flags = 0;
    SigilLedState cue;
    // Lead with the seat whose turn it is, the winner, or a living seat.
    auto lead = [&](int index) { if (index == secondary) std::swap(primary, secondary); };
    if (secondary >= 0 && game.players[primary].eliminated && !game.players[secondary].eliminated) lead(secondary);
    int turnSeat = -1;
    for (uint8_t i = 0; i < count; ++i) if (hasTurn(seats[i])) turnSeat = seats[i];
    int winnerSeat = -1;
    for (uint8_t i = 0; i < count; ++i) if (isWinner(seats[i])) winnerSeat = seats[i];
    if (game.state == TableState::Running || game.state == TableState::Paused) {
      if (turnSeat >= 0) { flags |= DISPLAY_FLAG_ACTIVE; lead(turnSeat); }
    }
    if (winnerSeat >= 0) { flags |= DISPLAY_FLAG_WINNER; lead(winnerSeat); }
    const std::optional<int> confirmer = game.winConfirmer();
    if (confirmer && (*confirmer == primary || *confirmer == secondary)) { flags |= DISPLAY_FLAG_ATTENTION; lead(*confirmer); }
    if (!inGame && game.starter && (*game.starter == primary || *game.starter == secondary)) flags |= DISPLAY_FLAG_STARTER;
    uint8_t slot = 1, sigil = 0;
    if (secondary >= 0 && TurnHubHosted::playerSigil(game, primary, sigil, slot) && slot == 2) flags |= DISPLAY_FLAG_PRIMARY_B;

    switch (game.state) {
      case TableState::Lobby:
        cue.cue = count ? LedCue::Joined : LedCue::Unassigned;
        cue.playerNumber = count ? static_cast<uint8_t>(primary + 1) : 0;
        break;
      case TableState::Starting: cue.cue = count ? LedCue::Starting : LedCue::Unassigned; break;
      case TableState::Running:
        if (!count) break;
        if (turnSeat >= 0) {
          const int64_t elapsed = game.elapsedMs - game.turnStartedAtElapsedMs;
          cue.cue = elapsed < 3000 ? LedCue::TurnStarted : LedCue::YourTurn;
          cue.seatSlot = TurnHubHosted::playerSigil(game, turnSeat, sigil, slot) ? slot : 1;
          cue.sharedSeat = count > 1;
        } else {
          cue.cue = LedCue::Waiting;
        }
        break;
      case TableState::Paused:
        if (!count) break;
        cue.cue = (flags & DISPLAY_FLAG_ATTENTION) ? LedCue::ConfirmationNeeded : LedCue::Paused;
        break;
      case TableState::GameOver:
        if (!count) break;
        cue.cue = LedCue::GameOver;
        if (winnerSeat >= 0) cue.overlays |= ledOverlayBit(LedOverlay::Winner);
        break;
    }

    const uint8_t primaryNumber = primary >= 0 ? static_cast<uint8_t>(primary + 1) : 0;
    const uint8_t secondaryNumber = secondary >= 0 ? static_cast<uint8_t>(secondary + 1) : 0;
    const int32_t payload = encodeDisplayState(mode, primaryNumber, secondaryNumber, 0, flags);
    if (inGame && primary >= 0) {
      GameDisplayPacket snapshot{};
      snapshot.version = VERSION;
      snapshot.type = PacketType::GameDisplay;
      snapshot.sigilId = id;
      snapshot.state = payload;
      snapshot.commander = turnhub::ProfileRules::commander(game.profile) ? 1 : 0;
      snapshot.primary.life = game.players[primary].life;
      copyName(game.players[primary].name, snapshot.primary.name, primary + 1);
      if (secondary >= 0) {
        snapshot.secondary.life = game.players[secondary].life;
        copyName(game.players[secondary].name, snapshot.secondary.name, secondary + 1);
      }
      if (snapshot.commander) for (const auto &entry : game.players[primary].commanderDamage) {
        const int source = entry.first;
        if (source < 0 || source >= n || source == primary || (!entry.second[0] && !entry.second[1])) continue;
        if (snapshot.sourceCount == DISPLAY_COMMANDER_SOURCES) { ++snapshot.omittedSources; continue; }
        auto &out = snapshot.sources[snapshot.sourceCount++];
        out.player = static_cast<uint8_t>(source + 1);
        copyName(game.players[source].name, out.name, source + 1);
        out.damage[0] = entry.second[0];
        out.damage[1] = entry.second[1];
      }
      if (!cache.displayValid || cache.displayPayload != payload || memcmp(&snapshot, &cache.gameDisplay, sizeof(snapshot)) != 0) {
        if (bus_.sendGameDisplay(snapshot)) {
          cache.gameDisplay = snapshot;
          cache.displayPayload = payload;
          cache.displayValid = true;
        }
      }
    } else if ((!cache.displayValid || cache.displayPayload != payload) &&
        bus_.send(id, PacketType::DisplayState, payload)) {
      cache.displayPayload = payload;
      cache.displayValid = true;
    }
    sendLedState(id, cue, nowMs);
  }
}

}  // namespace TurnHub
