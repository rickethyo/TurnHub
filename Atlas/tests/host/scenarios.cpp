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
#include "avatars.h"
#include "controller_profiles.h"
#include "profile_store.h"
#include "web_api_internal.h"
// Compile the actual application entry point; the handler and adapter
// modules it binds are linked from ../../src, not copied rules.
#include "../../src/main.cpp"
#include "profile_stats_bridge.h"
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
esp_err_t checkpointReadError = ESP_OK;

// Only hardware/transport/presentation boundaries are replaced.
namespace TurnHub {
static SigilBus *fixtureBus=nullptr;
static SigilRecord fixtureRecords[MAX_PHYSICAL_SIGILS];
static bool fixtureRadio=false;
SigilBus::SigilBus(uint8_t channel) : wifiChannel_(channel) { fixtureBus=this; }
SigilBus *SigilBus::activeInstance() { return fixtureBus; }
bool SigilBus::begin() { return true; }
static TurnHubProtocol::SigilUpdateOfferPacket fixtureUpdateOffer{};
bool SigilBus::sendUpdateOffer(const TurnHubProtocol::SigilUpdateOfferPacket &p) { fixtureUpdateOffer=p; return true; }
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
// Pairing v2 waiting slots (the real state machine is tested in
// Sigil/tests/host/pairing_scenarios.cpp); decisions are recorded here.
static TurnHubSecureLink::PendingPairing fixturePending[MAX_PHYSICAL_SIGILS];
static int fixturePairDecisions[MAX_PHYSICAL_SIGILS]{};  // +1 confirmed, -1 rejected.
static bool fixturePairStoreFails=false;
const TurnHubSecureLink::PendingPairing *SigilBus::pendingPairing(uint8_t slot) const {
  return slot<MAX_PHYSICAL_SIGILS&&fixturePending[slot].used?&fixturePending[slot]:nullptr;
}
uint8_t SigilBus::pendingPairingCount() const {
  uint8_t n=0; for (const auto &p:fixturePending) n+=p.used?1:0; return n;
}
bool SigilBus::decidePairing(uint8_t slot, bool confirm) {
  if(slot>=MAX_PHYSICAL_SIGILS||!fixturePending[slot].used) return false;
  fixturePending[slot]=TurnHubSecureLink::PendingPairing{};
  if(confirm&&fixturePairStoreFails){ fixturePairDecisions[slot]=-1; return false; }
  fixturePairDecisions[slot]=confirm?1:-1;
  if(confirm){ fixtureForgotten[slot]=false; }
  return true;
}
void SigilBus::cancelPendingPairings() {
  for (uint8_t s=0;s<MAX_PHYSICAL_SIGILS;++s) if(fixturePending[s].used) decidePairing(s,false);
}
static void fixtureWaitForCode(uint8_t slot, uint16_t code) {
  fixturePending[slot]=TurnHubSecureLink::PendingPairing{};
  fixturePending[slot].used=true; fixturePending[slot].slot=slot; fixturePending[slot].code=code;
  fixturePairDecisions[slot]=0;
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
static unsigned fixtureMenuStateSends=0;
static int32_t fixtureMenuState2[MAX_PHYSICAL_SIGILS]{};
static int32_t fixtureLifeRequest[MAX_PHYSICAL_SIGILS]{};
static int32_t fixturePassPending[MAX_PHYSICAL_SIGILS]{};
static int32_t fixtureStartingLife[MAX_PHYSICAL_SIGILS]{};
static int32_t fixtureUpdateNotice[MAX_PHYSICAL_SIGILS]{};
static int32_t fixtureSeatColor[MAX_PHYSICAL_SIGILS][2]{};  // [id][slot & 1]: A at 1, B at 0.
static int32_t fixtureHarnessCommand=-1;
static unsigned fixtureHarnessCommands=0;
static int fixtureFactoryResetSigil=-1;
static int32_t fixtureFactoryResetValue=0;
bool SigilBus::send(uint8_t id,TurnHubProtocol::PacketType type,int32_t value) {
  assert(id<MAX_PHYSICAL_SIGILS);++fixtureSends;
  if(type==TurnHubProtocol::PacketType::InputTiming&&fixtureRadio) { fixtureInputTiming[id]=value; ++fixtureInputTimingSends; }
  if(type==TurnHubProtocol::PacketType::LedState&&fixtureRadio) { fixtureLedState[id]=value; ++fixtureLedStateSends; }
  if(type==TurnHubProtocol::PacketType::MenuState2&&fixtureRadio) { fixtureMenuState2[id]=value; ++fixtureMenuStateSends; }
  if(type==TurnHubProtocol::PacketType::LifeRequest&&fixtureRadio) fixtureLifeRequest[id]=value;
  if(type==TurnHubProtocol::PacketType::PassPending&&fixtureRadio) fixturePassPending[id]=value;
  if(type==TurnHubProtocol::PacketType::StartingLife&&fixtureRadio) fixtureStartingLife[id]=value;
  if(type==TurnHubProtocol::PacketType::UpdateNotice&&fixtureRadio) fixtureUpdateNotice[id]=value;
  if(type==TurnHubProtocol::PacketType::SeatColor&&fixtureRadio) fixtureSeatColor[id][TurnHubProtocol::seatColorSlot(value)&1]=value;
  if(type==TurnHubProtocol::PacketType::HarnessCommand&&fixtureRadio) { fixtureHarnessCommand=value; ++fixtureHarnessCommands; }
  if(type==TurnHubProtocol::PacketType::FactoryReset&&fixtureRadio) { fixtureFactoryResetSigil=id; fixtureFactoryResetValue=value; }
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
  TurnHubProfileStats::persistCompletedGame(g);  // The firmware's own bridge.
}
using TurnHubProtocol::SigilAction;
// A Sigil choosing from its menu, through the real adapter (SelectAction on
// the current revision). A choice that isn't on offer is dropped, as on
// hardware.
static void choose(uint8_t id,SigilAction action) {
  syncSigilMenus(testNow);
  handleSelectAction(id,TurnHubProtocol::encodeSelectAction(action,sigilMenuRevision(id)));
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
    // No Hello yet: Join seats the Sigil directly (the picker needs Hello info).
    TurnHub::fixtureRecords[i].helloInfoValid=false; TurnHub::fixtureRecords[i].capabilities=0;
  }
  for(int i=0;i<modules;++i) choose(static_cast<uint8_t>(i),SigilAction::Join);
  if(shared) choose(0,SigilAction::AddSeatB);
  assert(lobby.playerCount()==modules+(shared?1:0));
}
static void startFromHost() {
  choose(0,SigilAction::StartGame);
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
  choose(0,SigilAction::CycleStarter); assert(lobby.selectedStarter(selected)&&selected.slot==1);
  // Any seated Sigil may ask for a random starter now (no table host).
  choose(1,SigilAction::RandomStarter); assert(lobby.selectedStarter(selected));
  assert(dispatchModuleIntent(IntentType::Leave,0,2).accepted());
  assert(lobby.playerCount()==2&&!lobby.hasSecondary(0));
  assert(!dispatchModuleIntent(IntentType::Leave,0,2).accepted());
  assert(dispatchModuleIntent(IntentType::Join,0,2).accepted());
  assert(!dispatchModuleIntent(IntentType::Join,0,2).accepted());
  choose(0,SigilAction::StartGame);
  assert(hubState==HubState::Starting);
  assert(!dispatchModuleIntent(IntentType::Join,2).accepted());
  assert(!dispatchSystemIntent(IntentType::CompleteStart).accepted());
  choose(1,SigilAction::CancelStart); assert(hubState==HubState::Lobby);
  assert(!dispatchModuleIntent(IntentType::StartGame,0).accepted());
  assert(dispatchModuleIntent(IntentType::Leave,0).accepted());
  assert(lobby.hostController()==1&&lobby.playerCount()==1);
  freshLobby(); startFromHost();
  assert(!dispatchModuleIntent(IntentType::Rematch,0).accepted());
  assert(!dispatchModuleIntent(IntentType::ResetGame,0).accepted());
}
static void winDecisions() {
  freshLobby(3,true); startFromHost();
  assert(!web(1,1,WebControl::ClaimWin));
  assert(web(0,1,WebControl::ClaimWin));
  assert(game.nextWinConfirmationPlayerNumber()==3);
  assert(!web(0,2,WebControl::ConfirmWin));
  assert(!web(2,1,WebControl::DenyWin));
  choose(1,SigilAction::ConfirmWin); assert(game.nextWinConfirmationPlayerNumber()==4);
  assert(web(2,1,WebControl::ConfirmWin));
  assert(game.nextWinConfirmationPlayerNumber()==2);
  choose(0,SigilAction::ConfirmWin);
  assert(game.gameOver()&&game.winnerPlayerNumber()==1&&completedGames==1);
  assert(!web(0,2,WebControl::ConfirmWin)); assert(completedGames==1);
  // Any seated Sigil may call the rematch (no table host).
  assert(dispatchModuleIntent(IntentType::Rematch,1).accepted());
  assert(hubState==HubState::Lobby&&lobby.playerCount()==4&&!game.hasPlayers());
  startFromHost(); assert(web(0,1,WebControl::ClaimWin));
  choose(1,SigilAction::DenyWin); assert(hubState==HubState::Running&&!game.hasWinClaim());
  assert(web(1,1,WebControl::PauseResume));
  assert(web(0,1,WebControl::ClaimWin));
  assert(web(1,1,WebControl::DenyWin)); assert(hubState==HubState::Paused);
  assert(web(1,1,WebControl::PauseResume));
  // A claim chosen during play resumes play when denied.
  choose(0,SigilAction::ClaimWin); assert(game.hasWinClaim());
  assert(web(1,1,WebControl::DenyWin)); assert(hubState==HubState::Running);
}
static void eliminationAndConcession() {
  freshLobby(3,true); startFromHost();
  assert(web(1,1,WebControl::PauseResume));
  Intent request;
  request.type=IntentType::RequestLifeChange;request.actor.origin=IntentOrigin::Simulator;
  request.actor.controllerId=0;request.actor.slot=1;request.actor.playerNumber=1;
  request.payload.targetPlayer=3;request.payload.value=-1;
  assert(intents.dispatch(request).accepted());
  choose(0,SigilAction::BeginElimination);
  assert(eliminationTargetPlayer==1);
  assert(game.lifeChangeFor(3)->state==TurnHub::LifeChangeState::Cancelled);
  choose(0,SigilAction::NextTarget); assert(eliminationTargetPlayer==2);
  choose(1,SigilAction::Eliminate); assert(!game.isEliminated(2));  // Not its seat: not offered.
  choose(0,SigilAction::Eliminate); assert(game.isEliminated(2)&&hubState==HubState::Paused);
  assert(!web(0,2,WebControl::Concede));
  assert(web(1,1,WebControl::PauseResume));
  assert(web(1,1,WebControl::Concede)); assert(hubState==HubState::Running&&game.isEliminated(3));
  assert(web(0,1,WebControl::Concede)); assert(hubState==HubState::GameOver&&game.winnerPlayerNumber()==4);
  assert(completedGames==1);
  choose(0,SigilAction::ResetTable); assert(hubState==HubState::Lobby&&lobby.playerCount()==0);
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
  choose(0,SigilAction::Pass); assert(pendingPass.active);
  assert(!dispatchModuleIntent(IntentType::CommitPass,0).accepted());
  testNow+=2999; updatePendingPass(testNow); assert(game.activePlayerNumber()==1);
  ++testNow; updatePendingPass(testNow); assert(game.activePlayerNumber()==2&&!pendingPass.active);
  assert(game.statsForPlayer(1)->turnsCompleted==1);
  assert(web(1,1,WebControl::Pass));
  choose(1,SigilAction::CancelPass); assert(!pendingPass.active&&hubState==HubState::Running);
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
  auto found=server.routes.find(std::to_string(method)+path);
  if(found!=server.routes.end()) { found->second(); return server.status; }
  for(RequestHandler *handler:server.handlers)  // web_api.cpp's route table
    if(handler->canHandle(method,path)) { assert(handler->handle(server,method,path)); return server.status; }
  assert(!"no route"); return 0;
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
  // No table host: the second phone may start too; its countdown is cancelable.
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

  // A password works wherever a PIN does: registration, change and login.
  for(const char *bad:{"","123","pass1","seven77","tab\there!",
      "this password is far too long to be accepted by atlas at all, honestly"})
    assert(request("/api/profiles/register","",{{"name","Bad secret"},{"pin",bad}})==400);
  assert(request("/api/profiles/register","",{{"name","Password user"},{"pin","correct horse battery"}})==200);
  const String passwordId=responseField("profileId");
  assert(request("/api/session/login","",{{"profileId",passwordId},{"pin","correct horse battery"}})==200);
  const String passwordSession=responseField("token");
  assert(request("/api/session/login","",{{"profileId",passwordId},{"pin","correct horse"}})==401);
  assert(request("/api/session/profile",passwordSession,{{"pin","2468"}})==200);
  assert(request("/api/session/login","",{{"profileId",passwordId},{"pin","2468"}})==200);
  assert(request("/api/session/profile",responseField("token"),{{"pin","Ünïcode pass 9"}})==200);
  assert(request("/api/session/login","",{{"profileId",passwordId},{"pin","Ünïcode pass 9"}})==200);
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
  choose(0,SigilAction::Pass); assert(pendingPass.active);
  assert(request("/api/control/pass",secondPhone)==200 && !pendingPass.active);
  assert(request("/api/control/pass",phone)==200);
  testNow+=3000;updatePendingPass(testNow);
  assert(game.activePlayerNumber()==2);
  assert(request("/api/control/concede",other)==200 && hubState==HubState::GameOver);
  assert(ProfileFixture::profiles[id].stats.gamesPlayed==1);
  assert(ProfileFixture::profiles[id].stats.gamesWon==1);
  assert(request("/api/session/me",secondPhone,{},HTTP_GET)==200);
  // The winner's name stays on the Sigil through GameOver (released on reset).
  assert(TurnHubControllers::profileForSeat(0,1)==id);
  assert(request("/api/control/rematch",phone)==200);
  assert(TurnHubControllers::profileForSeat(0,1)==id);
  assert(ProfileFixture::profiles[id].stats.gamesPlayed==1);
  assert(request("/api/control/reset",phone)==200);
  assert(request("/api/session/join",phone)==200); // Can play by phone again.
  assert(lobby.playerCount()==1);
  choose(0,SigilAction::Join); // Finished-game binding cleared: this is now a guest.
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
  { // The streamed download ends with exactly what the ring holds.
    const String held=TurnHub::serialLog.snapshot();
    assert(server.body.size()>held.size() &&
           server.body.compare(server.body.size()-held.size(),held.size(),held)==0);
  }
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
  // A reused response workspace must be rebuilt for each viewer, including
  // virtual controllers 8/9. Unrelated requests may never leak between phones.
  assert(request("/api/game/counters",second,{},HTTP_GET)==200);
  assert(server.body.find("\"target\":2")!=std::string::npos);
  assert(request("/api/game/counters",third,{},HTTP_GET)==200);
  assert(server.body.find("\"requests\":[]")!=std::string::npos);
  String::failReserve()=true;
  assert(request("/api/game/counters",second,{},HTTP_GET)==503);
  String::failReserve()=false;
  assert(request("/api/game/counters",second,{},HTTP_GET)==200); // Lease released on failure.
  const auto countersCallback=TurnHubWebApi::internal::readCountersHandler;
  TurnHubWebApi::internal::readCountersHandler=[](uint8_t,uint8_t,TurnHubWebApi::CounterSnapshot &out) {
    out.playerCount=TurnHub::MAX_PLAYERS+1; return true;
  };
  assert(request("/api/game/counters",second,{},HTTP_GET)==503);
  TurnHubWebApi::internal::readCountersHandler=countersCallback;
  assert(request("/api/game/counters",third,{},HTTP_GET)==200);
  assert(server.body.find("\"requests\":[]")!=std::string::npos);
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
  // Playtest 2026-09-29 items 3 and 4. Another Sigil's turn: seat A shows,
  // with the table round (not A's own next-turn count).
  assert(engine.passTurn(0,300)); render();
  assert(displayPrimaryPlayer(sentGameDisplays[0].state) == 1 && displayTurnNumber(sentGameDisplays[0].state) == 1);
  // Switch seat shows seat B (so its life keys work) and holds while the
  // turn goes round the other Sigils...
  assert(renderer.switchShownSeat(0,engine)); render();
  assert(displayPrimaryPlayer(sentGameDisplays[0].state) == 2);
  for (uint8_t controller : {1,2,8,9}) {
    assert(engine.passTurn(controller,400)); render();
    if (controller != 9) assert(displayPrimaryPlayer(sentGameDisplays[0].state) == 2);
  }
  // ...until the turn comes back to this Sigil, whose active seat shows.
  assert(engine.activePlayerNumber() == 1 && displayPrimaryPlayer(sentGameDisplays[0].state) == 1);
  assert(displayTurnNumber(sentGameDisplays[0].state) == 2 && engine.currentRound() == 2);
  // An eliminated seat A never leads over a living seat B, and there is
  // nothing to switch to any more.
  assert(engine.passTurn(0,500) && engine.passTurn(0,500) && engine.activePlayerNumber() == 3);
  assert(engine.pause(600)); bool finished = false;
  assert(engine.eliminatePlayer(1,600,finished) && !finished); render();
  assert(displayPrimaryPlayer(sentGameDisplays[0].state) == 2);
  assert(!renderer.switchShownSeat(0,engine));
  engine.reset(); settings.profile = GameProfile::Magic; settings.startingLife = 20;
  assert(engine.start(seats,6,seats[0],100,settings));
  renderer.invalidateAll(); render();
  assert(!sentGameDisplays[0].commander && !sentGameDisplays[0].sourceCount && sentGameDisplays[0].primary.life == 20);
  fixtureRadio = false;
}

// Every Sigil draws its own light: one semantic LedState packet per change
// (and on invalidation, i.e. every Hello), with the seated player's style.
static void ledStateTransport() {
  using namespace TurnHub;
  using namespace TurnHubProtocol;
  fixtureRadio = true;
  GameEngine engine;
  Lobby table;
  PlayerSeat seats[2] = {{1,0,1},{2,1,1}};
  GameSettings settings;
  assert(engine.start(seats,2,seats[0],100,settings));
  LedRenderer renderer(sigilBus);
  auto render = [&](uint32_t now) { renderer.render(HubState::Running,table,engine,0,0,0,now); };
  const unsigned states = fixtureLedStateSends;
  render(200);
  assert(fixtureLedStateSends == states + MAX_PHYSICAL_SIGILS);  // One each, seated or not.
  LedStateFields fields = decodeLedState(fixtureLedState[0]);
  assert(fields.cue == LedCue::TurnStarted && fields.anchorAgeMs == 96);  // 100 ms, 16 ms units.
  assert(decodeLedState(fixtureLedState[1]).style == LedStyle::Default);
  const unsigned sent = fixtureLedStateSends;
  render(300); render(2000);
  assert(fixtureLedStateSends == sent);  // Same cues: nothing more to send.
  render(3100);
  assert(fixtureLedStateSends == sent + 1 && decodeLedState(fixtureLedState[0]).cue == LedCue::YourTurn);
  renderer.setStyle(0, LedStyle::ReducedMotion);
  renderer.invalidate(0); render(3200);
  fields = decodeLedState(fixtureLedState[0]);
  assert(fixtureLedStateSends == sent + 2 && fields.style == LedStyle::ReducedMotion);
  fixtureRadio = false;
}

namespace TurnHubAccounts { extern std::map<std::string, Account> accounts; }
// Profile picker on Sigils: gating, pages by name, locked and
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
    record.helloInfoValid = true; record.capabilities = 0;
  }
  fixtureRecords[2].capabilities |= CAPABILITY_DISPLAY_OLED;
  fixtureRecords[6].capabilities |= CAPABILITY_HARNESS;
  assert(pickerSigil(1) && pickerSigil(2) && pickerSigil(3) && !pickerSigil(6));
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

  // Leave: the whole Sigil leaves the lobby.
  resetSigilMenus(); syncSigilMenus(testNow);
  assert((sigilMenuFor(1).actions & sigilActionBit(A::Leave)) != 0);
  assert((decodeMenuState2(fixtureMenuState2[1]).actions & sigilActionBit(A::Leave)) != 0);
  pick(1, A::Leave);
  assert(!lobby.isJoined(1) && TurnHubControllers::profileForSeat(1, 1).length() == 0);
  pick(1, A::Join); key(1, PickerKeyCode::Right); key(1, PickerKeyCode::Select);
  assert(lobby.isJoined(1) && TurnHubControllers::profileForSeat(1, 1) == "0000000A");

  // Playtest 2026-09-29 item 8: Add seat B opens the picker for seat B, and a
  // picked profile adds seat B in one step. Seat A's Alice is not offered.
  pick(1, A::AddSeatB);
  assert(pickerOpen(1) && !lobby.hasSecondary(1));
  page = profilePickerPage(1);
  assert(!strcmp(page.items[1].name, "bob") && !strcmp(page.items[2].name, "Carol"));
  key(1, PickerKeyCode::Right); key(1, PickerKeyCode::Select);
  assert(lobby.hasSecondary(1) && TurnHubControllers::profileForSeat(1, 2) == "0000000B" && !pickerOpen(1));
  pick(1, A::RemoveSeatB); assert(!lobby.hasSecondary(1));
  { // A profile already at the table cannot take seat B, whatever the page said.
    Intent twice; twice.type = IntentType::PickProfile; twice.actor.origin = IntentOrigin::PhysicalSigil;
    twice.actor.controllerId = 1; twice.actor.slot = 2; strcpy(twice.payload.profileId, "0000000A");
    assert(intents.dispatch(twice).status == IntentStatus::Conflict && !lobby.hasSecondary(1));
  }
  // Removing seat B freed bob's seat; leaving from a phone frees Alice's seat
  // A too, so the Sigil no longer shows her and its next Join is not her.
  assert(TurnHubControllers::profileForSeat(1, 2).length() == 0);
  {
    Intent leave; leave.type = IntentType::LeaveProfile; leave.actor.origin = IntentOrigin::Browser;
    strcpy(leave.payload.profileId, "0000000A");
    assert(intents.dispatch(leave).accepted() && !lobby.isJoined(1) &&
        TurnHubControllers::profileForSeat(1, 1).length() == 0);
  }
  pick(1, A::Join); key(1, PickerKeyCode::Right); key(1, PickerKeyCode::Select);
  assert(lobby.isJoined(1) && TurnHubControllers::profileForSeat(1, 1) == "0000000A");

  // The OLED Sigil gets the same picker (it draws it as a list); Guest joins.
  pick(2, A::Join); assert(pickerOpen(2) && !lobby.isJoined(2));
  key(2, PickerKeyCode::Up); assert(lobby.isJoined(2) && !pickerOpen(2));
  pick(4, A::Join); key(4, PickerKeyCode::Up);
  assert(lobby.isJoined(4) && TurnHubControllers::profileForSeat(4, 1).length() == 0 && !pickerOpen(4));
  pick(4, A::AddSeatB); key(4, PickerKeyCode::Up);  // Guest in seat B.
  assert(lobby.hasSecondary(4) && TurnHubControllers::profileForSeat(4, 2).length() == 0 && !pickerOpen(4));
  pick(4, A::RemoveSeatB); assert(!lobby.hasSecondary(4));

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
    record.helloInfoValid = false; record.capabilities = 0;
  }
  resetSigilMenus(); resetProfilePickers();
  enterEmptyLobby();
  ProfileFixture::bindings.clear();
  ProfileFixture::profiles = saved;
  TurnHubAccounts::accounts = savedAccounts;
}

