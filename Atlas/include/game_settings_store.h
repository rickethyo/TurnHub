#pragma once
#include "game_profile.h"
#include "storage.h"
namespace TurnHub {
TurnHubStorage::Status loadGameSettings(GameSettings &settings);
TurnHubStorage::Status saveGameSettings(const GameSettings &settings);

inline TurnHubStorage::Status readGameSettings(TurnHubStorage::BlobStore &store, GameSettings &settings) {
  using TurnHubStorage::Status;
  size_t size=0;
  auto status=store.read("gamecfg",nullptr,0,size);
  if(status!=Status::Ok)return status;
  if(size!=6)return Status::Corrupt;
  uint8_t data[6]={};status=store.read("gamecfg",data,sizeof(data),size);
  if(status!=Status::Ok)return status;
  if(size!=6)return Status::Corrupt;
  if(data[0]!=1)return Status::UnsupportedSchema;
  GameSettings value;
  value.profile=static_cast<GameProfile>(data[1]);
  const uint32_t life=static_cast<uint32_t>(data[2])|(static_cast<uint32_t>(data[3])<<8)|
      (static_cast<uint32_t>(data[4])<<16)|(static_cast<uint32_t>(data[5])<<24);
  if(life>1000000)return Status::Corrupt;
  value.startingLife=static_cast<int32_t>(life);
  if(!validGameSettings(value))return Status::Corrupt;
  settings=value;return Status::Ok;
}
inline TurnHubStorage::Status writeGameSettings(TurnHubStorage::BlobStore &store,const GameSettings &settings) {
  using TurnHubStorage::Status;
  if(!validGameSettings(settings))return Status::InvalidArgument;
  GameSettings old;const auto status=readGameSettings(store,old);
  if(status!=Status::Ok&&status!=Status::NotFound)return status;
  if(status==Status::Ok&&old.profile==settings.profile&&old.startingLife==settings.startingLife)return Status::Ok;
  const uint32_t life=static_cast<uint32_t>(settings.startingLife);
  const uint8_t data[]={1,static_cast<uint8_t>(settings.profile),static_cast<uint8_t>(life),
      static_cast<uint8_t>(life>>8),static_cast<uint8_t>(life>>16),static_cast<uint8_t>(life>>24)};
  return store.write("gamecfg",data,sizeof(data));
}
} // namespace TurnHub
