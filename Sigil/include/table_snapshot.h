#pragma once
#include <string.h>
#include "protocol.h"

namespace TurnHubSigil {
// Publish a complete roster only; partial pages never mix revisions.
class TableSnapshot {
 public:
  bool accept(const TurnHubProtocol::TableDisplayPacket &p) {
    using namespace TurnHubProtocol;
    if (!validTableDisplay(p)) return false;
    if (!pending_ || p.revision != revision_ || p.game != scope_) {
      revision_ = p.revision;
      mask_ = 0;
      pending_ = true;
      scope_ = p.game;
      memset(staging_, 0, sizeof(staging_));
      total_ = p.total;
      mode_ = p.mode;
      round_ = p.round;
    }
    if (p.total != total_ || p.mode != mode_ || p.round != round_) return false;
    memcpy(staging_ + p.page * TABLE_PAGE_ROWS, p.rows, sizeof(p.rows));
    mask_ |= 1u << p.page;
    if (mask_ != (total_ > TABLE_PAGE_ROWS ? 3 : 1)) return false;
    const bool changed = !valid || game != scope_ || total != total_ || mode != mode_ || round != round_ ||
        memcmp(rows, staging_, sizeof(rows)) != 0;
    memcpy(rows, staging_, sizeof(rows));
    // Empty/short rosters clear the second page from a previous game.
    if (total_ <= TABLE_PAGE_ROWS) memset(rows + TABLE_PAGE_ROWS, 0, sizeof(p.rows));
    total = total_; mode = mode_; round = round_; valid = true;
    game = scope_;
    return changed;
  }
  void clear() { *this = TableSnapshot(); }
  bool valid = false;
  uint8_t total = 0, mode = 0, round = 0, game = 0;
  TurnHubProtocol::TableDisplayRow rows[TurnHubProtocol::TABLE_MAX_PLAYERS] = {};
 private:
  bool pending_ = false;
  uint32_t revision_ = 0;
  uint8_t mask_ = 0, total_ = 0, mode_ = 0, round_ = 0, scope_ = 0;
  TurnHubProtocol::TableDisplayRow staging_[TurnHubProtocol::TABLE_MAX_PLAYERS] = {};
};
}  // namespace TurnHubSigil
