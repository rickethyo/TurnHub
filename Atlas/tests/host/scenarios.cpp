#include <cassert>
#include <iostream>
#include <Arduino.h>
#include "profile_fixture.h"
#include "profile_statistics.h"
#ifdef _MSC_VER
// Keep the production packet's packed layout when compiling with MSVC.
#define __attribute__(...)
#pragma pack(push, 1)
#include "protocol.h"
#pragma pack(pop)
#endif
#include "main_internal_fwd.h"
// Compile the actual application handlers and adapters, not copies of rules.
#include "../../src/main.cpp"

esp_err_t readError = ESP_ERR_NVS_NOT_FOUND;
esp_err_t eraseError = ESP_ERR_NVS_NOT_FOUND;
esp_err_t injectedCommitError = ESP_OK;

// Only hardware/transport/presentation boundaries are replaced.
namespace TurnHub {
static SigilBus *fixtureBus=nullptr;
static SigilRecord fixtureRecords[MAX_PHYSICAL_SIGILS];
static bool fixtureRadio=false;
SigilBus::SigilBus(uint8_t channel) : wifiChannel_(channel) { fixtureBus=this; }
SigilBus *SigilBus::activeInstance() { return fixtureBus; }
bool SigilBus::begin() { return true; }
bool SigilBus::poll(SigilEvent&) { return false; }
uint8_t SigilBus::activeCount(uint32_t) const { return 0; }
bool SigilBus::isOnline(uint8_t id,uint32_t) const { return fixtureRadio && id < MAX_PHYSICAL_SIGILS; }
const SigilRecord *SigilBus::record(uint8_t id) const { return fixtureRadio&&id<MAX_PHYSICAL_SIGILS?&fixtureRecords[id]:nullptr; }
static unsigned fixtureSends=0;
bool SigilBus::send(uint8_t id,TurnHubProtocol::PacketType,int32_t) { assert(id<MAX_PHYSICAL_SIGILS);++fixtureSends;return fixtureRadio; }
bool SigilBus::setBlue(uint8_t id,uint8_t v) { return send(id,PacketType::SetBlue,v); }
bool SigilBus::setRed(uint8_t id,bool v) { return send(id,PacketType::SetRed,v); }
bool SigilBus::setGreen(uint8_t id,bool v) { return send(id,PacketType::SetGreen,v); }
bool SigilBus::buzzer(uint8_t id,int32_t v) { return send(id,PacketType::Buzzer,v); }
OtaManager::OtaManager(WebServer &webServer,AllowedCallback allowed) : server_(webServer),allowedCallback_(allowed) {}
void OtaManager::begin() {}
void OtaManager::update(uint32_t) {}
bool OtaManager::inProgress() const { return false; }
}
static int completedGames=0;
static void completed(const GameEngine &g) {
  ++completedGames;
  TurnHubProfileStats::recordCompletedGame(g,+[](const PlayerSeat &s){return String(s.profileId);});
}
static void freshLobby(int modules=3,bool shared=false) {
  // Fixture reset; all actions under test go through adapters/dispatcher.
  enterEmptyLobby();
  testNow=1000;
  completedGames=0;
  TurnHub::fixtureRadio=true;
  for(uint8_t i=0;i<MAX_PHYSICAL_SIGILS;++i) {
    TurnHub::fixtureRecords[i].id=i; TurnHub::fixtureRecords[i].mac[5]=i;
  }
  for(int i=0;i<modules;++i) handleActionShort(static_cast<uint8_t>(i));
  if(shared) {
    handleActionDown(0); handlePass(0); handleActionUp(0); handleActionShort(0);
  }
  assert(lobby.playerCount()==modules+(shared?1:0));
}
static void startFromHost() {
  handleActionDown(0); handleActionLong(0); handleActionUp(0);
  assert(hubState==HubState::Starting);
  testNow+=2999; updateCountdown(testNow); assert(hubState==HubState::Starting);
  ++testNow; updateCountdown(testNow); assert(hubState==HubState::Running);
}
static bool web(uint8_t module,uint8_t slot,WebControl control) {
  String message;
  return handleWebControl(module,slot,control,message);
}
static PlayerSeat player(uint8_t number) { return *game.playerByNumber(number); }

