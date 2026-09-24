#include <cassert>
#include <iostream>
#include <fstream>
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
#include "../../../Sigil/include/received_packet.h"
#include "../../../Sigil/include/display_name.h"

esp_err_t readError = ESP_ERR_NVS_NOT_FOUND;
esp_err_t eraseError = ESP_ERR_NVS_NOT_FOUND;
esp_err_t injectedCommitError = ESP_OK;
esp_err_t openError = ESP_OK;
esp_err_t setError = ESP_OK;
std::map<std::string, std::vector<uint8_t>> testBlobs;
bool useRealNvsBlobs = false;

// Only hardware/transport/presentation boundaries are replaced.
namespace TurnHub {
static SigilBus *fixtureBus=nullptr;
static SigilRecord fixtureRecords[MAX_PHYSICAL_SIGILS];
static bool fixtureRadio=false;
SigilBus::SigilBus(uint8_t channel) : wifiChannel_(channel) { fixtureBus=this; }
SigilBus *SigilBus::activeInstance() { return fixtureBus; }
bool SigilBus::begin() { return true; }
bool SigilBus::openPairing() { return fixtureRadio; }
bool SigilBus::poll(SigilEvent&) { return false; }
uint8_t SigilBus::activeCount(uint32_t) const { return 0; }
bool SigilBus::isOnline(uint8_t id,uint32_t) const { return fixtureRadio && id < MAX_PHYSICAL_SIGILS; }
const SigilRecord *SigilBus::record(uint8_t id) const { return fixtureRadio&&id<MAX_PHYSICAL_SIGILS?&fixtureRecords[id]:nullptr; }
static unsigned fixtureSends=0;
static unsigned fixtureProfileSyncs=0;
void SigilBus::syncDisplayProfile(uint8_t id) { assert(id<MAX_PHYSICAL_SIGILS); ++fixtureProfileSyncs; }
bool SigilBus::send(uint8_t id,TurnHubProtocol::PacketType,int32_t) { assert(id<MAX_PHYSICAL_SIGILS);++fixtureSends;return fixtureRadio; }
static TurnHubProtocol::GameDisplayPacket sentGameDisplays[MAX_PHYSICAL_SIGILS]{};
static unsigned gameDisplaySends = 0;
bool SigilBus::sendGameDisplay(const TurnHubProtocol::GameDisplayPacket &p) {
  assert(TurnHubProtocol::validGameDisplay(p));
  sentGameDisplays[p.sigilId] = p;
  ++gameDisplaySends;
  return fixtureRadio;
}
bool SigilBus::setBlue(uint8_t id,uint8_t v) { return send(id,PacketType::SetBlue,v); }
bool SigilBus::setRed(uint8_t id,bool v) { return send(id,PacketType::SetRed,v); }
bool SigilBus::setGreen(uint8_t id,bool v) { return send(id,PacketType::SetGreen,v); }
static unsigned fixtureBuzzes[MAX_PHYSICAL_SIGILS]{};
bool SigilBus::buzzer(uint8_t id,int32_t v) { ++fixtureBuzzes[id]; return send(id,PacketType::Buzzer,v); }
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
static void deliberatePairing() {
  freshLobby(2);
  pairingActive = false;
  Intent intent; intent.type = IntentType::PairRequest;
  for (auto origin : {IntentOrigin::Browser, IntentOrigin::PhysicalSigil, IntentOrigin::System}) {
    intent.actor.origin = origin;
    assert(!intents.dispatch(intent).accepted());
    assert(!pairingActive);
  }
  intent.actor.origin = IntentOrigin::AtlasHardware;
  TurnHub::fixtureRadio = false;
  assert(!intents.dispatch(intent).accepted());
  TurnHub::fixtureRadio = true;
  testNow = UINT32_MAX - 10000;
  assert(intents.dispatch(intent).accepted() && pairingActive);
  testNow += TurnHubProtocol::PAIRING_WINDOW_MS - 1; updateFrontPanelLeds(testNow); assert(pairingActive);
  ++testNow; updateFrontPanelLeds(testNow); assert(!pairingActive);
  assert(intents.dispatch(intent).accepted());
  startFromHost(); updateFrontPanelLeds(testNow); assert(!pairingActive);
  assert(!intents.dispatch(intent).accepted());
  enterEmptyLobby();
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
  Intent request;
  request.type=IntentType::RequestLifeChange;request.actor.origin=IntentOrigin::Simulator;
  request.actor.controllerId=0;request.actor.slot=1;request.actor.playerNumber=1;
  request.payload.targetPlayer=3;request.payload.value=-1;
  assert(intents.dispatch(request).accepted());
  handleActionDown(0); handlePass(0); handleActionUp(0); handleActionShort(0);
  assert(eliminationTargetPlayer==1);
  assert(game.lifeChangeFor(3)->state==TurnHub::LifeChangeState::Cancelled);
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
  prefs.end();
  // A never-written namespace polled read-only (GET /api/network) is quiet.
  loggedErrors=0; openError=ESP_ERR_NVS_NOT_FOUND;
  for(int i=0;i<100;++i) { TurnHub::OptionalPreferences absent; assert(!absent.begin("absent",true)); }
  assert(loggedErrors==0);
  { TurnHub::OptionalPreferences absent; assert(!absent.begin("absent",false)); } assert(loggedErrors==1);
  openError=ESP_ERR_NVS_INVALID_HANDLE;
  { TurnHub::OptionalPreferences broken; assert(!broken.begin("broken",true)); } assert(loggedErrors==2);
  openError=ESP_OK;
  { TurnHub::OptionalPreferences present; assert(present.begin("present",true)); assert(!present.begin("present",true)); present.end(); }
  assert(loggedErrors==2);
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
  TurnHubWebApi::configureCounterControls(readCounters,changeCounter);
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

static void guestSigilsDoNotCreateAccounts() {
  ProfileFixture::bindings.clear();
  const size_t saved = ProfileFixture::profiles.size();
  freshLobby(2, true); // Primary and shared secondary guests can still play.
  assert(lobby.playerCount() == 3);
  for (int poll = 0; poll < 10; ++poll) {
    assert(request("/api/seats", "", {}, HTTP_GET) == 200);
    assert(server.body.find("\"profileId\":\"\"") != std::string::npos);
    assert(request("/api/devices", "", {}, HTTP_GET) == 200);
    assert(request("/api/profiles", "", {}, HTTP_GET) == 200);
  }
  assert(request("/api/session/request", "", {{"module", "0"}, {"slot", "1"}}) == 409);
  assert(server.body.find("guest") != std::string::npos);
  TurnHubWebApi::notePhysicalAction(0);
  assert(ProfileFixture::profiles.size() == saved && ProfileFixture::bindings.empty());
  startFromHost();
  for (uint8_t i = 0; i < game.playerCount(); ++i) assert(game.playerAt(i)->profileId[0] == '\0');
  assert(web(0, 2, WebControl::Concede));
  assert(web(1, 1, WebControl::Concede));
  assert(hubState == HubState::GameOver);
  assert(ProfileFixture::profiles.size() == saved && ProfileFixture::bindings.empty());
  enterEmptyLobby();
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
  const size_t saved = ProfileFixture::profiles.size();
  assert(request("/api/session/request",phone,{{"module","0"},{"slot","1"}})==202);
  assert(ProfileFixture::profiles.size() == saved);
  const String claim=responseField("requestId");
  TurnHubWebApi::notePhysicalAction(0);
  assert(request("/api/session/poll","",{{"id",claim}},HTTP_GET)==200);
  assert(responseField("token")==phone);
  assert(ProfileFixture::profiles.size() == saved);
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
  assert(TurnHubControllers::profileForSeat(0,1).length()==0);
  assert(request("/api/control/rematch",phone)==200);
  assert(TurnHubControllers::profileForSeat(0,1)==id);
  assert(ProfileFixture::profiles[id].stats.gamesPlayed==1);
  assert(request("/api/control/reset",phone)==200);
  assert(request("/api/session/join",phone)==200); // Can play by phone again.
  assert(lobby.playerCount()==1);
  handleActionShort(0); // Finished-game binding cleared: this is now a guest.
  assert(lobby.playerCount()==2);
  assert(TurnHubControllers::profileForSeat(0,1).length()==0);
  TurnHub::fixtureRadio=false;
}

static void attachNamedProfileToGuest() {
  ProfileFixture::bindings.clear();
  freshLobby(2, true);
  String id;
  const String phone = registerPhone("Named guest", id);
  PlayerSeat before[MAX_PLAYERS]; lobby.buildPlayers(before, MAX_PLAYERS);
  const unsigned syncs = TurnHub::fixtureProfileSyncs;
  assert(request("/api/session/request",phone,{{"module","0"},{"slot","1"}})==202);
  const String claim=responseField("requestId");
  TurnHubWebApi::notePhysicalAction(0);
  assert(request("/api/session/poll","",{{"id",claim}},HTTP_GET)==200);
  assert(lobby.playerCount()==3 && lobby.hostController()==0 && lobby.hasSecondary(0));
  assert(TurnHubControllers::profileForSeat(0,1)==id);
  assert(TurnHub::fixtureProfileSyncs == syncs+1);
  PlayerSeat after[MAX_PLAYERS]; lobby.buildPlayers(after,MAX_PLAYERS);
  assert(after[0].participantId==before[0].participantId);
  assert(request("/api/seats","",{},HTTP_GET)==200);
  assert(server.body.find("Named guest")!=std::string::npos);
  const String companion=loginPhone(id);
  assert(request("/api/session/join",companion)==200 && lobby.playerCount()==3);

  // Merge an already joined phone with the guest Sigil instead of duplicating it.
  ProfileFixture::bindings.clear();
  freshLobby(1);
  assert(request("/api/session/join",phone)==200 && lobby.playerCount()==2);
  assert(request("/api/session/request",phone,{{"module","0"},{"slot","1"}})==202);
  const String merge=responseField("requestId");
  TurnHubWebApi::notePhysicalAction(0);
  assert(request("/api/session/poll","",{{"id",merge}},HTTP_GET)==200);
  assert(lobby.playerCount()==1 && lobby.hostController()==0);
  assert(request("/api/session/join",companion)==200 && lobby.playerCount()==1);
  String otherId; const String other=registerPhone("Different owner",otherId);
  assert(request("/api/session/request",other,{{"module","0"},{"slot","1"}})==202);
  const String conflict=responseField("requestId");
  TurnHubWebApi::notePhysicalAction(0);
  assert(request("/api/session/poll","",{{"id",conflict}},HTTP_GET)==409);
  assert(TurnHubControllers::profileForSeat(0,1)==id && lobby.playerCount()==1);
  enterEmptyLobby();
  ProfileFixture::bindings.clear();
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
static void lifeApprovalsAndCommander() {
  using namespace TurnHub;
  enterEmptyLobby(); fixtureRadio=false;
  String firstId, secondId, thirdId;
  const String first=registerPhone("Counter host",firstId),second=registerPhone("Counter recipient",secondId),
      third=registerPhone("Counter observer",thirdId),companion=loginPhone(secondId);
  assert(request("/api/session/join",first)==200);
  assert(request("/api/session/join",second)==200);
  assert(request("/api/session/join",third)==200);
  assert(request("/api/game/settings",first,{{"gameProfile","mtg_commander"},{"startingLife","40"}})==200);
  assert(request("/api/control/life/request",first,{{"target","2"},{"delta","-7"}})==409);
  assert(request("/api/control/start",first)==200);
  testNow+=3000;updateCountdown(testNow);
  const auto propose=[&](int delta) {return request("/api/control/life/request",first,{{"target","2"},{"delta",String(delta)}});};
  const auto respond=[&](const String &token,uint32_t id,bool accept) {return request("/api/control/life/respond",token,{{"requestId",String(id)},{"accept",accept?"1":"0"}});};
  assert(request("/api/game/counters","",{},HTTP_GET)==401);
  assert(request("/api/control/life/request","",{{"target","2"},{"delta","-7"}})==401);
  for(const char *bad:{"0","17","-1","2x","256"})
    assert(request("/api/control/life/request",first,{{"target",bad},{"delta","-7"}})==400);
  assert(request("/api/control/life/request",first,{{"target","1"},{"delta","-7"}})==409);
  assert(propose(-7)==200&&game.lifeTotal(2)==40);
  auto id=game.lifeChangeFor(2)->id;
  assert(propose(-7)==409); // One pending request per target.
  assert(respond(third,id,true)==409&&respond(first,id,true)==409);
  assert(request("/api/control/life",second,{{"delta","2"}})==200);
  assert(respond(companion,id,true)==200&&game.lifeTotal(2)==35); // Delta, not stale total.
  assert(respond(second,id,true)==409&&game.lifeTotal(2)==35);
  assert(request("/api/game/counters",third,{},HTTP_GET)==200);
  assert(server.body.find("\"requests\":[]")!=std::string::npos); // No unrelated requests.
  assert(propose(-5)==200);
  const auto replacement=game.lifeChangeFor(2)->id;
  assert(replacement!=id&&respond(second,id,true)==409);
  assert(respond(second,replacement,false)==200&&game.lifeTotal(2)==35);
  assert(game.lifeChangeFor(2)->state==LifeChangeState::Rejected);
  assert(propose(-3)==200);
  testNow+=14999;dispatchSystemIntent(IntentType::ExpireLifeChanges);assert(game.lifeTotal(2)==35);
  ++testNow;dispatchSystemIntent(IntentType::ExpireLifeChanges);assert(game.lifeTotal(2)==32);
  dispatchSystemIntent(IntentType::ExpireLifeChanges);assert(game.lifeTotal(2)==32);
  assert(game.lifeChangeFor(2)->state==LifeChangeState::Automatic);
  assert(propose(1)==200);id=game.lifeChangeFor(2)->id;
  testNow+=15000;assert(respond(second,id,false)==409&&game.lifeTotal(2)==33); // Deadline race.
  for(const char *bad:{"0","-1","1x","4294967296","99999999999999"})
    assert(request("/api/control/life/respond",second,{{"requestId",bad},{"accept","1"}})==400);
  Intent forged;forged.type=IntentType::ExpireLifeChanges;forged.actor.origin=IntentOrigin::Browser;
  assert(!intents.dispatch(forged).accepted());
  assert(request("/api/control/pause",first)==200);
  assert(propose(2)==200);testNow+=15000;dispatchSystemIntent(IntentType::ExpireLifeChanges);
  assert(game.lifeTotal(2)==35&&hubState==HubState::Paused);
  // Automatic application must revalidate after intervening changes.
  assert(propose(1)==200);
  assert(request("/api/control/life",second,{{"delta","999965"}})==200);
  testNow+=15000;dispatchSystemIntent(IntentType::ExpireLifeChanges);
  assert(game.lifeTotal(2)==1000000&&game.lifeChangeFor(2)->state==LifeChangeState::Failed);
  assert(request("/api/control/life",second,{{"delta","-999960"}})==200);
  assert(propose(-1)==200);
  const uint32_t beforeRollover=testNow;
  game.cancelLifeChanges();
  testNow=UINT32_MAX-10000;
  IntentPayload payload;payload.targetPlayer=2;payload.value=-1;String message;
  assert(changeCounter(game.playerByNumber(1)->controllerId,1,IntentType::RequestLifeChange,payload,message));
  testNow+=14999;dispatchSystemIntent(IntentType::ExpireLifeChanges);assert(game.lifeTotal(2)==40);
  ++testNow;dispatchSystemIntent(IntentType::ExpireLifeChanges);assert(game.lifeTotal(2)==39);
  testNow=beforeRollover;
  const auto damage=[&](const String &token,int source,int commander,int delta){return request("/api/control/commander",token,{{"source",String(source)},{"commander",String(commander)},{"delta",String(delta)},{"target","1"}});};
  assert(damage("",1,1,2)==401);
  assert(damage(second,1,1,-2)==409);
  assert(server.body.find("Cannot remove more Commander damage") != std::string::npos);
  assert(game.commanderDamage(2,1,1)==0&&game.lifeTotal(2)==39);
  assert(damage(second,1,1,21)==200&&game.commanderDamage(2,1,1)==21&&game.lifeTotal(2)==18);
  assert(!game.isEliminated(2)); // No automatic rules adjudication.
  assert(damage(companion,1,2,3)==200&&game.commanderDamage(2,1,2)==3&&game.lifeTotal(2)==15);
  assert(damage(second,3,1,5)==200&&game.commanderDamage(2,3,1)==5&&game.lifeTotal(2)==10);
  assert(game.lifeTotal(1)==40); // Forged target ignored: received damage belongs to caller.
  assert(damage(second,1,1,-2)==200&&game.commanderDamage(2,1,1)==19&&game.lifeTotal(2)==12);
  assert(damage(second,1,1,-20)==409&&game.commanderDamage(2,1,1)==19&&game.lifeTotal(2)==12);
  assert(request("/api/control/life",second,{{"delta","999988"}})==200);
  assert(damage(second,1,1,-1)==409&&game.lifeTotal(2)==1000000&&game.commanderDamage(2,1,1)==19);
  assert(request("/api/control/life",second,{{"delta","-999988"}})==200);
  assert(damage(second,16,1,2)==409);
  assert(damage(second,1,3,2)==400);
  assert(damage(second,1,1,0)==400);
  assert(!game.changeCommanderDamage(2,1,1,INT32_MIN));
  assert(request("/api/control/life",second,{{"delta","-1000000"}})==200);
  assert(damage(second,1,1,13)==409&&game.lifeTotal(2)==-999988&&game.commanderDamage(2,1,1)==19);
  assert(request("/api/control/life",second,{{"delta","1000000"}})==200);
  assert(request("/api/game/counters",second,{},HTTP_GET)==200);
  assert(server.body.find("\"commanders\":[19,3]")!=std::string::npos);
  assert(propose(-1)==200);
  assert(request("/api/control/win",first)==200);
  assert(game.lifeChangeFor(2)->state==LifeChangeState::Cancelled);
  assert(damage(second,1,1,1)==409&&propose(-1)==409);
  assert(request("/api/control/deny",second)==200);
  assert(propose(-1)==200);id=game.lifeChangeFor(2)->id;
  assert(request("/api/control/concede",second)==200);
  assert(game.lifeChangeFor(2)->state==LifeChangeState::Cancelled&&damage(second,1,1,1)==409);
  assert(propose(-1)==409);
  assert(request("/api/control/life/request",third,{{"target","1"},{"delta","-1"}})==200);
  assert(request("/api/control/concede",third)==200&&hubState==HubState::GameOver);
  assert(game.lifeChangeFor(1)->state==LifeChangeState::Cancelled);
  assert(request("/api/control/rematch",first)==200);
  assert(request("/api/game/settings",first,{{"gameProfile","mtg"},{"startingLife","20"}})==200);
  assert(request("/api/control/start",first)==200);testNow+=3000;updateCountdown(testNow);
  assert(game.lifeTotal(2)==20&&game.commanderDamage(2,1,1)==0&&!game.lifeChangeFor(2)->id);
  assert(damage(second,1,1,1)==409); // Only Commander supports these counters.
  assert(propose(-1)==200&&game.lifeChangeFor(2)->id>id);
  assert(respond(second,id,true)==409&&game.lifeTotal(2)==20);
  enterEmptyLobby();
}

static void physicalGameDisplay() {
  using namespace TurnHub;
  using namespace TurnHubProtocol;
  fixtureRadio = true;
  fixtureRecords[0].capabilities = CAPABILITY_GAME_DISPLAY;
  GameEngine engine;
  Lobby table;
  PlayerSeat seats[6] = {{1,0,1},{2,0,2},{3,1,1},{4,2,1},{5,8,1},{6,9,1}};
  ProfileFixture::profiles["d1500001"].name = "Ricky";
  ProfileFixture::profiles["d1500003"].name = "Jaime";
  strcpy(seats[0].profileId,"d1500001");
  strcpy(seats[2].profileId,"d1500003");
  GameSettings settings;
  settings.profile = GameProfile::Commander;
  settings.startingLife = 40;
  assert(engine.start(seats,6,seats[0],100,settings));
  LedRenderer renderer(sigilBus);
  auto render = [&]() { renderer.render(HubState::Running,table,engine,0,0,0,100); };
  render();
  auto first = sentGameDisplays[0];
  assert(first.primary.life == 40 && first.secondary.life == 40 && first.commander);
  assert(!first.sourceCount && !strcmp(first.primary.name,"Ricky"));
  unsigned count = gameDisplaySends;
  render(); assert(gameDisplaySends == count);
  assert(engine.changeCommanderDamage(1,3,1,6));
  assert(engine.changeCommanderDamage(1,3,2,3));
  assert(engine.changeCommanderDamage(3,1,1,9)); // Opposite direction must not appear.
  render();
  auto snapshot = sentGameDisplays[0];
  assert(!strcmp(snapshot.sources[0].name,"Jaime"));
  assert(snapshot.primary.life == 31 && snapshot.sourceCount == 1);
  assert(snapshot.sources[0].player == 3 && snapshot.sources[0].damage[0] == 6 && snapshot.sources[0].damage[1] == 3);
  for (uint8_t source = 2; source <= 6; ++source)
    if (source != 3) assert(engine.changeCommanderDamage(1,source,1,1));
  render(); snapshot = sentGameDisplays[0];
  assert(snapshot.sourceCount == 3 && snapshot.omittedSources == 2);
  assert(snapshot.sources[0].player == 2 && snapshot.sources[2].player == 4);
  uint8_t bytes[sizeof(snapshot)]; memcpy(bytes,&snapshot,sizeof(snapshot));
  assert(bytes[0] == VERSION && bytes[1] == 32 && bytes[2] == 0);
  GameDisplayPacket decoded{}; memcpy(&decoded,bytes,sizeof(decoded));
  assert(validGameDisplay(decoded) && !memcmp(&decoded,&snapshot,sizeof(decoded)));
  decoded.sourceCount = 4; assert(!validGameDisplay(decoded));
  decoded = snapshot; decoded.primary.name[12] = 'x'; assert(!validGameDisplay(decoded));
  decoded = snapshot; decoded.sources[0].damage[0] = -1; assert(!validGameDisplay(decoded));
  assert(engine.changeLife(1,-100)); render(); assert(sentGameDisplays[0].primary.life < 0);
  assert(engine.passTurn(0,200)); render();
  assert(displayPrimaryPlayer(sentGameDisplays[0].state) == 2);
  assert(sentGameDisplays[0].primary.life == 40 && sentGameDisplays[0].secondary.life < 0);
  assert(!sentGameDisplays[0].sourceCount);
  count = gameDisplaySends; renderer.invalidate(0); render(); assert(gameDisplaySends == count + 1);
  engine.reset(); settings.profile = GameProfile::Magic; settings.startingLife = 20;
  assert(engine.start(seats,6,seats[0],100,settings));
  renderer.invalidateAll(); render();
  assert(!sentGameDisplays[0].commander && !sentGameDisplays[0].sourceCount && sentGameDisplays[0].primary.life == 20);
  fixtureRecords[0].capabilities = 0; renderer.invalidateAll(); count = gameDisplaySends;
  render(); assert(gameDisplaySends == count); // Legacy peers keep the seven-byte protocol.
  fixtureRadio = false;
}

static void saveClientFixture(const char *name, const String &json);

// Plays out queued cues on a private clock so game time does not move.
static void drainAudio() {
  for (uint32_t t = testNow; t < testNow + 3000; t += 10) audio.update(t);
  audio.clear();
}
static unsigned totalBuzzes() {
  unsigned total = 0;
  for (auto count : TurnHub::fixtureBuzzes) total += count;
  return total;
}
static void resetBuzzes() {
  drainAudio();
  for (auto &count : TurnHub::fixtureBuzzes) count = 0;
}

static void turnTimerEngine() {
  using namespace TurnHub;
  GameEngine engine;
  PlayerSeat seats[2] = {{1,0,1},{2,1,1}};
  GameSettings settings;  // OFF: no countdown, gentle long-turn cue only.
  assert(engine.start(seats,2,seats[0],1000,settings));
  assert(engine.turnTimerPhase(1000 + TURN_TIMER_LONG_TURN_MS - 1) == TurnTimerPhase::Normal);
  assert(engine.turnTimerPhase(1000 + TURN_TIMER_LONG_TURN_MS) == TurnTimerPhase::LongTurn);
  assert(engine.turnRemainingMs(5000) == 0);

  settings.turnTimerMs = 60000;
  const uint32_t start = UINT32_MAX - 20000;  // The turn crosses the 32-bit wrap.
  assert(engine.start(seats,2,seats[0],start,settings));
  assert(engine.turnRemainingMs(start) == 60000);
  assert(engine.turnTimerPhase(start + 49999) == TurnTimerPhase::Normal);
  assert(engine.turnTimerPhase(start + 50000) == TurnTimerPhase::Warning);
  assert(engine.turnRemainingMs(start + 50000) == TURN_TIMER_WARNING_MS);
  assert(engine.turnTimerPhase(start + 60000) == TurnTimerPhase::Expired);
  assert(engine.turnRemainingMs(start + 60000) == 0);
  // Expiry is a cue, never a rule: the turn simply continues.
  assert(engine.turnTimerPhase(start + 600000) == TurnTimerPhase::Expired);
  assert(engine.activePlayerNumber() == 1 && engine.running());
  // Pause freezes the countdown; resume continues it.
  assert(engine.passTurn(0,start + 700000));
  assert(engine.pause(start + 755000));
  assert(engine.turnRemainingMs(start + 900000) == 5000);
  assert(engine.turnTimerPhase(start + 900000) == TurnTimerPhase::Warning);
  assert(engine.resume(start + 900000));
  assert(engine.turnRemainingMs(start + 901000) == 4000);
  // A new turn starts a fresh countdown.
  assert(engine.passTurn(1,start + 902000));
  assert(engine.turnRemainingMs(start + 902000) == 60000);
  assert(engine.turnTimerPhase(start + 902000) == TurnTimerPhase::Normal);
  // Once the game is over, phases are quiet.
  bool finished = false;
  assert(engine.pause(start + 903000) && engine.eliminatePlayer(2,start + 903000,finished) && finished);
  assert(engine.turnTimerPhase(start + 9999999) == TurnTimerPhase::Normal);

  // A clock sampled just before the turn was stamped (loop() reads millis()
  // once, then the countdown starts the game with a later millis()) is zero
  // elapsed, not a wrapped ~49-day turn. This held across the 32-bit wrap too.
  assert(engine.start(seats,2,seats[0],start,settings));
  assert(engine.currentTurnElapsedMs(start - 3) == 0);
  assert(engine.gameElapsedMs(start - 3) == 0);
  assert(engine.turnRemainingMs(start - 3) == 60000);
  assert(engine.turnTimerPhase(start - 3) == TurnTimerPhase::Normal);
  assert(engine.passTurn(0,start + 30000));
  assert(engine.turnTimerPhase(start + 29999) == TurnTimerPhase::Normal);
  assert(engine.gameElapsedMs(start + 29999) == 29999);

  for (uint32_t bad : {1000u, 14000u, 15500u, TURN_TIMER_MAX_MS + 1000}) {
    settings.turnTimerMs = bad;
    assert(!validTurnTimerMs(bad) && !engine.start(seats,2,seats[0],1,settings));
  }
  for (auto preset : TURN_TIMER_PRESETS_MS) assert(validTurnTimerMs(preset));

  // The captured timer survives recovery in checkpoint schema 1's timer word.
  settings.turnTimerMs = 120000;
  assert(engine.start(seats,2,seats[0],1000,settings));
  GameCheckpoint saved; engine.checkpoint(saved,31000);
  uint8_t bytes[GAME_CHECKPOINT_CAPACITY];
  const size_t size = encodeCheckpoint(saved,bytes,sizeof(bytes));
  assert(size);
  GameCheckpoint decoded;
  assert(decodeCheckpoint(bytes,size,decoded) == TurnHubStorage::Status::Ok);
  GameEngine restored;
  assert(restored.restoreCheckpoint(decoded,500) && restored.paused());
  assert(restored.turnTimerMs() == 120000 && restored.turnRemainingMs(900000) == 90000);
}

static void ledCueSelection() {
  using namespace TurnHub;
  const auto &style = defaultLedCueProfile();
  // Established lobby behavior: host player 1 flashes red once per cycle, host green steady.
  freshLobby(2);
  auto lobbyCue = [](uint8_t id, uint32_t now) {
    return selectSigilLedState(id,HubState::Lobby,lobby,game,0,0,0,now);
  };
  const auto host = lobbyCue(0,0);
  assert(host.cue == LedCue::Joined && host.playerNumber == 1 && host.has(LedOverlay::Host));
  assert(ledLevels(style,host,0).red && ledLevels(style,host,200).green && !ledLevels(style,host,200).red);
  assert(!lobbyCue(1,0).has(LedOverlay::Host) && lobbyCue(1,0).playerNumber == 2);
  const auto invite = lobbyCue(5,0);
  assert(invite.cue == LedCue::Unassigned);
  assert(ledLevels(style,invite,100).blue == 255 && ledLevels(style,invite,700).green &&
      ledLevels(style,invite,1200).red);

  GameEngine engine; Lobby table;
  PlayerSeat seats[2] = {{1,0,1},{2,1,1}};
  GameSettings settings; settings.turnTimerMs = 60000;
  assert(engine.start(seats,2,seats[0],1000,settings));
  auto cue = [&](uint8_t id, uint32_t now) {
    return selectSigilLedState(id,HubState::Running,table,engine,0,0,0,now);
  };
  assert(cue(0,1000).cue == LedCue::TurnStarted && cue(0,4000).cue == LedCue::YourTurn);
  assert(cue(1,4000).cue == LedCue::Waiting && !cue(0,4000).overlays);
  assert(cue(0,51000).has(LedOverlay::TurnWarning) && cue(0,61000).has(LedOverlay::TimerExpired));
  assert(!cue(1,61000).overlays);  // Only the active Sigil shows its timer.
  auto levels = [&](uint32_t now) { return ledLevels(style,cue(0,now),now); };
  // Warning pulses slowly; expiry is steady: distinguishable without color.
  assert(levels(51000).red && !levels(51750).red && !levels(51000).green);
  assert(levels(61000).red && levels(61750).red);
  assert(ledLevels(style,cue(1,61000),61000).blue == 255);
  // TurnStarted flashes from the turn's own start.
  assert(levels(1000).blue == 255 && levels(1200).blue == 0 && levels(1400).blue == 255);

  GameEngine untimed; GameSettings off;
  assert(untimed.start(seats,2,seats[0],1000,off));
  const auto longTurn = selectSigilLedState(0,HubState::Running,table,untimed,0,0,0,1000 + TURN_TIMER_LONG_TURN_MS);
  assert(longTurn.has(LedOverlay::LongTurn) && ledLevels(style,longTurn,0).green && !ledLevels(style,longTurn,0).red);

  // Another profile changes presentation only: same state, different channels.
  LedCueProfile alternate = style;
  alternate.overlays[static_cast<uint8_t>(LedOverlay::TimerExpired)] =
      CueStyle{LedStyles::off(), LedStyles::off(), LedStyles::blink(2000,1000)};
  assert(!ledLevels(alternate,cue(0,62000),62000).red && ledLevels(alternate,cue(0,62000),62000).green);
  enterEmptyLobby();
}

static void turnTimerCuesAndMute() {
  using namespace TurnHub;
  freshLobby(2);
  nextGameSettings.turnTimerMs = 60000;
  startFromHost();
  assert(game.turnTimerMs() == 60000);
  const uint8_t active = game.activeController();
  resetBuzzes();
  // loop() samples millis() before the countdown stamps the new turn; that
  // stale sample must not raise a spurious EXPIRED cue at game start.
  updateTurnTimerCues(testNow - 1); drainAudio();
  assert(totalBuzzes() == 0 && turnTimerCue.phase == TurnTimerPhase::Normal);
  testNow += 49000; updateTurnTimerCues(testNow); drainAudio();
  assert(totalBuzzes() == 0);
  testNow += 1000; updateTurnTimerCues(testNow); updateTurnTimerCues(testNow); drainAudio();
  assert(fixtureBuzzes[active] == 1 && totalBuzzes() == 1);  // One chirp, once.
  testNow += 10000; updateTurnTimerCues(testNow); drainAudio();
  assert(fixtureBuzzes[active] == 3 && totalBuzzes() == 3);  // Two-note expiry, once.
  testNow += 600000; updateTurnTimerCues(testNow); drainAudio();
  assert(totalBuzzes() == 3 && hubState == HubState::Running && game.activeController() == active);
  // Pause/resume does not replay the timer cue.
  assert(web(active,1,WebControl::PauseResume) && web(active,1,WebControl::PauseResume));
  resetBuzzes(); updateTurnTimerCues(testNow); drainAudio();
  assert(totalBuzzes() == 0);
  // The next turn re-arms, and muted audio stays silent while LEDs still render.
  handlePass(active); testNow += 3000; updatePendingPass(testNow);
  const uint8_t next = game.activeController();
  assert(next != active);
  resetBuzzes();
  AudioCueProfile muted = defaultAudioCueProfile();
  muted.enabled = false;
  audio.setProfile(muted);
  testNow += 51000; updateTurnTimerCues(testNow); drainAudio();
  assert(totalBuzzes() == 0 && turnTimerCue.phase == TurnTimerPhase::Warning);
  assert(selectSigilLedState(next,hubState,lobby,game,0,0,0,testNow).has(LedOverlay::TurnWarning));
  audio.setProfile(defaultAudioCueProfile());
  // Settings stay lobby-only while the captured timer runs.
  nextGameSettings = GameSettings{};
  enterEmptyLobby();
}

static void turnTimerSettingsHttp() {
  enterEmptyLobby(); TurnHub::fixtureRadio = false; testNow = 1000;
  nextGameSettings = TurnHub::GameSettings{};
  String hostId, guestId;
  const String host = registerPhone("Timer host", hostId), guest = registerPhone("Timer guest", guestId);
  assert(request("/api/session/join", host) == 200 && request("/api/session/join", guest) == 200);
  assert(request("/api/game/settings", host, {}, HTTP_GET) == 200);
  assert(server.body.find("\"presetsMs\":[0,60000,120000,180000,300000]") != std::string::npos);
  assert(server.body.find("\"turnTimerMs\":0") != std::string::npos);
  assert(server.body.find("\"canEdit\":true") != std::string::npos);
  for (const char *bad : {"1000", "abc", "-60000", "3601000", "15500", "99999999"})
    assert(request("/api/game/settings", host, {{"turnTimerMs", bad}}) == 400);
  assert(request("/api/game/settings", guest, {{"turnTimerMs", "90000"}}) == 409);
  assert(nextGameSettings.turnTimerMs == 0);
  // Partial update: only the timer changes.
  nextGameSettings.profile = TurnHub::GameProfile::Magic; nextGameSettings.startingLife = 20;
  assert(request("/api/game/settings", host, {{"turnTimerMs", "90000"}}) == 200);
  assert(nextGameSettings.turnTimerMs == 90000 && nextGameSettings.profile == TurnHub::GameProfile::Magic &&
      nextGameSettings.startingLife == 20);
  assert(request("/api/v1/state", "", {}, HTTP_GET) == 200);
  assert(server.body.find("\"turnTimerMs\":90000") != std::string::npos);
  assert(server.body.find("\"turnTimer\":{\"phase\":\"NORMAL\",\"remainingMs\":null}") != std::string::npos);
  assert(request("/api/control/start", host) == 200);
  testNow += 3000; updateCountdown(testNow);
  assert(hubState == HubState::Running);
  testNow += 81000;
  assert(request("/api/v1/state", "", {}, HTTP_GET) == 200);
  assert(server.body.find("\"phase\":\"WARNING\",\"remainingMs\":9000") != std::string::npos);
  saveClientFixture("timer-warning", server.body);
  server.on("/api/status", HTTP_GET, handleStatus);  // Registered by startNetworking() on hardware.
  assert(request("/api/status", "", {}, HTTP_GET) == 200);
  assert(server.body.find("\"turnTimerMs\":90000") != std::string::npos &&
      server.body.find("\"timerPhase\":\"WARNING\"") != std::string::npos);
  assert(request("/api/game/settings", host, {{"turnTimerMs", "60000"}}) == 409);  // Lobby only.
  enterEmptyLobby(); nextGameSettings = TurnHub::GameSettings{};
}

static void sigilReceivePackets() {
  char name[13] = {};
  assert(!TurnHubSigil::updateDisplayName(name, nullptr));
  assert(TurnHubSigil::updateDisplayName(name, "Player Alice long"));
  assert(!strcmp(name, "Player Alice"));
  assert(!TurnHubSigil::updateDisplayName(name, "Player Alice different suffix"));
  assert(TurnHubSigil::updateDisplayName(name, "Bob"));
  assert(!strcmp(name, "Bob") && name[4] == 0);
  assert(TurnHubSigil::updateDisplayName(name, nullptr));
  assert(!TurnHubSigil::updateDisplayName(name, ""));
  TurnHubSigil::ReceivedPacket received{};
  const uint8_t mac[6] = {1,2,3,4,5,6};
  const auto legacy = TurnHubProtocol::makePacket(PacketType::DisplayState, 0, 42);
  assert(received.assign(mac, reinterpret_cast<const uint8_t *>(&legacy), sizeof(legacy)));
  assert(received.length==sizeof(legacy) && !memcmp(received.data,&legacy,sizeof(legacy)));
  TurnHubProtocol::GameDisplayPacket snapshot{};
  snapshot.type=PacketType::GameDisplay;
  snapshot.primary.life=37;
  snapshot.sources[2].damage[1]=9; // Last bytes must survive queue copying.
  assert(received.assign(mac, reinterpret_cast<const uint8_t *>(&snapshot), sizeof(snapshot)));
  const auto queued=received;
  assert(queued.length==sizeof(snapshot) && !memcmp(queued.data,&snapshot,sizeof(snapshot)));
  assert(!memcmp(queued.mac,mac,6));
  assert(!received.assign(mac,queued.data,sizeof(snapshot)-1));
  assert(!received.assign(mac,queued.data,sizeof(snapshot)+1));
}

static void saveClientFixture(const char *name, const String &json) {
  std::ofstream file(std::string("build/client-") + name + ".json");
  assert(file); file << json.c_str();
}

static void nativeClientBoundary() {
  TurnHubWebApi::configureClientState(clientSnapshot, clientRevision);
  enterEmptyLobby(); testNow = 1000;
  nextGameSettings = TurnHub::GameSettings{};
  assert(request("/api/v1/info", "", {}, HTTP_GET) == 200);
  const String epoch = responseField("bootId");
  assert(epoch.length() == 32);
  assert(server.body.find("\"intentEnvelope\":false") != std::string::npos);
  assert(server.body.find("password") == std::string::npos);
  saveClientFixture("info", server.body);
  assert(request("/api/v1/state", "", {}, HTTP_GET) == 200);
  assert(server.body.find("\"players\":[]") != std::string::npos);
  saveClientFixture("lobby", server.body);
  const auto emptyRevision = clientRevision();
  ++testNow; assert(clientRevision() == emptyRevision);
  String firstId, secondId;
  const String first = registerPhone("Native first", firstId);
  const String second = registerPhone("Native second", secondId);
  assert(request("/api/session/join", first) == 200);
  assert(clientRevision() > emptyRevision);
  const auto joinedRevision = clientRevision();
  assert(request("/api/session/join", first) == 200);
  assert(clientRevision() == joinedRevision); // Accepted no-op.
  assert(request("/api/session/join", second) == 200);
  assert(request("/api/control/start", first) == 200);
  testNow += 3000; updateCountdown(testNow);
  assert(hubState == HubState::Running);
  const auto runningRevision = clientRevision();
  testNow += 100; dispatchSystemIntent(IntentType::ExpireLifeChanges);
  assert(clientRevision() == runningRevision); // Clock samples are not mutations.
  assert(request("/api/v1/state", "", {}, HTTP_GET) == 200);
  saveClientFixture("running", server.body);
  assert(request("/api/control/pass", "") == 401);
  assert(request("/api/control/pass", second) == 409);
  assert(clientRevision() == runningRevision);
  for (const char *bad : {"", "-1", "01", "1x", "4294967296", "99999999999999999999"})
    assert(request("/api/control/pass", first, {{"expectedRevision", bad}, {"expectedBootId", epoch}}) == 400);
  assert(request("/api/control/pass", first, {{"expectedRevision", String(runningRevision)}, {"expectedBootId", "old-boot"}}) == 409);
  assert(!pendingPass.active);
  assert(request("/api/control/pass", first, {{"expectedRevision", String(runningRevision)}, {"expectedBootId", epoch}}) == 200);
  assert(pendingPass.active && clientRevision() > runningRevision);
  saveClientFixture("pass-result", server.body);
  const auto armedRevision = clientRevision();
  // Retry with the old revision cannot toggle/cancel the pending PASS.
  assert(request("/api/control/pass", first, {{"expectedRevision", String(runningRevision)}, {"expectedBootId", epoch}}) == 409);
  assert(pendingPass.active && clientRevision() == armedRevision);
  saveClientFixture("conflict", server.body);
  testNow += 3000; updatePendingPass(testNow);
  assert(game.activePlayerNumber() == 2 && clientRevision() > armedRevision);
  const String reconnected = loginPhone(firstId);
  assert(request("/api/session/me", reconnected, {}, HTTP_GET) == 200);
  assert(request("/api/v1/state", reconnected, {}, HTTP_GET) == 200);
  assert(server.body.find("\"activePlayer\":2") != std::string::npos);
  saveClientFixture("reconnected", server.body);

  freshLobby(2);
  nextGameSettings.profile = TurnHub::GameProfile::Commander;
  startFromHost();
  String message;
  TurnHub::IntentPayload payload;
  payload.counterSource = 2; payload.counterSlot = 2; payload.value = 3;
  assert(changeCounter(0, 1, IntentType::ChangeCounter, payload, message));
  const auto damageRevision = clientRevision();
  payload = TurnHub::IntentPayload{}; payload.targetPlayer = 2; payload.value = -2;
  assert(changeCounter(0, 1, IntentType::RequestLifeChange, payload, message));
  assert(clientRevision() > damageRevision);
  assert(request("/api/v1/state", "", {}, HTTP_GET) == 200);
  saveClientFixture("commander", server.body);
  const auto pendingRevision = clientRevision();
  testNow += TurnHub::LIFE_APPROVAL_MS;
  dispatchSystemIntent(IntentType::ExpireLifeChanges);
  assert(clientState.revision() > pendingRevision && game.lifeTotal(2) == 38);
  const auto settledRevision = clientState.revision();
  dispatchSystemIntent(IntentType::ExpireLifeChanges);
  assert(clientState.revision() == settledRevision);
  testNow = UINT32_MAX - 10000;
  assert(changeCounter(0, 1, IntentType::RequestLifeChange, payload, message));
  const auto rolloverRevision = clientState.revision();
  testNow += TurnHub::LIFE_APPROVAL_MS;
  dispatchSystemIntent(IntentType::ExpireLifeChanges);
  assert(clientState.revision() > rolloverRevision && game.lifeTotal(2) == 36);

  // Exercise the largest snapshot with every Commander source populated.
  GameEngine fullGame; Lobby fullLobby; TurnHub::ClientState full;
  PlayerSeat seats[MAX_PLAYERS];
  for (uint8_t i = 0; i < MAX_PLAYERS; ++i)
    seats[i] = PlayerSeat(i + 1, i / 2, i % 2 + 1);
  assert(fullGame.start(seats, MAX_PLAYERS, seats[0], testNow, nextGameSettings));
  for (uint8_t i = 1; i <= MAX_PLAYERS; ++i)
    for (uint8_t j = 1; j <= MAX_PLAYERS; ++j)
      for (uint8_t c = 1; c <= 2; ++c)
        assert(fullGame.changeCommanderDamage(i, j, c, 1));
  full.observe(HubState::Running, fullLobby, fullGame, nextGameSettings, {});
  const auto fullRevision = full.revision();
  full.observe(HubState::Running, fullLobby, fullGame, nextGameSettings, {});
  assert(full.revision() == fullRevision);
  const String fullJson = full.json("THA-TEST", epoch.c_str(), fullGame, testNow, PASS_GRACE_MS);
  assert(fullJson.length() > 10000);
  saveClientFixture("full", fullJson);
  enterEmptyLobby(); nextGameSettings = TurnHub::GameSettings{};
}

// Interrupted-match recovery: beginGameRecovery()/checkpointGame() live in a
// process-wide singleton (game_recovery_store.cpp), so this scenario must run
// last -- once it opens that store, every dispatched intent's observer starts
// persisting checkpoints for the rest of the process (matching production;
// see main.cpp's observeIntent()). Earlier scenarios never call
// beginGameRecovery(), so they are unaffected either way.
static void gameRecoveryLifecycle() {
  using TurnHubStorage::Status;

  testBlobs.clear();
  useRealNvsBlobs = true;
  readError = ESP_OK; openError = ESP_OK; setError = ESP_OK;

  // 1) First boot ever: nothing has been saved.
  freshLobby(2);
  testNow = 5000;
  assert(TurnHub::beginGameRecovery(game, lobby, testNow) == Status::NotFound);
  assert(!game.hasPlayers());

  // 2) Start a match and accept a semantic transition (a committed pass).
  // main.cpp's observer persists a checkpoint after every dispatched intent;
  // no code here calls checkpointGame() directly.
  startFromHost();
  handlePass(0);
  testNow += PASS_GRACE_MS; updatePendingPass(testNow);
  assert(!pendingPass.active && game.activePlayerNumber() == 2);
  assert(testBlobs.count("checkpoint") == 1);
  testNow += 45000; // 45s of real play before "power loss".
  handlePass(1);
  testNow += PASS_GRACE_MS; updatePendingPass(testNow);
  const uint32_t elapsedBeforeLoss = game.gameElapsedMs(testNow);
  assert(elapsedBeforeLoss >= 45000);

  // 3) Simulate a reboot: fresh in-RAM objects standing in for cleared RAM,
  // reading back whatever step 2 left in "flash" (testBlobs). Ten minutes of
  // downtime must never be charged to a player, and restoring never replays
  // the game-completed statistics callback.
  const uint32_t rebootAtMs = testNow + 600000;
  const int completedBeforeReboot = completedGames;
  GameEngine rebooted; Lobby rebootedLobby;
  assert(TurnHub::beginGameRecovery(rebooted, rebootedLobby, rebootAtMs) == Status::Ok);
  assert(rebooted.hasPlayers() && rebooted.paused() && !rebooted.gameOver());
  assert(rebooted.gameElapsedMs(rebootAtMs) == elapsedBeforeLoss);
  assert(rebootedLobby.playerCount() == 2 && rebootedLobby.hostController() == 0);
  assert(completedGames == completedBeforeReboot);

  // 4) A damaged record must fail safe -- Atlas boots to a fresh, empty lobby
  // rather than loading ambiguous state -- instead of crashing or guessing.
  assert(testBlobs.count("checkpoint") == 1);
  testBlobs["checkpoint"][0] ^= 0xFF; // flip a magic byte
  GameEngine corrupt; Lobby corruptLobby;
  assert(TurnHub::beginGameRecovery(corrupt, corruptLobby, rebootAtMs) == Status::Corrupt);
  assert(!corrupt.hasPlayers() && corruptLobby.playerCount() == 0);

  useRealNvsBlobs = false;
  readError = ESP_ERR_NVS_NOT_FOUND;
  testBlobs.clear();
  enterEmptyLobby();
}

int main() {
  sigilReceivePackets(); std::cout<<"PASS Sigil radio queue preserves legacy and game display packets\n";
  assert(configureIntentHandlers());
  observeClientState(); intents.setObserver(observeIntent);
  GameEngine::setGameCompletedCallback(completed);
  dispatcherContract(); std::cout<<"PASS dispatcher contract\n";
  deliberatePairing(); std::cout<<"PASS deliberate pairing authorization, radio failure, timeout, rollover and gameplay exclusion\n";
  lobbyLifecycle(); std::cout<<"PASS lobby and lifecycle\n";
  winDecisions(); std::cout<<"PASS win decisions and shared-seat order\n";
  eliminationAndConcession(); std::cout<<"PASS elimination versus concession\n";
  passTimingAndActors(); std::cout<<"PASS pass timing, cancellation, rollover, actors\n";
  optionalStorage(); std::cout<<"PASS optional storage error policy\n";
  virtualProfileFlow(); std::cout<<"PASS profile registration/login, phone-only game, companion sessions, authorization and throttling\n";
  nativeClientBoundary(); std::cout<<"PASS native snapshots, revisions, stale requests, PASS, Commander and reconnect\n";
  guestSigilsDoNotCreateAccounts(); std::cout<<"PASS guest Sigil joins, shared seats, polling and games create no accounts\n";
  physicalCompanionFlow(); std::cout<<"PASS mixed table, physical attachment, two phones and one Sigil, statistics once\n";
  attachNamedProfileToGuest(); std::cout<<"PASS named guest attachment, browser merge and ownership protection\n";
  profilePolicyFlow(); std::cout<<"PASS profile choices, physical authorization, companion privacy, expiry and claim revalidation\n";
  gameProfilesAndLife(); std::cout<<"PASS game settings, own life, companion state, limits, rematch and authorization\n";
  lifeApprovalsAndCommander(); std::cout<<"PASS life approval authorization, deadlines, rollover, atomic Commander counters and lifecycle\n";
  accountPermissionsAndModeration(); std::cout<<"PASS account setup, independent permissions, moderation, revocation and private counts\n";
  physicalGameDisplay(); std::cout<<"PASS physical game display snapshots, received damage, shared focus, bounds and deduplication\n";
  turnTimerEngine(); std::cout<<"PASS turn timer phases, no automatic pass, pause freeze, rollover, validation and recovery\n";
  ledCueSelection(); std::cout<<"PASS LED cue selection, default styles and profile-only presentation changes\n";
  turnTimerCuesAndMute(); std::cout<<"PASS one-shot timer audio cues, pause/resume, re-arm and independent mute\n";
  turnTimerSettingsHttp(); std::cout<<"PASS turn timer settings API, partial update, lobby-only edits and state projection\n";
  virtualCapacity(); std::cout<<"PASS virtual capacity and 16-player win confirmation\n";
  // Must run last: see the comment on gameRecoveryLifecycle().
  gameRecoveryLifecycle(); std::cout<<"PASS interrupted-match recovery: boot load, checkpoint-after-intent, downtime exclusion, corrupt fail-safe\n";
}
