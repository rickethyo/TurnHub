#include "game_recovery.h"
#include "game_engine.h"
#include "lobby.h"
#include <cstring>

namespace TurnHub {
namespace {
constexpr uint32_t MAGIC = 0x50434854; // THCP, explicit little-endian wire bytes
constexpr uint32_t SCHEMA = 1;
constexpr uint32_t CLOCK_CHECKPOINT_MS = 60000;
struct Writer {
  Writer(uint8_t *dataIn, size_t capacityIn) : data(dataIn), capacity(capacityIn) {}
  uint8_t *data; size_t capacity, at = 0; bool ok = true;
  void byte(uint8_t v) { if (at < capacity) data[at++] = v; else ok = false; }
  void word(uint32_t v) { for (uint8_t i=0;i<4;++i) byte(static_cast<uint8_t>(v >> (8*i))); }
};
struct Reader {
  Reader(const uint8_t *dataIn, size_t sizeIn) : data(dataIn), size(sizeIn) {}
  const uint8_t *data; size_t size, at = 0; bool ok = true;
  uint8_t byte() { if (at < size) return data[at++]; ok = false; return 0; }
  uint32_t word() { uint32_t v=0; for (uint8_t i=0;i<4;++i) v |= uint32_t(byte()) << (8*i); return v; }
  bool flag() { const uint8_t v=byte(); if (v>1) ok=false; return v!=0; }
};
uint32_t crc32(const uint8_t *bytes, size_t size) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i=0;i<size;++i) {
    crc ^= bytes[i];
    for (uint8_t bit=0;bit<8;++bit) crc = (crc>>1) ^ (0xEDB88320u & (0u-(crc&1u)));
  }
  return ~crc;
}
}
size_t encodeCheckpoint(const GameCheckpoint &s, uint8_t *bytes, size_t capacity) {
  if (!bytes || !validCheckpoint(s)) return 0;
  Writer w{bytes,capacity};
  w.word(MAGIC); w.word(SCHEMA);
  w.byte(s.count); w.byte(s.active); w.byte(s.starter); w.byte(s.winner);
  w.byte(s.paused); w.byte(s.over); w.byte(static_cast<uint8_t>(s.settings.profile));
  w.word(static_cast<uint32_t>(s.settings.startingLife));
  w.word(s.gameElapsed); w.word(s.turnElapsed); w.word(s.warningMs); w.word(s.nextRequestId);
  for (uint8_t i=0;i<s.count;++i) {
    const auto &p=s.players[i]; const auto &stats=s.stats[i];
    w.byte(p.playerNumber); w.byte(p.controllerId); w.byte(p.slot); w.word(p.participantId);
    for (char c : p.profileId) w.byte(static_cast<uint8_t>(c));
    w.word(stats.turnsCompleted); w.word(stats.totalTurnMs);
    w.word(stats.fastestTurnMs); w.word(stats.longestTurnMs);
    w.byte(s.eliminated[i]); w.word(static_cast<uint32_t>(s.life[i]));
    for (uint8_t j=0;j<s.count;++j) for (uint8_t c=0;c<2;++c)
      w.word(static_cast<uint32_t>(s.damage[i][j][c]));
  }
  if (!w.ok) return 0;
  w.word(crc32(bytes,w.at));
  return w.ok ? w.at : 0;
}
TurnHubStorage::Status decodeCheckpoint(const uint8_t *bytes, size_t size, GameCheckpoint &s) {
  using TurnHubStorage::Status;
  if (!bytes || size < 39 || size > GAME_CHECKPOINT_CAPACITY) return Status::Corrupt;
  Reader header{bytes,size};
  if (header.word()!=MAGIC) return Status::Corrupt;
  if (header.word()!=SCHEMA) return Status::UnsupportedSchema;
  Reader checksum{bytes+size-4,4};
  if (checksum.word()!=crc32(bytes,size-4)) return Status::Corrupt;
  Reader r{bytes,size-4}; r.word(); r.word();
  s = GameCheckpoint{};
  s.count=r.byte(); s.active=r.byte(); s.starter=r.byte(); s.winner=r.byte();
  s.paused=r.flag(); s.over=r.flag(); s.settings.profile=static_cast<GameProfile>(r.byte());
  s.settings.startingLife=static_cast<int32_t>(r.word());
  s.gameElapsed=r.word(); s.turnElapsed=r.word(); s.warningMs=r.word(); s.nextRequestId=r.word();
  if (s.count>MAX_PLAYERS) return Status::Corrupt;
  for (uint8_t i=0;i<s.count;++i) {
    auto &p=s.players[i]; auto &stats=s.stats[i];
    p.playerNumber=r.byte(); p.controllerId=r.byte(); p.slot=r.byte(); p.participantId=r.word();
    for (char &c : p.profileId) c=static_cast<char>(r.byte());
    stats.turnsCompleted=r.word(); stats.totalTurnMs=r.word();
    stats.fastestTurnMs=r.word(); stats.longestTurnMs=r.word();
    s.eliminated[i]=r.flag(); s.life[i]=static_cast<int32_t>(r.word());
    for (uint8_t j=0;j<s.count;++j) for (uint8_t c=0;c<2;++c)
      s.damage[i][j][c]=static_cast<int32_t>(r.word());
  }
  return r.ok && r.at==size-4 && validCheckpoint(s) ? Status::Ok : Status::Corrupt;
}