static void dispatcherContract() {
  IntentDispatcher dispatcher;
  const auto handler=+[](const Intent&,void*) { return IntentResult::accept(); };
  assert(!dispatcher.bind(IntentType::None,handler));
  assert(dispatcher.bind(IntentType::Pass,handler));
  assert(!dispatcher.bind(IntentType::Pass,handler));
  Intent intent; intent.type=IntentType::Pass;
  assert(dispatcher.dispatch(intent).accepted());
  intent.type=IntentType::Count; assert(!dispatcher.dispatch(intent).accepted());
  intent.type=IntentType::ChangeLife; assert(!dispatcher.dispatch(intent).accepted());
}
static void lobbyLifecycle() {
  freshLobby(2,true);
  assert(lobby.hostController()==0);
  assert(!dispatchModuleIntent(IntentType::Join,255).accepted());
  assert(!dispatchModuleIntent(IntentType::ArmStart,1).accepted());
  assert(web(0,2,WebControl::SelectStarter));
  PlayerSeat selected; assert(lobby.selectedStarter(selected)&&selected.slot==2);
  handleActionShort(0); assert(lobby.selectedStarter(selected)&&selected.slot==1);
  handlePass(1); assert(lobby.selectedStarter(selected)&&selected.controllerId==0);
  assert(dispatchModuleIntent(IntentType::Leave,0,2).accepted());
  assert(lobby.playerCount()==2&&!lobby.hasSecondary(0));
  assert(!dispatchModuleIntent(IntentType::Leave,0,2).accepted());
  assert(dispatchModuleIntent(IntentType::Join,0,2).accepted());
  assert(!dispatchModuleIntent(IntentType::Join,0,2).accepted());
  handleActionDown(1);
  assert(!dispatchModuleIntent(IntentType::ArmStart,0).accepted());
  handleActionUp(1);
  handleActionDown(0); handleActionLong(0); handleActionUp(0);
  assert(hubState==HubState::Starting);
  assert(!dispatchModuleIntent(IntentType::Join,2).accepted());
  assert(!dispatchSystemIntent(IntentType::CompleteStart).accepted());
  handleActionDown(1); assert(hubState==HubState::Lobby); handleActionUp(1);
  assert(!dispatchModuleIntent(IntentType::StartGame,0).accepted());
  assert(dispatchModuleIntent(IntentType::Leave,0).accepted());
  assert(lobby.hostController()==1&&lobby.playerCount()==1);
  freshLobby(); startFromHost();
  assert(!dispatchModuleIntent(IntentType::Rematch,0).accepted());
  assert(!dispatchModuleIntent(IntentType::ResetGame,0).accepted());
  freshLobby(2); handleActionDown(0); handleActionLong(0); handleActionWin(0);
  assert(hubState==HubState::Lobby&&lobby.playerCount()==0);
}
static void winDecisions() {
  freshLobby(3,true); startFromHost();
  assert(!web(1,1,WebControl::ClaimWin));
  assert(web(0,1,WebControl::ClaimWin));
  assert(game.nextWinConfirmationPlayerNumber()==3);
  assert(!web(0,2,WebControl::ConfirmWin));
  assert(!web(2,1,WebControl::DenyWin));
  handleActionShort(1); assert(game.nextWinConfirmationPlayerNumber()==4);
  assert(web(2,1,WebControl::ConfirmWin));
  assert(game.nextWinConfirmationPlayerNumber()==2);
  handleActionShort(0);
  assert(game.gameOver()&&game.winnerPlayerNumber()==1&&completedGames==1);
  assert(!web(0,2,WebControl::ConfirmWin)); assert(completedGames==1);
  assert(!dispatchModuleIntent(IntentType::Rematch,1).accepted());
  handleActionShort(0); assert(hubState==HubState::Lobby&&lobby.playerCount()==4&&!game.hasPlayers());
  startFromHost(); assert(web(0,1,WebControl::ClaimWin));
  handlePass(1); assert(hubState==HubState::Running&&!game.hasWinClaim());
  assert(web(1,1,WebControl::PauseResume));
  assert(web(0,1,WebControl::ClaimWin));
  assert(web(1,1,WebControl::DenyWin)); assert(hubState==HubState::Paused);
  assert(web(1,1,WebControl::PauseResume));
  handleActionDown(0); handleActionLong(0); assert(hubState==HubState::Paused);
  handleActionWin(0); assert(game.hasWinClaim());
  assert(web(1,1,WebControl::DenyWin)); assert(hubState==HubState::Running);
  assert(!dispatchSeatIntent(IntentType::ClaimWin,IntentOrigin::Simulator,player(1),
      TurnHub::CLAIM_FROM_ARMED_PAUSE).accepted());
}
static void eliminationAndConcession() {
  freshLobby(3,true); startFromHost();
  assert(web(1,1,WebControl::PauseResume));
  handleActionDown(0); handlePass(0); handleActionUp(0); handleActionShort(0);
  assert(eliminationTargetPlayer==1);
  handleActionShort(0); assert(eliminationTargetPlayer==2);
  handlePass(1); assert(!game.isEliminated(2));
  handlePass(0); assert(game.isEliminated(2)&&hubState==HubState::Paused);
  assert(!web(0,2,WebControl::Concede));
  assert(web(1,1,WebControl::PauseResume));
  assert(web(1,1,WebControl::Concede)); assert(hubState==HubState::Running&&game.isEliminated(3));
  assert(web(0,1,WebControl::Concede)); assert(hubState==HubState::GameOver&&game.winnerPlayerNumber()==4);
  assert(completedGames==1);
  handleActionLong(0); assert(hubState==HubState::Lobby&&lobby.playerCount()==0);
  freshLobby(); startFromHost();
  assert(web(1,1,WebControl::PauseResume));
  assert(dispatchModuleIntent(IntentType::BeginElimination,0).accepted());
  assert(!web(0,1,WebControl::ClaimWin));
  assert(!web(1,1,WebControl::Concede));
  assert(dispatchModuleIntent(IntentType::CancelElimination,1).accepted());
  assert(eliminationTargetPlayer==0);
  assert(web(0,1,WebControl::ClaimWin));
  assert(!dispatchModuleIntent(IntentType::BeginElimination,1).accepted());
}
static void passTimingAndActors() {
  freshLobby(); startFromHost();
  assert(!web(1,1,WebControl::Pass));
  Intent stale; stale.type=IntentType::ClaimWin; stale.actor={IntentOrigin::Browser,1,1,1};
  assert(!intents.dispatch(stale).accepted());
  handlePass(0); assert(pendingPass.active);
  assert(!dispatchModuleIntent(IntentType::CommitPass,0).accepted());
  testNow+=2999; updatePendingPass(testNow); assert(game.activePlayerNumber()==1);
  ++testNow; updatePendingPass(testNow); assert(game.activePlayerNumber()==2&&!pendingPass.active);
  assert(game.statsForPlayer(1)->turnsCompleted==1);
  assert(web(1,1,WebControl::Pass));
  handleActionDown(1); assert(!pendingPass.active);
  handleActionUp(1); handleActionShort(1); assert(hubState==HubState::Running);
  assert(web(1,1,WebControl::Pass)); assert(web(1,1,WebControl::Pass)); assert(!pendingPass.active);
  assert(web(1,1,WebControl::Pass)); assert(web(0,1,WebControl::PauseResume)); assert(!pendingPass.active);
  testNow+=3000; updatePendingPass(testNow); assert(game.activePlayerNumber()==2);
  assert(web(0,1,WebControl::PauseResume));
  testNow=UINT32_MAX-1000;
  assert(web(1,1,WebControl::Pass));
  testNow+=2999; updatePendingPass(testNow); assert(game.activePlayerNumber()==2);
  ++testNow; updatePendingPass(testNow); assert(game.activePlayerNumber()==3);
}
static void optionalStorage() {
  TurnHub::OptionalPreferences prefs; assert(prefs.begin("test"));
  loggedErrors=0; readError=ESP_ERR_NVS_NOT_FOUND;
  for(int i=0;i<100;++i) { assert(prefs.getString("missing","").empty()); assert(prefs.getBytesLength("missing")==0); }
  assert(loggedErrors==0);
  readError=ESP_ERR_NVS_TYPE_MISMATCH;
  prefs.getString("wrong-type",""); prefs.getBytesLength("wrong-type"); assert(loggedErrors==2);
  readError=ESP_ERR_NVS_INVALID_HANDLE; prefs.getString("broken",""); assert(loggedErrors==3);
  readError=ESP_OK; assert(prefs.getString("present","")=="stored"); assert(prefs.getBytesLength("present")==4);
  eraseError=ESP_ERR_NVS_NOT_FOUND; assert(prefs.remove("missing")); assert(loggedErrors==3);
  eraseError=ESP_ERR_NVS_INVALID_HANDLE; assert(!prefs.remove("broken")); assert(loggedErrors==4);
  eraseError=ESP_OK; injectedCommitError=ESP_ERR_NVS_INVALID_HANDLE; assert(!prefs.remove("present")); assert(loggedErrors==5);
  injectedCommitError=ESP_OK; assert(prefs.remove("present"));
}