// Life on Sigils: AdjustLife only while ChangeLife could succeed, a
// batched LifeAdjust for the Sigil's own player only, and life requests
// shown on (and answered from) the target's Sigil, tag-checked.
static void sigilLife() {
  using namespace TurnHub;
  using namespace TurnHubProtocol;
  freshLobby(2); resetSigilMenus();
  for (auto &record : fixtureRecords) {
    record.helloInfoValid = true; record.capabilities = 0;
  }
  const auto offered = [](uint8_t id) { return (sigilMenuFor(id).actions & sigilActionBit(SigilAction::AdjustLife)) != 0; };
  assert(!offered(0));  // Lobby.
  startFromHost();
  assert(offered(0) && offered(1));
  // One seat per Sigil here: nothing to switch to (two-seat case: physicalGameDisplay).
  assert(!(sigilMenuFor(0).actions & sigilActionBit(SigilAction::SwitchSeat)));
  // The test harness plays life like a real Sigil.
  fixtureRecords[1].capabilities |= CAPABILITY_HARNESS;
  assert(offered(1));
  fixtureRecords[1].capabilities = 0;
  const PlayerSeat a = *game.playerByNumber(lobby.playerNumber(0, 1));
  const PlayerSeat b = *game.playerByNumber(lobby.playerNumber(1, 1));
  const int32_t start = game.lifeTotal(a.playerNumber);
  handleLifeAdjust(0, encodeLifeAdjust(a.playerNumber, -39));
  assert(game.lifeTotal(a.playerNumber) == start - 39);
  handleLifeAdjust(0, encodeLifeAdjust(a.playerNumber, 11));
  assert(game.lifeTotal(a.playerNumber) == start - 28);
  handleLifeAdjust(0, encodeLifeAdjust(b.playerNumber, -5));  // Not this Sigil's player.
  assert(game.lifeTotal(b.playerNumber) == start);

  // Sigil 1's player asks to take 3 from Sigil 0's player.
  assert(sigilLifeRequestFor(0) == 0);
  Intent ask; ask.type = IntentType::RequestLifeChange; ask.actor.origin = IntentOrigin::PhysicalSigil;
  ask.actor.controllerId = b.controllerId; ask.actor.slot = b.slot; ask.actor.playerNumber = b.playerNumber;
  ask.payload.targetPlayer = a.playerNumber; ask.payload.value = -3;
  assert(intents.dispatch(ask).accepted());
  const LifeRequestFields shown = decodeLifeRequest(sigilLifeRequestFor(0));
  assert(shown.target == a.playerNumber && shown.requester == b.playerNumber && shown.delta == -3);
  assert(sigilLifeRequestFor(1) == 0);
  syncSigilMenus(testNow); assert(fixtureLifeRequest[0] == sigilLifeRequestFor(0));
  handleLifeResponse(0, encodeLifeResponse(a.playerNumber, true, shown.tag + 1));  // Stale tag.
  assert(game.lifeTotal(a.playerNumber) == start - 28);
  handleLifeResponse(1, encodeLifeResponse(a.playerNumber, true, shown.tag));  // Not the target's Sigil.
  assert(game.lifeTotal(a.playerNumber) == start - 28);
  handleLifeResponse(0, encodeLifeResponse(a.playerNumber, true, shown.tag));
  assert(game.lifeTotal(a.playerNumber) == start - 31 && sigilLifeRequestFor(0) == 0);
  syncSigilMenus(testNow); assert(fixtureLifeRequest[0] == 0);
  assert(intents.dispatch(ask).accepted());
  handleLifeResponse(0, encodeLifeResponse(a.playerNumber, false, decodeLifeRequest(sigilLifeRequestFor(0)).tag));
  assert(game.lifeTotal(a.playerNumber) == start - 31);

  // Every menu Sigil learns the starting life and sees a pending pass, not
  // only the passer (turntest, 2026-09-26).
  syncSigilMenus(testNow);
  assert(fixtureStartingLife[0] == start && fixtureStartingLife[1] == start);
  const PlayerSeat *passer = game.activePlayer();
  assert(passer != nullptr);
  Intent pass; pass.type = IntentType::Pass; pass.actor.origin = IntentOrigin::PhysicalSigil;
  pass.actor.controllerId = passer->controllerId; pass.actor.slot = passer->slot;
  pass.actor.playerNumber = passer->playerNumber;
  assert(intents.dispatch(pass).accepted() && pendingPass.active);
  syncSigilMenus(testNow);
  assert(fixturePassPending[0] == passer->playerNumber && fixturePassPending[1] == passer->playerNumber);
  assert(intents.dispatch(pass).accepted() && !pendingPass.active);  // Pressed again: undone.
  syncSigilMenus(testNow);
  assert(fixturePassPending[0] == 0 && fixturePassPending[1] == 0);

  // An elimination selection blocks life changes.
  assert(dispatchSeatIntent(IntentType::Pause, IntentOrigin::PhysicalSigil, a).accepted());
  assert(offered(0));
  dispatchModuleIntent(IntentType::BeginElimination, 0);
  assert(eliminationTargetPlayer != 0 && !offered(0));
  for (auto &record : fixtureRecords) {
    record.helloInfoValid = false; record.capabilities = 0;
  }
  resetSigilMenus(); enterEmptyLobby();
}

