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
#include "controller_profiles.h"
#include "profile_store.h"
#include "web_api_internal.h"
// Compile the actual application entry point; the handler and adapter
// modules it binds are linked from ../../src, not copied rules.
#include "../../src/main.cpp"
#include "touch_calibration.h"
#include "touch_controls.h"
#include "harness_link.h"
#include "../../../Sigil/include/received_packet.h"
#include "../../../Sigil/include/display_name.h"

namespace TurnHub { extern uint32_t fixturePairingWindowSaved; extern int fixtureSpeakerVolumeSaved; }
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
static uint32_t fixturePairingWindowMs=0;
bool SigilBus::openPairing(uint32_t windowMs) { fixturePairingWindowMs=windowMs; return fixtureRadio; }
// Forgotten slots read as unpaired until the next freshLobby() re-pairs them.
static bool fixtureForgotten[MAX_PHYSICAL_SIGILS]{};
static unsigned fixtureUnpairs[MAX_PHYSICAL_SIGILS]{};
static bool fixtureForgetFails=false;
bool SigilBus::forget(uint8_t id) {
  if(id>=MAX_PHYSICAL_SIGILS||!fixtureRadio||fixtureForgotten[id]||fixtureForgetFails) return false;
  ++fixtureUnpairs[id]; fixtureForgotten[id]=true; return true;
}
bool SigilBus::poll(SigilEvent&) { return false; }
uint8_t SigilBus::activeCount(uint32_t) const { return 0; }
bool SigilBus::isOnline(uint8_t id,uint32_t) const { return fixtureRadio && id < MAX_PHYSICAL_SIGILS; }
const SigilRecord *SigilBus::record(uint8_t id) const { return fixtureRadio&&id<MAX_PHYSICAL_SIGILS&&!fixtureForgotten[id]?&fixtureRecords[id]:nullptr; }
static unsigned fixtureSends=0;
static unsigned fixtureProfileSyncs=0;
void SigilBus::syncDisplayProfile(uint8_t id) { assert(id<MAX_PHYSICAL_SIGILS); ++fixtureProfileSyncs; }
static int32_t fixtureInputTiming[MAX_PHYSICAL_SIGILS]{};
static unsigned fixtureInputTimingSends=0;
static int32_t fixtureLedState[MAX_PHYSICAL_SIGILS]{};
static unsigned fixtureLedStateSends=0;
static unsigned fixtureChannelSends=0;
static int32_t fixtureMenuState[MAX_PHYSICAL_SIGILS]{};
static unsigned fixtureMenuStateSends=0;
static int32_t fixtureHarnessCommand=-1;
static unsigned fixtureHarnessCommands=0;
static int fixtureFactoryResetSigil=-1;
static int32_t fixtureFactoryResetValue=0;
bool SigilBus::send(uint8_t id,TurnHubProtocol::PacketType type,int32_t value) {
  assert(id<MAX_PHYSICAL_SIGILS);++fixtureSends;
  if(type==TurnHubProtocol::PacketType::InputTiming&&fixtureRadio) { fixtureInputTiming[id]=value; ++fixtureInputTimingSends; }
  if(type==TurnHubProtocol::PacketType::LedState&&fixtureRadio) { fixtureLedState[id]=value; ++fixtureLedStateSends; }
  if(type==TurnHubProtocol::PacketType::MenuState&&fixtureRadio) { fixtureMenuState[id]=value; ++fixtureMenuStateSends; }
  if(type==TurnHubProtocol::PacketType::HarnessCommand&&fixtureRadio) { fixtureHarnessCommand=value; ++fixtureHarnessCommands; }
  if(type==TurnHubProtocol::PacketType::FactoryReset&&fixtureRadio) { fixtureFactoryResetSigil=id; fixtureFactoryResetValue=value; }
  if(type==TurnHubProtocol::PacketType::SetBlue||type==TurnHubProtocol::PacketType::SetRed||
     type==TurnHubProtocol::PacketType::SetGreen) ++fixtureChannelSends;
  return fixtureRadio;
}
static TurnHubProtocol::GameDisplayPacket sentGameDisplays[MAX_PHYSICAL_SIGILS]{};
static unsigned gameDisplaySends = 0;
bool SigilBus::sendGameDisplay(const TurnHubProtocol::GameDisplayPacket &p) {
  assert(TurnHubProtocol::validGameDisplay(p));
  sentGameDisplays[p.sigilId] = p;
  ++gameDisplaySends;
  return fixtureRadio;
}
static TurnHubProtocol::ProfilePickerPacket sentPickers[MAX_PHYSICAL_SIGILS]{};
static unsigned pickerSends = 0;
bool SigilBus::sendProfilePicker(const TurnHubProtocol::ProfilePickerPacket &p) {
  assert(TurnHubProtocol::validProfilePicker(p));
  sentPickers[p.sigilId] = p;
  ++pickerSends;
  return fixtureRadio;
}
bool SigilBus::setBlue(uint8_t id,uint8_t v) { return send(id,PacketType::SetBlue,v); }
bool SigilBus::setRed(uint8_t id,bool v) { return send(id,PacketType::SetRed,v); }
bool SigilBus::setGreen(uint8_t id,bool v) { return send(id,PacketType::SetGreen,v); }
static unsigned fixtureBuzzes[MAX_PHYSICAL_SIGILS]{};
static std::vector<int32_t> fixtureTones[MAX_PHYSICAL_SIGILS];
bool SigilBus::buzzer(uint8_t id,int32_t v) { ++fixtureBuzzes[id]; fixtureTones[id].push_back(v); return send(id,PacketType::Buzzer,v); }
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
    TurnHub::fixtureForgotten[i]=false;
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
  testNow += TurnHubProtocol::PAIRING_WINDOW_MS - 1; updatePairingWindow(testNow); assert(pairingActive);
  ++testNow; updatePairingWindow(testNow); assert(!pairingActive);
  assert(intents.dispatch(intent).accepted());
  startFromHost(); updatePairingWindow(testNow); assert(!pairingActive);
  assert(!intents.dispatch(intent).accepted());
  enterEmptyLobby();
}
static void lobbyLifecycle() {
  freshLobby(2,true);
  assert(lobby.hostController()==0);
  assert(!dispatchModuleIntent(IntentType::Join,255).accepted());
  // No table host: any seated Sigil may arm Start, not only the first to join.
  assert(dispatchModuleIntent(IntentType::ArmStart,1).accepted() && lobby.startArmedBy()==1);
  lobby.clearStartArm();
  assert(web(0,2,WebControl::SelectStarter));
  PlayerSeat selected; assert(lobby.selectedStarter(selected)&&selected.slot==2);
  handleActionShort(0); assert(lobby.selectedStarter(selected)&&selected.slot==1);
  // Any seated Sigil may ask for a random starter now (no table host).
  handlePass(1); assert(lobby.selectedStarter(selected));
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
  // Any seated Sigil may call the rematch (no table host).
  assert(dispatchModuleIntent(IntentType::Rematch,1).accepted());
  assert(hubState==HubState::Lobby&&lobby.playerCount()==4&&!game.hasPlayers());
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
// Table presence over HTTP, as a phone does it: ask for a code, read it off
// the Atlas screen (the pending request), and enter it.
namespace TurnHubAtlas { TurnHubWebApi::PresenceHooks presenceHooks(); }
static void verifyAtTable(const String &token) {
  assert(request("/api/presence/request",token)==200);
  const PresenceRequest *shown=pendingPresenceCode(testNow); assert(shown!=nullptr);
  char digits[8]; snprintf(digits,sizeof(digits),"%06lu",static_cast<unsigned long>(shown->code));
  assert(request("/api/presence/confirm",token,{{"code",digits}})==200);
  assert(pendingPresenceCode(testNow)==nullptr);
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
  // No table host: the second phone may start too; its countdown is cancellable.
  assert(request("/api/control/start",second)==200 && hubState==HubState::Starting);
  assert(request("/api/control/cancel-start",first)==200 && hubState==HubState::Lobby);
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
  // Any seated phone may reset the table now (no table host).
  assert(request("/api/control/reset",second)==200 && lobby.playerCount()==0);
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
  // Any seated player may edit the next-game settings (no table host).
  assert(request("/api/game/settings",second,magic)==200);
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
  TurnHubWebApi::configurePresence(presenceHooks());
  String adminId,gmId,playerId,devId;
  const String admin=registerPhone("Administrator",adminId),gm=registerPhone("Moderator",gmId),player=registerPhone("Participant",playerId),dev=registerPhone("Developer",devId);
  assert(request("/api/accounts/setup","",{},HTTP_GET)==200&&server.body.find("true")!=std::string::npos);
  assert(request("/api/accounts/setup",admin)==403); // Physical confirmation needed.
  verifyAtTable(admin);assert(request("/api/accounts/setup",admin)==200);resetPresence();
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
  TurnHubProfiles::ModerationStats history;
  assert(TurnHubProfiles::loadModerationStatsForProfile(playerId,history)&&history.connectionResets==1);
  // Moderation counts are private statistics: no account listing exposes them,
  // not even to Game Masters or the account itself.
  for (const String &reader : {admin,gm}) {
    assert(request("/api/accounts",reader,{},HTTP_GET)==200&&server.body.find("connectionResets")==std::string::npos);
  }
  const String reconnected=loginPhone(playerId);assert(!TurnHubWebApi::connectionBlocked(playerId));
  assert(request("/api/accounts",reconnected,{},HTTP_GET)==200&&server.body.find("connectionResets")==std::string::npos&&server.body.find(gmId)==std::string::npos);
  // Only the owner's PIN-verified session sees them, on its statistics.
  assert(request("/api/session/stats",reconnected,{},HTTP_GET)==200&&
      server.body.find("\"moderation\":{\"visible\":true,\"connectionResets\":1,\"gameRemovals\":0}")!=std::string::npos);
  assert(request("/api/session/stats",gm,{},HTTP_GET)==200&&server.body.find("\"connectionResets\":0")!=std::string::npos);
  for (auto &session : TurnHubWebApi::internal::sessions) {
    if (session.used && reconnected == session.token) session.pinVerified = false;  // As after a Sigil-press claim.
  }
  assert(request("/api/session/stats",reconnected,{},HTTP_GET)==200&&
      server.body.find("\"visible\":false")!=std::string::npos&&server.body.find("connectionResets")==std::string::npos);
  assert(request("/api/session/stats/export",reconnected,{},HTTP_GET)==200&&server.body.find("esets")==std::string::npos);
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","pass"}})==200);
  assert(game.activePlayerNumber()==2&&!pendingPass.active);
  assert(request("/api/accounts/permissions",admin,{{"profileId",gmId},{"permissions","2"}})==200);
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","remove"}})==409);
  assert(request("/api/accounts/permissions",admin,{{"profileId",gmId},{"permissions","26"}})==200);
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","remove"}})==200);
  assert(game.isEliminated(1)&&game.livingPlayerCount()==2);
  assert(TurnHubProfiles::loadModerationStatsForProfile(playerId,history)&&history.gameRemovals==1&&history.connectionResets==1);
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","remove"}})==409);
  assert(request("/api/accounts/permissions",admin,{{"profileId",adminId},{"permissions","31"}})==200);
  assert(has(adminId,Admin|GameMaster|Developer));
  // Direct page requests with credentials must enforce the independent permission.
  server.headers["X-TurnHub-Token"]=dev;TurnHubWebApi::serveRestrictedPage(server,"dev-secret",Developer);assert(server.status==200&&server.body=="dev-secret");
  server.headers["X-TurnHub-Token"]=gm;TurnHubWebApi::serveRestrictedPage(server,"dev-secret",Developer);assert(server.status==403);
  // The serial log download is a Developer diagnostic, like /api/diagnostics.
  assert(request("/api/diagnostics/log","",{},HTTP_GET)==401);
  assert(request("/api/diagnostics/log",gm,{},HTTP_GET)==403);
  assert(request("/api/diagnostics/log",dev,{},HTTP_GET)==200);
  assert(server.body.rfind("# TurnHub Atlas serial log\n# atlasId=THA-",0)==0);
  assert(server.body.find(std::string("ATLAS|LOBBY|JOIN|BROWSER|"))!=std::string::npos);
  assert(server.body.find(std::string("|PROFILE|")+devId.c_str()+"\n")!=std::string::npos);
}
static bool logHas(const char *text) {
  return TurnHub::serialLog.snapshot().find(text)!=std::string::npos;
}
static void serialLogStream() {
  TurnHub::SerialLog log;
  log.printlnRedacted("password=secret", "password=<redacted>");
  const String expected = log.snapshot();
  char buffer[31];
  uint64_t cursor = 0, lost = 0;
  std::string drained;
  size_t n;
  while ((n = log.readSince(cursor, buffer, sizeof(buffer), lost))) {
    assert(lost == 0);
    drained.append(buffer, n);
  }
  assert(drained == expected && drained.find("secret") == std::string::npos);
  assert(log.snapshot() == expected); // Reading is independent of HTTP download.
  assert(log.readSince(cursor, buffer, sizeof(buffer), lost) == 0 && lost == 0);
  log.print("later");
  assert(log.readSince(cursor, buffer, sizeof(buffer), lost) > 0 && lost == 0);
  for (size_t i = 0; i < TurnHub::SerialLog::CAPACITY + 50; ++i) log.write('x');
  n = log.readSince(cursor, buffer, sizeof(buffer), lost);
  assert(n == sizeof(buffer) && lost == 50);
  assert(std::string(buffer, n) == std::string(n, 'x'));
  // Clearing capture also advances a lagging reader, without replaying stale bytes.
  log.clear(); log.println("fresh");
  const String fresh = log.snapshot();
  n = log.readSince(cursor, buffer, sizeof(buffer), lost);
  assert(lost == TurnHub::SerialLog::CAPACITY - sizeof(buffer));
  assert(std::string(buffer, n) == fresh);
  uint64_t independent = cursor;
  assert(log.readSince(independent, nullptr, 1, lost) == 0 && independent == cursor);
}

static void serialLogCapture() {
  using TurnHub::serialLog;
  serialLog.clear(); testNow=12345;
  serialLog.print("ATLAS|A|"); serialLog.println(7); serialLog.println('B');
  serialLog.printf("ATLAS|HEX|%02X\n",0xAB);
  assert(serialLog.snapshot()=="[     12.345] ATLAS|A|7\n[     12.345] B\n[     12.345] ATLAS|HEX|AB\n");
  // The port can show a secret; the downloadable copy never does.
  serialLog.printlnRedacted("ATLAS|WIFI_AP|PASSWORD|hunter22","ATLAS|WIFI_AP|PASSWORD|<redacted>");
  assert(!logHas("hunter22") && logHas("ATLAS|WIFI_AP|PASSWORD|<redacted>\n"));
  // Filling the ring drops the oldest lines, starts on a whole line and reports the loss.
  assert(serialLog.droppedBytes()==0);
  for(int i=0;i<2000;++i) { serialLog.print("ATLAS|FILL|"); serialLog.println(i); }
  const String wrapped=serialLog.snapshot();
  assert(serialLog.droppedBytes()>0 && wrapped.size()<=TurnHub::SerialLog::CAPACITY);
  assert(wrapped.rfind("[",0)==0 && wrapped.back()=='\n');
  assert(wrapped.find("ATLAS|A|7")==std::string::npos && wrapped.find("ATLAS|FILL|1999\n")!=std::string::npos);
  serialLog.clear(); assert(serialLog.snapshot().empty() && serialLog.droppedBytes()==0);

  // Log lines that make a downloaded log self-explanatory.
  freshLobby(2);
  assert(logHas("ATLAS|LOBBY|EMPTY|RESET|ORIGIN|SYSTEM|FROM|"));
  nextGameSettings.turnTimerMs=60000;
  startFromHost();
  assert(logHas("|PROFILE|generic|LIFE|40|TIMER_MS|60000|PLAYERS|2\n"));
  assert(web(0,1,WebControl::Concede) && hubState==HubState::GameOver);
  serialLog.clear();
  assert(web(0,1,WebControl::Reset) && hubState==HubState::Lobby);
  assert(logHas("ATLAS|LOBBY|EMPTY|RESET|ORIGIN|BROWSER|CONTROLLER|0|FROM|GAME_OVER\n"));
  nextGameSettings=TurnHub::GameSettings{};
  enterEmptyLobby();
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

// Sigils with CAPABILITY_LED_STATE get one semantic LedState packet per
// change (and on invalidation, i.e. every Hello) instead of channel frames.
static void ledStateTransport() {
  using namespace TurnHub;
  using namespace TurnHubProtocol;
  fixtureRadio = true;
  for (auto &record : fixtureRecords) { record.helloInfoValid = true; record.capabilities = 0; }
  fixtureRecords[0].capabilities = CAPABILITY_LED_STATE;
  GameEngine engine;
  Lobby table;
  PlayerSeat seats[2] = {{1,0,1},{2,1,1}};
  GameSettings settings;
  assert(engine.start(seats,2,seats[0],100,settings));
  LedRenderer renderer(sigilBus);
  auto render = [&](uint32_t now) { renderer.render(HubState::Running,table,engine,0,0,0,now); };
  const unsigned states = fixtureLedStateSends, channels = fixtureChannelSends;
  render(200);
  assert(fixtureLedStateSends == states + 1 && fixtureChannelSends > channels);  // Sigil 1 is legacy.
  LedStateFields fields = decodeLedState(fixtureLedState[0]);
  assert(fields.cue == LedCue::TurnStarted && fields.anchorAgeMs == 96);  // 100 ms, 16 ms units.
  render(300); render(2000);
  assert(fixtureLedStateSends == states + 1);  // Same cue: nothing more to send.
  render(3100);
  assert(fixtureLedStateSends == states + 2 && decodeLedState(fixtureLedState[0]).cue == LedCue::YourTurn);
  renderer.setProfile(0, reducedMotionLedCueProfile());
  renderer.invalidate(0); render(3200);
  fields = decodeLedState(fixtureLedState[0]);
  assert(fixtureLedStateSends == states + 3 && fields.style == TurnHubProtocol::LedStyle::ReducedMotion);
  for (auto &record : fixtureRecords) { record.helloInfoValid = false; record.capabilities = 0; }
  fixtureRadio = false;
}

namespace TurnHubAccounts { extern std::map<std::string, Account> accounts; }
// Profile picker on e-ink menu Sigils: gating, pages by name, locked and
// blocked profiles, stale keys, Guest, confirm/back, policy at the handler,
// and closing when idle or when the game starts.
static void profilePicker() {
  using namespace TurnHub;
  using namespace TurnHubProtocol;
  using A = SigilAction;
  const auto saved = ProfileFixture::profiles;
  const auto savedAccounts = TurnHubAccounts::accounts;
  ProfileFixture::profiles.clear();
  ProfileFixture::bindings.clear();
  const auto add = [](const char *id, const char *name) { ProfileFixture::profiles[id].name = name; };
  add("0000000B", "bob"); add("0000000A", "Alice"); add("0000000C", "Carol");
  add("0000000D", "Dan"); add("0000000E", "Eve");
  ProfileFixture::profiles["0000000D"].policy.allowPhysicalWithoutPin = false;
  TurnHubAccounts::accounts["0000000E"].archived = true;

  freshLobby(1);  // Sigil 0 joined.
  resetSigilMenus(); resetProfilePickers();
  for (auto &record : fixtureRecords) {
    record.helloInfoValid = true; record.capabilities = CAPABILITY_MENU;
    record.firmwareMajor = 0; record.firmwareMinor = 8;
  }
  fixtureRecords[2].capabilities |= CAPABILITY_DISPLAY_OLED;
  fixtureRecords[3].firmwareMinor = 7;
  assert(pickerSigil(1) && !pickerSigil(2) && !pickerSigil(3));
  const auto pick = [](uint8_t id, A a) { handleSelectAction(id, encodeSelectAction(a, sigilMenuRevision(id))); };
  const auto key = [](uint8_t id, PickerKeyCode k) {
    handlePickerKey(id, encodePickerKey(k, profilePickerPage(id).revision), testNow);
  };

  // Join opens the picker; browsing changes nothing at the table.
  pick(1, A::Join);
  assert(pickerOpen(1) && !lobby.isJoined(1) && lobby.playerCount() == 1);
  ProfilePickerPacket page = profilePickerPage(1);
  assert(page.mode == PickerMode::List && page.page == 0 && page.pageCount == 2 && page.itemCount == 3);
  assert(page.items[0].flags == PICKER_ITEM_GUEST && !strcmp(page.items[0].name, "Guest"));
  assert(!strcmp(page.items[1].name, "Alice") && !strcmp(page.items[2].name, "bob"));
  const unsigned sends = pickerSends;
  syncProfilePickers(testNow);
  assert(pickerSends > sends && sentPickers[1].mode == PickerMode::List);
  const unsigned quiet = pickerSends; syncProfilePickers(testNow); assert(pickerSends == quiet);

  // A key from an older page is dropped (and the page resent).
  handlePickerKey(1, encodePickerKey(PickerKeyCode::Up, page.revision - 1), testNow);
  assert(pickerOpen(1) && !lobby.isJoined(1) && profilePickerPage(1).revision == page.revision);
  syncProfilePickers(testNow); assert(pickerSends == quiet + 1);

  // Click: next page. Archived Eve is left out; Dan needs a phone sign-in.
  key(1, PickerKeyCode::Select);
  page = profilePickerPage(1);
  assert(page.page == 1 && page.itemCount == 2 && !strcmp(page.items[0].name, "Carol"));
  assert(!strcmp(page.items[1].name, "Dan") && (page.items[1].flags & PICKER_ITEM_LOCKED));
  key(1, PickerKeyCode::Right);
  page = profilePickerPage(1);
  assert(page.mode == PickerMode::List && page.notice == PickerNotice::NeedsPhone && !lobby.isJoined(1));
  // The handler enforces the same policy, whatever the page said.
  Intent sneaky; sneaky.type = IntentType::PickProfile; sneaky.actor.origin = IntentOrigin::PhysicalSigil;
  sneaky.actor.controllerId = 1; sneaky.actor.slot = 1; strcpy(sneaky.payload.profileId, "0000000D");
  assert(intents.dispatch(sneaky).status == IntentStatus::Unauthorized && !lobby.isJoined(1));

  // Left: back a page. Right: Alice, then confirm; Left backs out, click joins.
  key(1, PickerKeyCode::Left); assert(profilePickerPage(1).page == 0);
  key(1, PickerKeyCode::Right);
  page = profilePickerPage(1);
  assert(page.mode == PickerMode::Confirm && page.itemCount == 1 && !strcmp(page.items[0].name, "Alice"));
  key(1, PickerKeyCode::Left); assert(profilePickerPage(1).mode == PickerMode::List && !lobby.isJoined(1));
  key(1, PickerKeyCode::Right); key(1, PickerKeyCode::Select);
  assert(lobby.isJoined(1) && TurnHubControllers::profileForSeat(1, 1) == "0000000A");
  assert(!pickerOpen(1) && profilePickerPage(1).mode == PickerMode::Closed);
  syncProfilePickers(testNow); assert(sentPickers[1].mode == PickerMode::Closed);

  // An OLED Sigil still joins as a guest at once; a picker Sigil can pick Guest.
  pick(2, A::Join); assert(lobby.isJoined(2) && !pickerOpen(2));
  pick(4, A::Join); key(4, PickerKeyCode::Up);
  assert(lobby.isJoined(4) && TurnHubControllers::profileForSeat(4, 1).length() == 0 && !pickerOpen(4));

  // Alice now plays on a Sigil, so she is no longer offered elsewhere.
  pick(5, A::Join);
  page = profilePickerPage(5);
  assert(page.pageCount == 2 && !strcmp(page.items[1].name, "bob") && !strcmp(page.items[2].name, "Carol"));
  // Left on the first page cancels.
  key(5, PickerKeyCode::Left); assert(!pickerOpen(5) && !lobby.isJoined(5));

  // Idle for a minute closes it.
  pick(5, A::Join); assert(pickerOpen(5));
  testNow += PICKER_IDLE_MS; syncProfilePickers(testNow); assert(!pickerOpen(5));
  // Leaving the lobby closes it too.
  pick(5, A::Join); assert(pickerOpen(5));
  pick(0, A::StartGame); assert(hubState == HubState::Starting);
  syncProfilePickers(testNow); assert(!pickerOpen(5));

  for (auto &record : fixtureRecords) {
    record.helloInfoValid = false; record.capabilities = 0; record.firmwareMajor = 0; record.firmwareMinor = 0;
  }
  resetSigilMenus(); resetProfilePickers();
  enterEmptyLobby();
  ProfileFixture::bindings.clear();
  ProfileFixture::profiles = saved;
  TurnHubAccounts::accounts = savedAccounts;
}

// Menu Sigils: availability per state, the default action, MenuState
// transport and revisions, and SelectAction dispatching through Intents.
static void sigilMenus() {
  using namespace TurnHub;
  using namespace TurnHubProtocol;
  using A = SigilAction;
  const auto has = [](uint8_t id, A a) { return (sigilMenuFor(id).actions & sigilActionBit(a)) != 0; };
  const auto only = [](uint8_t id, std::initializer_list<A> list) {
    uint32_t mask = 0;
    for (A a : list) mask |= sigilActionBit(a);
    return sigilMenuFor(id).actions == mask;
  };
  const auto pick = [](uint8_t id, A a) { handleSelectAction(id, encodeSelectAction(a, sigilMenuRevision(id))); };

  freshLobby(2);  // Sigils 0 and 1 joined.
  resetSigilMenus();
  for (auto &record : fixtureRecords) { record.helloInfoValid = true; record.capabilities = CAPABILITY_MENU; }

  // Lobby: every joined Sigil can start and pick a random starter (no table
  // host), cycle the starter or add Seat B; an unjoined Sigil can only join.
  assert(only(0, {A::CycleStarter, A::AddSeatB, A::StartGame, A::RandomStarter}));
  assert(sigilMenuFor(0).defaultAction == static_cast<uint8_t>(A::StartGame));
  assert(only(1, {A::CycleStarter, A::AddSeatB, A::StartGame, A::RandomStarter}));
  assert(only(2, {A::Join}) && sigilMenuFor(2).defaultAction == static_cast<uint8_t>(A::Join));

  // Transport: one MenuState per Sigil, none while unchanged, resend when invalidated.
  const unsigned sends = fixtureMenuStateSends;
  syncSigilMenus(testNow);
  assert(fixtureMenuStateSends == sends + MAX_PHYSICAL_SIGILS);
  MenuStateFields sent = decodeMenuState(fixtureMenuState[2]);
  assert(sent.actions == sigilActionBit(A::Join) && sent.defaultAction == static_cast<uint8_t>(A::Join));
  syncSigilMenus(testNow); assert(fixtureMenuStateSends == sends + MAX_PHYSICAL_SIGILS);
  invalidateSigilMenu(2); syncSigilMenus(testNow); assert(fixtureMenuStateSends == sends + MAX_PHYSICAL_SIGILS + 1);

  // Joining changes Sigil 2's menu: its revision moves on.
  const uint8_t oldRevision = sigilMenuRevision(2);
  pick(2, A::Join); assert(lobby.isJoined(2) && lobby.playerCount() == 3);
  syncSigilMenus(testNow);
  assert(sigilMenuRevision(2) != oldRevision && decodeMenuState(fixtureMenuState[2]).revision == sigilMenuRevision(2));
  // A choice from the old menu is dropped (and the menu resent).
  handleSelectAction(2, encodeSelectAction(A::Join, oldRevision));
  assert(lobby.playerCount() == 3);
  // Unoffered actions are dropped even at the current revision.
  pick(1, A::Rematch); assert(hubState == HubState::Lobby && lobby.playerCount() == 3);

  pick(1, A::AddSeatB); assert(lobby.hasSecondary(1) && has(1, A::RemoveSeatB));
  pick(1, A::RemoveSeatB); assert(!lobby.hasSecondary(1));

  // Start from the menu: one choice arms and starts; anyone seated may cancel.
  pick(0, A::StartGame); assert(hubState == HubState::Starting);
  assert(only(1, {A::CancelStart}));
  pick(1, A::CancelStart); assert(hubState == HubState::Lobby);
  pick(0, A::StartGame); assert(hubState == HubState::Starting);
  testNow += 3000; updateCountdown(testNow); assert(hubState == HubState::Running);

  // Running: the active Sigil passes, claims or pauses; others may pause.
  const uint8_t activeId = game.activePlayer()->controllerId;
  const uint8_t otherId = activeId == 0 ? 1 : 0;
  assert(only(activeId, {A::Pass, A::ClaimWin, A::Pause}));
  assert(sigilMenuFor(activeId).defaultAction == static_cast<uint8_t>(A::Pass));
  assert(only(otherId, {A::Pause}));
  pick(activeId, A::Pass); assert(pendingPass.active);
  assert(has(activeId, A::CancelPass) && !has(activeId, A::Pass));
  pick(activeId, A::CancelPass); assert(!pendingPass.active);

  // Paused: resume, "I'm out", and a claim for the active player.
  pick(otherId, A::Pause); assert(hubState == HubState::Paused);
  assert(only(otherId, {A::Resume, A::BeginElimination}));
  assert(only(activeId, {A::Resume, A::BeginElimination, A::ClaimWin}));
  pick(otherId, A::BeginElimination);
  assert(eliminationTargetPlayer != 0 && game.playerByNumber(eliminationTargetPlayer)->controllerId == otherId);
  assert(only(otherId, {A::Eliminate, A::CancelElimination}));
  assert(sigilMenuFor(otherId).defaultAction == static_cast<uint8_t>(A::Eliminate));
  assert(only(activeId, {A::CancelElimination}));
  pick(activeId, A::CancelElimination); assert(eliminationTargetPlayer == 0);
  pick(otherId, A::Resume); assert(hubState == HubState::Running);

  // A win claim asks the other players to confirm or deny.
  pick(activeId, A::ClaimWin); assert(game.hasWinClaim());
  const PlayerSeat *confirmer = game.playerByNumber(game.nextWinConfirmationPlayerNumber());
  assert(confirmer != nullptr);
  assert(only(confirmer->controllerId, {A::ConfirmWin, A::DenyWin}));
  assert(sigilMenuFor(confirmer->controllerId).defaultAction == static_cast<uint8_t>(A::ConfirmWin));
  pick(confirmer->controllerId, A::DenyWin); assert(!game.hasWinClaim());

  for (auto &record : fixtureRecords) { record.helloInfoValid = false; record.capabilities = 0; }
  resetSigilMenus();
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
  // Lobby: player 1 flashes red once per cycle. There is no host overlay (no
  // table host since 2026-09-25).
  freshLobby(2);
  auto lobbyCue = [](uint8_t id, uint32_t now) {
    return selectSigilLedState(id,HubState::Lobby,lobby,game,0,0,0,now);
  };
  const auto host = lobbyCue(0,0);
  assert(host.cue == LedCue::Joined && host.playerNumber == 1 && !host.has(LedOverlay::Host));
  assert(ledLevels(style,host,0).red && !ledLevels(style,host,200).red);
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

// ActionRequired is two 60 ms notes at 1150 Hz; no other cue uses that tone.
static bool heardActionRequired(uint8_t id) {
  const int32_t tone = TurnHubProtocol::encodeTone(1150, 60);
  for (int32_t v : TurnHub::fixtureTones[id]) if (v == tone) return true;
  return false;
}
static void clearTones() { drainAudio(); for (auto &tones : TurnHub::fixtureTones) tones.clear(); }

static bool onlyPatterns(const TurnHub::CueStyle &style, bool (*ok)(const TurnHub::ChannelStyle &)) {
  return ok(style.blue) && ok(style.red) && ok(style.green);
}

static void accessibilityPreferences() {
  using namespace TurnHub;
  using TurnHubProfiles::AccessibilityPrefs;
  using TurnHubProfiles::LedStyle;
  // Reduced motion: only steady, dim or slow (>= 4 s) blinking lights.
  const auto calm = +[](const ChannelStyle &c) {
    return c.pattern == LedPattern::Off || c.pattern == LedPattern::Solid || c.pattern == LedPattern::Dim ||
        (c.pattern == LedPattern::Blink && c.periodMs >= 4000 && c.onMs >= 1000);
  };
  const LedCueProfile &calmProfile = reducedMotionLedCueProfile();
  for (const auto &style : calmProfile.cues) assert(onlyPatterns(style, calm));
  for (const auto &style : calmProfile.overlays) assert(onlyPatterns(style, calm));
  // Your turn and waiting differ by brightness, not only by colour.
  SigilLedState yours; yours.cue = LedCue::YourTurn;
  SigilLedState waiting; waiting.cue = LedCue::Waiting;
  assert(ledLevels(calmProfile, yours, 1000).blue == 255 && ledLevels(calmProfile, waiting, 1000).blue > 0 &&
      ledLevels(calmProfile, waiting, 1000).blue < 128);
  // Monochrome-safe: cues sharing a situation differ in cadence, not only hue.
  const LedCueProfile &mono = monochromeSafeLedCueProfile();
  const auto sameCadence = [](const CueStyle &a, const CueStyle &b) {
    const auto lit = [](const CueStyle &c) {
      return c.blue.pattern != LedPattern::Off ? c.blue : c.red.pattern != LedPattern::Off ? c.red : c.green;
    };
    const ChannelStyle x = lit(a), y = lit(b);
    return x.pattern == y.pattern && x.periodMs == y.periodMs && x.onMs == y.onMs;
  };
  assert(!sameCadence(mono.overlay(LedOverlay::TimerExpired), mono.overlay(LedOverlay::LongTurn)));
  assert(!sameCadence(mono.cue(LedCue::ConfirmationNeeded), mono.cue(LedCue::EliminationSelect)));
  assert(!sameCadence(calmProfile.overlay(LedOverlay::TimerExpired), calmProfile.overlay(LedOverlay::LongTurn)));
  assert(!sameCadence(calmProfile.cue(LedCue::ConfirmationNeeded), calmProfile.cue(LedCue::EliminationSelect)));
  assert(sameCadence(defaultLedCueProfile().overlay(LedOverlay::TimerExpired),
      defaultLedCueProfile().overlay(LedOverlay::LongTurn)));  // Why the alternatives exist.

  // Sharing a Sigil keeps each player's accommodation.
  AccessibilityPrefs a, b;
  a.sigilSound = false; a.ledStyle = LedStyle::MonochromeSafe; a.longPressMs = 3000; a.winHoldMs = 6000;
  b.ledStyle = LedStyle::ReducedMotion; b.longPressMs = 2500; b.winHoldMs = 7000;
  const AccessibilityPrefs merged = TurnHubProfiles::mergeSeatPrefs(a, b);
  assert(!merged.sigilSound && merged.ledStyle == LedStyle::ReducedMotion &&
      merged.longPressMs == 3000 && merged.winHoldMs == 7000 && validAccessibilityPrefs(merged));
  assert(TurnHubProfiles::mergeSeatPrefs(AccessibilityPrefs{}, AccessibilityPrefs{}).sigilSound);

  // HTTP: only the signed-in profile reads or changes its own preferences.
  enterEmptyLobby(); testNow = 1000; TurnHub::fixtureRadio = true;
  registerWebCallbacks();  // Production wiring, including the save -> restyle callback.
  for (uint8_t i = 0; i < MAX_PHYSICAL_SIGILS; ++i) { fixtureRecords[i].id = i; fixtureRecords[i].mac[5] = i; }
  String aliceId, bobId;
  const String alice = registerPhone("Access A", aliceId), bob = registerPhone("Access B", bobId);
  assert(request("/api/session/accessibility", "", {}, HTTP_GET) == 401);
  assert(request("/api/session/accessibility", "", {{"sigilSound", "0"}}) == 401);
  assert(request("/api/session/accessibility", alice, {}, HTTP_GET) == 200);
  assert(server.body.find("\"sigilSound\":true") != std::string::npos);
  assert(server.body.find("\"ledStyle\":\"standard\"") != std::string::npos);
  assert(server.body.find("\"longPressMs\":2000") != std::string::npos);
  assert(server.body.find("\"winHoldMs\":5000") != std::string::npos);
  for (const auto &bad : std::vector<std::map<std::string, String>>{
           {{"sigilSound", "yes"}}, {{"ledStyle", "sparkly"}}, {{"longPressMs", "500"}},
           {{"longPressMs", "2100"}}, {{"winHoldMs", "99999"}}, {{"longPressMs", "4000"}, {"winHoldMs", "4500"}},
           {{"longPressMs", "-2000"}}, {{"winHoldMs", "abc"}}})
    assert(request("/api/session/accessibility", alice, bad) == 400);
  assert(ProfileFixture::profiles[aliceId.c_str()].accessibility.longPressMs == 2000);
  // Partial updates keep the other fields.
  assert(request("/api/session/accessibility", alice, {{"ledStyle", "monochrome-safe"}}) == 200);
  assert(request("/api/session/accessibility", alice,
      {{"sigilSound", "0"}, {"longPressMs", "3000"}, {"winHoldMs", "6000"}}) == 200);
  assert(server.body.find("\"ledStyle\":\"monochrome-safe\"") != std::string::npos);
  saveClientFixture("accessibility", server.body);
  const AccessibilityPrefs saved = ProfileFixture::profiles[aliceId.c_str()].accessibility;
  assert(!saved.sigilSound && saved.ledStyle == LedStyle::MonochromeSafe &&
      saved.longPressMs == 3000 && saved.winHoldMs == 6000);
  assert(ProfileFixture::profiles[bobId.c_str()].accessibility.sigilSound);

  // Applying: Alice is bound to Sigil 1; Sigil 1 supports InputTiming, Sigil 2 does not.
  ProfileFixture::bindings.clear();
  ProfileFixture::bindings[ProfileFixture::key(fixtureRecords[1].mac, 1).c_str()] = aliceId;
  ProfileFixture::bindings[ProfileFixture::key(fixtureRecords[2].mac, 1).c_str()] = aliceId;
  fixtureRecords[1].helloInfoValid = true;
  fixtureRecords[1].capabilities = TurnHubProtocol::CAPABILITY_INPUT_TIMING;
  fixtureRecords[2].helloInfoValid = true; fixtureRecords[2].capabilities = 0;
  fixtureInputTiming[1] = fixtureInputTiming[2] = 0;
  applyAllSigilAccessibility(testNow);
  assert(&leds.profile(1) == &monochromeSafeLedCueProfile() && &leds.profile(0) == &defaultLedCueProfile());
  assert(audio.mutedSigils() == static_cast<uint16_t>((1u << 1) | (1u << 2)));
  assert(fixtureInputTiming[1] == TurnHubProtocol::encodeInputTiming(3000, 6000));
  assert(fixtureInputTiming[2] == 0);  // Older Sigil firmware keeps its defaults.
  // Not resent until the keepalive interval, then resent.
  const unsigned sends = fixtureInputTimingSends;
  applyAllSigilAccessibility(testNow + 1000); assert(fixtureInputTimingSends == sends);
  applyAllSigilAccessibility(testNow + 10000); assert(fixtureInputTimingSends == sends + 1);
  // A muted Sigil hears nothing, including queued notes; others still do.
  clearTones(); resetBuzzes();
  audio.actionRequired(1); audio.actionRequired(0); drainAudio();
  assert(fixtureBuzzes[1] == 0 && heardActionRequired(0));
  // Bob shares Sigil 1 with reduced motion: the merged style applies at once after his save.
  ProfileFixture::bindings[ProfileFixture::key(fixtureRecords[1].mac, 2).c_str()] = bobId;
  assert(request("/api/session/accessibility", bob,
      {{"ledStyle", "reduced-motion"}, {"longPressMs", "2500"}, {"winHoldMs", "7000"}}) == 200);
  assert(&leds.profile(1) == &reducedMotionLedCueProfile());
  assert(fixtureInputTiming[1] == TurnHubProtocol::encodeInputTiming(3000, 7000));
  assert(audio.mutedSigils() & (1u << 1));
  // Unbinding restores the defaults (and sound) on the next refresh.
  ProfileFixture::bindings.clear();
  for (uint32_t t = 0; t < 8 * 250 + 250; t += 250) updateSigilAccessibility(testNow + 20000 + t);
  assert(audio.mutedSigils() == 0 && &leds.profile(1) == &defaultLedCueProfile());
  assert(fixtureInputTiming[1] == TurnHubProtocol::encodeInputTiming(2000, 5000));
  fixtureRecords[1] = SigilRecord{}; fixtureRecords[2] = SigilRecord{};
  assert(request("/api/session/logout", alice) == 200 && request("/api/session/logout", bob) == 200);
  enterEmptyLobby();
}

static void actionRequiredCues() {
  // Win claim: the next responder's Sigil hears ActionRequired, then the next.
  freshLobby(3); startFromHost();
  clearTones();
  assert(web(0,1,WebControl::ClaimWin)); drainAudio();
  const uint8_t first = player(game.nextWinConfirmationPlayerNumber()).controllerId;
  assert(heardActionRequired(first) && !heardActionRequired(0));
  for (uint8_t id = 0; id < 3; ++id) if (id != first) assert(!heardActionRequired(id));
  clearTones();
  handleActionShort(first); drainAudio();
  const uint8_t second = player(game.nextWinConfirmationPlayerNumber()).controllerId;
  assert(second != first && heardActionRequired(second) && !heardActionRequired(first));
  clearTones();
  handleActionShort(second); drainAudio();
  assert(game.gameOver());
  for (uint8_t id = 0; id < 3; ++id) assert(!heardActionRequired(id));  // Nothing left to decide.
  // A life change request reaches only the recipient, who must approve it.
  handleActionShort(0); assert(hubState == HubState::Lobby);
  startFromHost();
  clearTones();
  TurnHub::IntentPayload payload; payload.targetPlayer = 2; payload.value = -3; String message;
  const uint8_t requester = game.playerByNumber(1)->controllerId;
  const uint8_t recipient = game.playerByNumber(2)->controllerId;
  assert(changeCounter(requester, 1, IntentType::RequestLifeChange, payload, message)); drainAudio();
  assert(heardActionRequired(recipient) && !heardActionRequired(requester));
  game.cancelLifeChanges();
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
  // Any seated player may set the timer (no table host).
  assert(request("/api/game/settings", guest, {{"turnTimerMs", "90000"}}) == 200);
  assert(nextGameSettings.turnTimerMs == 90000);
  assert(request("/api/game/settings", guest, {{"turnTimerMs", "0"}}) == 200);
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
// Drives the touchscreen adapter with screen-coordinate samples.
static const TouchButton *screenButton(const AtlasScreen &screen, TouchAction action) {
  for (uint8_t i=0;i<screen.buttonCount;++i) if (screen.buttons[i].action==action) return &screen.buttons[i];
  return nullptr;
}
static bool startsWith(const char *text,const char *prefix) { return strncmp(text,prefix,strlen(prefix))==0; }
static AtlasScreen currentScreen() { AtlasScreen s; buildAtlasScreen(testNow,s); return s; }
static int16_t lastTouchX=0, lastTouchY=0;
static void touchAt(int16_t x,int16_t y) { lastTouchX=x; lastTouchY=y; updateTouchControls(testNow,true,x,y); }
// The finger stays where it is, even after its button has gone.
static void keepPressing() { touchAt(lastTouchX,lastTouchY); }
static void touchRelease() { testNow+=TOUCH_RELEASE_MS; updateTouchControls(testNow,false,0,0); }
static void pressButton(TouchAction action) {
  const TouchButton *b=screenButton(currentScreen(),action); assert(b!=nullptr);
  touchAt(b->x+b->w/2,b->y+b->h/2);
}
static void tapButton(TouchAction action) { pressButton(action); testNow+=30; pressButton(action); touchRelease(); }
// End match and Master pass live on the in-game Table screen.
static void openTableScreen() {
  if (!screenButton(currentScreen(),TouchAction::EndMatch)) tapButton(TouchAction::OpenTable);
}
// Holds the on-screen End match button for the full hold, then lets go.
static void holdEndMatch() {
  openTableScreen();
  pressButton(TouchAction::EndMatch); testNow+=END_MATCH_HOLD_MS; pressButton(TouchAction::EndMatch); touchRelease();
}
// A player's own PASS from their Sigil (the touchscreen no longer passes).
static void seatPass() {
  assert(dispatchSeatIntent(IntentType::Pass,IntentOrigin::PhysicalSigil,*game.activePlayer()).accepted());
}

// Touch calibration math: solve from four simulated presses, then map.
static void touchCalibrationMath() {
  const int16_t W=ATLAS_SCREEN_WIDTH, H=ATLAS_SCREEN_HEIGHT;
  // Simulated panels: raw = offset + scale * pixel on each channel, optionally
  // swapped. The second has a different Y offset and scale from the shipped
  // defaults, like the first E32R28T, whose touches landed low.
  struct Panel { bool swap; int32_t x0,xStep100,y0,yStep100; };
  for (const Panel &p : {Panel{false,3700,-1094,3800,-1483}, Panel{false,3700,-1094,3500,-1400},
                         Panel{true,300,1100,3900,-1500}}) {
    uint16_t rawX[TOUCH_CAL_POINTS], rawY[TOUCH_CAL_POINTS];
    auto rawAt=[&](int16_t sx,int16_t sy,uint16_t &rx,uint16_t &ry){
      const int32_t a=p.x0+p.xStep100*sx/100, b=p.y0+p.yStep100*sy/100;
      rx=static_cast<uint16_t>(p.swap?b:a); ry=static_cast<uint16_t>(p.swap?a:b);
    };
    for (uint8_t i=0;i<TOUCH_CAL_POINTS;++i) {
      int16_t tx,ty; touchCalibrationTarget(i,W,H,tx,ty); rawAt(tx,ty,rawX[i],rawY[i]);
    }
    TouchCalibration cal; assert(solveTouchCalibration(rawX,rawY,W,H,cal));
    assert(cal.swapXY==(p.swap?1:0) && validTouchCalibration(cal));
    for (int16_t sy : {0,60,104,134,164,239}) for (int16_t sx : {0,8,160,311,319}) {
      uint16_t rx,ry; rawAt(sx,sy,rx,ry); int16_t mx,my; mapTouch(cal,rx,ry,W,H,mx,my);
      assert(abs(mx-sx)<=2 && abs(my-sy)<=2);
    }
  }
  // Presses that do not span the panel, or axes that do not separate, are refused.
  uint16_t same[TOUCH_CAL_POINTS]={2000,2000,2000,2000};
  TouchCalibration untouched; assert(!solveTouchCalibration(same,same,W,H,untouched));
  uint16_t diagX[TOUCH_CAL_POINTS]={500,3500,3500,500}, diagY[TOUCH_CAL_POINTS]={500,3500,3500,500};
  assert(!solveTouchCalibration(diagX,diagY,W,H,untouched));
  // The shipped defaults are a valid fallback, and mapping clamps to the screen.
  const TouchCalibration fallback=defaultTouchCalibration(); assert(validTouchCalibration(fallback));
  int16_t mx,my; mapTouch(fallback,0,65535,W,H,mx,my); assert(mx>=0&&mx<W&&my>=0&&my<H);
  // Recalibration may only take over the screen in the lobby.
  freshLobby(2); assert(touchCalibrationAllowed());
  startFromHost(); assert(!touchCalibrationAllowed()); enterEmptyLobby();
}

// An OLED Sigil (CAPABILITY_DISPLAY_OLED) seats one player: Atlas refuses its
// Seat B and will not start while one is left over from before it said so.
static void oledSigilSeatsOnePlayer() {
  using TurnHubProtocol::CAPABILITY_DISPLAY_OLED;
  freshLobby(2);
  TurnHub::fixtureRecords[1].capabilities=CAPABILITY_DISPLAY_OLED;
  assert(sigilSeatsOnePlayer(1) && !sigilSeatsOnePlayer(0) && !sigilSeatsOnePlayer(MAX_PHYSICAL_SIGILS));
  // The Action + PASS chord and a direct Seat B join are both refused.
  handleActionDown(1); handlePass(1); handleActionUp(1); handleActionShort(1);
  assert(!lobby.hasSecondary(1) && lobby.playerCount()==2);
  IntentResult refused=dispatchModuleIntent(IntentType::Join,1,2);
  assert(refused.status==IntentStatus::Conflict && String(refused.message)==ONE_PLAYER_SIGIL_MESSAGE);
  assert(!lobby.hasSecondary(1));
  // An e-paper Sigil still shares.
  assert(dispatchModuleIntent(IntentType::Join,0,2).accepted() && lobby.hasSecondary(0));
  // Seat B joined before the Sigil reported OLED (e.g. reflashed while seated)
  // blocks the start until it leaves; leaving is always allowed.
  TurnHub::fixtureRecords[0].capabilities=CAPABILITY_DISPLAY_OLED;
  assert(dispatchModuleIntent(IntentType::ArmStart,0).status==IntentStatus::Conflict);
  assert(dispatchModuleIntent(IntentType::Leave,0,2).accepted() && !lobby.hasSecondary(0));
  assert(dispatchModuleIntent(IntentType::ArmStart,0).accepted());
  TurnHub::fixtureRecords[0].capabilities=0; TurnHub::fixtureRecords[1].capabilities=0;
  enterEmptyLobby();
}

static void touchControls() {
  resetTouchControls(); freshLobby(2); TurnHub::fixtureRadio=true; pairingActive=false;
  AtlasScreen s=currentScreen();
  assert(String(s.title)=="Lobby" && s.buttonCount==4 && screenButton(s,TouchAction::StartGame) &&
      screenButton(s,TouchAction::Pair) && screenButton(s,TouchAction::OpenQr) && screenButton(s,TouchAction::OpenInfo));
  // Every button fits on screen and meets the 44 px minimum target size.
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.y>=0 && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);

  // Touches outside a button, or sliding off one, do nothing.
  touchAt(4,4); touchRelease(); assert(!pairingActive);
  pressButton(TouchAction::Pair); touchAt(4,4); touchRelease(); assert(!pairingActive);
  // A brief resistive drop-out is not a release; the tap acts once on release.
  pressButton(TouchAction::Pair); updateTouchControls(testNow+TOUCH_RELEASE_MS-1,false,0,0);
  assert(!pairingActive && currentScreen().pressed==TouchAction::Pair);
  pressButton(TouchAction::Pair); touchRelease();
  assert(pairingActive && currentScreen().pressed==TouchAction::None);
  s=currentScreen();
  assert(startsWith(s.detail,"Pairing open: ") && String(s.notice)=="Pairing window opened");
  testNow+=TOUCH_NOTICE_MS; assert(currentScreen().notice[0]=='\0');
  testNow+=pairingWindowMs; updatePairingWindow(testNow); assert(!pairingActive);
  assert(String(currentScreen().detail)=="2 players, 0 Sigils");
  // A press drifting just past the edge (resistive jitter, a rolling
  // fingertip) still counts; beyond the slop it cancels.
  { const TouchButton *pair=screenButton(currentScreen(),TouchAction::Pair);
    const int16_t px=pair->x+pair->w/2, py=pair->y+pair->h/2, edge=pair->y-1;
    touchAt(px,py); touchAt(px,edge-TOUCH_SLOP_PX+2); touchRelease(); assert(pairingActive);
    testNow+=pairingWindowMs; updatePairingWindow(testNow); assert(!pairingActive);
    touchAt(px,py); touchAt(px,edge-TOUCH_SLOP_PX); touchRelease(); assert(!pairingActive);
    // A press that starts in the slop, off every button, does nothing.
    touchAt(px,edge); touchRelease(); assert(!pairingActive);
    testNow+=TOUCH_NOTICE_MS; }
  // Pairing still goes through its handler: a radio failure is reported, not hidden.
  TurnHub::fixtureRadio=false; tapButton(TouchAction::Pair);
  assert(!pairingActive && String(currentScreen().notice)=="Radio unavailable");
  TurnHub::fixtureRadio=true;

  // A presence code a phone asked for shows over the screen, with only Cancel;
  // Cancel or its expiry takes it away. The code never becomes a notice.
  testNow+=TOUCH_NOTICE_MS;
  assert(requestPresenceCode(String("ABCDEFGH"),false,testNow));
  s=currentScreen();
  assert(s.kind==ScreenKind::Code && String(s.badge)=="VERIFY" && strlen(s.code)==7 && s.code[3]==' ' &&
      startsWith(s.qr,"http://192.168.4.1/portal#code=") && s.buttonCount==1 && screenButton(s,TouchAction::CancelCode));
  tapButton(TouchAction::CancelCode);
  assert(pendingPresenceCode(testNow)==nullptr && currentScreen().kind==ScreenKind::Status);
  assert(requestPresenceCode(String("ABCDEFGH"),true,testNow) && String(currentScreen().badge)=="SETUP");
  testNow+=PRESENCE_CODE_MS; updatePairingWindow(testNow);
  assert(currentScreen().kind==ScreenKind::Status && !anyPresenceActive(testNow));
  testNow+=TOUCH_NOTICE_MS;

  // Running: just Pause and Table. Players pass from their own seats; the
  // touchscreen shows a pending PASS but has no Pass button of its own.
  startFromHost(); s=currentScreen();
  assert(startsWith(s.title,"Player ") && s.buttonCount==2 &&
      screenButton(s,TouchAction::Pause) && screenButton(s,TouchAction::OpenTable) &&
      !screenButton(s,TouchAction::EndMatch) && !screenButton(s,TouchAction::MasterPass));
  seatPass(); assert(String(currentScreen().detail)=="Pass pending: that seat can cancel");
  testNow+=PASS_GRACE_MS; updatePendingPass(testNow);
  tapButton(TouchAction::Pause); assert(hubState==HubState::Paused);
  s=currentScreen();
  assert(String(s.title)=="Paused" && s.buttonCount==2 && screenButton(s,TouchAction::Resume) &&
      screenButton(s,TouchAction::OpenTable));
  // Paused, the Table screen offers End match but not the master pass.
  tapButton(TouchAction::OpenTable); s=currentScreen();
  assert(s.kind==ScreenKind::Table && String(s.detail)=="Resume to use Master pass" &&
      !screenButton(s,TouchAction::MasterPass) && screenButton(s,TouchAction::EndMatch)->hold());
  tapButton(TouchAction::CloseScreen); tapButton(TouchAction::Resume); assert(hubState==HubState::Running);

  // End match needs the full hold, shows a countdown, and acts once.
  tapButton(TouchAction::OpenTable); s=currentScreen();
  assert(s.kind==ScreenKind::Table && String(s.badge)=="TABLE" && s.buttonCount==3 &&
      screenButton(s,TouchAction::MasterPass)->holdMs==MASTER_PASS_HOLD_MS &&
      screenButton(s,TouchAction::EndMatch)->holdMs==END_MATCH_HOLD_MS && screenButton(s,TouchAction::CloseScreen));
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.y>=SCREEN_BODY_Y && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);
  tapButton(TouchAction::EndMatch);
  assert(hubState==HubState::Running && String(currentScreen().notice)=="Keep holding for 5 s to end the match");
  completedGames=0;
  pressButton(TouchAction::EndMatch); testNow+=END_MATCH_HOLD_MS-1; pressButton(TouchAction::EndMatch);
  s=currentScreen(); assert(s.pressed==TouchAction::EndMatch && s.holdSecondsLeft==1);
  assert(hubState==HubState::Running);
  testNow+=1; pressButton(TouchAction::EndMatch);
  assert(hubState==HubState::GameOver && game.endedInDraw() && completedGames==1);
  touchAt(160,120); testNow+=5000; touchAt(160,120); touchRelease();
  assert(completedGames==1);
  s=currentScreen();
  assert(s.kind==ScreenKind::Status && String(s.title)=="Game over" && String(s.detail)=="The match ended in a draw" &&
      s.buttonCount==4 && screenButton(s,TouchAction::Rematch) && screenButton(s,TouchAction::ResetTable) &&
      screenButton(s,TouchAction::OpenQr) && screenButton(s,TouchAction::OpenInfo));

  // A press whose button disappears before release does nothing.
  enterEmptyLobby(); freshLobby(2); startFromHost();
  pressButton(TouchAction::Pause); assert(web(0,1,WebControl::PauseResume) && hubState==HubState::Paused);
  touchRelease(); assert(hubState==HubState::Paused);
  resetTouchControls(); enterEmptyLobby();
}

// The status screen's chips, the NO SD CARD warning, and the Info and QR
// screens, which change no table state.
extern bool fixtureSdCardReady;
static void atlasScreens() {
  resetTouchControls(); enterEmptyLobby(); pairingActive=false;
  AtlasScreen s=currentScreen();
  assert(s.kind==ScreenKind::Status && String(s.badge)=="LOBBY" && s.playerCount==0 && !s.sdMissing);
  assert(String(s.qr)=="http://192.168.4.1/portal" && s.lineCount>0);  // Empty table: how to join.
  fixtureSdCardReady=false; assert(currentScreen().sdMissing); fixtureSdCardReady=true;

  freshLobby(3); s=currentScreen();
  assert(s.playerCount==3 && s.qr[0]=='\0' && !s.showLife);
  assert(String(s.players[0].name)=="Player 1");
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);

  tapButton(TouchAction::OpenInfo); s=currentScreen();
  assert(s.kind==ScreenKind::Info && String(s.title)=="Table info" && s.lineCount==5 &&
      screenButton(s,TouchAction::OpenQr) && screenButton(s,TouchAction::CloseScreen));
  fixtureSdCardReady=false; assert(String(currentScreen().lines[3])=="SD card: NOT INSERTED"); fixtureSdCardReady=true;
  tapButton(TouchAction::OpenQr); s=currentScreen();
  assert(s.kind==ScreenKind::Qr && String(s.qr)=="http://192.168.4.1/portal" &&
      screenButton(s,TouchAction::QrPortal)->selected && !screenButton(s,TouchAction::QrWifi)->selected);
  tapButton(TouchAction::QrWifi); s=currentScreen();
  // The shipped default password is public, so its Wi-Fi code shows freely.
  assert(String(s.qr)=="WIFI:T:WPA;S:TurnHub-Atlas;P:TurnHub-Setup;;" && screenButton(s,TouchAction::QrWifi)->selected);
  tapButton(TouchAction::QrSignIn); assert(String(currentScreen().qr)=="http://192.168.4.1/login");
  assert(hubState==HubState::Lobby && lobby.playerCount()==3);  // Screens change no table state.
  tapButton(TouchAction::CloseScreen); assert(currentScreen().kind==ScreenKind::Status);

  startFromHost(); s=currentScreen();
  assert(String(s.badge)=="PLAYING" && s.showLife && s.playerCount==3 && s.clock[0]!='\0' && s.timerPermille==-1);
  uint8_t active=0;
  for (uint8_t i=0;i<s.playerCount;++i) {
    assert(s.players[i].life==game.lifeTotal(s.players[i].number));
    if (s.players[i].flags&CHIP_ACTIVE) { ++active; assert(s.players[i].number==game.activePlayerNumber()); }
  }
  assert(active==1);
  char title[32]; snprintf(title,sizeof(title),"Player %u's turn",static_cast<unsigned>(game.activePlayerNumber()));
  assert(String(s.title)==title);
  resetTouchControls(); enterEmptyLobby();
}

// A connected test harness (CAPABILITY_HARNESS) adds Tests to the lobby; its
// screen starts premade tests over the radio and shows progress in words.
static void harnessScreen() {
  using namespace TurnHubProtocol;
  resetTouchControls(); resetHarnessLink(); freshLobby(2); pairingActive=false;
  AtlasScreen s=currentScreen();
  assert(!screenButton(s,TouchAction::OpenTests) && harnessSigilId(testNow)==INVALID_ID);
  // A report from an ordinary Sigil is ignored.
  HarnessReportFields running; running.state=HarnessRunState::Running;
  running.test=static_cast<uint8_t>(HarnessTest::FullGame); running.step=static_cast<uint8_t>(HarnessStep::Turn);
  running.passed=12;
  noteHarnessReport(2,encodeHarnessReport(running),testNow); HarnessReportFields r;
  assert(!harnessReport(testNow,r));

  TurnHub::SigilRecord &rec=TurnHub::fixtureRecords[2];
  rec.helloInfoValid=true; rec.capabilities=CAPABILITY_MENU|CAPABILITY_HARNESS;
  assert(harnessSigilId(testNow)==2);
  s=currentScreen();
  const TouchButton *pair=screenButton(s,TouchAction::Pair), *tests=screenButton(s,TouchAction::OpenTests);
  assert(pair && tests && pair->w>=44 && tests->w>=44 && tests->x>=pair->x+pair->w);
  tapButton(TouchAction::OpenTests); s=currentScreen();
  assert(String(s.title)=="Test harness" && String(s.detail)=="Ready: pick a test" && s.buttonCount==6);
  for (const TouchButton &b : s.buttons)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);

  TurnHub::fixtureHarnessCommands=0; tapButton(TouchAction::RunFullGame);
  assert(TurnHub::fixtureHarnessCommands==1 && harnessCommandKind(TurnHub::fixtureHarnessCommand)==static_cast<uint8_t>(HarnessCommandKind::Run) &&
      harnessCommandTest(TurnHub::fixtureHarnessCommand)==static_cast<uint8_t>(HarnessTest::FullGame));
  assert(hubState==HubState::Lobby);  // Atlas changes nothing itself; the harness plays.

  noteHarnessReport(2,encodeHarnessReport(running),testNow); s=currentScreen();
  assert(String(s.title)=="4-player game" && String(s.detail)=="TURN, 12 ok" &&
      s.buttonCount==2 && screenButton(s,TouchAction::StopTest) && screenButton(s,TouchAction::CloseTests));
  tapButton(TouchAction::StopTest);
  assert(harnessCommandKind(TurnHub::fixtureHarnessCommand)==static_cast<uint8_t>(HarnessCommandKind::Stop));
  HarnessReportFields failed=running; failed.state=HarnessRunState::Failed; failed.failed=1;
  noteHarnessReport(2,encodeHarnessReport(failed),testNow);
  assert(String(currentScreen().detail)=="FAILED at TURN");
  // The screen stays up while the harness plays, then a stale report lapses.
  startFromHost(); assert(String(currentScreen().detail)=="FAILED at TURN");
  testNow+=HARNESS_REPORT_STALE_MS; assert(String(currentScreen().detail)=="Ready: pick a test");
  tapButton(TouchAction::CloseTests); assert(!screenButton(currentScreen(),TouchAction::StopTest));
  assert(String(currentScreen().title)!="Test harness");
  enterEmptyLobby();
  // Without a harness the Tests button goes, and an open test screen offers only Back.
  tapButton(TouchAction::OpenTests); rec.capabilities=CAPABILITY_MENU; s=currentScreen();
  assert(String(s.detail)=="Harness offline" && s.buttonCount==1 && screenButton(s,TouchAction::CloseTests));
  tapButton(TouchAction::CloseTests); assert(!screenButton(currentScreen(),TouchAction::OpenTests));
  rec.helloInfoValid=false; rec.capabilities=0; resetTouchControls(); resetHarnessLink();
}