static String responseField(const char *field) {
  const std::string marker=std::string("\"")+field+"\":\"";
  auto start=server.body.find(marker); assert(start!=std::string::npos);
  start+=marker.size(); return server.body.substr(start,server.body.find('"',start)-start);
}
static int request(const char *path,const String &token=String(),
    std::map<std::string,String> args={},int method=HTTP_POST) {
  server.arguments=args; server.headers.clear(); server.headers["X-TurnHub-Token"]=token;
  server.status=0; server.body.clear();
  auto found=server.routes.find(std::to_string(method)+path); assert(found!=server.routes.end());
  found->second(); return server.status;
}
static String registerPhone(const char *name,String &id) {
  assert(request("/api/profiles/register","",{{"name",name},{"pin","1234"}})==200);
  id=responseField("profileId"); return responseField("token");
}
static String loginPhone(const String &id) {
  assert(request("/api/session/login","",{{"profileId",id},{"pin","1234"}})==200);
  return responseField("token");
}
static void virtualProfileFlow() {
  enterEmptyLobby(); TurnHub::fixtureRadio=false; testNow=1000;
  TurnHubWebApi::configure(resolveWebSeat,handleWebControl,handleProfileControl,resolveProfileParticipant);
  TurnHubWebApi::configureGameControls(readGameSettings,configureGame,changeLife);
  TurnHubWebApi::begin(server);
  String firstId, secondId;
  const String first=registerPhone("Phone One",firstId), second=registerPhone("Phone Two",secondId);
  assert(request("/api/session/me",first,{},HTTP_GET)==200);
  assert(server.body.find("\"participating\":false")!=std::string::npos);
  assert(request("/api/control/pass",first)==409);
  assert(request("/api/session/join","not-a-token")==401);
  assert(request("/api/session/join",first)==200);
  assert(request("/api/session/join",second)==200);
  const String companion=loginPhone(firstId);
  assert(request("/api/session/join",companion)==200 && lobby.playerCount()==2);
  assert(request("/api/control/start",second)==409);
  assert(request("/api/control/start",first)==200 && hubState==HubState::Starting);
  assert(request("/api/session/leave",first)==409);
  testNow+=3000; updateCountdown(testNow); assert(hubState==HubState::Running);
  assert(game.playerCount()==2 && game.playerAt(0)->controllerId>=MAX_PHYSICAL_SIGILS);
  assert(String(game.playerAt(0)->profileId)==firstId);
  assert(request("/api/control/pass",second,{{"module","8"},{"profileId",firstId}})==409);
  assert(request("/api/control/pass",first)==200 && pendingPass.active);
  assert(request("/api/control/pass",companion)==200 && !pendingPass.active);
  assert(request("/api/session/logout",first)==200);
  assert(request("/api/control/pass",first)==401);
  assert(request("/api/session/me",companion,{},HTTP_GET)==200);
  const String returned=loginPhone(firstId);
  assert(request("/api/session/me",returned,{},HTTP_GET)==200);
  assert(server.body.find("\"participating\":true")!=std::string::npos);
  assert(game.playerCount()==2);
  assert(request("/api/control/win",returned)==200);
  assert(request("/api/control/confirm",returned)==409);
  assert(request("/api/control/confirm",second)==200 && hubState==HubState::GameOver);
  assert(ProfileFixture::profiles[firstId].stats.gamesPlayed==1);
  assert(ProfileFixture::profiles[secondId].stats.gamesPlayed==1);
  assert(request("/api/control/confirm",second)==409);
  assert(ProfileFixture::profiles[firstId].stats.gamesPlayed==1);
  assert(request("/api/control/rematch",returned)==200 && lobby.playerCount()==2);
  assert(request("/api/control/reset",second)==409);
  assert(request("/api/control/reset",returned)==200 && lobby.playerCount()==0);
  assert(request("/api/session/me",returned,{},HTTP_GET)==200);
  assert(server.body.find("\"participating\":false")!=std::string::npos);
  assert(request("/api/session/stats",returned,{{"profileId",secondId}},HTTP_GET)==200);
  assert(responseField("profileId")==firstId);

  for(int i=0;i<5;++i) assert(request("/api/session/login","",{{"profileId",firstId},{"pin","0000"}})==401);
  assert(request("/api/session/login","",{{"profileId",firstId},{"pin","1234"}})==429);
  testNow+=30000; const String afterLimit=loginPhone(firstId);
  assert(request("/api/session/profile",afterLimit,{{"pin","5678"}})==200);
  assert(request("/api/session/me",returned,{},HTTP_GET)==401);
  assert(request("/api/session/me",companion,{},HTTP_GET)==401);
  assert(request("/api/session/me",afterLimit,{},HTTP_GET)==200);
}