// Jewel color: the signed-in profile sets or clears it; a seat bound to that
// profile gets a SeatColor, and a guest seat gets none.
static void jewelColors() {
  using namespace TurnHubProtocol;
  enterEmptyLobby(); TurnHub::fixtureRadio = true;
  String id;
  const String owner = registerPhone("Jewel owner", id);
  assert(request("/api/session/personalization", "", {}, HTTP_GET) == 401);
  assert(request("/api/session/personalization", owner, {}, HTTP_GET) == 200 && server.body.find("\"color\":null") != std::string::npos);
  for (const char *bad : {"ff8800", "#ff88", "#gg8800", "#ff88001"})
    assert(request("/api/session/personalization", owner, {{"color", bad}}) == 400);
  assert(request("/api/session/personalization", owner, {{"color", "#FF8800"}}) == 200);
  assert(server.body.find("\"color\":\"#ff8800\"") != std::string::npos);
  for (auto &record : TurnHub::fixtureRecords) {
    record.helloInfoValid = true; record.capabilities = 0;
  }
  resetSigilMenus();
  assert(TurnHubProfiles::bindSeatToProfile(TurnHub::fixtureRecords[0].mac, 1, id));
  int32_t c = sigilSeatColorFor(0, 1);
  assert(seatColorSet(c) && seatColorRgb(c) == 0xFF8800 && seatColorSlot(c) == 1);
  assert(!seatColorSet(sigilSeatColorFor(0, 2)) && !seatColorSet(sigilSeatColorFor(1, 1)));
  syncSigilMenus(testNow); assert(TurnHub::fixtureSeatColor[0][1] == c);
  // Avatars: presets only; 0 clears; custom and unknown values are refused.
  for (const char *bad : {"13", "128", "-1", "x", "1000"})
    assert(request("/api/session/personalization", owner, {{"avatar", bad}}) == 400);
  assert(request("/api/session/personalization", owner, {{"avatar", "3"}}) == 200 &&
      server.body.find("\"avatar\":3") != std::string::npos && server.body.find("#ff8800") != std::string::npos);
  c = sigilSeatColorFor(0, 1);
  assert(seatAvatar(c) == 3 && seatColorRgb(c) == 0xFF8800);
  assert(request("/api/avatars", "", {}, HTTP_GET) == 200 && server.body.find("\"key\":\"sword\"") != std::string::npos &&
      server.body.find("\"id\":12") != std::string::npos);
  assert(dispatchModuleIntent(IntentType::Join, 0, 1).accepted());  // Sigil 0 joins as its bound profile.
  assert(request("/api/seats", "", {}, HTTP_GET) == 200 && server.body.find("\"avatar\":3") != std::string::npos);
  AtlasScreen screen; buildAtlasScreen(testNow, screen);
  assert(screen.playerCount == 1 && screen.players[0].avatar == 3);
  TurnHubProfiles::saveAvatarForProfile(id, TurnHubAvatars::AVATAR_CUSTOM);  // Custom: never public.
  assert(request("/api/seats", "", {}, HTTP_GET) == 200 && server.body.find("\"avatar\":0") != std::string::npos);
  assert(seatAvatar(sigilSeatColorFor(0, 1)) == 0);
  buildAtlasScreen(testNow, screen); assert(screen.players[0].avatar == 0);
  assert(request("/api/session/personalization", owner, {{"avatar", "0"}}) == 200);
  assert(request("/api/session/personalization", owner, {{"color", "none"}}) == 200 && server.body.find("\"color\":null") != std::string::npos);
  assert(!seatColorSet(sigilSeatColorFor(0, 1)));
  // Seat colors are re-read every 500 ms, not every loop pass.
  syncSigilMenus(testNow + 500); assert(!seatColorSet(TurnHub::fixtureSeatColor[0][1]));
  for (auto &record : TurnHub::fixtureRecords) {
    record.helloInfoValid = false; record.capabilities = 0;
  }
  resetSigilMenus(); enterEmptyLobby(); ProfileFixture::bindings.clear();
}

// Sigil menus: availability per state, the default action, MenuState2
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
  for (auto &record : fixtureRecords) { record.helloInfoValid = true; record.capabilities = 0; }

  // Lobby: every joined Sigil can start and pick a random starter (no table
  // host), cycle the starter or add Seat B; an unjoined Sigil can only join.
  assert(only(0, {A::CycleStarter, A::AddSeatB, A::StartGame, A::RandomStarter, A::Leave}));
  assert(sigilMenuFor(0).defaultAction == static_cast<uint8_t>(A::StartGame));
  assert(only(1, {A::CycleStarter, A::AddSeatB, A::StartGame, A::RandomStarter, A::Leave}));
  assert(only(2, {A::Join}) && sigilMenuFor(2).defaultAction == static_cast<uint8_t>(A::Join));

  // Transport: one MenuState2 per Sigil, none while unchanged, resend when invalidated.
  const unsigned sends = fixtureMenuStateSends;
  syncSigilMenus(testNow);
  assert(fixtureMenuStateSends == sends + MAX_PHYSICAL_SIGILS);
  MenuStateFields sent = decodeMenuState2(fixtureMenuState2[2]);
  assert(sent.actions == sigilActionBit(A::Join) && sent.defaultAction == static_cast<uint8_t>(A::Join));
  syncSigilMenus(testNow); assert(fixtureMenuStateSends == sends + MAX_PHYSICAL_SIGILS);
  invalidateSigilMenu(2); syncSigilMenus(testNow); assert(fixtureMenuStateSends == sends + MAX_PHYSICAL_SIGILS + 1);

  // Joining changes Sigil 2's menu: its revision moves on.
  const uint8_t oldRevision = sigilMenuRevision(2);
  pick(2, A::Join); assert(pickerOpen(2));  // Every Sigil picks who joins; Guest is Up.
  handlePickerKey(2, encodePickerKey(PickerKeyCode::Up, profilePickerPage(2).revision), testNow);
  assert(lobby.isJoined(2) && lobby.playerCount() == 3);
  syncSigilMenus(testNow);
  assert(sigilMenuRevision(2) != oldRevision && decodeMenuState2(fixtureMenuState2[2]).revision == sigilMenuRevision(2));
  // A choice from the old menu is dropped (and the menu resent).
  handleSelectAction(2, encodeSelectAction(A::Join, oldRevision));
  assert(lobby.playerCount() == 3);
  // Unoffered actions are dropped even at the current revision.
  pick(1, A::Rematch); assert(hubState == HubState::Lobby && lobby.playerCount() == 3);

  pick(1, A::AddSeatB);  // Seat B opens the picker too; Guest.
  handlePickerKey(1, encodePickerKey(PickerKeyCode::Up, profilePickerPage(1).revision), testNow);
  assert(lobby.hasSecondary(1) && has(1, A::RemoveSeatB));
  pick(1, A::RemoveSeatB); assert(!lobby.hasSecondary(1));

  // Start from the menu: one choice arms and starts; anyone seated may cancel.
  pick(0, A::StartGame); assert(hubState == HubState::Starting);
  assert(only(1, {A::CancelStart}));
  pick(1, A::CancelStart); assert(hubState == HubState::Lobby);
  pick(0, A::StartGame); assert(hubState == HubState::Starting);
  testNow += 3000; updateCountdown(testNow); assert(hubState == HubState::Running);

  // Running: the active Sigil passes, claims or pauses; others may pause.
  // Every living seat can change its own life.
  const uint8_t activeId = game.activePlayer()->controllerId;
  const uint8_t otherId = activeId == 0 ? 1 : 0;
  assert(only(activeId, {A::Pass, A::ClaimWin, A::Pause, A::AdjustLife}));
  assert(sigilMenuFor(activeId).defaultAction == static_cast<uint8_t>(A::Pass));
  assert(only(otherId, {A::Pause, A::AdjustLife}));
  pick(activeId, A::Pass); assert(pendingPass.active);
  assert(has(activeId, A::CancelPass) && !has(activeId, A::Pass));
  pick(activeId, A::CancelPass); assert(!pendingPass.active);

  // Paused: resume, "I'm out", and a claim for the active player.
  pick(otherId, A::Pause); assert(hubState == HubState::Paused);
  assert(only(otherId, {A::Resume, A::BeginElimination, A::AdjustLife}));
  assert(only(activeId, {A::Resume, A::BeginElimination, A::ClaimWin, A::AdjustLife}));
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
  // Recovery reuses one maximum-sized workspace across records. A smaller
  // match must clear the previous match's tail without losing nonzero defaults.
  decoded.players[MAX_PLAYERS-1] = PlayerSeat{MAX_PLAYERS,7,2};
  decoded.stats[MAX_PLAYERS-1].turnsCompleted = 99;
  decoded.damage[MAX_PLAYERS-1][MAX_PLAYERS-1][1] = 123;
  decoded.life[MAX_PLAYERS-1] = 999;
  assert(decodeCheckpoint(bytes,size,decoded) == TurnHubStorage::Status::Ok);
  assert(decoded.players[MAX_PLAYERS-1].controllerId == INVALID_ID &&
      decoded.players[MAX_PLAYERS-1].slot == 1);
  assert(decoded.stats[MAX_PLAYERS-1].turnsCompleted == 0 &&
      decoded.damage[MAX_PLAYERS-1][MAX_PLAYERS-1][1] == 0 && decoded.life[MAX_PLAYERS-1] == 0);
  GameEngine restored;
  assert(restored.restoreCheckpoint(decoded,500) && restored.paused());
  assert(restored.turnTimerMs() == 120000 && restored.turnRemainingMs(900000) == 90000);
  GameEngine empty;
  empty.checkpoint(decoded,31000);
  assert(decoded.count == 0 && decoded.settings.startingLife == 40 &&
      decoded.settings.turnTimerMs == TURN_TIMER_OFF && decoded.gameElapsed == 0 &&
      decoded.players[0].controllerId == INVALID_ID && decoded.players[0].slot == 1 &&
      decoded.players[0].profileId[0] == 0);
}