// The touchscreen between games: Start (two or more players), Cancel start,
// Rematch and Reset, all as table actions from the Atlas hardware.
static void touchTableLifecycle() {
  resetTouchControls(); freshLobby(1); pairingActive=false;
  assert(!screenButton(currentScreen(),TouchAction::StartGame));
  Intent start; start.type=IntentType::StartGame; start.actor.origin=IntentOrigin::AtlasHardware;
  assert(intents.dispatch(start).status==IntentStatus::InvalidState && hubState==HubState::Lobby);
  freshLobby(3); tapButton(TouchAction::StartGame);
  assert(hubState==HubState::Starting);
  AtlasScreen s=currentScreen();
  assert(s.buttonCount==1 && screenButton(s,TouchAction::CancelStart));
  tapButton(TouchAction::CancelStart); assert(hubState==HubState::Lobby && lobby.playerCount()==3);
  // Only the touchscreen skips the seat and arming: an unseated browser cannot.
  Intent stranger=start; stranger.actor.origin=IntentOrigin::Browser;
  stranger.actor.controllerId=MAX_PHYSICAL_SIGILS; stranger.actor.slot=1;
  assert(!intents.dispatch(stranger).accepted() && hubState==HubState::Lobby);
  tapButton(TouchAction::StartGame); testNow+=START_COUNTDOWN_MS; updateCountdown(testNow);
  assert(hubState==HubState::Running);
  // Rematch and Reset only after a game.
  for (IntentType type : {IntentType::Rematch, IntentType::ResetGame}) {
    Intent early; early.type=type; early.actor.origin=IntentOrigin::AtlasHardware;
    assert(intents.dispatch(early).status==IntentStatus::InvalidState && hubState==HubState::Running);
  }
  holdEndMatch(); assert(hubState==HubState::GameOver);
  tapButton(TouchAction::Rematch); assert(hubState==HubState::Lobby && lobby.playerCount()==3);
  tapButton(TouchAction::StartGame); testNow+=START_COUNTDOWN_MS; updateCountdown(testNow);
  holdEndMatch(); assert(hubState==HubState::GameOver);
  tapButton(TouchAction::ResetTable); assert(hubState==HubState::Lobby && lobby.playerCount()==0);
  resetTouchControls(); enterEmptyLobby();
}

