#pragma once
#include <Arduino.h>
#include "storage.h"

namespace TurnHubAccounts {
enum Permission : uint8_t { Admin=1, GameMaster=2, Developer=4, ResetConnections=8, RemovePlayer=16 };
struct Account {
  uint8_t permissions = 0;
  bool nudgeMuted = false;
  bool archived = false;
  bool reconnectRequired = false;
  uint32_t connectionResets = 0;
  uint32_t gameRemovals = 0;
};
inline TurnHubStorage::Status read(TurnHubStorage::BlobStore &store, const char *key, Account &a) {
  using TurnHubStorage::Status;
  uint8_t b[12] = {}; size_t n=0;
  auto s=store.read(key,nullptr,0,n);
  if(s!=Status::Ok)return s;
  if(n!=sizeof(b))return Status::Corrupt;
  s=store.read(key,b,sizeof(b),n); if(s!=Status::Ok)return s;
  if(n!=sizeof(b))return Status::Corrupt;
  if(b[0]!=1&&b[0]!=2)return Status::UnsupportedSchema;
  if(b[1]>31||b[2]>(b[0]==1?1:3)||b[3]>1)return Status::Corrupt;
  a=Account{}; a.permissions=b[1];a.nudgeMuted=b[2]&1;a.archived=b[2]&2;a.reconnectRequired=b[3];
  for(unsigned i=0;i<4;++i){a.connectionResets|=uint32_t(b[4+i])<<(i*8);a.gameRemovals|=uint32_t(b[8+i])<<(i*8);}
  return Status::Ok;
}
inline TurnHubStorage::Status write(TurnHubStorage::BlobStore &store,const char *key,const Account &a){
  using TurnHubStorage::Status;
  Account old;auto s=read(store,key,old);if(s!=Status::Ok&&s!=Status::NotFound)return s;
  if(a.permissions>31)return Status::Corrupt;
  uint8_t b[12]={2,a.permissions,uint8_t((a.nudgeMuted?1:0)|(a.archived?2:0)),uint8_t(a.reconnectRequired)};
  for(unsigned i=0;i<4;++i){b[4+i]=uint8_t(a.connectionResets>>(i*8));b[8+i]=uint8_t(a.gameRemovals>>(i*8));}
  return store.write(key,b,sizeof(b));
}
bool load(const String &id,Account &account);
bool save(const String &id,const Account &account);
// Missing bootstrap record is distinct from unreadable storage.
bool primaryAdmin(String &id);
bool establishAdmin(const String &id);
inline bool has(const String &id,uint8_t permission){Account a;return load(id,a)&&!a.archived&&(a.permissions&permission)==permission;}
}