// Which cue each Sigil gets. How a cue looks (colors, cadences, the player's
// style) is the Sigil's job: Sigil/tests/host/led_scenarios.cpp.
static void ledCueSelection() {
  using namespace TurnHub;
  // Lobby: each joined Sigil shows its player number. There is no host
  // overlay (no table host since 2026-09-25).
  freshLobby(2);
  auto lobbyCue = [](uint8_t id, uint32_t now) {
    return selectSigilLedState(id,HubState::Lobby,lobby,game,0,0,0,now);
  };
  const auto host = lobbyCue(0,0);
  assert(host.cue == LedCue::Joined && host.playerNumber == 1 && !host.has(LedOverlay::Host));
  assert(!lobbyCue(1,0).has(LedOverlay::Host) && lobbyCue(1,0).playerNumber == 2);
  assert(lobbyCue(5,0).cue == LedCue::Unassigned);

  GameEngine engine; Lobby table;
  PlayerSeat seats[2] = {{1,0,1},{2,1,1}};
  GameSettings settings; settings.turnTimerMs = 60000;
  assert(engine.start(seats,2,seats[0],1000,settings));
  auto cue = [&](uint8_t id, uint32_t now) {
    return selectSigilLedState(id,HubState::Running,table,engine,0,0,0,now);
  };
  assert(cue(0,1000).cue == LedCue::TurnStarted && cue(0,4000).cue == LedCue::YourTurn);
  assert(cue(0,1000).anchorMs == 1000);  // TurnStarted runs from the turn's own start.
  assert(cue(1,4000).cue == LedCue::Waiting && !cue(0,4000).overlays);
  assert(cue(0,51000).has(LedOverlay::TurnWarning) && cue(0,61000).has(LedOverlay::TimerExpired));
  assert(!cue(1,61000).overlays);  // Only the active Sigil shows its timer.

  GameEngine untimed; GameSettings off;
  assert(untimed.start(seats,2,seats[0],1000,off));
  const auto longTurn = selectSigilLedState(0,HubState::Running,table,untimed,0,0,0,1000 + TURN_TIMER_LONG_TURN_MS);
  assert(longTurn.has(LedOverlay::LongTurn) && !longTurn.has(LedOverlay::TimerExpired));
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
  choose(active,SigilAction::Pass); testNow += 3000; updatePendingPass(testNow);
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

static void accessibilityPreferences() {
  using namespace TurnHub;
  using TurnHubProfiles::AccessibilityPrefs;
  using TurnHubProfiles::LedStyle;
  // How each style looks on the ring is tested on the Sigil (led_scenarios.cpp).

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

  // Applying: Alice is bound to Sigils 1 and 2; each gets her style, mute and hold times.
  ProfileFixture::bindings.clear();
  ProfileFixture::bindings[ProfileFixture::key(fixtureRecords[1].mac, 1).c_str()] = aliceId;
  ProfileFixture::bindings[ProfileFixture::key(fixtureRecords[2].mac, 1).c_str()] = aliceId;
  fixtureRecords[1].helloInfoValid = true; fixtureRecords[2].helloInfoValid = true;
  fixtureInputTiming[1] = fixtureInputTiming[2] = 0;
  applyAllSigilAccessibility(testNow);
  assert(leds.style(1) == TurnHubProtocol::LedStyle::MonochromeSafe && leds.style(0) == TurnHubProtocol::LedStyle::Default);
  assert(audio.mutedSigils() == static_cast<uint16_t>((1u << 1) | (1u << 2)));
  assert(fixtureInputTiming[1] == TurnHubProtocol::encodeInputTiming(3000, 6000));
  assert(fixtureInputTiming[2] == TurnHubProtocol::encodeInputTiming(3000, 6000));
  // Not resent until the keepalive interval, then resent.
  const unsigned sends = fixtureInputTimingSends;
  applyAllSigilAccessibility(testNow + 1000); assert(fixtureInputTimingSends == sends);
  applyAllSigilAccessibility(testNow + 10000); assert(fixtureInputTimingSends > sends);
  // A muted Sigil hears nothing, including queued notes; others still do.
  clearTones(); resetBuzzes();
  audio.actionRequired(1); audio.actionRequired(0); drainAudio();
  assert(fixtureBuzzes[1] == 0 && heardActionRequired(0));
  // Bob shares Sigil 1 with reduced motion: the merged style applies at once after his save.
  ProfileFixture::bindings[ProfileFixture::key(fixtureRecords[1].mac, 2).c_str()] = bobId;
  assert(request("/api/session/accessibility", bob,
      {{"ledStyle", "reduced-motion"}, {"longPressMs", "2500"}, {"winHoldMs", "7000"}}) == 200);
  assert(leds.style(1) == TurnHubProtocol::LedStyle::ReducedMotion);
  assert(fixtureInputTiming[1] == TurnHubProtocol::encodeInputTiming(3000, 7000));
  assert(audio.mutedSigils() & (1u << 1));
  // Unbinding restores the defaults (and sound) on the next refresh.
  ProfileFixture::bindings.clear();
  for (uint32_t t = 0; t < 8 * 250 + 250; t += 250) updateSigilAccessibility(testNow + 20000 + t);
  assert(audio.mutedSigils() == 0 && leds.style(1) == TurnHubProtocol::LedStyle::Default);
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
  choose(first,SigilAction::ConfirmWin); drainAudio();
  const uint8_t second = player(game.nextWinConfirmationPlayerNumber()).controllerId;
  assert(second != first && heardActionRequired(second) && !heardActionRequired(first));
  clearTones();
  choose(second,SigilAction::ConfirmWin); drainAudio();
  assert(game.gameOver());
  for (uint8_t id = 0; id < 3; ++id) assert(!heardActionRequired(id));  // Nothing left to decide.
  // A life change request reaches only the recipient, who must approve it.
  choose(0,SigilAction::Rematch); assert(hubState == HubState::Lobby);
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
  clientState.setNameLookup(displayNameForTableSeat);  // As setup() does.
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
  // Playtest 2026-09-29 item 5: phone-joined players carry their names in
  // the state itself and on the Atlas screen, in the lobby and in the game.
  const auto namesAgree = []() {
    assert(request("/api/v1/state", "", {}, HTTP_GET) == 200);
    assert(server.body.find("\"displayName\":\"Native first\"") != std::string::npos);
    assert(server.body.find("\"displayName\":\"Native second\"") != std::string::npos);
    AtlasScreen screen; buildAtlasScreen(testNow, screen);
    assert(screen.playerCount == 2 && !strcmp(screen.players[0].name, "Native first"));
  };
  namesAgree();
  assert(request("/api/control/start", first) == 200);
  testNow += 3000; updateCountdown(testNow);
  assert(hubState == HubState::Running);
  namesAgree();
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

// Recovery fault-injection scenarios run last: they change the process-wide
// store's writable state. The recovery namespace uses map-backed reads across
// simulated restarts, independently of OptionalPreferences fault injection.
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
  const AtlasScreen screen=currentScreen();  // keep it alive: b points into it
  const TouchButton *b=screenButton(screen,action); assert(b!=nullptr);
  touchAt(b->x+b->w/2,b->y+b->h/2);
}
static void tapButton(TouchAction action) { pressButton(action); testNow+=30; pressButton(action); touchRelease(); }
// Pair, QR codes, Tests and Info live on the between-games Menu screen.
static void openMenuScreen() {
  if (currentScreen().kind!=ScreenKind::Menu) tapButton(TouchAction::OpenMenu);
  assert(currentScreen().kind==ScreenKind::Menu);
}
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

// Both display variants can join Seat B and start a shared-seat match.
static void oledSigilSeatsTwoPlayers() {
  using TurnHubProtocol::CAPABILITY_DISPLAY_OLED;
  freshLobby(2);
  TurnHub::fixtureRecords[1].capabilities=CAPABILITY_DISPLAY_OLED;
  choose(1,SigilAction::AddSeatB);
  assert(lobby.hasSecondary(1) && lobby.playerCount()==3);
  assert(dispatchModuleIntent(IntentType::Leave,1,2).accepted());
  assert(dispatchModuleIntent(IntentType::Join,1,2).accepted() && lobby.hasSecondary(1));
  assert(dispatchModuleIntent(IntentType::Join,0,2).accepted() && lobby.hasSecondary(0));
  assert(dispatchModuleIntent(IntentType::ArmStart,0).accepted());
  TurnHub::fixtureRecords[1].capabilities=0;
  enterEmptyLobby();
}

static void touchControls() {
  resetTouchControls(); freshLobby(2); TurnHub::fixtureRadio=true; pairingActive=false;
  AtlasScreen s=currentScreen();
  // The lobby row keeps Start and Clear up front; the rest waits under Menu.
  assert(String(s.title)=="Lobby" && s.buttonCount==3 && screenButton(s,TouchAction::StartGame) &&
      screenButton(s,TouchAction::ClearLobby) && screenButton(s,TouchAction::OpenMenu) &&
      !screenButton(s,TouchAction::Pair) && !screenButton(s,TouchAction::OpenQr) && !screenButton(s,TouchAction::OpenInfo));
  assert(s.round==0 && s.gameClock[0]=='\0' && s.players[0].turnTime[0]=='\0');
  // Every button fits on screen and meets the 44 px minimum target size.
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.y>=0 && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);
  tapButton(TouchAction::OpenMenu); s=currentScreen();
  assert(s.kind==ScreenKind::Menu && String(s.badge)=="MENU" && screenButton(s,TouchAction::Pair) &&
      screenButton(s,TouchAction::OpenQr) && screenButton(s,TouchAction::OpenInfo) &&
      screenButton(s,TouchAction::CloseScreen) && hubState==HubState::Lobby);
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.y>=SCREEN_BODY_Y && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);

  // Touches outside a button, or sliding off one, do nothing.
  touchAt(4,4); touchRelease(); assert(!pairingActive);
  pressButton(TouchAction::Pair); touchAt(4,4); touchRelease(); assert(!pairingActive);
  // A brief resistive drop-out is not a release; the tap acts once on release.
  pressButton(TouchAction::Pair); updateTouchControls(testNow+TOUCH_RELEASE_MS-1,false,0,0);
  assert(!pairingActive && currentScreen().pressed==TouchAction::Pair);
  pressButton(TouchAction::Pair); touchRelease();
  assert(pairingActive && currentScreen().pressed==TouchAction::None);
  // Pairing from the Menu returns to the status screen and its countdown.
  s=currentScreen();
  assert(s.kind==ScreenKind::Status && startsWith(s.detail,"Pairing open: ") && String(s.notice)=="Pairing window opened");
  testNow+=TOUCH_NOTICE_MS; assert(currentScreen().notice[0]=='\0');
  testNow+=pairingWindowMs; updatePairingWindow(testNow); assert(!pairingActive);
  assert(String(currentScreen().detail)=="2 players, 0 Sigils");
  // A press drifting just past the edge (resistive jitter, a rolling
  // fingertip) still counts; beyond the slop it cancels.
  openMenuScreen();
  { const AtlasScreen pairScreen=currentScreen();
    const TouchButton *pair=screenButton(pairScreen,TouchAction::Pair);
    const int16_t px=pair->x+pair->w/2, py=pair->y+pair->h/2, edge=pair->y-1;
    touchAt(px,py); touchAt(px,edge-TOUCH_SLOP_PX+2); touchRelease(); assert(pairingActive);
    testNow+=pairingWindowMs; updatePairingWindow(testNow); assert(!pairingActive);
    openMenuScreen();
    touchAt(px,py); touchAt(px,edge-TOUCH_SLOP_PX); touchRelease(); assert(!pairingActive);
    // A press that starts in the slop, off every button, does nothing.
    touchAt(px,edge); touchRelease(); assert(!pairingActive);
    testNow+=TOUCH_NOTICE_MS; }
  // Pairing still goes through its handler: a radio failure is reported, not hidden.
  TurnHub::fixtureRadio=false; tapButton(TouchAction::Pair);
  assert(!pairingActive && String(currentScreen().notice)=="Radio unavailable");
  // A refused pairing leaves the Menu up; Back returns to the lobby.
  assert(currentScreen().kind==ScreenKind::Menu);
  tapButton(TouchAction::CloseScreen); assert(currentScreen().kind==ScreenKind::Status);
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
  seatPass(); assert(String(currentScreen().detail)=="Passing in 3s: that seat can cancel");
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

  // Playtest 2026-09-29 item 9: a player's chip opens their screen, which
  // changes their own life and concedes only after asking again.
  {
    const PlayerSeat second=*game.playerAt(1);
    const int32_t life=game.lifeTotal(second.playerNumber);
    int16_t cx,cy,cw,ch; screenChipCell(1,game.playerCount(),cx,cy,cw,ch);
    touchAt(cx+cw/2,cy+ch/2); testNow+=30; touchAt(cx+cw/2,cy+ch/2); touchRelease();
    s=currentScreen();
    assert(s.kind==ScreenKind::Player && String(s.badge)=="PLAYER" && s.buttonCount==6 &&
        String(screenButton(s,TouchAction::LifeMinus5)->label)=="-5" && screenButton(s,TouchAction::CloseScreen) &&
        !screenButton(s,TouchAction::Concede)->hold());
    for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None) assert(b.w>=44 && b.h>=44);
    tapButton(TouchAction::LifeMinus5); tapButton(TouchAction::LifePlus1);
    assert(game.lifeTotal(second.playerNumber)==life-4);
    tapButton(TouchAction::Concede); s=currentScreen();
    assert(!game.isEliminated(second.playerNumber) && screenButton(s,TouchAction::ConfirmConcede) &&
        screenButton(s,TouchAction::CancelConcede) && startsWith(s.detail,"Concede for "));
    tapButton(TouchAction::CancelConcede); assert(!game.isEliminated(second.playerNumber));
    tapButton(TouchAction::CloseScreen); assert(currentScreen().kind==ScreenKind::Status);
  }

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
      s.buttonCount==3 && screenButton(s,TouchAction::Rematch) && screenButton(s,TouchAction::ResetTable) &&
      screenButton(s,TouchAction::OpenMenu));
  // After a game the Menu has no Pair; the round and match length stay up.
  assert(s.round>=1 && s.gameClock[0]!='\0');
  tapButton(TouchAction::OpenMenu); s=currentScreen();
  assert(s.kind==ScreenKind::Menu && !screenButton(s,TouchAction::Pair) && screenButton(s,TouchAction::OpenQr) &&
      screenButton(s,TouchAction::OpenInfo));
  tapButton(TouchAction::CloseScreen);

  // Playtest 2026-09-29 item 11: in the lobby a chip opens that seat's turn
  // order; any player may move it, from the Atlas screen only.
  enterEmptyLobby(); freshLobby(3);
  {
    PlayerSeat seats[3]; lobby.buildPlayers(seats,3);
    const uint8_t mover=seats[2].controllerId;
    int16_t cx,cy,cw,ch; screenChipCell(2,3,cx,cy,cw,ch);
    touchAt(cx+cw/2,cy+ch/2); touchRelease();
    s=currentScreen();
    assert(s.kind==ScreenKind::Player && String(s.badge)=="ORDER" && startsWith(s.detail,"Turn order: 3 of 3") &&
        screenButton(s,TouchAction::MoveEarlier) && screenButton(s,TouchAction::MoveLater) &&
        !screenButton(s,TouchAction::Concede));
    tapButton(TouchAction::MoveEarlier);
    assert(lobby.playerNumber(mover)==2 && startsWith(currentScreen().detail,"Turn order: 2 of 3"));
    tapButton(TouchAction::MoveEarlier); assert(lobby.playerNumber(mover)==1);
    tapButton(TouchAction::MoveEarlier);
    assert(lobby.playerNumber(mover)==1 && startsWith(currentScreen().notice,"Already first"));
    Intent fromPhone; fromPhone.type=IntentType::MoveSeat; fromPhone.actor.origin=IntentOrigin::Browser;
    fromPhone.payload.targetPlayer=1; fromPhone.payload.value=1;
    assert(intents.dispatch(fromPhone).status==IntentStatus::Unauthorized && lobby.playerNumber(mover)==1);
    tapButton(TouchAction::CloseScreen);
    // The new order is the game's turn order.
    startFromHost(); assert(game.playerAt(0)->controllerId==mover);
    Intent late; late.type=IntentType::MoveSeat; late.actor.origin=IntentOrigin::AtlasHardware;
    late.payload.targetPlayer=1; late.payload.value=1;
    assert(intents.dispatch(late).status==IntentStatus::InvalidState);
  }

  // A confirmed concession from the Player screen goes through Concede: the
  // player is out, the match goes on, and their screen closes.
  {
    const uint8_t third=game.playerAt(2)->playerNumber;
    int16_t cx,cy,cw,ch; screenChipCell(2,game.playerCount(),cx,cy,cw,ch);
    touchAt(cx+cw/2,cy+ch/2); touchRelease();
    tapButton(TouchAction::Concede); tapButton(TouchAction::ConfirmConcede);
    assert(game.isEliminated(third) && hubState==HubState::Running && currentScreen().kind==ScreenKind::Status);
    // An eliminated player's chip opens nothing.
    touchAt(cx+cw/2,cy+ch/2); touchRelease(); assert(currentScreen().kind==ScreenKind::Status);
  }

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
  assert(s.qr[0]=='\0' && s.lineCount>0);  // Empty table: how to join, no QR code (it is under Menu).
  fixtureSdCardReady=false; assert(currentScreen().sdMissing); fixtureSdCardReady=true;

  freshLobby(3); s=currentScreen();
  assert(s.playerCount==3 && s.qr[0]=='\0' && !s.showLife);
  assert(String(s.players[0].name)=="Player 1");
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);

  openMenuScreen(); tapButton(TouchAction::OpenInfo); s=currentScreen();
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
  // Back steps out one level: QR codes to the Menu, then the Menu to the lobby.
  tapButton(TouchAction::CloseScreen); assert(currentScreen().kind==ScreenKind::Menu);
  tapButton(TouchAction::CloseScreen); assert(currentScreen().kind==ScreenKind::Status);
  // A game starting closes the Menu.
  openMenuScreen();

  startFromHost(); s=currentScreen();
  assert(s.kind==ScreenKind::Status && String(s.badge)=="PLAYING" && s.showLife && s.playerCount==3 &&
      s.clock[0]!='\0' && s.timerPermille==-1);
  // Round 1, the match clock and every player's own turn time.
  assert(s.round==1 && String(s.gameClock)=="0:00");
  for (uint8_t i=0;i<s.playerCount;++i) assert(String(s.players[i].turnTime)=="0:00");
  // The round turns over when the play comes back to the first seat, and
  // each player's turn time counts only their own turns.
  testNow+=65000; s=currentScreen();
  { uint8_t ticking=0;
    for (uint8_t i=0;i<s.playerCount;++i) ticking+=String(s.players[i].turnTime)=="1:05";
    assert(ticking==1 && String(s.gameClock)=="1:05"); }
  seatPass(); testNow+=PASS_GRACE_MS; updatePendingPass(testNow); assert(currentScreen().round==1);
  seatPass(); testNow+=PASS_GRACE_MS; updatePendingPass(testNow); assert(currentScreen().round==1);
  seatPass(); testNow+=PASS_GRACE_MS; updatePendingPass(testNow); assert(currentScreen().round==2);
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