// Master pass: the Table screen's hold passes a stuck turn at once (no
// grace), logged as a master pass. Atlas hardware only, running games only,
// never over an open table decision.
static void masterPass() {
  resetTouchControls(); freshLobby(3); startFromHost();
  Intent master; master.type=IntentType::MasterPass; master.actor.origin=IntentOrigin::AtlasHardware;
  for (auto origin : {IntentOrigin::Browser, IntentOrigin::AndroidApp, IntentOrigin::PhysicalSigil,
                      IntentOrigin::Simulator, IntentOrigin::System}) {
    Intent other=master; other.actor.origin=origin;
    assert(intents.dispatch(other).status==IntentStatus::Unauthorized);
  }
  const uint8_t first=game.activePlayerNumber();
  // A short press only explains; the full hold passes once, immediately, and
  // returns to the status screen.
  tapButton(TouchAction::OpenTable);
  char detail[48]; snprintf(detail,sizeof(detail),"Stuck turn? Master pass skips Player %u",static_cast<unsigned>(first));
  assert(String(currentScreen().detail)==detail);
  tapButton(TouchAction::MasterPass);
  assert(game.activePlayerNumber()==first && String(currentScreen().notice)=="Keep holding for 2 s to pass this turn");
  const size_t activityBefore=TurnHub::Diagnostics::activityLog().count;
  pressButton(TouchAction::MasterPass); testNow+=MASTER_PASS_HOLD_MS-1; pressButton(TouchAction::MasterPass);
  assert(game.activePlayerNumber()==first);
  testNow+=1; keepPressing();
  const uint8_t second=game.activePlayerNumber();
  assert(second!=first && !pendingPass.active && hubState==HubState::Running);
  assert(currentScreen().kind==ScreenKind::Status && String(currentScreen().notice)=="Master pass: turn passed");
  testNow+=5000; keepPressing(); touchRelease(); assert(game.activePlayerNumber()==second);
  { const TurnHub::Diagnostics::ActivityLog &log=TurnHub::Diagnostics::activityLog();
    assert(log.count==activityBefore+1 || log.count==TurnHub::Diagnostics::ACTIVITY_CAPACITY);
    const TurnHub::Diagnostics::ActivityEvent &last=log.entries[(log.next+TurnHub::Diagnostics::ACTIVITY_CAPACITY-1)%TurnHub::Diagnostics::ACTIVITY_CAPACITY];
    assert(String(last.kind)=="master_pass" && startsWith(last.message,"player=")); }
  // It overrides a queued PASS: one turn passes, not two.
  seatPass(); assert(pendingPass.active);
  assert(intents.dispatch(master).accepted());
  const uint8_t third=game.activePlayerNumber();
  assert(third!=second && !pendingPass.active);
  testNow+=PASS_GRACE_MS; updatePendingPass(testNow); assert(game.activePlayerNumber()==third);
  // Not while paused, nor over a win claim.
  assert(web(0,1,WebControl::PauseResume) && hubState==HubState::Paused);
  assert(intents.dispatch(master).status==IntentStatus::InvalidState);
  assert(web(0,1,WebControl::PauseResume) && hubState==HubState::Running);
  const PlayerSeat claimant=*game.activePlayer();
  assert(web(claimant.controllerId,claimant.slot,WebControl::ClaimWin) && game.hasWinClaim());
  assert(!intents.dispatch(master).accepted() && game.hasWinClaim());
  // The Table screen closes with the match.
  holdEndMatch(); assert(hubState==HubState::GameOver && currentScreen().kind==ScreenKind::Status);
  resetTouchControls(); enterEmptyLobby();
}

