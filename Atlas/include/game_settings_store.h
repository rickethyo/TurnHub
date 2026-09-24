#pragma once
#include "game_profile.h"
#include "storage.h"
namespace TurnHub {
TurnHubStorage::Status loadGameSettings(GameSettings &settings);
TurnHubStorage::Status saveGameSettings(const GameSettings &settings);

// "gamecfg" little-endian image. Schema 1: {1, profile, life[4]} (timer OFF).
// Schema 2 appends turnTimerMs[4]. Writes always use schema 2.
inline uint32_t readLe32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0])|(static_cast<uint32_t>(data[1])<<8)|
      (static_cast<uint32_t>(data[2])<<16)|(static_cast<uint32_t>(data[3])<<24);
}
inline TurnHubStorage::Status readGameSettings(TurnHubStorage::BlobStore &store, GameSettings &settings) {
  using TurnHubStorage::Status;
  size_t size=0;
  auto status=store.read("gamecfg",nullptr,0,size);
  if(status!=Status::Ok)return status;
  if(size!=6&&size!=10)return Status::Corrupt;
  uint8_t data[10]={};status=store.read("gamecfg",data,sizeof(data),size);
  if(status!=Status::Ok)return status;
  if(size!=6&&size!=10)return Status::Corrupt;
  if(data[0]!=1&&data[0]!=2)return Status::UnsupportedSchema;
  if(size!=(data[0]==1?6u:10u))return Status::Corrupt;
  GameSettings value;
  value.profile=static_cast<GameProfile>(data[1]);
  const uint32_t life=readLe32(data+2);
  if(life>1000000)return Status::Corrupt;
  value.startingLife=static_cast<int32_t>(life);
  value.turnTimerMs=data[0]==2?readLe32(data+6):TURN_TIMER_OFF;
  if(!validGameSettings(value))return Status::Corrupt;
  settings=value;return Status::Ok;
}
inline TurnHubStorage::Status writeGameSettings(TurnHubStorage::BlobStore &store,const GameSettings &settings) {
  using TurnHubStorage::Status;
  if(!validGameSettings(settings))return Status::InvalidArgument;
  GameSettings old;const auto status=readGameSettings(store,old);
  if(status!=Status::Ok&&status!=Status::NotFound)return status;
  if(status==Status::Ok&&sameGameSettings(old,settings))return Status::Ok;
  const uint32_t life=static_cast<uint32_t>(settings.startingLife),timer=settings.turnTimerMs;
  const uint8_t data[]={2,static_cast<uint8_t>(settings.profile),static_cast<uint8_t>(life),
      static_cast<uint8_t>(life>>8),static_cast<uint8_t>(life>>16),static_cast<uint8_t>(life>>24),
      static_cast<uint8_t>(timer),static_cast<uint8_t>(timer>>8),static_cast<uint8_t>(timer>>16),
      static_cast<uint8_t>(timer>>24)};
  return store.write("gamecfg",data,sizeof(data));
}
} // namespace TurnHub