// Newer firmware (update_notice.h): the app reports the release feed's
// versions; Atlas counts itself and each paired Sigil that runs something
// older (never the harness), and says so on the Menu and Info screens.
static void updateNotice() {
  using namespace TurnHubProtocol;
  TurnHub::FirmwareRelease r;
  assert(TurnHub::parseFirmwareRelease("0.9.3", r) && r.major==0 && r.minor==9 && r.patch==3);
  assert(TurnHub::parseFirmwareRelease("1.2.3-dev", r) && r.major==1);
  for (const char *bad : {"", "1.2", "1.2.3.4", "a.b.c", "1.2.256", "1.2.3x", "01234.1.1"})
    assert(!TurnHub::parseFirmwareRelease(bad, r));
  assert(TurnHub::releaseNewer(r, 1, 2, 2) && !TurnHub::releaseNewer(r, 1, 2, 3) && !TurnHub::releaseNewer(r, 1, 3, 0));

  registerWebCallbacks();  // Production wiring of the update hooks.
  resetTouchControls(); enterEmptyLobby(); pairingActive=false; TurnHub::fixtureRadio=true;
  for (auto &record : TurnHub::fixtureRecords) { record.helloInfoValid=false; record.capabilities=0; }
  assert(request("/api/updates","",{},HTTP_GET)==200 && server.body.find("\"reported\":false")!=std::string::npos);
  assert(request("/api/updates/latest","",{})==400);
  assert(request("/api/updates/latest","",{{"atlas","nine"}})==400);
  char newer[16];
  snprintf(newer,sizeof(newer),"%u.%u.%u",TurnHubFirmware::MAJOR,TurnHubFirmware::MINOR,TurnHubFirmware::PATCH+1);
  // Two e-ink Sigils on 0.9.2 and an OLED on 0.9.3, plus the harness on 0.1.0.
  auto hello=[](uint8_t id,uint8_t caps,uint8_t patch){
    auto &record=TurnHub::fixtureRecords[id];
    record.helloInfoValid=true; record.capabilities=caps;
    record.firmwareMajor=0; record.firmwareMinor=9; record.firmwarePatch=patch;
  };
  hello(0,0,2); hello(1,0,2); hello(2,CAPABILITY_DISPLAY_OLED,3); hello(3,CAPABILITY_HARNESS,0);
  assert(request("/api/updates/latest","",{{"sigilEink","0.9.3"},{"sigilOled","0.9.3"}})==200);
  assert(firmwareUpdatesAvailable()==2 && server.body.find("\"updatesAvailable\":2")!=std::string::npos &&
      server.body.find("\"atlas\":null")!=std::string::npos);
  assert(request("/api/updates/latest","",{{"atlas",newer},{"sigilEink","0.9.2"},{"sigilOled","0.9.4-dev"}})==200);
  assert(firmwareUpdatesAvailable()==2);  // Atlas and the OLED Sigil.
  openMenuScreen(); AtlasScreen s=currentScreen();
  assert(firmwareUpdateKind()==TurnHub::UpdateKind::Sigils && s.update==TurnHub::UpdateKind::Sigils);
  assert(String(s.detail)=="Update available: use the app");
  tapButton(TouchAction::OpenInfo); assert(String(currentScreen().lines[4])=="Update available: use the app");
  // Only Atlas behind: the words name it, and every Sigil is told the same.
  assert(request("/api/updates/latest","",{{"atlas",newer},{"sigilEink","0.9.2"},{"sigilOled","0.9.3"}})==200);
  assert(firmwareUpdatesAvailable()==1 && firmwareUpdateKind()==TurnHub::UpdateKind::AtlasOnly);
  assert(String(TurnHub::updateKindText(firmwareUpdateKind()))=="Update available for Atlas");
  assert(String(currentScreen().lines[4])=="Update available for Atlas: use the app");
  AtlasScreen header; buildAtlasScreen(testNow,header);
  assert(header.update==TurnHub::UpdateKind::AtlasOnly);
  syncSigilMenus(testNow);
  assert(TurnHub::fixtureUpdateNotice[0]==static_cast<int32_t>(TurnHub::UpdateKind::AtlasOnly));
  assert(request("/api/updates/latest","",{{"atlas",newer},{"sigilEink","0.9.2"},{"sigilOled","0.9.4-dev"}})==200);
  syncSigilMenus(testNow);
  assert(TurnHub::fixtureUpdateNotice[0]==static_cast<int32_t>(TurnHub::UpdateKind::Sigils));
  // Up to date again: the notice goes, and the uptime line comes back.
  assert(request("/api/updates/latest","",{{"sigilEink","0.9.2"}})==200 && firmwareUpdatesAvailable()==0);
  assert(startsWith(currentScreen().lines[4],"Up "));
  for (auto &record : TurnHub::fixtureRecords) { record.helloInfoValid=false; record.capabilities=0; }
  resetTouchControls();
}

// A connected test harness (CAPABILITY_HARNESS) adds Tests to the lobby; its
// screen starts premade tests over the radio and shows progress in words.
static void harnessScreen() {
  using namespace TurnHubProtocol;
  resetTouchControls(); resetHarnessLink(); freshLobby(2); pairingActive=false;
  openMenuScreen(); AtlasScreen s=currentScreen();
  assert(!screenButton(s,TouchAction::OpenTests) && harnessSigilId(testNow)==INVALID_ID);
  // A report from an ordinary Sigil is ignored.
  HarnessReportFields running; running.state=HarnessRunState::Running;
  running.test=static_cast<uint8_t>(HarnessTest::FullGame); running.step=static_cast<uint8_t>(HarnessStep::Turn);
  running.passed=12;
  noteHarnessReport(2,encodeHarnessReport(running),testNow); HarnessReportFields r;
  assert(!harnessReport(testNow,r));

  TurnHub::SigilRecord &rec=TurnHub::fixtureRecords[2];
  rec.helloInfoValid=true; rec.capabilities=CAPABILITY_HARNESS;
  assert(harnessSigilId(testNow)==2);
  s=currentScreen();
  const TouchButton *info=screenButton(s,TouchAction::OpenInfo), *tests=screenButton(s,TouchAction::OpenTests);
  assert(info && tests && info->w>=44 && tests->w>=44 && info->x>=tests->x+tests->w && tests->y==info->y);
  tapButton(TouchAction::OpenTests); s=currentScreen();
  assert(String(s.title)=="Test harness" && String(s.detail)=="Ready: pick a test" && s.buttonCount==6);
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
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
  enterEmptyLobby(); openMenuScreen();
  // Without a harness the Tests button goes, and an open test screen offers only Back.
  tapButton(TouchAction::OpenTests); rec.capabilities=0; s=currentScreen();
  assert(String(s.detail)=="Harness offline" && s.buttonCount==1 && screenButton(s,TouchAction::CloseTests));
  tapButton(TouchAction::CloseTests); assert(currentScreen().kind==ScreenKind::Menu);
  assert(!screenButton(currentScreen(),TouchAction::OpenTests));
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
  // Lobby Clear: a hold, shown only with someone joined; a tap only explains.
  assert(!screenButton(currentScreen(),TouchAction::ClearLobby));
  freshLobby(3); resetTouchControls();
  assert(screenButton(currentScreen(),TouchAction::ClearLobby));
  tapButton(TouchAction::ClearLobby); assert(lobby.playerCount()==3);
  pressButton(TouchAction::ClearLobby); testNow+=LOBBY_CLEAR_HOLD_MS; pressButton(TouchAction::ClearLobby); touchRelease();
  assert(hubState==HubState::Lobby && lobby.playerCount()==0);
  assert(!screenButton(currentScreen(),TouchAction::ClearLobby));
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
  assert(request("/api/pairing",player,{{"windowMs","90000"}})==403);
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

  // Pairing window: 60 s default and minimum, admins choose 60/90/120 s,
  // never shorter (the old 15 and 30 s are refused), and Atlas uses it.
  assert(request("/api/pairing",admin,{},HTTP_GET)==200);
  assert(server.body.find("\"windowMs\":60000")!=std::string::npos &&
      server.body.find("\"sigilWindowMs\":60000")!=std::string::npos &&
      server.body.find("\"choicesMs\":[60000,90000,120000]")!=std::string::npos);
  for (const char *bad : {"15000","30000","20000","0","-60000","600000","abc"}) {
    assert(request("/api/pairing",admin,{{"windowMs",bad}})==409 && pairingWindowMs==60000);
  }
  assert(request("/api/pairing",admin)==400);
  assert(request("/api/pairing",admin,{{"windowMs","90000"}})==200 && pairingWindowMs==90000);
  assert(TurnHub::fixturePairingWindowSaved==90000);
  ProfileFixture::gameSettingsWritable=false;
  assert(request("/api/pairing",admin,{{"windowMs","120000"}})==409 && pairingWindowMs==90000);
  ProfileFixture::gameSettingsWritable=true;

  freshLobby(2); pairingActive=false;
  Intent pair; pair.type=IntentType::PairRequest; pair.actor.origin=IntentOrigin::AtlasHardware;
  assert(intents.dispatch(pair).accepted() && TurnHub::fixturePairingWindowMs==90000);
  testNow+=89999; updatePairingWindow(testNow); assert(pairingActive);
  ++testNow; updatePairingWindow(testNow); assert(!pairingActive);
  assert(request("/api/pairing",admin,{{"windowMs","60000"}})==200 && pairingWindowMs==60000);
  enterEmptyLobby();
}