static void endMatchAsDraw() {
  using TurnHubProfiles::LastGameResult;
  // Only a match in progress, and only the Atlas hardware, can end it.
  resetTouchControls(); freshLobby(2);
  Intent end; end.type=IntentType::EndMatch; end.actor.origin=IntentOrigin::AtlasHardware;
  assert(!intents.dispatch(end).accepted() && hubState==HubState::Lobby);
  assert(lobby.playerCount()==2 && !screenButton(currentScreen(),TouchAction::EndMatch));
  startFromHost();
  for (auto origin : {IntentOrigin::Browser, IntentOrigin::AndroidApp, IntentOrigin::PhysicalSigil,
                      IntentOrigin::Simulator, IntentOrigin::System}) {
    Intent other=end; other.actor.origin=origin;
    assert(intents.dispatch(other).status==IntentStatus::Unauthorized);
  }
  assert(hubState==HubState::Running && !game.gameOver());

  // The full hold overrides a queued PASS and ends the match once, as a draw.
  seatPass(); assert(pendingPass.active);
  openTableScreen(); pressButton(TouchAction::EndMatch); testNow+=END_MATCH_HOLD_MS-1; pressButton(TouchAction::EndMatch);
  assert(hubState==HubState::Running && pendingPass.active);
  testNow+=1; keepPressing();
  assert(hubState==HubState::GameOver && game.endedInDraw() && game.winnerPlayerNumber()==0);
  assert(!pendingPass.active && completedGames==1);
  testNow+=10000; keepPressing(); touchRelease();
  assert(hubState==HubState::GameOver && completedGames==1 && !pendingPass.active);
  assert(!intents.dispatch(end).accepted() && completedGames==1);

  // A draw survives recovery validation (it used to be rejected as corrupt,
  // which would have locked the recovery store) and restores as a draw.
  TurnHub::GameCheckpoint saved; game.checkpoint(saved,testNow);
  assert(saved.over && saved.winner==0 && TurnHub::validCheckpoint(saved));
  GameEngine restored; assert(restored.restoreCheckpoint(saved,testNow));
  assert(restored.gameOver() && restored.endedInDraw());
  saved.over=false; saved.winner=1; assert(!TurnHub::validCheckpoint(saved));

  // It overrides an open win claim, and works from a paused (e.g. recovered) match.
  freshLobby(2); startFromHost();
  assert(web(0,1,WebControl::ClaimWin) && game.hasWinClaim() && hubState==HubState::Paused);
  holdEndMatch();
  assert(game.endedInDraw() && !game.hasWinClaim() && completedGames==1);

  // Statistics: every player gets a game played and a Draw, not a win or loss;
  // a player who conceded first keeps Eliminated.
  enterEmptyLobby(); TurnHub::fixtureRadio=false; completedGames=0;
  String aId,bId,cId;
  const String a=registerPhone("Draw one",aId),b=registerPhone("Draw two",bId),c=registerPhone("Draw three",cId);
  assert(request("/api/session/join",a)==200 && request("/api/session/join",b)==200 &&
      request("/api/session/join",c)==200);
  assert(request("/api/control/start",a)==200); testNow+=3000; updateCountdown(testNow);
  assert(hubState==HubState::Running);
  assert(request("/api/control/concede",c)==200 && hubState==HubState::Running);
  assert(request("/api/control/pause",b)==200 && hubState==HubState::Paused);
  holdEndMatch();
  assert(hubState==HubState::GameOver && game.endedInDraw() && completedGames==1);
  for (const String *id : {&aId,&bId,&cId}) {
    const auto &stats=ProfileFixture::profiles[id->c_str()].stats;
    assert(stats.gamesPlayed==1 && stats.gamesWon==0);
  }
  assert(ProfileFixture::profiles[aId.c_str()].stats.lastGameResult==LastGameResult::Draw);
  assert(ProfileFixture::profiles[bId.c_str()].stats.lastGameResult==LastGameResult::Draw);
  assert(ProfileFixture::profiles[cId.c_str()].stats.lastGameResult==LastGameResult::Eliminated);
  assert(String(TurnHubProfileStats::resultName(LastGameResult::Draw))=="Draw");
  assert(request("/api/v1/state",a,{},HTTP_GET)==200);
  assert(server.body.find("\"state\":\"GAME_OVER\"")!=std::string::npos &&
      server.body.find("\"winnerPlayer\":null")!=std::string::npos);
  assert(request("/api/control/rematch",a)==200 && hubState==HubState::Lobby);
  assert(request("/api/control/reset",a)==200);
  enterEmptyLobby();
}