static void physicalCompanionFlow() {
  enterEmptyLobby(); TurnHub::fixtureRadio=true;
  for(uint8_t i=0;i<MAX_PHYSICAL_SIGILS;++i) {
    TurnHub::fixtureRecords[i].id=i; TurnHub::fixtureRecords[i].mac[5]=i;
  }
  String id, otherId;
  const String phone=registerPhone("Hybrid",id), other=registerPhone("Virtual opponent",otherId);
  assert(request("/api/session/join",phone)==200);
  assert(request("/api/session/join",other)==200);
  PlayerSeat before[MAX_PLAYERS]; lobby.buildPlayers(before,MAX_PLAYERS);
  assert(request("/api/session/request",phone,{{"module","0"},{"slot","1"}})==202);
  const String claim=responseField("requestId");
  TurnHubWebApi::notePhysicalAction(0);
  assert(request("/api/session/poll","",{{"id",claim}},HTTP_GET)==200);
  assert(responseField("token")==phone);
  PlayerSeat after[MAX_PLAYERS]; lobby.buildPlayers(after,MAX_PLAYERS);
  assert(lobby.playerCount()==2 && lobby.hostController()==0);
  assert(after[0].participantId==before[0].participantId && after[0].controllerId==0);
  const String secondPhone=loginPhone(id);
  assert(request("/api/session/join",secondPhone)==200 && lobby.playerCount()==2);
  assert(request("/api/control/start",phone)==200);
  testNow+=3000;updateCountdown(testNow);
  handlePass(0); assert(pendingPass.active);
  assert(request("/api/control/pass",secondPhone)==200 && !pendingPass.active);
  assert(request("/api/control/pass",phone)==200);
  testNow+=3000;updatePendingPass(testNow);
  assert(game.activePlayerNumber()==2);
  assert(request("/api/control/concede",other)==200 && hubState==HubState::GameOver);
  assert(ProfileFixture::profiles[id].stats.gamesPlayed==1);
  assert(ProfileFixture::profiles[id].stats.gamesWon==1);
  assert(request("/api/session/me",secondPhone,{},HTTP_GET)==200);
  assert(request("/api/control/reset",phone)==200);
  assert(request("/api/session/join",phone)==200); // Can play by phone again.
  assert(lobby.playerCount()==1);
  handleActionShort(0); // Same persisted physical profile must not duplicate it.
  assert(lobby.playerCount()==1);
  TurnHub::fixtureRadio=false;
}