// First-run guided setup (FIRST_RUN_SETUP.md): the boot stage, the Welcome
// and "You're all set" screens, and the phone's steps over HTTP, which the
// Android app and the portal share.
namespace TurnHub { extern int fixtureSetupStageSaved; }
namespace TurnHubAccounts { extern String primary; extern std::map<std::string,Account> accounts; }
// The Hello capability byte carries only what varies (protocol 3).
static void helloCapabilityLayout() {
  using namespace TurnHubProtocol;
  assert(helloCapabilities(encodeHelloInfo(0,9,0,CAPABILITY_DISPLAY_OLED)) == CAPABILITY_DISPLAY_OLED);
  assert(helloCapabilities(encodeHelloInfo(0,9,0,0)) == 0);  // E-paper.
  assert(helloCapabilities(encodeHelloInfo(1,2,3,CAPABILITY_HARNESS)) == CAPABILITY_HARNESS);
  assert(helloFirmwareMajor(encodeHelloInfo(1,2,3,0)) == 1 && helloFirmwareMinor(encodeHelloInfo(1,2,3,0)) == 2 &&
      helloFirmwarePatch(encodeHelloInfo(1,2,3,0)) == 3);
}
static void firstRunSetup() {
  using TurnHub::SetupStage;
  TurnHubWebApi::configureDevices(manageDevices, []() { return pairingWindowMs; });
  TurnHubWebApi::configureSetup([]() { return static_cast<uint8_t>(setupStage); });
  const String previousPrimary=TurnHubAccounts::primary;
  const bool previousRadio=TurnHub::fixtureRadio;
  freshLobby(2); resetPresence(); resetTouchControls(); testNow+=1000; pairingActive=false;
  Preferences::strings().erase(AtlasConfig::WIFI_PREF_KEY);

  // Boot: an Atlas set up before this feature (an Admin exists) skips it; a
  // new one starts at Welcome; either answer is saved, and a saved stage wins.
  TurnHub::fixtureSetupStageSaved=-1; TurnHubAccounts::primary="00000001";
  beginFirstRunSetup(); assert(setupStage==SetupStage::Complete && TurnHub::fixtureSetupStageSaved==2);
  TurnHub::fixtureSetupStageSaved=-1; TurnHubAccounts::primary="";
  beginFirstRunSetup(); assert(setupStage==SetupStage::Welcome && TurnHub::fixtureSetupStageSaved==0);
  TurnHubAccounts::primary="00000001"; beginFirstRunSetup(); assert(setupStage==SetupStage::Welcome);
  TurnHubAccounts::primary="";
  // Storage faults never lock the table in setup.
  assert(TurnHub::bootSetupStage(TurnHubStorage::Status::IoError,SetupStage::Welcome,false)==SetupStage::Complete);
  assert(TurnHub::bootSetupStage(TurnHubStorage::Status::Corrupt,SetupStage::Welcome,false)==SetupStage::Complete);

  // Welcome, in words: the app first, then the Wi-Fi by hand. No QR code.
  AtlasScreen screen=currentScreen();
  assert(screen.kind==ScreenKind::Setup && strcmp(screen.badge,"SETUP")==0);
  assert(strcmp(screen.title,"Welcome to TurnHub")==0 && screen.qr[0]=='\0' && screen.lineCount==5);
  assert(strstr(screen.lines[0],"TurnHub app") && strstr(screen.lines[2],AtlasConfig::WIFI_SSID));
  assert(strstr(screen.lines[3],AtlasConfig::WIFI_DEFAULT_PASSWORD) && strstr(screen.lines[4],"192.168.4.1"));
  assert(screenButton(screen,TouchAction::SkipSetup) && screenButton(screen,TouchAction::OpenMenu));
  // Pairing is part of setup (one update prompt for every device): the
  // Welcome screen opens it and shows the countdown.
  tapButton(TouchAction::Pair);
  assert(pairingActive && currentScreen().kind==ScreenKind::Setup &&
      startsWith(currentScreen().detail,"Pairing open"));
  pairingActive=false;
  // Skip for now shows the lobby; Setup under Menu brings Welcome back.
  tapButton(TouchAction::SkipSetup); assert(currentScreen().kind==ScreenKind::Status);
  openMenuScreen(); tapButton(TouchAction::OpenSetup); assert(currentScreen().kind==ScreenKind::Setup);
  // Only in the lobby: a game played before setup shows the game.
  startFromHost(); assert(currentScreen().kind==ScreenKind::Status);
  enterEmptyLobby(); { const uint32_t now=testNow; freshLobby(2); testNow=now; }
  assert(currentScreen().kind==ScreenKind::Setup);

  // The phone's first look, without signing in, never shows the password.
  assert(request("/api/setup","",{},HTTP_GET)==200);
  assert(server.body.find("\"stage\":\"welcome\"")!=std::string::npos &&
      server.body.find("\"adminExists\":false")!=std::string::npos &&
      server.body.find("\"passwordIsDefault\":true")!=std::string::npos &&
      server.body.find(AtlasConfig::WIFI_DEFAULT_PASSWORD)==std::string::npos);

  // Step 1, an account; step 2, the table code, which makes it the Admin.
  String ownerId,guestId;
  const String owner=registerPhone("Owner",ownerId),guest=registerPhone("Guest",guestId);
  assert(request("/api/setup/finish",owner,{{"password","table-pass-1"}})==403);
  verifyAtTable(owner); assert(request("/api/accounts/setup",owner)==200);
  assert(request("/api/setup","",{},HTTP_GET)==200 && server.body.find("\"adminExists\":true")!=std::string::npos);
  assert(request("/api/setup/finish",guest,{{"password","table-pass-1"}})==403);

  // Step 3, the Wi-Fi password: never the printed one, 8 to 63 characters.
  assert(request("/api/setup/finish",owner,{{"password",AtlasConfig::WIFI_DEFAULT_PASSWORD}})==400);
  assert(request("/api/setup/finish",owner,{{"password","short"}})==400);
  assert(setupStage==SetupStage::Welcome);
  // The validator: an Admin with a private password, not the touchscreen.
  Intent advance; advance.type=IntentType::AdvanceSetup; advance.actor.origin=IntentOrigin::Browser;
  strncpy(advance.payload.moderatorId,ownerId.c_str(),8);
  advance.payload.value=static_cast<int32_t>(SetupStage::Finished);
  assert(intents.dispatch(advance).status==IntentStatus::Rejected);  // Still the printed password.
  advance.actor.origin=IntentOrigin::AtlasHardware;
  assert(intents.dispatch(advance).status==IntentStatus::Unauthorized);
  advance.actor.origin=IntentOrigin::Browser; advance.payload.value=7;
  assert(intents.dispatch(advance).status==IntentStatus::Rejected && setupStage==SetupStage::Welcome);
  // Not mid-match (Atlas restarts afterwards). freshLobby rewinds the test
  // clock, which would expire the owner's table code.
  { const uint32_t now=testNow; freshLobby(2); testNow=now; }
  startFromHost();
  assert(request("/api/setup/finish",owner,{{"password","table-pass-1"}})==409 && setupStage==SetupStage::Welcome);
  // A refused finish leaves the Wi-Fi as it was (the printed password).
  assert(!TurnHub::validWifiPassword(TurnHub::readStoredWifiPassword()));
  enterEmptyLobby();

  // Step 4, finish: the password is stored, the stage saved, Atlas restarts.
  TurnHub::fixtureRadio=false;  // No Sigils paired: "You're all set" says to pair them.
  assert(request("/api/setup/finish",owner,{{"password","table-pass-1"}})==200);
  assert(server.body.find("\"restarting\":true")!=std::string::npos);
  assert(setupStage==SetupStage::Finished && TurnHub::fixtureSetupStageSaved==1);
  assert(Preferences::strings()[AtlasConfig::WIFI_PREF_KEY]=="table-pass-1");
  TurnHub::fixtureRadio=previousRadio;
  assert(request("/api/setup/finish",owner,{{"password","table-pass-2"}})==409);
  assert(request("/api/setup","",{},HTTP_GET)==200 && server.body.find("\"stage\":\"finished\"")!=std::string::npos &&
      server.body.find("\"passwordIsDefault\":false")!=std::string::npos &&
      server.body.find("table-pass-1")==std::string::npos);
  advance.payload.value=static_cast<int32_t>(SetupStage::Welcome);
  assert(intents.dispatch(advance).status==IntentStatus::InvalidState);

  // "You're all set": no password on screen, Pair a Sigil or Done.
  screen=currentScreen();
  assert(screen.kind==ScreenKind::Setup && strcmp(screen.title,"You're all set")==0);
  for (uint8_t i=0;i<screen.lineCount;++i) assert(!strstr(screen.lines[i],"table-pass-1"));
  assert(screenButton(screen,TouchAction::SetupPair) && screenButton(screen,TouchAction::SetupDone) &&
      !screenButton(screen,TouchAction::SkipSetup));
  advance.payload.value=static_cast<int32_t>(SetupStage::Complete);
  strncpy(advance.payload.moderatorId,guestId.c_str(),8);
  assert(intents.dispatch(advance).status==IntentStatus::Unauthorized && setupStage==SetupStage::Finished);
  // Pair a Sigil leaves setup and opens pairing.
  tapButton(TouchAction::SetupPair);
  assert(setupStage==SetupStage::Complete && TurnHub::fixtureSetupStageSaved==2 && pairingActive);
  assert(currentScreen().kind==ScreenKind::Status);
  advance.actor.origin=IntentOrigin::AtlasHardware;
  assert(intents.dispatch(advance).status==IntentStatus::InvalidState);
  openMenuScreen(); assert(!screenButton(currentScreen(),TouchAction::OpenSetup));
  tapButton(TouchAction::CloseScreen);

  // Sigils paired during the phone's steps: nothing is left for "You're all
  // set" to say, so finishing goes straight to Complete. The test harness
  // alone doesn't count as a Sigil.
  TurnHub::fixtureRadio=true;
  TurnHub::SigilRecord savedRecords[MAX_PHYSICAL_SIGILS]; memcpy(savedRecords,TurnHub::fixtureRecords,sizeof(savedRecords));
  for (auto &record : TurnHub::fixtureRecords) { record.helloInfoValid=true; record.capabilities=TurnHubProtocol::CAPABILITY_HARNESS; }
  setupStage=SetupStage::Welcome; Preferences::strings().erase(AtlasConfig::WIFI_PREF_KEY);
  assert(request("/api/setup/finish",owner,{{"password","table-pass-3"}})==200);
  assert(setupStage==SetupStage::Finished && TurnHub::fixtureSetupStageSaved==1);
  TurnHub::fixtureRecords[1].capabilities=0;
  setupStage=SetupStage::Welcome; Preferences::strings().erase(AtlasConfig::WIFI_PREF_KEY);
  assert(request("/api/setup/finish",owner,{{"password","table-pass-3"}})==200);
  assert(server.body.find("\"restarting\":true")!=std::string::npos);
  assert(setupStage==SetupStage::Complete && TurnHub::fixtureSetupStageSaved==2);
  assert(currentScreen().kind==ScreenKind::Status);
  memcpy(TurnHub::fixtureRecords,savedRecords,sizeof(savedRecords)); TurnHub::fixtureRadio=previousRadio;

  assert(request("/api/session/logout",owner)==200 && request("/api/session/logout",guest)==200);
  // The fixture's profile store is small; later scenarios need the room.
  for (const String &id : {ownerId,guestId}) {
    ProfileFixture::profiles.erase(id.c_str()); TurnHubAccounts::accounts.erase(id.c_str());
  }
  TurnHubAccounts::primary=previousPrimary; Preferences::strings().erase(AtlasConfig::WIFI_PREF_KEY);
  TurnHub::fixtureSetupStageSaved=-1; setupStage=SetupStage::Complete; pairingActive=false;
  resetPresence(); resetTouchControls(); enterEmptyLobby();
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

// An Admin verified at the table (presence code) returns the
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
  choose(0,SigilAction::Join); choose(1,SigilAction::Join); completedGames=0; assert(lobby.playerCount()==2);
  choose(0,SigilAction::StartGame); assert(hubState==HubState::Starting);
  assert(request("/api/table/reset",admin)==200 && hubState==HubState::Lobby && lobby.playerCount()==0);
  assert(completedGames==0);

  // Verification lapses after PRESENCE_GRANT_MS, even for the Admin.
  choose(0,SigilAction::Join); choose(1,SigilAction::Join); testNow+=PRESENCE_GRANT_MS; updatePairingWindow(testNow);
  assert(request("/api/table/reset",admin)==403 && lobby.playerCount()==2);
  strncpy(forged.payload.moderatorId,adminId.c_str(),8);
  assert(intents.dispatch(forged).status==IntentStatus::Unauthorized && lobby.playerCount()==2);
  enterEmptyLobby();
}

// Factory reset from Device Settings: Admin verified at the table (presence code),
// between games. A Sigil is told to erase itself and is forgotten; Atlas
// erases its NVS and restarts after the reply has gone.
extern unsigned fixtureFactoryResets;
extern unsigned fixtureSdWipes;
extern bool fixtureSdWipedBeforeErase;
// Pairing v2 code check: the Atlas screen or a portal Admin, lobby only,
// only for a Sigil that is actually waiting; confirm stores, reject doesn't.
static void pairConfirmIntent() {
  freshLobby(2);
  String adminId,playerId;
  registerPhone("Pair admin",adminId); registerPhone("Pair player",playerId);
  TurnHubAccounts::Account account; account.permissions=TurnHubAccounts::Admin;
  assert(TurnHubAccounts::save(adminId,account));
  const auto decide=[](IntentOrigin origin,const String &who,int32_t value) {
    Intent intent; intent.type=IntentType::PairConfirm; intent.actor.origin=origin;
    intent.payload.value=value; strncpy(intent.payload.moderatorId,who.c_str(),8);
    return intents.dispatch(intent).status;
  };
  const int32_t accept=TurnHub::PAIR_CONFIRM_ACCEPT;

  // Nothing waiting: refused.
  assert(decide(IntentOrigin::AtlasHardware,"",5|accept)==IntentStatus::InvalidActor);
  TurnHub::fixtureWaitForCode(5,427);
  assert(sigilBus.pendingPairingCount()==1 && sigilBus.pendingPairing(5)->code==427);
  // Not an Admin, a Sigil, or out of range: refused and still waiting.
  assert(decide(IntentOrigin::Browser,playerId,5|accept)==IntentStatus::Unauthorized);
  assert(decide(IntentOrigin::PhysicalSigil,adminId,5|accept)==IntentStatus::Unauthorized);
  assert(decide(IntentOrigin::AtlasHardware,"",99|accept)==IntentStatus::InvalidActor);
  assert(decide(IntentOrigin::AtlasHardware,"",-1)==IntentStatus::InvalidActor);
  assert(sigilBus.pendingPairing(5) && TurnHub::fixturePairDecisions[5]==0);
  // Not during a match.
  startFromHost();
  assert(decide(IntentOrigin::AtlasHardware,"",5|accept)==IntentStatus::InvalidState);
  enterEmptyLobby();
  TurnHub::fixtureWaitForCode(5,427);

  // The Atlas screen confirms; the Sigil is stored with its key.
  assert(decide(IntentOrigin::AtlasHardware,"",5|accept)==IntentStatus::Accepted);
  assert(TurnHub::fixturePairDecisions[5]==1 && !sigilBus.pendingPairing(5) &&
      sigilBus.record(5));
  // A portal Admin rejects another; nothing is stored.
  TurnHub::fixtureWaitForCode(6,1234);
  assert(decide(IntentOrigin::Browser,adminId,6)==IntentStatus::Accepted);
  assert(TurnHub::fixturePairDecisions[6]==-1 && !sigilBus.pendingPairing(6));
  // A failed store is reported, and the Sigil is told no.
  TurnHub::fixtureWaitForCode(7,9);
  TurnHub::fixturePairStoreFails=true;
  assert(decide(IntentOrigin::AtlasHardware,"",7|accept)==IntentStatus::Rejected);
  assert(TurnHub::fixturePairDecisions[7]==-1 && !sigilBus.pendingPairing(7));
  TurnHub::fixturePairStoreFails=false;
  enterEmptyLobby();
}