static void deviceManagement() {
  TurnHubWebApi::configureDevices(manageDevices, []() { return pairingWindowMs; });
  freshLobby(2);
  String adminId,playerId;
  const String admin=registerPhone("Device admin",adminId),player=registerPhone("Device player",playerId);
  TurnHubAccounts::Account account; account.permissions=TurnHubAccounts::Admin;
  assert(TurnHubAccounts::save(adminId,account));

  // Admin only, re-checked by the Intent handler as well as the route.
  assert(request("/api/device/forget",player,{{"module","3"}})==403 && sigilBus.record(3));
  assert(request("/api/pairing",player,{},HTTP_GET)==403);
  assert(request("/api/pairing",player,{{"windowMs","30000"}})==403);
  Intent forged; forged.type=IntentType::ForgetPairing; forged.actor.origin=IntentOrigin::Browser;
  strncpy(forged.payload.moderatorId,playerId.c_str(),8); forged.payload.value=3;
  assert(intents.dispatch(forged).status==IntentStatus::Unauthorized && sigilBus.record(3));
  strncpy(forged.payload.moderatorId,adminId.c_str(),8); forged.actor.origin=IntentOrigin::PhysicalSigil;
  assert(intents.dispatch(forged).status==IntentStatus::Unauthorized && sigilBus.record(3));

  // A Sigil with seated players is kept, alone or in "forget all".
  assert(request("/api/device/forget",admin,{{"module","0"}})==409 && sigilBus.record(0));
  assert(request("/api/device/forget",admin,{{"all","1"}})==409);
  for (uint8_t id=0;id<MAX_PHYSICAL_SIGILS;++id) assert(sigilBus.record(id));
  assert(request("/api/device/forget",admin)==400);
  assert(request("/api/device/forget",admin,{{"module","99"}})==409);

  // An idle Sigil is forgotten and told so; a second request finds nothing.
  assert(request("/api/device/forget",admin,{{"module","3"}})==200);
  assert(!sigilBus.record(3) && TurnHub::fixtureUnpairs[3]==1);
  assert(request("/api/device/forget",admin,{{"module","3"}})==409 && TurnHub::fixtureUnpairs[3]==1);

  // Never during a match; a storage failure keeps the pairing.
  startFromHost();
  assert(request("/api/device/forget",admin,{{"module","4"}})==409 && sigilBus.record(4));
  enterEmptyLobby();
  TurnHub::fixtureForgetFails=true;
  assert(request("/api/device/forget",admin,{{"module","4"}})==409 && sigilBus.record(4));
  TurnHub::fixtureForgetFails=false;
  assert(request("/api/device/forget",admin,{{"all","1"}})==200);
  for (uint8_t id=0;id<MAX_PHYSICAL_SIGILS;++id) assert(!sigilBus.record(id));
  assert(request("/api/device/forget",admin,{{"all","1"}})==409);

  // Pairing window: 15 s default, admins choose 15/30/60 s, Atlas uses it.
  assert(request("/api/pairing",admin,{},HTTP_GET)==200);
  assert(server.body.find("\"windowMs\":15000")!=std::string::npos &&
      server.body.find("\"sigilWindowMs\":15000")!=std::string::npos &&
      server.body.find("\"choicesMs\":[15000,30000,60000]")!=std::string::npos);
  for (const char *bad : {"20000","0","-15000","600000","abc"}) {
    assert(request("/api/pairing",admin,{{"windowMs",bad}})==409 && pairingWindowMs==15000);
  }
  assert(request("/api/pairing",admin)==400);
  assert(request("/api/pairing",admin,{{"windowMs","30000"}})==200 && pairingWindowMs==30000);
  assert(TurnHub::fixturePairingWindowSaved==30000);
  ProfileFixture::gameSettingsWritable=false;
  assert(request("/api/pairing",admin,{{"windowMs","60000"}})==409 && pairingWindowMs==30000);
  ProfileFixture::gameSettingsWritable=true;

  freshLobby(2); pairingActive=false;
  Intent pair; pair.type=IntentType::PairRequest; pair.actor.origin=IntentOrigin::AtlasHardware;
  assert(intents.dispatch(pair).accepted() && TurnHub::fixturePairingWindowMs==30000);
  testNow+=29999; updatePairingWindow(testNow); assert(pairingActive);
  ++testNow; updatePairingWindow(testNow); assert(!pairingActive);
  assert(request("/api/pairing",admin,{{"windowMs","15000"}})==200 && pairingWindowMs==15000);
  enterEmptyLobby();
}