static void profilePolicyFlow() {
  enterEmptyLobby(); TurnHub::fixtureRadio=true;
  String id, otherId;
  String owner=registerPhone("Policy owner",id), other=registerPhone("Other owner",otherId);
  assert(TurnHubProfiles::bindSeatToProfile(TurnHub::fixtureRecords[0].mac,1,id));
  assert(request("/api/session/policy","",{{"allowPhysicalWithoutPin","0"},{"hideStatsWithoutAuthentication","1"}})==401);
  assert(request("/api/session/policy",owner,{{"allowPhysicalWithoutPin","false"},{"hideStatsWithoutAuthentication","1"}})==400);
  // Caller cannot change another profile by supplying its ID.
  assert(request("/api/session/policy",other,{{"profileId",id},{"allowPhysicalWithoutPin","0"},{"hideStatsWithoutAuthentication","0"}})==200);
  assert(ProfileFixture::profiles[id].policy.allowPhysicalWithoutPin);
  assert(request("/api/session/policy",owner,{{"allowPhysicalWithoutPin","0"},{"hideStatsWithoutAuthentication","1"}})==200);
  assert(TurnHubWebApi::physicalUseAllowed(id) && TurnHubWebApi::physicalStatsVisible(id));
  assert(request("/api/session/logout",owner)==200);
  assert(!TurnHubWebApi::physicalUseAllowed(id) && !TurnHubWebApi::physicalStatsVisible(id));
  assert(!dispatchModuleIntent(IntentType::Join,0).accepted());
  assert(lobby.playerCount()==0);
  owner=loginPhone(id);
  assert(dispatchModuleIntent(IntentType::Join,0).accepted());
  assert(request("/api/session/request","",{{"module","0"},{"slot","1"}})==403);
  assert(request("/api/session/profile",owner,{{"clearPin","1"}})==409);
  const String companion=loginPhone(id);
  assert(request("/api/session/logout",owner)==200);
  assert(TurnHubWebApi::physicalStatsVisible(id));
  assert(request("/api/session/logout",companion)==200);
  assert(!TurnHubWebApi::physicalStatsVisible(id));
  assert(lobby.playerCount()==1); // Losing browser auth does not evict a player.
  owner=loginPhone(id);
  for (const char *physical : {"0","1"}) for (const char *hidden : {"0","1"}) {
    assert(request("/api/session/policy",owner,{{"allowPhysicalWithoutPin",physical},{"hideStatsWithoutAuthentication",hidden}})==200);
    assert(request("/api/session/logout",owner)==200);
    assert(TurnHubWebApi::physicalUseAllowed(id)==(physical[0]=='1'));
    assert(TurnHubWebApi::physicalStatsVisible(id)==(hidden[0]=='0'));
    owner=loginPhone(id);
  }
  assert(request("/api/session/me",owner,{},HTTP_GET)==200);
  assert(server.body.find("\"policyAvailable\":true")!=std::string::npos);
  assert(request("/api/session/join",other)==200);
  assert(request("/api/control/start",owner)==200);
  testNow+=3000;updateCountdown(testNow);
  assert(request("/api/session/logout",owner)==200);
  assert(!TurnHubWebApi::physicalStatsVisible(id));
  assert(request("/api/control/concede",other)==200);
  assert(ProfileFixture::profiles[id].stats.gamesPlayed==1);
  assert(ProfileFixture::profiles[id].stats.gamesWon==1);
  owner=loginPhone(id);
  assert(request("/api/session/stats",owner,{},HTTP_GET)==200);
  ProfileFixture::profiles[id].policyReadable=false;
  assert(!TurnHubWebApi::physicalUseAllowed(id) && !TurnHubWebApi::physicalStatsVisible(id));
  assert(request("/api/session/policy",owner,{{"allowPhysicalWithoutPin","1"},{"hideStatsWithoutAuthentication","0"}})==503);
  ProfileFixture::profiles[id].policyReadable=true;
  testNow+=8UL*60*60*1000+1;
  assert(!TurnHubWebApi::profileAuthenticated(id));
  assert(!TurnHubWebApi::physicalStatsVisible(id));

  // Legacy PIN-less profiles can bootstrap a browser session, but a PIN added
  // while the physical claim is pending must not be bypassed at confirmation.
  enterEmptyLobby();
  const String legacy=TurnHubProfiles::createProfile();
  assert(TurnHubProfiles::bindSeatToProfile(TurnHub::fixtureRecords[0].mac,1,legacy));
  assert(dispatchModuleIntent(IntentType::Join,0).accepted());
  assert(request("/api/session/request","",{{"module","0"},{"slot","1"}})==202);
  const String claim=responseField("requestId");
  TurnHubProfiles::setPinHashForProfile(legacy,String(std::string(64,'A')));
  TurnHubWebApi::notePhysicalAction(0);
  assert(request("/api/session/poll","",{{"id",claim}},HTTP_GET)==409);
  assert(!TurnHubWebApi::profileAuthenticated(legacy));
  TurnHub::fixtureRadio=false;
}