// The Atlas screen shows a waiting Sigil's code over the lobby, with Codes
// match and Reject acting through PairConfirm; a phone's presence code
// comes first, and the screen closes once nothing is waiting.
static void pairCodeTouchScreen() {
  resetTouchControls(); freshLobby(2);
  assert(currentScreen().kind==ScreenKind::Status);
  TurnHub::fixtureWaitForCode(2,7);
  TurnHub::fixturePending[2].startedMs=testNow;
  AtlasScreen s=currentScreen();
  assert(s.kind==ScreenKind::PairCode && strcmp(s.code,"0007")==0 && strcmp(s.badge,"PAIR")==0);
  assert(strcmp(s.title,"Pair Sigil 3")==0 && startsWith(s.detail,"Check the Sigil (60 s)"));
  assert(s.lineCount==2 && strcmp(s.lines[1],"If not, Reject.")==0);
  assert(screenButton(s,TouchAction::PairConfirm) && screenButton(s,TouchAction::PairReject));
  // A second Sigil waits its turn.
  TurnHub::fixtureWaitForCode(5,4321);
  assert(strcmp(currentScreen().lines[1],"1 more waiting after")==0);
  tapButton(TouchAction::PairConfirm);
  assert(TurnHub::fixturePairDecisions[2]==1 && !sigilBus.pendingPairing(2));
  s=currentScreen();
  assert(s.kind==ScreenKind::PairCode && strcmp(s.code,"4321")==0 && strcmp(s.title,"Pair Sigil 6")==0);
  tapButton(TouchAction::PairReject);
  assert(TurnHub::fixturePairDecisions[5]==-1 && currentScreen().kind==ScreenKind::Status);
  // Not shown outside the lobby.
  TurnHub::fixtureWaitForCode(4,1);
  startFromHost();
  assert(currentScreen().kind!=ScreenKind::PairCode);
  TurnHub::fixturePending[4]=TurnHubSecureLink::PendingPairing{};
  enterEmptyLobby();
}

// The portal's pairing-code check: listed in /api/devices, answered by an
// Admin verified at the table, through PairConfirm.
static void pairConfirmFromPortal() {
  TurnHubWebApi::configureDevices(manageDevices, []() { return pairingWindowMs; });
  TurnHubWebApi::configurePresence(presenceHooks());
  resetPresence(); resetTouchControls();
  freshLobby(2);
  String adminId,playerId;
  const String admin=registerPhone("Portal pair admin",adminId),player=registerPhone("Portal pair player",playerId);
  TurnHubAccounts::Account account; account.permissions=TurnHubAccounts::Admin;
  assert(TurnHubAccounts::save(adminId,account));
  TurnHub::fixtureWaitForCode(3,58); TurnHub::fixturePending[3].startedMs=testNow;

  assert(request("/api/devices",admin,{},HTTP_GET)==200);
  assert(server.body.find("\"pendingPairings\":[{\"id\":3,\"code\":\"0058\",\"secondsLeft\":60}]")!=std::string::npos);
  // Admin only, and only once verified at the table.
  assert(request("/api/device/pair-confirm",admin,{{"module","3"},{"accept","1"}})==403);
  verifyAtTable(admin);
  assert(request("/api/device/pair-confirm",player,{{"module","3"},{"accept","1"}})==403);
  assert(request("/api/device/pair-confirm",admin,{{"accept","1"}})==400);
  assert(request("/api/device/pair-confirm",admin,{{"module","4"},{"accept","1"}})==409);
  assert(TurnHub::fixturePairDecisions[3]==0 && sigilBus.pendingPairing(3));
  // Codes match: stored with its key, and listed.
  assert(request("/api/device/pair-confirm",admin,{{"module","3"},{"accept","1"}})==200);
  assert(TurnHub::fixturePairDecisions[3]==1 && !sigilBus.pendingPairing(3));
  assert(request("/api/devices",admin,{},HTTP_GET)==200);
  assert(server.body.find("\"pendingPairings\":[]")!=std::string::npos &&
      server.body.find("{\"id\":3,")!=std::string::npos);
  // Reject stores nothing.
  TurnHub::fixtureWaitForCode(6,1);
  assert(request("/api/device/pair-confirm",admin,{{"module","6"},{"accept","0"}})==200);
  assert(TurnHub::fixturePairDecisions[6]==-1);
}

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
  assert(server.body.find("microSD")!=std::string::npos);
  serviceFactoryReset(millis()); assert(fixtureFactoryResets==0 && fixtureSdWipes==0);
  // The microSD card is emptied first, then NVS (owner decision 2026-09-29).
  serviceFactoryReset(millis()+2000);
  assert(fixtureFactoryResets==1 && fixtureSdWipes==1 && fixtureSdWipedBeforeErase && !factoryResetScheduled());
  serviceFactoryReset(millis()+4000); assert(fixtureFactoryResets==1 && fixtureSdWipes==1);
  TurnHub::fixtureUnpairs[3]=0;
  resetPresence();
}

// The board's BOOT button: quick press pairs, a 3 s hold forgets every Sigil, a
// 10 s hold factory resets Atlas, even mid-match (the backup for a frozen
// screen). Each gesture is an AtlasHardware Intent; the handlers still apply
// their rules (a Sigil with seated players is kept).
static uint32_t bootT=1000000;
static void bootPress(uint32_t heldMs) {
  updateBootButton(false,bootT); bootT+=100; updateBootButton(false,bootT); bootT+=100;  // Seen released.
  updateBootButton(true,bootT); bootT+=40; updateBootButton(true,bootT);                   // Debounced down.
  for(uint32_t held=0;held<heldMs;held+=100){ bootT+=100; updateBootButton(true,bootT); }  // The loop keeps running.
  bootT+=100; updateBootButton(false,bootT); bootT+=40; updateBootButton(false,bootT); bootT+=100;
}
static void bootButtonGestures() {
  resetPresence(); resetTouchControls();
  freshLobby(2);
  TurnHub::fixtureUnpairs[3]=0; fixtureFactoryResets=0;

  // Quick press: the pairing window opens (on release).
  pairingActive=false;
  bootPress(500);
  assert(pairingActive);

  // Medium hold: forget all Sigils, but never one with seated players.
  assert(sigilBus.record(0) && sigilBus.record(3));
  pairingActive=false;
  bootPress(TurnHubProtocol::UNPAIR_HOLD_MS+500);
  assert(sigilBus.record(0) && sigilBus.record(3) && TurnHub::fixtureUnpairs[3]==0);
  assert(!pairingActive);  // A hold never pairs on release.
  enterEmptyLobby();
  bootPress(TurnHubProtocol::UNPAIR_HOLD_MS+500);
  assert(!sigilBus.record(0) && !sigilBus.record(3) && TurnHub::fixtureUnpairs[3]==1);
  assert(!factoryResetScheduled() && fixtureFactoryResets==0);

  // Long hold in a match: Atlas erases itself, no Admin or table code needed. A
  // Sigil in the match is not forgotten on the way (forget all is lobby-only).
  freshLobby(2); startFromHost();
  assert(!factoryResetScheduled());
  bootPress(TurnHubProtocol::FACTORY_RESET_HOLD_MS+500);
  assert(factoryResetScheduled() && sigilBus.record(0));
  serviceFactoryReset(millis()); assert(fixtureFactoryResets==0);
  serviceFactoryReset(millis()+2000);
  assert(fixtureFactoryResets==1 && !factoryResetScheduled());
  enterEmptyLobby();
  TurnHub::fixtureUnpairs[3]=0;
}

// Menu > Device on the touchscreen (owner 2026-10-02): the BOOT button's
// Unpair and Factory reset as held buttons, between games only.
static void touchDeviceScreen() {
  resetPresence(); resetTouchControls();
  freshLobby(2);
  TurnHub::fixtureUnpairs[3]=0; fixtureFactoryResets=0;
  openMenuScreen();
  AtlasScreen s=currentScreen();
  assert(screenButton(s,TouchAction::OpenDevice) && s.buttonCount<=MAX_TOUCH_BUTTONS);
  tapButton(TouchAction::OpenDevice); s=currentScreen();
  assert(s.kind==ScreenKind::Device && String(s.badge)=="DEVICE");
  const TouchButton *unpair=screenButton(s,TouchAction::UnpairSigils);
  const TouchButton *reset=screenButton(s,TouchAction::FactoryResetAtlas);
  assert(unpair && reset && screenButton(s,TouchAction::CloseScreen));
  assert(unpair->holdMs==TurnHubProtocol::UNPAIR_HOLD_MS && reset->holdMs==TurnHubProtocol::FACTORY_RESET_HOLD_MS);
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.y>=SCREEN_BODY_Y && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);

  // A tap does nothing; a short hold says how long to keep holding.
  tapButton(TouchAction::UnpairSigils);
  assert(sigilBus.record(0) && sigilBus.record(3));
  pressButton(TouchAction::UnpairSigils); testNow+=1000; pressButton(TouchAction::UnpairSigils);
  assert(String(currentScreen().notice)=="Keep holding for 3 s to unpair every Sigil");
  touchRelease(); assert(sigilBus.record(0));
  // Held through: the handler still keeps a Sigil with seated players.
  pressButton(TouchAction::UnpairSigils); testNow+=DEVICE_UNPAIR_HOLD_MS; pressButton(TouchAction::UnpairSigils); touchRelease();
  assert(sigilBus.record(0) && sigilBus.record(3) && TurnHub::fixtureUnpairs[3]==0);
  // With everyone out of the lobby, every Sigil is forgotten.
  enterEmptyLobby(); resetTouchControls(); openMenuScreen(); tapButton(TouchAction::OpenDevice);
  pressButton(TouchAction::UnpairSigils); testNow+=DEVICE_UNPAIR_HOLD_MS; pressButton(TouchAction::UnpairSigils); touchRelease();
  assert(!sigilBus.record(0) && !sigilBus.record(3) && TurnHub::fixtureUnpairs[3]==1);
  assert(String(currentScreen().notice)=="All Sigils forgotten" && !factoryResetScheduled());
  // Back returns to the Menu.
  tapButton(TouchAction::CloseScreen); assert(currentScreen().kind==ScreenKind::Menu);

  // After a game: no Unpair (lobby only), but Factory reset, held 10 s.
  freshLobby(2); startFromHost(); holdEndMatch();
  assert(hubState==HubState::GameOver);
  openMenuScreen(); tapButton(TouchAction::OpenDevice); s=currentScreen();
  assert(s.kind==ScreenKind::Device && !screenButton(s,TouchAction::UnpairSigils) &&
      screenButton(s,TouchAction::FactoryResetAtlas));
  pressButton(TouchAction::FactoryResetAtlas); testNow+=DEVICE_RESET_HOLD_MS-1000; pressButton(TouchAction::FactoryResetAtlas);
  touchRelease(); assert(!factoryResetScheduled());
  pressButton(TouchAction::FactoryResetAtlas); testNow+=DEVICE_RESET_HOLD_MS; pressButton(TouchAction::FactoryResetAtlas);
  touchRelease();
  assert(factoryResetScheduled());
  serviceFactoryReset(millis()+2000);
  assert(fixtureFactoryResets==1 && !factoryResetScheduled());
  // A game starting closes the Device screen with the Menu.
  enterEmptyLobby(); freshLobby(2); resetTouchControls(); openMenuScreen(); tapButton(TouchAction::OpenDevice);
  startFromHost(); assert(currentScreen().kind==ScreenKind::Status);
  enterEmptyLobby(); resetTouchControls();
  TurnHub::fixtureUnpairs[3]=0;
}

// Menu > Device Sleep (owner 2026-10-02): a tap, between games, from the
// touchscreen only; Atlas sleeps once the notice has been shown.
extern unsigned fixtureSleeps;
static void touchDeviceSleep() {
  resetPresence(); resetTouchControls();
  freshLobby(2); fixtureSleeps=0; fixtureFactoryResets=0;
  openMenuScreen(); tapButton(TouchAction::OpenDevice);
  AtlasScreen s=currentScreen();
  const TouchButton *sleep=screenButton(s,TouchAction::SleepAtlas);
  assert(sleep && !sleep->hold() && screenButton(s,TouchAction::CloseScreen) && s.buttonCount<=MAX_TOUCH_BUTTONS);
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);
  // Only the touchscreen may ask, and never in a match.
  Intent web; web.type=IntentType::Sleep; web.actor.origin=IntentOrigin::Browser;
  assert(intents.dispatch(web).status==IntentStatus::Unauthorized && !sleepScheduled());
  tapButton(TouchAction::SleepAtlas);
  assert(sleepScheduled() && String(currentScreen().notice)=="Going to sleep. Touch the screen to wake");
  // Nothing else erases or updates Atlas meanwhile.
  Intent reset; reset.type=IntentType::FactoryReset; reset.actor.origin=IntentOrigin::AtlasHardware;
  reset.payload.value=TurnHub::FACTORY_RESET_ATLAS;
  assert(intents.dispatch(reset).status==IntentStatus::Conflict && !factoryResetScheduled());
  serviceSleep(millis()+1000); assert(fixtureSleeps==0 && sleepScheduled());
  serviceSleep(millis()+3000); assert(fixtureSleeps==1 && !sleepScheduled());
  serviceSleep(millis()+6000); assert(fixtureSleeps==1);
  // After a game it is offered too; during one it is refused.
  startFromHost(); holdEndMatch(); assert(hubState==HubState::GameOver);
  openMenuScreen(); tapButton(TouchAction::OpenDevice);
  assert(screenButton(currentScreen(),TouchAction::SleepAtlas));
  enterEmptyLobby(); freshLobby(2); startFromHost();
  Intent touch; touch.type=IntentType::Sleep; touch.actor.origin=IntentOrigin::AtlasHardware;
  assert(intents.dispatch(touch).status==IntentStatus::InvalidState && !sleepScheduled());
  enterEmptyLobby(); resetTouchControls();
  assert(fixtureFactoryResets==0);
}