// Atlas's speaker plays table-wide cues, including for a table with no
// Sigil; lobby feedback stays on Sigils. Volume 0 silences only the speaker,
// and muting every Sigil leaves it. Admins set the volume through an Intent.
struct FakeSpeaker final : TurnHub::ToneOutput {
  std::vector<uint8_t> volumes;
  void tone(uint16_t,uint16_t,uint8_t volume) override { volumes.push_back(volume); }
};
static void playQueuedAudio() { for (int i=0;i<300;++i) { testNow+=20; audio.update(testNow); } }
static void passActiveTurn() {
  const PlayerSeat *active=game.activePlayer(); assert(active);
  assert(dispatchSeatIntent(IntentType::Pass,IntentOrigin::AtlasHardware,*active).accepted());
  testNow+=PASS_GRACE_MS; updatePendingPass(testNow); playQueuedAudio();
}
static void atlasSpeaker() {
  FakeSpeaker speaker;
  audio.clear(); audio.setSpeaker(&speaker); audio.setSpeakerVolume(TurnHub::DEFAULT_SPEAKER_VOLUME);
  freshLobby(2); playQueuedAudio();
  assert(speaker.volumes.empty());  // Joining is Sigil feedback only.
  startFromHost(); playQueuedAudio();
  assert(!speaker.volumes.empty());  // Countdown and game start.
  for (uint8_t v : speaker.volumes) assert(v==TurnHub::DEFAULT_SPEAKER_VOLUME);
  speaker.volumes.clear(); passActiveTurn(); assert(!speaker.volumes.empty());
  audio.setMutedSigils(0xFF); speaker.volumes.clear(); passActiveTurn();
  assert(!speaker.volumes.empty());
  audio.setMutedSigils(0);
  audio.setSpeakerVolume(0); speaker.volumes.clear();
  const unsigned buzzes=totalBuzzes(); passActiveTurn();
  assert(speaker.volumes.empty() && totalBuzzes()>buzzes);
  enterEmptyLobby(); audio.clear();

  // A phone-only table still hears turn changes on Atlas.
  audio.setSpeakerVolume(3); TurnHub::fixtureRadio=false;
  String aId,bId;
  const String a=registerPhone("Speaker one",aId),b=registerPhone("Speaker two",bId);
  assert(request("/api/session/join",a)==200 && request("/api/session/join",b)==200);
  assert(request("/api/control/start",a)==200); testNow+=3000; updateCountdown(testNow);
  assert(hubState==HubState::Running); playQueuedAudio();
  speaker.volumes.clear(); passActiveTurn();
  assert(!speaker.volumes.empty() && speaker.volumes.back()==3);
  enterEmptyLobby(); audio.clear();
  TurnHub::fixtureRadio=true;

  // Volume setting: Admin only, 0-3, saved, and a storage failure changes nothing.
  TurnHubWebApi::configureDevices(manageDevices, []() { return pairingWindowMs; });
  TurnHubWebApi::configureSpeaker([]() { return audio.speakerVolume(); });
  audio.setSpeakerVolume(TurnHub::DEFAULT_SPEAKER_VOLUME);
  String adminId,playerId;
  const String admin=registerPhone("Speaker admin",adminId),player=registerPhone("Speaker player",playerId);
  TurnHubAccounts::Account account; account.permissions=TurnHubAccounts::Admin;
  assert(TurnHubAccounts::save(adminId,account));
  assert(request("/api/speaker",player,{},HTTP_GET)==403);
  assert(request("/api/speaker",player,{{"volume","3"}})==403);
  assert(request("/api/speaker",admin,{},HTTP_GET)==200);
  assert(server.body.find("\"volume\":2")!=std::string::npos && server.body.find("\"name\":\"medium\"")!=std::string::npos);
  for (const char *bad : {"4","9","-1","abc","","10"}) {
    const int status=request("/api/speaker",admin,{{"volume",bad}});
    assert((status==400 || status==409) && audio.speakerVolume()==2);
  }
  assert(request("/api/speaker",admin)==400);
  assert(request("/api/speaker",admin,{{"volume","0"}})==200 && audio.speakerVolume()==0);
  assert(request("/api/speaker",admin,{{"volume","3"}})==200 && audio.speakerVolume()==3);
  assert(TurnHub::fixtureSpeakerVolumeSaved==3);
  ProfileFixture::gameSettingsWritable=false;
  assert(request("/api/speaker",admin,{{"volume","1"}})==409 && audio.speakerVolume()==3);
  ProfileFixture::gameSettingsWritable=true;
  Intent forged; forged.type=IntentType::ConfigureSpeaker; forged.actor.origin=IntentOrigin::Browser;
  strncpy(forged.payload.moderatorId,playerId.c_str(),8); forged.payload.value=1;
  assert(intents.dispatch(forged).status==IntentStatus::Unauthorized && audio.speakerVolume()==3);

  audio.setSpeaker(nullptr); audio.setSpeakerVolume(0); audio.clear(); enterEmptyLobby();
}