static void gameProfilesAndLife() {
  using namespace TurnHub;
  enterEmptyLobby(); TurnHub::fixtureRadio=false;
  String firstId, secondId, thirdId;
  const String first=registerPhone("Life host",firstId),second=registerPhone("Life opponent",secondId),
      third=registerPhone("Third player",thirdId);
  assert(request("/api/game/settings","",{},HTTP_GET)==200);
  assert(server.body.find("\"canEdit\":false")!=std::string::npos);
  const std::map<std::string,String> magic={{"gameProfile","mtg"},{"startingLife","20"}};
  assert(request("/api/game/settings","",magic)==401);
  assert(request("/api/game/settings",first,magic)==409); // Not joined yet.
  assert(request("/api/session/join",first)==200);
  assert(request("/api/session/join",second)==200);
  assert(request("/api/session/join",third)==200);
  assert(request("/api/game/settings",first,{},HTTP_GET)==200);
  assert(server.body.find("\"canEdit\":true")!=std::string::npos);
  assert(request("/api/game/settings",second,magic)==409);
  for (const char *profile : {"generic","mtg","mtg_commander","yugioh"}) {
    assert(request("/api/game/settings",first,{{"gameProfile",profile},{"startingLife","27"}})==200);
    assert(request("/api/game/settings",first,{},HTTP_GET)==200);
    assert(responseField("gameProfile")==profile);
    GameSettings saved; assert(loadGameSettings(saved)==TurnHubStorage::Status::Ok);
    assert(String(gameProfileKey(saved.profile))==profile && saved.startingLife==27);
  }
  assert(request("/api/game/settings",first,magic)==200);
  for (const char *invalid : {"", "-1", "1.5", "20abc", "1000001", "999999999999999"})
    assert(request("/api/game/settings",first,{{"gameProfile","mtg"},{"startingLife",invalid}})==400);
  assert(request("/api/game/settings",first,{{"gameProfile","unknown"},{"startingLife","20"}})==400);
  ProfileFixture::gameSettingsWritable=false;
  assert(request("/api/game/settings",first,{{"gameProfile","yugioh"},{"startingLife","8000"}})==409);
  assert(nextGameSettings.profile==GameProfile::Magic && nextGameSettings.startingLife==20);
  ProfileFixture::gameSettingsWritable=true;
  assert(request("/api/control/life",first,{{"delta","-1"}})==409); // Lobby.
  gameSettingsAvailable=false;
  assert(request("/api/control/start",first)==409 && hubState==HubState::Lobby);
  gameSettingsAvailable=true;
  assert(request("/api/control/start",first)==200);
  assert(request("/api/game/settings",first,magic)==409); // Countdown is frozen.
  testNow+=3000;updateCountdown(testNow);
  assert(game.settings().profile==GameProfile::Magic && game.lifeTotal(1)==20 && game.lifeTotal(2)==20);
  assert(request("/api/game/settings",first,magic)==409);
  assert(request("/api/control/life","",{{"delta","-1"}})==401);
  for (const char *invalid : {"", "0", "-", "1.5", "1abc", "1000001", "9999999999999"})
    assert(request("/api/control/life",first,{{"delta",invalid}})==400);
  assert(request("/api/control/life",first,{{"delta","-21"},{"player","2"},{"profileId",secondId}})==200);
  assert(game.lifeTotal(1)==-1 && game.lifeTotal(2)==20 && !game.isEliminated(1));
  const String companion=loginPhone(firstId);
  assert(request("/api/control/life",companion,{{"delta","6"}})==200 && game.lifeTotal(1)==5);
  assert(request("/api/session/me",first,{},HTTP_GET)==200);
  assert(server.body.find("\"life\":5")!=std::string::npos);
  assert(request("/api/seats","",{},HTTP_GET)==200);
  assert(server.body.find("\"life\":5")!=std::string::npos && server.body.find("\"life\":20")!=std::string::npos);
  assert(request("/api/control/life",first,{{"delta","999995"}})==200);
  assert(request("/api/control/life",first,{{"delta","1"}})==409 && game.lifeTotal(1)==1000000);
  assert(!game.changeLife(1,INT32_MAX) && game.lifeTotal(1)==1000000);
  assert(request("/api/control/life",first,{{"delta","-1000000"}})==200);
  assert(request("/api/control/life",first,{{"delta","-1000000"}})==200);
  assert(request("/api/control/life",first,{{"delta","-1"}})==409 && game.lifeTotal(1)==-1000000);
  assert(request("/api/control/pause",first)==200);
  assert(request("/api/control/life",second,{{"delta","-5"}})==200 && game.lifeTotal(2)==15);
  assert(request("/api/control/win",first)==200);
  assert(request("/api/control/life",second,{{"delta","1"}})==409); // Pending win decision.
  assert(request("/api/control/deny",second)==200);
  assert(request("/api/control/concede",second)==200);
  assert(request("/api/control/life",second,{{"delta","1"}})==409); // Eliminated, game continues.
  assert(request("/api/control/concede",third)==200 && hubState==HubState::GameOver);
  assert(request("/api/control/life",first,{{"delta","1"}})==409);
  assert(request("/api/control/rematch",first)==200);
  assert(request("/api/control/start",first)==200);
  testNow+=3000;updateCountdown(testNow);
  assert(game.lifeTotal(1)==20 && game.lifeTotal(2)==20 && !game.isEliminated(2));
  assert(request("/api/control/concede",second)==200);
  assert(request("/api/control/concede",third)==200);
  assert(request("/api/control/reset",first)==200);
  assert(request("/api/session/join",first)==200 && request("/api/session/join",second)==200);
  assert(request("/api/game/settings",first,{{"gameProfile","generic"},{"startingLife","0"}})==200);
  assert(request("/api/control/start",first)==200);
  testNow+=3000;updateCountdown(testNow);
  assert(game.lifeTotal(1)==0 && game.lifeTotal(2)==0 && game.livingPlayerCount()==2);
}