struct CompletionPowerLoss {};
static unsigned completionWritesBeforeLoss = 0;
static void interruptCompletionWrite() {
  if (--completionWritesBeforeLoss == 0) throw CompletionPowerLoss{};
}

static void prepareCompletionProbe(GameEngine &subject, Lobby &subjectLobby) {
  testBlobs.clear(); checkpointReadError = ESP_OK;
  setError = ESP_OK; injectedCommitError = ESP_OK;
  testNow = 5000;
  ProfileFixture::profiles["CAFE0001"] = ProfileFixture::Profile{};
  ProfileFixture::profiles["CAFE0002"] = ProfileFixture::Profile{};
  assert(beginGameRecovery(subject, subjectLobby, testNow) == TurnHubStorage::Status::NotFound);
  PlayerSeat seats[] = {PlayerSeat{1, 0}, PlayerSeat{2, 1}};
  strcpy(seats[0].profileId, "CAFE0001"); strcpy(seats[1].profileId, "CAFE0002");
  assert(subject.start(seats, 2, seats[0], testNow));
  assert(checkpointGame(subject, testNow) == TurnHubStorage::Status::Ok);
  testNow += 1000;
}

static void completionRecoveryOrdering() {
  using TurnHubStorage::Status;
  // Draw, confirmed win and last-player-standing all share the real bridge.
  // Interrupt within profile writes, before the Intent observer can run.
  for (unsigned ending = 0; ending < 3; ++ending) {
    for (unsigned cutAfter = 1; cutAfter <= 2; ++cutAfter) {
      GameEngine subject; Lobby subjectLobby;
      prepareCompletionProbe(subject, subjectLobby);
      completionWritesBeforeLoss = cutAfter;
      ProfileFixture::afterStatsSave = interruptCompletionWrite;
      bool interrupted = false;
      try {
        if (ending == 0) {
          subject.endInDraw(testNow);
        } else if (ending == 1) {
          assert(subject.beginWinClaim(1, true, testNow));
          bool finished = false; subject.confirmWinClaim(2, testNow, finished);
        } else {
          assert(subject.pause(testNow));
          bool finished = false; subject.eliminatePlayer(2, testNow, finished);
        }
      } catch (const CompletionPowerLoss &) { interrupted = true; }
      ProfileFixture::afterStatsSave = nullptr;
      assert(interrupted);
      assert(ProfileFixture::profiles["CAFE0001"].stats.gamesPlayed == 1);
      assert(ProfileFixture::profiles["CAFE0002"].stats.gamesPlayed == (cutAfter == 2 ? 1u : 0u));
      const int beforeRestore = completedGames;
      GameEngine rebooted; Lobby rebootedLobby;
      assert(beginGameRecovery(rebooted, rebootedLobby, testNow + 600000) == Status::Ok);
      assert(rebooted.gameOver() && rebooted.winnerPlayerNumber() == (ending == 0 ? 0 : 1));
      assert(!rebooted.endInDraw(testNow + 601000));
      assert(completedGames == beforeRestore);  // No unsafe aggregate replay.
      assert(ProfileFixture::profiles["CAFE0001"].stats.gamesPlayed == 1);
      assert(ProfileFixture::profiles["CAFE0002"].stats.gamesPlayed == (cutAfter == 2 ? 1u : 0u));
    }
  }

  // Refuse increments after failed or uncertain checkpoint writes, read errors,
  // corrupt records or future schemas. The later observer must not bypass this.
  for (unsigned fault = 0; fault < 5; ++fault) {
    GameEngine subject; Lobby subjectLobby;
    prepareCompletionProbe(subject, subjectLobby);
    if (fault == 0) setError = ESP_ERR_NVS_INVALID_HANDLE;
    if (fault == 1) injectedCommitError = ESP_ERR_NVS_INVALID_HANDLE;
    if (fault == 2) checkpointReadError = ESP_ERR_NVS_INVALID_HANDLE;
    if (fault == 3) testBlobs["checkpoint"][0] ^= 0xFF;
    if (fault == 4) testBlobs["checkpoint"][4] = 99;
    const auto before = testBlobs["checkpoint"];
    const Status expected = fault < 3 ? Status::IoError :
        (fault == 3 ? Status::Corrupt : Status::UnsupportedSchema);
    assert(subject.endInDraw(testNow));
    assert(TurnHub::gameRecoveryStatus() == expected);
    assert(ProfileFixture::profiles["CAFE0001"].stats.gamesPlayed == 0);
    assert(ProfileFixture::profiles["CAFE0002"].stats.gamesPlayed == 0);
    assert(serialLog.snapshot().find("ATLAS|PROFILE_STATS|SKIPPED_CHECKPOINT|") != std::string::npos);
    const auto after = testBlobs["checkpoint"];
    if (fault != 1) assert(before == after);
    checkpointReadError = ESP_OK; setError = ESP_OK; injectedCommitError = ESP_OK;
    assert(checkpointGame(subject, testNow) == expected && testBlobs["checkpoint"] == after);
    GameEngine rebooted; Lobby rebootedLobby;
    const auto restored = beginGameRecovery(rebooted, rebootedLobby, testNow + 600000);
    if (fault < 3) {
      assert(restored == Status::Ok);
      // The stub's failed commit can still have written the finished record.
      assert(rebooted.gameOver() == (fault == 1));
      if (fault != 1) assert(rebooted.paused());
    } else {
      assert(restored == expected && !rebooted.hasPlayers());
    }
  }
  checkpointReadError = ESP_OK;
  testBlobs.clear();
}

static void gameRecoveryLifecycle() {
  using TurnHubStorage::Status;

  testBlobs.clear();
  checkpointReadError = ESP_OK; openError = ESP_OK; setError = ESP_OK;

  // 1) First boot ever: nothing has been saved.
  freshLobby(2);
  testBlobs.clear();  // Ignore any fixture-reset observer checkpoint.
  testNow = 5000;
  assert(TurnHub::beginGameRecovery(game, lobby, testNow) == Status::NotFound);
  assert(!game.hasPlayers());

  // 2) Start a match and accept a semantic transition (a committed pass).
  // main.cpp's observer persists a checkpoint after every dispatched intent;
  // no code here calls checkpointGame() directly.
  startFromHost();
  choose(0,SigilAction::Pass);
  testNow += PASS_GRACE_MS; updatePendingPass(testNow);
  assert(!pendingPass.active && game.activePlayerNumber() == 2);
  assert(testBlobs.count("checkpoint") == 1);
  testNow += 45000; // 45s of real play before "power loss".
  choose(1,SigilAction::Pass);
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

  checkpointReadError = ESP_OK;
  testBlobs.clear();
  enterEmptyLobby();
}

#include "ota_service_scenarios.inc"

static void stackDiagnostics() {
  TurnHub::LowStackWarning warning;
  assert(!warning.due(3072,100));
  assert(warning.due(3071,100));
  assert(!warning.due(1000,30099));
  assert(warning.due(1000,30100));
  TurnHub::LowStackWarning wrap;
  assert(wrap.due(0,UINT32_MAX-100));
  assert(!wrap.due(1000,50));
  assert(wrap.due(1000,30000));
  const uint32_t savedNow=testNow;
  TurnHub::serialLog.clear();
  testNow=UINT32_MAX-10;
  {
    TurnHub::HttpRequestTrace trace("/api/v1/state");
    testNow=9;
  }
#if TURNHUB_HTTP_TRACE
  const auto log=TurnHub::serialLog.snapshot();
  assert(log.find("ATLAS|HTTP|BEGIN|id=")!=std::string::npos);
  assert(log.find("ATLAS|HTTP|END|id=")!=std::string::npos);
  assert(log.find("|route=/api/v1/state|durationMs=20|")!=std::string::npos);
#else
  assert(TurnHub::serialLog.snapshot().empty());
#endif
  const auto json=TurnHub::runtimeDiagnosticsJson();
  assert(json.find("\"loopStackSizeBytes\":16384")!=std::string::npos);
  assert(json.find("\"loopStackMinimumFreeBytes\":0")!=std::string::npos);
  assert(json.find("loopStackFreeBytes")==std::string::npos);
  testNow=savedNow;
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
  stackDiagnostics(); std::cout<<"PASS stack watermark semantics, warning rate limit, rollover and request tracing\n";
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
  touchTableLifecycle(); std::cout<<"PASS touchscreen Start, Cancel start, Rematch and Reset between games, lobby Clear hold\n";
  masterPass(); std::cout<<"PASS master pass: Table screen hold, Atlas hardware only, immediate, logged, overrides a queued PASS\n";
  touchCalibrationMath(); std::cout<<"PASS touch calibration: solve, swap/invert, offset panel, refusals, clamp, lobby-only\n";
  touchControls(); std::cout<<"PASS touchscreen: Pair, Start, presence code screen/cancel/expiry, Pause/Resume, Table screen, end-match hold, slide-off, drop-out, stale press\n";
  harnessScreen(); std::cout<<"PASS test harness screen: Tests button, premade tests, progress, stop, stale reports, offline\n";
  updateNotice(); std::cout<<"PASS update notice: app-reported versions, devices behind (harness excluded), Menu and Info text\n";
  atlasScreens(); std::cout<<"PASS Atlas screens: player chips, NO SD CARD, info, QR codes (Wi-Fi, portal, sign in), turn clock" << std::endl;
  oledSigilSeatsTwoPlayers(); std::cout<<"PASS OLED and e-paper shared seats: chord, join, leave and game start\n";
  atlasSpeaker(); std::cout<<"PASS Atlas speaker: table-wide cues, phone-only table, Sigil mute independence, admin volume setting\n";
  resetTableFromPortal(); std::cout<<"PASS admin returns the table to an empty lobby: permission, presence code (wrong, too many, other phone, expiry), draw once, countdown\n";
  pairConfirmIntent(); std::cout<<"PASS pairing v2 code check: Atlas screen or portal Admin, lobby only, waiting Sigil only, confirm stores, reject and store failure store nothing" << std::endl;
  pairCodeTouchScreen(); std::cout<<"PASS pairing code on the Atlas screen: shown in the lobby after presence codes, Codes match and Reject, one Sigil at a time" << std::endl;
  pairConfirmFromPortal(); std::cout<<"PASS pairing code check from the portal: listed with the code, Admin verified at the table, confirm stores securely, reject stores nothing" << std::endl;
  factoryResetFromPortal(); std::cout<<"PASS factory reset: admin verified at the table, seated/in-game refusal, Sigil told and forgotten, Atlas erase after the reply" << std::endl;
  bootButtonGestures(); std::cout<<"PASS BOOT button: quick press pairs, medium hold forgets all Sigils (seated kept), long hold factory resets Atlas even mid-match" << std::endl;
  touchDeviceScreen(); std::cout<<"PASS touchscreen Device screen: held Unpair Sigils (lobby, seated kept) and Factory reset (between games)" << std::endl;
  touchDeviceSleep(); std::cout<<"PASS touchscreen Device screen: Sleep (a tap, between games, touchscreen only)" << std::endl;
  deviceManagement(); std::cout<<"PASS admin forget one/all Sigils, seated and in-game refusal, storage failure, pairing window setting\n";
  helloCapabilityLayout(); std::cout<<"PASS Hello capability layout and firmware version fields\n";
  firstRunSetup(); std::cout<<"PASS first-run setup: boot stage, Welcome and Skip, account, table code, private Wi-Fi password, finish, all set, Pair a Sigil\n";
  physicalGameDisplay(); std::cout<<"PASS physical game display snapshots, received damage, shared focus, bounds and deduplication\n";
  turnTimerEngine(); std::cout<<"PASS turn timer phases, no automatic pass, pause freeze, rollover, validation and recovery\n";
  ledCueSelection(); std::cout<<"PASS LED cue selection, default styles and profile-only presentation changes\n";
  ledStateTransport(); std::cout<<"PASS LedState transport: one packet per change, anchor age, style, legacy channel peers\n";
  profilePicker(); std::cout<<"PASS Sigil profile picker: gating, pages by name, locked/blocked profiles, stale keys, guest, confirm, policy, closing\n";
  sigilLife(); std::cout<<"PASS Sigil life: AdjustLife availability, batched own-life changes, requests shown and answered with tag checks\n";
  jewelColors(); std::cout<<"PASS personalization: Jewel color and preset avatars, validation, /api/avatars, /api/seats, TFT chips, SeatColor; custom stays private\n";
  sigilMenus(); std::cout<<"PASS Sigil menus: availability per state, defaults, MenuState2 revisions, stale choices, SelectAction Intents\n";
  turnTimerCuesAndMute(); std::cout<<"PASS one-shot timer audio cues, pause/resume, re-arm and independent mute\n";
  turnTimerSettingsHttp(); std::cout<<"PASS turn timer settings API, partial update, lobby-only edits and state projection\n";
  accessibilityPreferences(); std::cout<<"PASS per-player accessibility: LED profiles, merge rules, API, Sigil mute, hold-timing radio\n";
  actionRequiredCues(); std::cout<<"PASS ActionRequired reaches only the Sigil whose win confirmation is next\n";
  virtualCapacity(); std::cout<<"PASS virtual capacity and 16-player win confirmation\n";
  serialLogStream();
  serialLogCapture(); std::cout<<"PASS serial log capture, redaction, stream draining, ring overflow and self-describing log lines\n";
  otaServiceScenarios(); std::cout<<"PASS OTA service: authorization, variant checks, jobs, single-use downloads, fresh reboot confirmation\n";
  // Recovery fault injection runs last; see the recovery fixture comment.
  completionRecoveryOrdering(); std::cout<<"PASS completion checkpoint before stats: interrupted profile writes, failed/uncertain commits, protected records, no replay\n";
  gameRecoveryLifecycle(); std::cout<<"PASS interrupted-match recovery: boot load, checkpoint-after-intent, downtime exclusion, corrupt fail-safe\n";
}