// An Admin at the table (admin unlocked on the Atlas screen) returns the
// table to an empty lobby from the portal; a match in progress ends as a draw.
static void resetTableFromPortal() {
  TurnHubWebApi::configureDevices(manageDevices, []() { return pairingWindowMs; });
  TurnHubWebApi::configurePresence(presenceHooks());
  resetPresence(); resetTouchControls();
  freshLobby(2);
  String adminId,playerId;
  const String admin=registerPhone("Reset admin",adminId),player=registerPhone("Reset player",playerId);
  TurnHubAccounts::Account account; account.permissions=TurnHubAccounts::Admin;
  assert(TurnHubAccounts::save(adminId,account));
  startFromHost(); completedGames=0;

  // Admin only, and only once that Admin is verified at the table (the code
  // the Atlas screen shows). A player cannot even ask for a code.
  assert(request("/api/table/reset",player)==403 && hubState==HubState::Running);
  assert(request("/api/table/reset",admin)==403 && hubState==HubState::Running);
  assert(server.body.find("presenceRequired")!=std::string::npos);
  assert(request("/api/presence/request",player)==403 && pendingPresenceCode(testNow)==nullptr);
  // A wrong code does not verify; five wrong codes cancel it.
  assert(request("/api/presence/request",admin)==200);
  for (int i=0;i<4;++i) assert(request("/api/presence/confirm",admin,{{"code","000000"}})==400);
  assert(request("/api/presence/confirm",admin,{{"code","000000"}})==429 && pendingPresenceCode(testNow)==nullptr);
  assert(!presenceConfirmedFor(adminId,testNow));
  // Only the phone that asked may use the code.
  assert(request("/api/presence/request",admin)==200);
  { char digits[8]; snprintf(digits,sizeof(digits),"%06lu",static_cast<unsigned long>(pendingPresenceCode(testNow)->code));
    assert(request("/api/presence/confirm",player,{{"code",digits}})==409); }
  verifyAtTable(admin);
  assert(request("/api/presence",admin,{},HTTP_GET)==200 && server.body.find("\"verified\":true")!=std::string::npos);
  assert(request("/api/table/reset",player)==403 && hubState==HubState::Running);
  Intent forged; forged.type=IntentType::ResetTable; forged.actor.origin=IntentOrigin::Browser;
  strncpy(forged.payload.moderatorId,playerId.c_str(),8);
  assert(intents.dispatch(forged).status==IntentStatus::Unauthorized && hubState==HubState::Running);

  // A paused match ends as a draw once, and the table empties.
  assert(web(0,1,WebControl::PauseResume) && hubState==HubState::Paused);
  assert(request("/api/table/reset",admin)==200);
  assert(server.body.find("ended as a draw")!=std::string::npos);
  assert(hubState==HubState::Lobby && lobby.playerCount()==0 && !game.hasPlayers() && completedGames==1);

  // From a lobby (or a countdown) it just empties the table.
  handleActionShort(0); handleActionShort(1); completedGames=0; assert(lobby.playerCount()==2);
  handleActionDown(0); handleActionLong(0); handleActionUp(0); assert(hubState==HubState::Starting);
  assert(request("/api/table/reset",admin)==200 && hubState==HubState::Lobby && lobby.playerCount()==0);
  assert(completedGames==0);

  // Verification lapses after PRESENCE_GRANT_MS, even for the Admin.
  handleActionShort(0); handleActionShort(1); testNow+=PRESENCE_GRANT_MS; updatePairingWindow(testNow);
  assert(request("/api/table/reset",admin)==403 && lobby.playerCount()==2);
  strncpy(forged.payload.moderatorId,adminId.c_str(),8);
  assert(intents.dispatch(forged).status==IntentStatus::Unauthorized && lobby.playerCount()==2);
  enterEmptyLobby();
}