static void accountPermissionsAndModeration(){
  using namespace TurnHubAccounts;
  enterEmptyLobby();TurnHubWebApi::configureModeration(moderateAccount);
  String adminId,gmId,playerId,devId;
  const String admin=registerPhone("Administrator",adminId),gm=registerPhone("Moderator",gmId),player=registerPhone("Participant",playerId),dev=registerPhone("Developer",devId);
  assert(request("/api/accounts/setup","",{},HTTP_GET)==200&&server.body.find("true")!=std::string::npos);
  assert(request("/api/accounts/setup",admin)==403); // Physical confirmation needed.
  testDigitalRead=LOW;assert(request("/api/accounts/setup",admin)==200);testDigitalRead=HIGH;
  assert(request("/api/accounts/setup",gm)==409);
  assert(request("/api/accounts/permissions",player,{{"profileId",playerId},{"permissions","31"}})==403);
  assert(request("/api/accounts/permissions",admin,{{"profileId",adminId},{"permissions","0"}})==409);
  assert(request("/api/accounts/permissions",admin,{{"profileId",gmId},{"permissions","26"}})==200);
  assert(request("/api/accounts/permissions",admin,{{"profileId",devId},{"permissions","4"}})==200);
  assert(request("/api/device/name",player,{{"module","0"},{"name","No"}})==403);
  assert(request("/api/network",gm,{},HTTP_GET)==403);
  assert(request("/api/network",dev,{},HTTP_GET)==403);
  assert(request("/api/network",admin,{},HTTP_GET)==200);
  assert(request("/api/session/profile",admin,{{"clearPin","1"}})==403);
  assert(request("/api/accounts/moderate",admin,{{"profileId",playerId},{"action","reset"}})==403);
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","mute"}})==200);
  Account stored;assert(load(playerId,stored)&&stored.nudgeMuted);
  assert(request("/api/session/join",player)==200);
  assert(request("/api/session/join",gm)==200);
  assert(request("/api/session/join",dev)==200);
  const String companion=loginPhone(playerId);
  assert(request("/api/control/start",player)==200);testNow+=3000;updateCountdown(testNow);
  const auto life=game.lifeTotal(1);
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","reset"}})==200);
  assert(game.lifeTotal(1)==life&&game.livingPlayerCount()==3);
  assert(request("/api/session/me",player,{},HTTP_GET)==401);
  assert(request("/api/session/me",companion,{},HTTP_GET)==401);
  assert(TurnHubWebApi::connectionBlocked(playerId));
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","reset"}})==409);
  assert(load(playerId,stored)&&stored.connectionResets==1);
  assert(request("/api/accounts",admin,{},HTTP_GET)==200);
  // Admin sees only their own private count fields; other accounts do not expose them.
  auto targetAt=server.body.find(std::string("\"profileId\":\"")+playerId.c_str());
  auto targetEnd=server.body.find('}',targetAt);
  assert(server.body.substr(targetAt,targetEnd-targetAt).find("connectionResets")==std::string::npos);
  const String reconnected=loginPhone(playerId);assert(!TurnHubWebApi::connectionBlocked(playerId));
  assert(request("/api/accounts",reconnected,{},HTTP_GET)==200&&server.body.find("\"connectionResets\":1")!=std::string::npos&&server.body.find(gmId)==std::string::npos);
  assert(request("/api/accounts",gm,{},HTTP_GET)==200&&server.body.find("\"connectionResets\":1")!=std::string::npos);
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","pass"}})==200);
  assert(game.activePlayerNumber()==2&&!pendingPass.active);
  assert(request("/api/accounts/permissions",admin,{{"profileId",gmId},{"permissions","2"}})==200);
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","remove"}})==409);
  assert(request("/api/accounts/permissions",admin,{{"profileId",gmId},{"permissions","26"}})==200);
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","remove"}})==200);
  assert(game.isEliminated(1)&&game.livingPlayerCount()==2);
  assert(load(playerId,stored)&&stored.gameRemovals==1&&stored.connectionResets==1);
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","remove"}})==409);
  assert(request("/api/accounts/permissions",admin,{{"profileId",adminId},{"permissions","31"}})==200);
  assert(has(adminId,Admin|GameMaster|Developer));
  // Direct page requests with credentials must enforce the independent permission.
  server.headers["X-TurnHub-Token"]=dev;TurnHubWebApi::serveRestrictedPage(server,"dev-secret",Developer);assert(server.status==200&&server.body=="dev-secret");
  server.headers["X-TurnHub-Token"]=gm;TurnHubWebApi::serveRestrictedPage(server,"dev-secret",Developer);assert(server.status==403);
}
static void virtualCapacity() {
  enterEmptyLobby();
  for(uint8_t i=0;i<MAX_PLAYERS;++i) {
    const String id=TurnHubProfiles::createProfile(); String message;
    assert(handleProfileControl(id,WebControl::Join,INVALID_ID,1,message));
  }
  assert(lobby.playerCount()==MAX_PLAYERS);
  const String extra=TurnHubProfiles::createProfile(); String message;
  assert(!handleProfileControl(extra,WebControl::Join,INVALID_ID,1,message));
  assert(!dispatchModuleIntent(IntentType::Join,MAX_PHYSICAL_SIGILS).accepted());
  const uint8_t host=lobby.hostController();
  assert(web(host,1,WebControl::Start));
  testNow+=3000; updateCountdown(testNow);
  assert(web(host,1,WebControl::ClaimWin));
  for(uint8_t i=1;i<MAX_PLAYERS;++i) assert(web(MAX_PHYSICAL_SIGILS+i,1,WebControl::ConfirmWin));
  assert(hubState==HubState::GameOver);
}
int main() {
  assert(configureIntentHandlers());
  GameEngine::setGameCompletedCallback(completed);
  dispatcherContract(); std::cout<<"PASS dispatcher contract\n";
  lobbyLifecycle(); std::cout<<"PASS lobby and lifecycle\n";
  winDecisions(); std::cout<<"PASS win decisions and shared-seat order\n";
  eliminationAndConcession(); std::cout<<"PASS elimination versus concession\n";
  passTimingAndActors(); std::cout<<"PASS pass timing, cancellation, rollover, actors\n";
  optionalStorage(); std::cout<<"PASS optional storage error policy\n";
  virtualProfileFlow(); std::cout<<"PASS profile registration/login, phone-only game, companion sessions, authorization and throttling\n";
  physicalCompanionFlow(); std::cout<<"PASS mixed table, physical attachment, two phones and one Sigil, statistics once\n";
  profilePolicyFlow(); std::cout<<"PASS profile choices, physical authorization, companion privacy, expiry and claim revalidation\n";
  gameProfilesAndLife(); std::cout<<"PASS game settings, own life, companion state, limits, rematch and authorization\n";
  accountPermissionsAndModeration(); std::cout<<"PASS account setup, independent permissions, moderation, revocation and private counts\n";
  virtualCapacity(); std::cout<<"PASS virtual capacity and 16-player win confirmation\n";
}