TurnHubStorage::Status GameRecovery::load(GameEngine &game, Lobby &lobby, uint32_t nowMs) {
  using TurnHubStorage::Status;
  writable_=false; previousSize_=0;
  size_t size=0;
  status_=store_.read("checkpoint",bytes_,sizeof(bytes_),size);
  if (status_==Status::NotFound) { writable_=true; return status_; }
  if (status_!=Status::Ok) return status_;
  status_=decodeCheckpoint(bytes_,size,scratch_);
  if (status_!=Status::Ok) return status_; // Preserve unreadable/future records.
  if (!game.restoreCheckpoint(scratch_,nowMs)) return status_=Status::Corrupt;
  if (scratch_.count) lobby.restorePlayers(scratch_.players,scratch_.count,scratch_.starter);
  else lobby.resetEmpty();
  scratch_.gameElapsed=0; scratch_.turnElapsed=0;
  previousSize_=encodeCheckpoint(scratch_,previous_,sizeof(previous_));
  lastSavedMs_=nowMs; writable_=true;
  return status_;
}
TurnHubStorage::Status GameRecovery::save(const GameEngine &game, uint32_t nowMs) {
  using TurnHubStorage::Status;
  if (!writable_) return status_;
  game.checkpoint(scratch_,nowMs);
  const uint32_t gameTime=scratch_.gameElapsed, turnTime=scratch_.turnElapsed;
  scratch_.gameElapsed=0; scratch_.turnElapsed=0;
  const size_t normalizedSize=encodeCheckpoint(scratch_,bytes_,sizeof(bytes_));
  if (!normalizedSize) return status_=Status::InvalidArgument;
  const bool changed=normalizedSize!=previousSize_ || memcmp(bytes_,previous_,normalizedSize);
  const bool clockDue=game.hasPlayers() && !game.paused() && !game.gameOver() &&
      nowMs-lastSavedMs_>=CLOCK_CHECKPOINT_MS;
  if (!changed && !clockDue) return status_;
  scratch_.gameElapsed=gameTime; scratch_.turnElapsed=turnTime;
  const size_t size=encodeCheckpoint(scratch_,bytes_,sizeof(bytes_));
  status_=store_.write("checkpoint",bytes_,size);
  if (status_==Status::Ok) {
    scratch_.gameElapsed=0; scratch_.turnElapsed=0;
    previousSize_=encodeCheckpoint(scratch_,previous_,sizeof(previous_));
    lastSavedMs_=nowMs;
  }
  return status_;
}
} // namespace TurnHub