// Factory reset from Device Settings: Admin verified at the table (presence code),
// between games. A Sigil is told to erase itself and is forgotten; Atlas
// erases its NVS and restarts after the reply has gone.
extern unsigned fixtureFactoryResets;
static void factoryResetFromPortal() {
  TurnHubWebApi::configureDevices(manageDevices, []() { return pairingWindowMs; });
  TurnHubWebApi::configurePresence(presenceHooks());
  resetPresence(); resetTouchControls();
  freshLobby(2);
  String adminId,playerId;
  const String admin=registerPhone("Factory admin",adminId),player=registerPhone("Factory player",playerId);
  TurnHubAccounts::Account account; account.permissions=TurnHubAccounts::Admin;
  assert(TurnHubAccounts::save(adminId,account));
  TurnHub::fixtureFactoryResetSigil=-1; fixtureFactoryResets=0; TurnHub::fixtureUnpairs[3]=0;

  // Admin only, and only once verified at the table (presence code).
  assert(request("/api/device/factory-reset",admin,{{"module","3"}})==403 && sigilBus.record(3));
  verifyAtTable(admin);
  assert(request("/api/device/factory-reset",player,{{"module","3"}})==403 && sigilBus.record(3));
  Intent forged; forged.type=IntentType::FactoryReset; forged.actor.origin=IntentOrigin::Browser;
  forged.payload.value=3; strncpy(forged.payload.moderatorId,playerId.c_str(),8);
  assert(intents.dispatch(forged).status==IntentStatus::Unauthorized && sigilBus.record(3));
  assert(request("/api/device/factory-reset",admin)==400);

  // A Sigil someone is seated on is refused; a free one is told and forgotten.
  assert(request("/api/device/factory-reset",admin,{{"module","0"}})==409 && sigilBus.record(0));
  assert(request("/api/device/factory-reset",admin,{{"module","3"}})==200);
  assert(TurnHub::fixtureFactoryResetSigil==3 &&
      TurnHub::fixtureFactoryResetValue==TurnHubProtocol::FACTORY_RESET_CONFIRM && !sigilBus.record(3) &&
      TurnHub::fixtureUnpairs[3]==1);
  assert(request("/api/device/factory-reset",admin,{{"module","3"}})==409);  // No longer paired.
  assert(fixtureFactoryResets==0);

  // Never during a match, for Sigils or Atlas.
  startFromHost();
  assert(request("/api/device/factory-reset",admin,{{"atlas","1"}})==409 && !factoryResetScheduled());
  enterEmptyLobby();

  // Atlas: scheduled, then erased once the reply has had time to leave.
  assert(request("/api/device/factory-reset",admin,{{"atlas","1"}})==200 && factoryResetScheduled());
  serviceFactoryReset(millis()); assert(fixtureFactoryResets==0);
  serviceFactoryReset(millis()+2000); assert(fixtureFactoryResets==1 && !factoryResetScheduled());
  serviceFactoryReset(millis()+4000); assert(fixtureFactoryResets==1);
  TurnHub::fixtureUnpairs[3]=0;
  resetPresence();
}

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
  endMatchAsDraw(); std::cout<<"PASS touchscreen hold ends a match as a draw: authorization, overrides, stats once, recovery\n";
  touchTableLifecycle(); std::cout<<"PASS touchscreen Start, Cancel start, Rematch and Reset between games\n";
  masterPass(); std::cout<<"PASS master pass: Table screen hold, Atlas hardware only, immediate, logged, overrides a queued PASS\n";
  touchCalibrationMath(); std::cout<<"PASS touch calibration: solve, swap/invert, offset panel, refusals, clamp, lobby-only\n";
  touchControls(); std::cout<<"PASS touchscreen: Pair, Start, presence code screen/cancel/expiry, Pause/Resume, Table screen, end-match hold, slide-off, drop-out, stale press\n";
  harnessScreen(); std::cout<<"PASS test harness screen: Tests button, premade tests, progress, stop, stale reports, offline\n";
  atlasScreens(); std::cout<<"PASS Atlas screens: player chips, NO SD CARD, info, QR codes (Wi-Fi, portal, sign in), turn clock" << std::endl;
  oledSigilSeatsOnePlayer(); std::cout<<"PASS OLED Sigil seats one player: Seat B refused, e-paper still shares, start blocked by a stale Seat B\n";
  atlasSpeaker(); std::cout<<"PASS Atlas speaker: table-wide cues, phone-only table, Sigil mute independence, admin volume setting\n";
  resetTableFromPortal(); std::cout<<"PASS admin returns the table to an empty lobby: permission, presence code (wrong, too many, other phone, expiry), draw once, countdown\n";
  factoryResetFromPortal(); std::cout<<"PASS factory reset: admin verified at the table, seated/in-game refusal, Sigil told and forgotten, Atlas erase after the reply" << std::endl;
  deviceManagement(); std::cout<<"PASS admin forget one/all Sigils, seated and in-game refusal, storage failure, pairing window setting\n";
  physicalGameDisplay(); std::cout<<"PASS physical game display snapshots, received damage, shared focus, bounds and deduplication\n";
  turnTimerEngine(); std::cout<<"PASS turn timer phases, no automatic pass, pause freeze, rollover, validation and recovery\n";
  ledCueSelection(); std::cout<<"PASS LED cue selection, default styles and profile-only presentation changes\n";
  ledStateTransport(); std::cout<<"PASS LedState transport: one packet per change, anchor age, style, legacy channel peers\n";
  profilePicker(); std::cout<<"PASS Sigil profile picker: gating, pages by name, locked/blocked profiles, stale keys, guest, confirm, policy, closing\n";
  sigilMenus(); std::cout<<"PASS Sigil menus: availability per state, defaults, MenuState revisions, stale choices, SelectAction Intents\n";
  turnTimerCuesAndMute(); std::cout<<"PASS one-shot timer audio cues, pause/resume, re-arm and independent mute\n";
  turnTimerSettingsHttp(); std::cout<<"PASS turn timer settings API, partial update, lobby-only edits and state projection\n";
  accessibilityPreferences(); std::cout<<"PASS per-player accessibility: LED profiles, merge rules, API, Sigil mute, hold-timing radio\n";
  actionRequiredCues(); std::cout<<"PASS ActionRequired reaches only the Sigil whose win confirmation is next\n";
  virtualCapacity(); std::cout<<"PASS virtual capacity and 16-player win confirmation\n";
  serialLogStream();
  serialLogCapture(); std::cout<<"PASS serial log capture, redaction, stream draining, ring overflow and self-describing log lines\n";
  // Must run last: see the comment on gameRecoveryLifecycle().
  gameRecoveryLifecycle(); std::cout<<"PASS interrupted-match recovery: boot load, checkpoint-after-intent, downtime exclusion, corrupt fail-safe\n";
}
