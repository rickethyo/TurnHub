#include <cassert>
#include <iostream>
#include <fstream>
#include <Arduino.h>
#include "profile_fixture.h"
#include "profile_statistics.h"
#include "standalone_import.h"
#include "match_archive.h"
#include "turnhub_core.h"
#ifdef _MSC_VER
// Keep the production packet's packed layout when compiling with MSVC.
#define __attribute__(...)
#pragma pack(push, 1)
#include "protocol.h"
#pragma pack(pop)
#endif
#include "battery_checkpoint.h"
#include "avatars.h"
#include "avatar_artwork.h"
#include "controller_profiles.h"
#include "profile_store.h"
#include "web_api_internal.h"
#include "web_pages.h"
// Compile the actual application entry point; the handler and adapter
// modules it binds are linked from ../../src, not copied rules.
#include "../../src/main.cpp"
#include "profile_stats_bridge.h"
#include "touch_controls.h"
#include "../../../Sigil/include/received_packet.h"
#include "../../../Sigil/include/display_name.h"

namespace TurnHub { extern uint32_t fixturePairingWindowSaved; extern int fixtureSpeakerVolumeSaved; }
static TurnHubProtocol::TableDisplayPacket sentTableDisplays[8][2]{};
bool TurnHub::SigilBus::sendTableDisplay(const TurnHubProtocol::TableDisplayPacket &p) {
  assert(TurnHubProtocol::validTableDisplay(p));
  sentTableDisplays[p.sigilId][p.page] = p;
  return true;
}
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
static TurnHubProtocol::CommanderFlowPacket sentCommander[MAX_PHYSICAL_SIGILS]{};
bool SigilBus::sendCommanderFlow(const TurnHubProtocol::CommanderFlowPacket &p) {
  assert(TurnHubProtocol::validCommanderFlow(p)); sentCommander[p.sigilId] = p; return fixtureRadio;
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
  assert(table().lobby.playerCount()==modules+(shared?1:0));
}
static void startFromHost() {
  choose(0,SigilAction::StartGame);
  assert(table().hubState==HubState::Starting);
  testNow+=2999; updateCountdown(testNow); assert(table().hubState==HubState::Starting);
  ++testNow; updateCountdown(testNow); assert(table().hubState==HubState::Running);
}
static bool web(uint8_t module,uint8_t slot,WebControl control) {
  String message;
  return handleWebControl(module,slot,control,message);
}
static PlayerSeat player(uint8_t number) { return *table().game.playerByNumber(number); }

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
  assert(table().lobby.hostController()==0);
  assert(!dispatchModuleIntent(IntentType::Join,255).accepted());
  // No table host: any seated Sigil may arm Start, not only the first to join.
  assert(dispatchModuleIntent(IntentType::ArmStart,1).accepted() && table().lobby.startArmedBy()==1);
  table().lobby.clearStartArm();
  assert(web(0,2,WebControl::SelectStarter));
  PlayerSeat selected; assert(table().lobby.selectedStarter(selected)&&selected.slot==2);
  choose(0,SigilAction::CycleStarter); assert(table().lobby.selectedStarter(selected)&&selected.slot==1);
  // Any seated Sigil may ask for a random starter now (no table host).
  choose(1,SigilAction::RandomStarter); assert(table().lobby.selectedStarter(selected));
  assert(dispatchModuleIntent(IntentType::Leave,0,2).accepted());
  assert(table().lobby.playerCount()==2&&!table().lobby.hasSecondary(0));
  assert(!dispatchModuleIntent(IntentType::Leave,0,2).accepted());
  assert(dispatchModuleIntent(IntentType::Join,0,2).accepted());
  assert(!dispatchModuleIntent(IntentType::Join,0,2).accepted());
  choose(0,SigilAction::StartGame);
  assert(table().hubState==HubState::Starting);
  assert(!dispatchModuleIntent(IntentType::Join,2).accepted());
  assert(!dispatchSystemIntent(IntentType::CompleteStart).accepted());
  choose(1,SigilAction::CancelStart); assert(table().hubState==HubState::Lobby);
  assert(!dispatchModuleIntent(IntentType::StartGame,0).accepted());
  assert(dispatchModuleIntent(IntentType::Leave,0).accepted());
  assert(table().lobby.hostController()==1&&table().lobby.playerCount()==1);
  freshLobby(); startFromHost();
  assert(!dispatchModuleIntent(IntentType::Rematch,0).accepted());
  assert(!dispatchModuleIntent(IntentType::ResetGame,0).accepted());
}
static void winDecisions() {
  freshLobby(3,true); startFromHost();
  assert(!web(1,1,WebControl::ClaimWin));
  assert(web(0,1,WebControl::ClaimWin));
  // The other players confirm in turn order after the claimant (the app's rule).
  assert(table().game.nextWinConfirmationPlayerNumber()==2);
  assert(!web(2,1,WebControl::ConfirmWin));
  assert(!web(2,1,WebControl::DenyWin));
  assert(web(0,2,WebControl::ConfirmWin)); assert(table().game.nextWinConfirmationPlayerNumber()==3);
  choose(1,SigilAction::ConfirmWin); assert(table().game.nextWinConfirmationPlayerNumber()==4);
  assert(web(2,1,WebControl::ConfirmWin));
  assert(table().game.gameOver()&&table().game.winnerPlayerNumber()==1&&completedGames==1);
  assert(!web(0,2,WebControl::ConfirmWin)); assert(completedGames==1);
  // Any seated Sigil may call the rematch (no table host).
  assert(dispatchModuleIntent(IntentType::Rematch,1).accepted());
  assert(table().hubState==HubState::Lobby&&table().lobby.playerCount()==4&&!table().game.hasPlayers());
  startFromHost(); assert(web(0,1,WebControl::ClaimWin));
  // Player 2 (Sigil 0's seat B) answers first.
  assert(web(0,2,WebControl::DenyWin)); assert(table().hubState==HubState::Running&&!table().game.hasWinClaim());
  assert(web(1,1,WebControl::PauseResume));
  assert(web(0,1,WebControl::ClaimWin));
  assert(web(0,2,WebControl::DenyWin)); assert(table().hubState==HubState::Paused);
  assert(web(1,1,WebControl::PauseResume));
  // A claim chosen during play resumes play when denied.
  choose(0,SigilAction::ClaimWin); assert(table().game.hasWinClaim());
  assert(web(0,2,WebControl::DenyWin)); assert(table().hubState==HubState::Running);
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
  assert(table().eliminationTargetPlayer==1);
  assert(table().game.lifeChangeFor(3)->state==TurnHub::LifeChangeState::Cancelled);
  choose(0,SigilAction::NextTarget); assert(table().eliminationTargetPlayer==2);
  choose(1,SigilAction::Eliminate); assert(!table().game.isEliminated(2));  // Not its seat: not offered.
  choose(0,SigilAction::Eliminate); assert(table().game.isEliminated(2)&&table().hubState==HubState::Paused);
  assert(!web(0,2,WebControl::Concede));
  assert(web(1,1,WebControl::PauseResume));
  assert(web(1,1,WebControl::Concede)); assert(table().hubState==HubState::Running&&table().game.isEliminated(3));
  assert(web(0,1,WebControl::Concede)); assert(table().hubState==HubState::GameOver&&table().game.winnerPlayerNumber()==4);
  assert(completedGames==1);
  choose(0,SigilAction::ResetTable); assert(table().hubState==HubState::Lobby&&table().lobby.playerCount()==0);
  freshLobby(); startFromHost();
  assert(web(1,1,WebControl::PauseResume));
  assert(dispatchModuleIntent(IntentType::BeginElimination,0).accepted());
  assert(!web(0,1,WebControl::ClaimWin));
  assert(!web(1,1,WebControl::Concede));
  assert(dispatchModuleIntent(IntentType::CancelElimination,1).accepted());
  assert(table().eliminationTargetPlayer==0);
  assert(web(0,1,WebControl::ClaimWin));
  assert(!dispatchModuleIntent(IntentType::BeginElimination,1).accepted());
}
static void passTimingAndActors() {
  freshLobby(); startFromHost();
  assert(!web(1,1,WebControl::Pass));
  Intent stale; stale.type=IntentType::ClaimWin; stale.actor={IntentOrigin::Browser,1,1,1};
  assert(!intents.dispatch(stale).accepted());
  choose(0,SigilAction::Pass); assert(table().pendingPass.active);
  assert(!dispatchModuleIntent(IntentType::CommitPass,0).accepted());
  testNow+=2999; updatePendingPass(testNow); assert(table().game.activePlayerNumber()==1);
  ++testNow; updatePendingPass(testNow); assert(table().game.activePlayerNumber()==2&&!table().pendingPass.active);
  assert(table().game.statsForPlayer(1)->turnsCompleted==1);
  assert(web(1,1,WebControl::Pass));
  choose(1,SigilAction::CancelPass); assert(!table().pendingPass.active&&table().hubState==HubState::Running);
  assert(web(1,1,WebControl::Pass)); assert(web(1,1,WebControl::Pass)); assert(!table().pendingPass.active);
  assert(web(1,1,WebControl::Pass)); assert(web(0,1,WebControl::PauseResume)); assert(!table().pendingPass.active);
  testNow+=3000; updatePendingPass(testNow); assert(table().game.activePlayerNumber()==2);
  assert(web(0,1,WebControl::PauseResume));
  testNow=UINT32_MAX-1000;
  assert(web(1,1,WebControl::Pass));
  testNow+=2999; updatePendingPass(testNow); assert(table().game.activePlayerNumber()==2);
  ++testNow; updatePendingPass(testNow); assert(table().game.activePlayerNumber()==3);
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
  assert(request("/api/session/join",companion)==200 && table().lobby.playerCount()==2);
  // No table host: the second phone may start too; its countdown is cancelable.
  assert(request("/api/control/start",second)==200 && table().hubState==HubState::Starting);
  assert(request("/api/control/cancel-start",first)==200 && table().hubState==HubState::Lobby);
  assert(request("/api/control/start",first)==200 && table().hubState==HubState::Starting);
  assert(request("/api/session/leave",first)==409);
  testNow+=3000; updateCountdown(testNow); assert(table().hubState==HubState::Running);
  assert(table().game.playerCount()==2 && table().game.playerAt(0)->controllerId>=MAX_PHYSICAL_SIGILS);
  assert(String(table().game.playerAt(0)->profileId)==firstId);
  assert(request("/api/control/pass",second,{{"module","8"},{"profileId",firstId}})==409);
  assert(request("/api/control/pass",first)==200 && table().pendingPass.active);
  assert(request("/api/control/pass",companion)==200 && !table().pendingPass.active);
  assert(request("/api/session/logout",first)==200);
  assert(request("/api/control/pass",first)==401);
  assert(request("/api/session/me",companion,{},HTTP_GET)==200);
  const String returned=loginPhone(firstId);
  assert(request("/api/session/me",returned,{},HTTP_GET)==200);
  assert(server.body.find("\"participating\":true")!=std::string::npos);
  assert(table().game.playerCount()==2);
  assert(request("/api/control/win",returned)==200);
  assert(request("/api/control/confirm",returned)==409);
  assert(request("/api/control/confirm",second)==200 && table().hubState==HubState::GameOver);
  assert(ProfileFixture::profiles[firstId].stats.gamesPlayed==1);
  assert(ProfileFixture::profiles[secondId].stats.gamesPlayed==1);
  assert(request("/api/control/confirm",second)==409);
  assert(ProfileFixture::profiles[firstId].stats.gamesPlayed==1);
  assert(request("/api/control/rematch",returned)==200 && table().lobby.playerCount()==2);
  // Any seated phone may reset the table now (no table host).
  assert(request("/api/control/reset",second)==200 && table().lobby.playerCount()==0);
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
  assert(request("/api/profiles/register","",{{"name","Local account"},{"pin","2468"},{"profileId","C0DE1234"}})==200);
  assert(responseField("profileId")=="C0DE1234");
  const String localToken=responseField("token");
  assert(request("/api/session/logout",localToken)==200);
  assert(request("/api/profiles/register","",{{"name","Overwrite attempt"},{"pin","9999"},{"profileId","C0DE1234"}})==409);
  assert(TurnHubProfiles::nameForProfile("C0DE1234")=="Local account");
  assert(request("/api/session/login","",{{"profileId","C0DE1234"},{"pin","2468"}})==200);
  assert(request("/api/session/logout",responseField("token"))==200);
  assert(request("/api/profiles/register","",{{"name","Bad imported ID"},{"pin","1234"},{"profileId","NOTHEX!!"}})==400);
  ProfileFixture::profiles.erase("C0DE1234"); // Leave the shared capacity fixture unchanged.
  // A PIN-less local account: uploaded by any signed-in phone under its own ID,
  // never over an existing one; its first sign-in sets the PIN.
  assert(request("/api/profiles/register","",{{"name","No PIN friend"},{"pin",""},{"profileId","FEED0001"}})==401);
  assert(request("/api/profiles/register",afterLimit,{{"name","No PIN friend"},{"pin",""},{"profileId","FEED0001"}})==200);
  assert(responseField("profileId")=="FEED0001" && server.body.find("\"hasPin\":false")!=std::string::npos);
  const String uploadKey=responseField("linkKey");
  assert(TurnHubProfiles::nameForProfile("FEED0001")=="No PIN friend" && !TurnHubProfiles::hasPinForProfile("FEED0001"));
  assert(request("/api/profiles/register",afterLimit,{{"name","Again"},{"pin",""},{"profileId","FEED0001"}})==409);
  // Profile sync (plans: profile-transfer F): the uploading phone's link key
  // syncs the profile's settings; the newer side wins, stats never do.
  {
    const String key=uploadKey; assert(key.length()==32);
    auto sync=[&](const String &linkKey,const char *activity,const char *name){
      return request("/api/profiles/sync","",{{"profileId","FEED0001"},{"linkKey",linkKey},{"now","1800000000000"},
        {"activity",activity},{"name",name},{"color","#112233"},{"avatar","2"},{"reduceMotion","1"}});
    };
    assert(sync("0123456789ABCDEF0123456789ABCDEF","1799999990000","Thief")==401);
    assert(request("/api/profiles/sync","",{{"profileId","FEED0001"},{"linkKey",key},{"now","12"},{"activity","1"},{"name","x"}})==400);
    // The phone's copy is newer: it replaces Atlas's as a whole.
    const uint32_t played=ProfileFixture::profiles["FEED0001"].stats.gamesPlayed;
    assert(sync(key,"1799999990000","Renamed on phone")==200 && server.body.find("\"applied\":\"phone\"")!=std::string::npos);
    assert(TurnHubProfiles::nameForProfile("FEED0001")=="Renamed on phone" && TurnHubProfiles::avatarForProfile("FEED0001")==2);
    uint32_t rgb=0; assert(TurnHubProfiles::jewelColorForProfile("FEED0001",rgb) && rgb==0x112233);
    TurnHubProfiles::AccessibilityPrefs prefs; TurnHubProfiles::loadAccessibilityForProfile("FEED0001",prefs);
    assert(prefs.ledStyle==TurnHubProfiles::LedStyle::ReducedMotion);
    assert(server.body.find("\"activity\":1799999990000")!=std::string::npos);
    assert(ProfileFixture::profiles["FEED0001"].stats.gamesPlayed==played);
    assert(sync(key,"1799999990000","Same time")==200 && server.body.find("\"applied\":\"same\"")!=std::string::npos);
    assert(TurnHubProfiles::nameForProfile("FEED0001")=="Renamed on phone");
    // A later change on Atlas wins over the phone's older copy.
    TurnHubProfiles::setNameForProfile("FEED0001","Renamed on Atlas");
    TurnHubWebApi::internal::noteProfileChange("FEED0001");
    assert(sync(key,"1799999995000","Older phone copy")==200 && server.body.find("\"applied\":\"atlas\"")!=std::string::npos);
    assert(TurnHubProfiles::nameForProfile("FEED0001")=="Renamed on Atlas");
    assert(server.body.find("\"name\":\"Renamed on Atlas\"")!=std::string::npos);
    // Two phones may hold keys; a third link retires the oldest.
    const String second=TurnHubWebApi::internal::issueLinkKey("FEED0001");
    assert(second.length()==32 && TurnHubWebApi::internal::linkKeyValid("FEED0001",key));
    assert(TurnHubWebApi::internal::issueLinkKey("FEED0001").length()==32);
    assert(!TurnHubWebApi::internal::linkKeyValid("FEED0001",key) && TurnHubWebApi::internal::linkKeyValid("FEED0001",second));
    // A signed-in phone asks for its own profile's key.
    assert(request("/api/profiles/link-key","")==401);
    assert(request("/api/profiles/link-key",afterLimit)==200 && responseField("profileId")==firstId);
    assert(TurnHubWebApi::internal::linkKeyValid(firstId,responseField("linkKey")));
  }
  assert(request("/api/profiles/register",afterLimit,{{"name",""},{"pin",""},{"profileId","FEED0002"}})==400);
  assert(request("/api/session/login","",{{"profileId","FEED0001"},{"pin","1357"}})==200);
  assert(TurnHubProfiles::hasPinForProfile("FEED0001"));
  assert(request("/api/session/logout",responseField("token"))==200);
  ProfileFixture::profiles.erase("FEED0001");
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

  // A profile with no PIN yet (made on a Sigil or the tablet) takes the PIN
  // of its first phone sign-in; after that the PIN is checked as usual.
  const String pinless=TurnHubProfiles::createProfileWithName("No pin yet");
  assert(pinless.length() && !TurnHubProfiles::hasPinForProfile(pinless));
  assert(request("/api/session/login","",{{"profileId",pinless},{"pin","12"}})==401 &&
      !TurnHubProfiles::hasPinForProfile(pinless));
  assert(request("/api/session/login","",{{"profileId",pinless},{"pin","1357"}})==200);
  assert(request("/api/session/me",responseField("token"),{},HTTP_GET)==200 &&
      server.body.find("\"hasPin\":true")!=std::string::npos);
  assert(request("/api/session/login","",{{"profileId",pinless},{"pin","2468"}})==401);
  assert(request("/api/session/login","",{{"profileId",pinless},{"pin","1357"}})==200);
  ProfileFixture::profiles.erase(pinless.c_str());
}

static void guestSigilsDoNotCreateAccounts() {
  ProfileFixture::bindings.clear();
  const size_t saved = ProfileFixture::profiles.size();
  freshLobby(2, true); // Primary and shared secondary guests can still play.
  assert(table().lobby.playerCount() == 3);
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
  for (uint8_t i = 0; i < table().game.playerCount(); ++i) assert(table().game.playerAt(i)->profileId[0] == '\0');
  assert(web(0, 2, WebControl::Concede));
  assert(web(1, 1, WebControl::Concede));
  assert(table().hubState == HubState::GameOver);
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
  PlayerSeat before[MAX_PLAYERS]; table().lobby.buildPlayers(before,MAX_PLAYERS);
  const size_t saved = ProfileFixture::profiles.size();
  assert(request("/api/session/request",phone,{{"module","0"},{"slot","1"}})==202);
  assert(ProfileFixture::profiles.size() == saved);
  const String claim=responseField("requestId");
  TurnHubWebApi::notePhysicalAction(0);
  assert(request("/api/session/poll","",{{"id",claim}},HTTP_GET)==200);
  assert(responseField("token")==phone);
  assert(ProfileFixture::profiles.size() == saved);
  PlayerSeat after[MAX_PLAYERS]; table().lobby.buildPlayers(after,MAX_PLAYERS);
  assert(table().lobby.playerCount()==2 && table().lobby.hostController()==0);
  assert(after[0].participantId==before[0].participantId && after[0].controllerId==0);
  const String secondPhone=loginPhone(id);
  assert(request("/api/session/join",secondPhone)==200 && table().lobby.playerCount()==2);
  assert(request("/api/control/start",phone)==200);
  testNow+=3000;updateCountdown(testNow);
  choose(0,SigilAction::Pass); assert(table().pendingPass.active);
  assert(request("/api/control/pass",secondPhone)==200 && !table().pendingPass.active);
  assert(request("/api/control/pass",phone)==200);
  testNow+=3000;updatePendingPass(testNow);
  assert(table().game.activePlayerNumber()==2);
  assert(request("/api/control/concede",other)==200 && table().hubState==HubState::GameOver);
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
  assert(table().lobby.playerCount()==1);
  choose(0,SigilAction::Join); // Finished-game binding cleared: this is now a guest.
  assert(table().lobby.playerCount()==2);
  assert(TurnHubControllers::profileForSeat(0,1).length()==0);
  TurnHub::fixtureRadio=false;
}

static void attachNamedProfileToGuest() {
  ProfileFixture::bindings.clear();
  freshLobby(2, true);
  String id;
  const String phone = registerPhone("Named guest", id);
  PlayerSeat before[MAX_PLAYERS]; table().lobby.buildPlayers(before, MAX_PLAYERS);
  const unsigned syncs = TurnHub::fixtureProfileSyncs;
  assert(request("/api/session/request",phone,{{"module","0"},{"slot","1"}})==202);
  const String claim=responseField("requestId");
  TurnHubWebApi::notePhysicalAction(0);
  assert(request("/api/session/poll","",{{"id",claim}},HTTP_GET)==200);
  assert(table().lobby.playerCount()==3 && table().lobby.hostController()==0 && table().lobby.hasSecondary(0));
  assert(TurnHubControllers::profileForSeat(0,1)==id);
  assert(TurnHub::fixtureProfileSyncs == syncs+1);
  PlayerSeat after[MAX_PLAYERS]; table().lobby.buildPlayers(after,MAX_PLAYERS);
  assert(after[0].participantId==before[0].participantId);
  assert(request("/api/seats","",{},HTTP_GET)==200);
  assert(server.body.find("Named guest")!=std::string::npos);
  const String companion=loginPhone(id);
  assert(request("/api/session/join",companion)==200 && table().lobby.playerCount()==3);

  // Merge an already joined phone with the guest Sigil instead of duplicating it.
  ProfileFixture::bindings.clear();
  freshLobby(1);
  assert(request("/api/session/join",phone)==200 && table().lobby.playerCount()==2);
  assert(request("/api/session/request",phone,{{"module","0"},{"slot","1"}})==202);
  const String merge=responseField("requestId");
  TurnHubWebApi::notePhysicalAction(0);
  assert(request("/api/session/poll","",{{"id",merge}},HTTP_GET)==200);
  assert(table().lobby.playerCount()==1 && table().lobby.hostController()==0);
  assert(request("/api/session/join",companion)==200 && table().lobby.playerCount()==1);
  String otherId; const String other=registerPhone("Different owner",otherId);
  assert(request("/api/session/request",other,{{"module","0"},{"slot","1"}})==202);
  const String conflict=responseField("requestId");
  TurnHubWebApi::notePhysicalAction(0);
  assert(request("/api/session/poll","",{{"id",conflict}},HTTP_GET)==409);
  assert(TurnHubControllers::profileForSeat(0,1)==id && table().lobby.playerCount()==1);
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
  assert(table().lobby.playerCount()==0);
  owner=loginPhone(id);
  assert(dispatchModuleIntent(IntentType::Join,0).accepted());
  assert(request("/api/session/request","",{{"module","0"},{"slot","1"}})==403);
  assert(request("/api/session/profile",owner,{{"clearPin","1"}})==409);
  const String companion=loginPhone(id);
  assert(request("/api/session/logout",owner)==200);
  assert(TurnHubWebApi::physicalStatsVisible(id));
  assert(request("/api/session/logout",companion)==200);
  assert(!TurnHubWebApi::physicalStatsVisible(id));
  assert(table().lobby.playerCount()==1); // Losing browser auth does not evict a player.
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
  // Official modes keep their official life unless house rules are on.
  for (const char *house : {"0","1"}) {
    for (const char *profile : {"generic","mtg","mtg_commander","yugioh"}) {
      assert(request("/api/game/settings",first,{{"gameProfile",profile},{"startingLife","27"},{"houseRules",house}})==200);
      assert(request("/api/game/settings",first,{},HTTP_GET)==200);
      assert(responseField("gameProfile")==profile);
      GameSettings saved; assert(loadGameSettings(saved)==TurnHubStorage::Status::Ok);
      const int32_t official = String(profile)=="mtg" ? 20 : String(profile)=="mtg_commander" ? 40 : String(profile)=="yugioh" ? 8000 : 27;
      assert(String(gameProfileKey(saved.profile))==profile && saved.houseRules==(house[0]=='1'));
      assert(saved.startingLife==(house[0]=='1' ? 27 : official));
    }
  }
  assert(request("/api/game/settings",first,{{"houseRules","2"}})==400);
  assert(request("/api/game/settings",first,{{"houseRules","0"}})==200 && !table().nextGameSettings.houseRules);
  assert(request("/api/game/settings",first,magic)==200);
  for (const char *invalid : {"", "-1", "1.5", "20abc", "100000", "999999999999999"})
    assert(request("/api/game/settings",first,{{"gameProfile","mtg"},{"startingLife",invalid}})==400);
  assert(request("/api/game/settings",first,{{"gameProfile","unknown"},{"startingLife","20"}})==400);
  ProfileFixture::gameSettingsWritable=false;
  assert(request("/api/game/settings",first,{{"gameProfile","yugioh"},{"startingLife","8000"}})==409);
  assert(table().nextGameSettings.profile==GameProfile::Magic && table().nextGameSettings.startingLife==20);
  ProfileFixture::gameSettingsWritable=true;
  assert(request("/api/control/life",first,{{"delta","-1"}})==409); // Lobby.
  gameSettingsAvailable=false;
  assert(request("/api/control/start",first)==409 && table().hubState==HubState::Lobby);
  gameSettingsAvailable=true;
  assert(request("/api/control/start",first)==200);
  assert(request("/api/game/settings",first,magic)==409); // Countdown is frozen.
  testNow+=3000;updateCountdown(testNow);
  assert(table().game.settings().profile==GameProfile::Magic && table().game.lifeTotal(1)==20 && table().game.lifeTotal(2)==20);
  assert(request("/api/game/settings",first,magic)==200); // Unchanged setup is legal.
  assert(request("/api/game/settings",first,{{"autoEliminateAtZeroLife","0"}})==200);
  assert(request("/api/control/life","",{{"delta","-1"}})==401);
  for (const char *invalid : {"", "0", "-", "1.5", "1abc", "100000", "9999999999999"})
    assert(request("/api/control/life",first,{{"delta",invalid}})==400);
  assert(request("/api/control/life",first,{{"delta","-21"},{"player","2"},{"profileId",secondId}})==200);
  assert(table().game.lifeTotal(1)==-1 && table().game.lifeTotal(2)==20 && !table().game.isEliminated(1));
  const String companion=loginPhone(firstId);
  assert(request("/api/control/life",companion,{{"delta","6"}})==200 && table().game.lifeTotal(1)==5);
  assert(request("/api/session/me",first,{},HTTP_GET)==200);
  assert(server.body.find("\"life\":5")!=std::string::npos);
  assert(request("/api/seats","",{},HTTP_GET)==200);
  assert(server.body.find("\"life\":5")!=std::string::npos && server.body.find("\"life\":20")!=std::string::npos);
  // Life stays within the app's +/-99,999.
  assert(request("/api/control/life",first,{{"delta","99994"}})==200);
  assert(request("/api/control/life",first,{{"delta","1"}})==409 && table().game.lifeTotal(1)==99999);
  assert(!table().game.changeLife(1,INT32_MAX) && table().game.lifeTotal(1)==99999);
  assert(request("/api/control/life",first,{{"delta","-99999"}})==200);
  assert(request("/api/control/life",first,{{"delta","-99999"}})==200);
  assert(request("/api/control/life",first,{{"delta","-1"}})==409 && table().game.lifeTotal(1)==-99999);
  assert(request("/api/control/pause",first)==200);
  assert(request("/api/control/life",second,{{"delta","-5"}})==200 && table().game.lifeTotal(2)==15);
  assert(request("/api/control/win",first)==200);
  assert(request("/api/control/life",second,{{"delta","1"}})==409); // Pending win decision.
  assert(request("/api/control/deny",second)==200);
  assert(request("/api/control/concede",second)==200);
  assert(request("/api/control/life",second,{{"delta","1"}})==409); // Eliminated, game continues.
  assert(request("/api/control/concede",third)==200 && table().hubState==HubState::GameOver);
  assert(request("/api/control/life",first,{{"delta","1"}})==409);
  assert(request("/api/control/rematch",first)==200);
  assert(request("/api/control/start",first)==200);
  testNow+=3000;updateCountdown(testNow);
  assert(table().game.lifeTotal(1)==20 && table().game.lifeTotal(2)==20 && !table().game.isEliminated(2));
  assert(request("/api/control/concede",second)==200);
  assert(request("/api/control/concede",third)==200);
  assert(request("/api/control/reset",first)==200);
  assert(request("/api/session/join",first)==200 && request("/api/session/join",second)==200);
  assert(request("/api/game/settings",first,{{"gameProfile","generic"},{"startingLife","0"},{"autoEliminateAtZeroLife","0"}})==200);
  assert(request("/api/control/start",first)==200);
  testNow+=3000;updateCountdown(testNow);
  assert(table().game.lifeTotal(1)==0 && table().game.lifeTotal(2)==0 && table().game.livingPlayerCount()==2);
}


namespace {
struct MemoryBlobs : TurnHubStorage::BlobStore {
  std::map<std::string,std::vector<uint8_t>> blobs; bool failWrites=false;
  TurnHubStorage::Status read(const char *key,void *data,size_t capacity,size_t &size) override {
    auto found=blobs.find(key); if(found==blobs.end()) return TurnHubStorage::Status::NotFound;
    size=found->second.size(); if(data==nullptr&&capacity==0) return TurnHubStorage::Status::Ok;
    if(capacity<size) return TurnHubStorage::Status::InvalidArgument;
    memcpy(data,found->second.data(),size); return TurnHubStorage::Status::Ok;
  }
  TurnHubStorage::Status write(const char *key,const void *data,size_t size) override {
    if(failWrites) return TurnHubStorage::Status::IoError;
    const auto *b=static_cast<const uint8_t*>(data); blobs[key]=std::vector<uint8_t>(b,b+size); return TurnHubStorage::Status::Ok;
  }
  TurnHubStorage::Status remove(const char *key) override { return blobs.erase(key)?TurnHubStorage::Status::Ok:TurnHubStorage::Status::NotFound; }
};
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
  // Tablet access stands alone (no Game Master needed); nothing above it exists.
  assert(request("/api/accounts/permissions",admin,{{"profileId",devId},{"permissions","36"}})==200 &&
      has(devId,TabletAccess));
  assert(request("/api/accounts/permissions",admin,{{"profileId",devId},{"permissions","64"}})==400);
  assert(request("/api/accounts/permissions",admin,{{"profileId",devId},{"permissions","4"}})==200 &&
      !has(devId,TabletAccess));
  assert(request("/api/device/name",player,{{"module","0"},{"name","No"}})==403);
  assert(request("/api/network",gm,{},HTTP_GET)==403);
  assert(request("/api/network",dev,{},HTTP_GET)==403);
  assert(request("/api/network",admin,{},HTTP_GET)==200);
  // Home Wi-Fi (home_wifi.h): Admin only, verified at the table, never the
  // password; on needs a saved network; the table channel stays 1-11.
  assert(server.body.find("\"home\":{\"enabled\":false,\"configured\":false")!=std::string::npos);
  assert(server.body.find("\"channel\":6")!=std::string::npos);
  assert(request("/api/network/home",gm,{{"ssid","Home"}})==403);
  assert(request("/api/network/home",admin,{{"enabled","1"}})==403);  // Not verified yet.
  verifyAtTable(admin);
  assert(request("/api/network/home",admin,{{"enabled","1"}})==409);  // No network saved.
  assert(request("/api/network/home/check",admin)==409);
  assert(request("/api/network/home",admin,{{"ssid",""}})==400);
  assert(request("/api/network/home",admin,{{"ssid","Home Net"},{"password","hunter2hunter2"},{"enabled","1"}})==200);
  assert(request("/api/network",admin,{},HTTP_GET)==200);
  assert(server.body.find("\"ssid\":\"Home Net\"")!=std::string::npos &&
      server.body.find("\"enabled\":true")!=std::string::npos && server.body.find("hunter2")==std::string::npos);
  assert(request("/api/network/home/check",gm)==403&&request("/api/network/home/check",admin)==200);
  assert(request("/api/network/home",admin,{{"forget","1"}})==200&&!TurnHubAtlas::homeWifiEnabled()&&
      !TurnHubAtlas::homeWifiConfigured());
  assert(request("/api/network/channel",gm,{{"channel","11"}})==403);
  for(const char *bad:{"0","12","x"}) assert(request("/api/network/channel",admin,{{"channel",bad}})==400);
  assert(request("/api/network/channel",admin,{{"channel","6"}})==200&&server.body.find("\"changed\":false")!=std::string::npos);
  resetPresence();
  assert(request("/api/session/profile",admin,{{"clearPin","1"}})==403);
  assert(request("/api/accounts/moderate",admin,{{"profileId",playerId},{"action","reset"}})==403);
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","mute"}})==200);
  Account stored;assert(load(playerId,stored)&&stored.nudgeMuted);
  assert(request("/api/session/join",player)==200);
  assert(request("/api/session/join",gm)==200);
  assert(request("/api/session/join",dev)==200);
  const String companion=loginPhone(playerId);
  assert(request("/api/control/start",player)==200);testNow+=3000;updateCountdown(testNow);
  // Nudge: the muted account is refused; others nudge the active player once
  // per NUDGE_COOLDOWN_MS, never themselves, and state shows the last nudge.
  {
    const uint32_t seq0=table().nudgeState.seq;
    assert(request("/api/control/nudge",player)==409&&table().nudgeState.seq==seq0);
    int sent=0;
    for(const String *c:{&gm,&dev}) if(request("/api/control/nudge",*c)==200) ++sent;
    assert(sent>=1&&table().nudgeState.seq==seq0+sent&&table().nudgeState.toPlayer==table().game.activePlayer()->playerNumber);
    assert(table().nudgeState.fromPlayer!=table().nudgeState.toPlayer);
    for(const String *c:{&gm,&dev}) assert(request("/api/control/nudge",*c)==409);
    assert(table().nudgeState.seq==seq0+sent);
    assert(request("/api/v1/state",gm,{},HTTP_GET)==200&&server.body.find("\"nudge\":{\"seq\":")!=std::string::npos);
    testNow+=NUDGE_COOLDOWN_MS;
    int again=0;
    for(const String *c:{&gm,&dev}) if(request("/api/control/nudge",*c)==200) ++again;
    assert(again==sent&&table().nudgeState.seq==seq0+2*sent);
  }
  const auto life=table().game.lifeTotal(1);
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","reset"}})==200);
  assert(table().game.lifeTotal(1)==life&&table().game.livingPlayerCount()==3);
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
    // The people screens explain actions up front: who is seated, who is
    // the initial Admin, who has a PIN and who must sign in again.
    const std::string body=server.body.c_str();
    const auto entry=[&](const String &id){const size_t at=body.find(std::string("\"profileId\":\"")+id.c_str());assert(at!=std::string::npos);return body.substr(at,body.find('}',at)-at);};
    assert(entry(playerId).find("\"atTable\":true")!=std::string::npos&&entry(playerId).find("\"reconnectRequired\":true")!=std::string::npos);
    assert(entry(playerId).find("\"hasPin\":true")!=std::string::npos&&entry(playerId).find("\"primary\":false")!=std::string::npos);
    assert(entry(adminId).find("\"primary\":true")!=std::string::npos&&entry(adminId).find("\"atTable\":false")!=std::string::npos);
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
  assert(table().game.activePlayerNumber()==2&&!table().pendingPass.active);
  assert(request("/api/accounts/permissions",admin,{{"profileId",gmId},{"permissions","2"}})==200);
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","remove"}})==409);
  assert(request("/api/accounts/permissions",admin,{{"profileId",gmId},{"permissions","26"}})==200);
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","remove"}})==200);
  assert(table().game.isEliminated(1)&&table().game.livingPlayerCount()==2);
  assert(TurnHubProfiles::loadModerationStatsForProfile(playerId,history)&&history.gameRemovals==1&&history.connectionResets==1);
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","remove"}})==409);
  assert(request("/api/accounts/permissions",admin,{{"profileId",adminId},{"permissions","31"}})==200);
  assert(has(adminId,Admin|GameMaster|Developer));
  // Every maintenance route is present without a card; the separate pack
  // installer/status are gone. /assets cannot expose permission-gated pages.
  ota.begin();
  assert(server.routes.count("0/portal") && server.routes.count("0/dev") && server.routes.count("0/update"));
  assert(server.uploads.count("/api/firmware"));
  assert(!server.routes.count("0/api/portal") && !server.uploads.count("/api/portal/install"));
  server.headers["X-TurnHub-Token"]=gm;
  server.routes.at("0/update")();assert(server.status==403);
  server.headers["X-TurnHub-Token"]=admin;
  server.routes.at("0/update")();assert(server.status==200 && server.contentType=="text/html");
  assert(static_cast<unsigned char>(server.body[0])==0x1f);
  // Direct page requests with credentials must enforce the independent permission.
  // The full Developer page and all fonts/styles are flash assets, independent
  // of the SD stub (which has no card). Binary-safe lengths and HTTP metadata.
  WebServer assets(80);
  assert(TurnHubWeb::serveFile(assets,"index.html"));
  assert(assets.status==200 && assets.contentType=="text/html");
  assert(assets.responseHeaders["Cache-Control"]=="no-store");
  assert(assets.responseHeaders["Content-Encoding"]=="gzip");
  assert(static_cast<unsigned char>(assets.body[0])==0x1f && static_cast<unsigned char>(assets.body[1])==0x8b);
  assets.status=0;
  assert(!TurnHubWeb::serveFile(assets,"../index.html") && assets.status==0);
  assert(!TurnHubWeb::serveFile(assets,"assets/missing.css") && assets.status==0);
  server.headers["X-TurnHub-Token"]=dev;TurnHubWebApi::serveRestrictedPage(server,Developer,"dev.html");assert(server.status==200&&!server.body.empty());
  server.headers["X-TurnHub-Token"]=gm;TurnHubWebApi::serveRestrictedPage(server,Developer,"dev.html");assert(server.status==403);
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

  // One person, two profiles: an Admin merges them (plans: profile-transfer D).
  String oldId,keepId; const String oldToken=registerPhone("Phone copy",oldId); registerPhone("Atlas copy",keepId);
  auto &oldStats=ProfileFixture::profiles[oldId.c_str()].stats, &keepStats=ProfileFixture::profiles[keepId.c_str()].stats;
  oldStats.gamesPlayed=3; oldStats.gamesWon=2; oldStats.fastestTurnMs=5000; oldStats.longestTurnMs=90000;
  keepStats.gamesPlayed=4; keepStats.gamesWon=1; keepStats.fastestTurnMs=8000; keepStats.longestTurnMs=60000;
  keepStats.lastGameResult=TurnHubProfiles::LastGameResult::Loss;
  const String oldKey=TurnHubWebApi::internal::issueLinkKey(oldId);
  assert(request("/api/accounts/merge",oldToken,{{"profileId",oldId},{"into",keepId}})==403);
  assert(request("/api/accounts/merge",admin,{{"profileId",oldId},{"into",oldId}})==400);
  assert(request("/api/accounts/merge",admin,{{"profileId",adminId},{"into",keepId}})==409);
  assert(request("/api/accounts/merge",admin,{{"profileId",oldId},{"into",keepId}})==200);
  assert(keepStats.gamesPlayed==7 && keepStats.gamesWon==3 && keepStats.fastestTurnMs==5000 && keepStats.longestTurnMs==90000);
  assert(keepStats.lastGameResult==TurnHubProfiles::LastGameResult::Loss);
  { TurnHubAccounts::Account merged; assert(TurnHubAccounts::load(oldId,merged) && merged.archived); }
  assert(TurnHubProfiles::mergedProfileFor(oldId)==keepId);
  assert(request("/api/session/me",oldToken,{},HTTP_GET)==401);
  assert(request("/api/accounts/merge",admin,{{"profileId",oldId},{"into",keepId}})==409);
  // A phone still linked to the old profile follows it with a fresh key.
  assert(request("/api/profiles/sync","",{{"profileId",oldId},{"linkKey",oldKey},{"now","1800000000000"},
      {"activity","1"},{"name","Phone copy"}})==409);
  assert(responseField("mergedInto")==keepId && TurnHubWebApi::internal::linkKeyValid(keepId,responseField("linkKey")));
  // A game imported under the old ID credits the profile it was merged into, once.
  {
    MemoryBlobs blobs; TurnHubStandalone::useImportStore(&blobs);
    TurnHubStandalone::ImportedGame game; game.recordId="merged-into-record"; game.playerCount=2; game.winner=0;
    game.players[0].name="Phone copy"; game.players[0].profileId=oldId; game.players[1].name="Guest";
    assert(TurnHubStandalone::importGame(game).status==TurnHubStandalone::ImportStatus::Imported && keepStats.gamesPlayed==8);
    game.players[0].profileId=keepId;
    assert(TurnHubStandalone::importGame(game).status==TurnHubStandalone::ImportStatus::Duplicate && keepStats.gamesPlayed==8);
    TurnHubStandalone::useImportStore(nullptr);
  }
  ProfileFixture::profiles.erase(oldId.c_str()); ProfileFixture::profiles.erase(keepId.c_str()); // Shared capacity.
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

  // Framework output waits in its own buffer and is stamped in order on the next write.
  for (const char *c="[E][WiFiGeneric.cpp:1] boom\n"; *c; ++c) TurnHub::captureFrameworkChar(*c);
  assert(serialLog.snapshot().empty());
  serialLog.println("ATLAS|AFTER");
  assert(serialLog.snapshot()=="[     12.345] [E][WiFiGeneric.cpp:1] boom\n[     12.345] ATLAS|AFTER\n");
  // An overrun keeps the newest bytes.
  serialLog.clear();
  for (int i=0;i<3000;++i) TurnHub::captureFrameworkChar(i<2990 ? 'x' : 'y');
  serialLog.drainFramework();
  const String overrun=serialLog.snapshot();
  assert(overrun.size()==14+1024 && overrun.substr(overrun.size()-10)==std::string(10,'y'));
  serialLog.clear();

  // Log lines that make a downloaded log self-explanatory.
  freshLobby(2);
  assert(logHas("ATLAS|LOBBY|EMPTY|RESET|ORIGIN|SYSTEM|FROM|"));
  table().nextGameSettings.turnTimerMs=60000;
  startFromHost();
  assert(logHas("|PROFILE|generic|LIFE|40|TIMER_MS|60000|PLAYERS|2\n"));
  assert(web(0,1,WebControl::Concede) && table().hubState==HubState::GameOver);
  serialLog.clear();
  assert(web(0,1,WebControl::Reset) && table().hubState==HubState::Lobby);
  assert(logHas("ATLAS|LOBBY|EMPTY|RESET|ORIGIN|BROWSER|CONTROLLER|0|FROM|GAME_OVER\n"));
  table().nextGameSettings=TurnHub::GameSettings{};
  enterEmptyLobby();
}
static void virtualCapacity() {
  enterEmptyLobby();
  for(uint8_t i=0;i<MAX_PLAYERS;++i) {
    const String id=TurnHubProfiles::createProfile(); String message;
    assert(handleProfileControl(id,WebControl::Join,INVALID_ID,1,message));
  }
  assert(table().lobby.playerCount()==MAX_PLAYERS);
  const String extra=TurnHubProfiles::createProfile(); String message;
  assert(!handleProfileControl(extra,WebControl::Join,INVALID_ID,1,message));
  assert(!dispatchModuleIntent(IntentType::Join,MAX_PHYSICAL_SIGILS).accepted());
  const uint8_t host=table().lobby.hostController();
  assert(web(host,1,WebControl::Start));
  testNow+=3000; updateCountdown(testNow);
  assert(web(host,1,WebControl::ClaimWin));
  for(uint8_t i=1;i<MAX_PLAYERS;++i) assert(web(MAX_PHYSICAL_SIGILS+i,1,WebControl::ConfirmWin));
  assert(table().hubState==HubState::GameOver);
}
// Venue tables: two games side by side on one Atlas (PLANNED_DESIGNS.md).
static void venueTables() {
  using TurnHubProtocol::SigilAction;
  const auto menuHas=[](uint8_t id,SigilAction a) {
    return (sigilMenuFor(id).actions & TurnHubProtocol::sigilActionBit(a))!=0;
  };
  const auto startTable=[](uint8_t index,uint8_t sigil) {
    choose(sigil,SigilAction::StartGame);
    TableScope scope(index);
    assert(table().hubState==HubState::Starting);
    testNow+=3000; updateCountdown(testNow); assert(table().hubState==HubState::Running);
  };
  registerWebCallbacks();
  freshLobby(2);  // Sigils 0 and 1 joined Game 1.
  // An unseated Sigil switches to Game 2 and joins there; a joined one leaves first.
  assert(menuHas(2,SigilAction::SwitchTable) && menuHas(0,SigilAction::SwitchTable));
  choose(2,SigilAction::SwitchTable); assert(sigilTable[2]==1 && tableForController(2)==1);
  choose(3,SigilAction::SwitchTable); choose(1,SigilAction::SwitchTable);
  assert(tableForController(1)==1 && !tables[0].lobby.isJoined(1) && tables[0].lobby.playerCount()==1);
  choose(1,SigilAction::Join); choose(2,SigilAction::Join); choose(3,SigilAction::Join);
  assert(tables[1].lobby.playerCount()==3 && tables[1].lobby.isJoined(2));
  choose(4,SigilAction::Join); assert(tables[0].lobby.playerCount()==2);  // Game 1 is the default.
  // Phones follow a game: Game 2's player can't join Game 1 while seated there.
  for (auto &session : TurnHubWebApi::internal::sessions) session = TurnHubWebApi::internal::WebSession{};
  String gameTwoId, gameOneId;
  const String gameTwo=registerPhone("Game Two phone",gameTwoId), gameOne=registerPhone("Game One phone",gameOneId);
  assert(request("/api/session/game",gameTwo,{{"game","3"}})==400);
  assert(request("/api/session/game",gameTwo,{{"game","2"}})==200);
  assert(request("/api/session/join",gameTwo)==200 && tables[1].lobby.playerCount()==4);
  assert(tableForProfile(gameTwoId)==1 && tables[0].lobby.playerCount()==2);
  assert(request("/api/v1/state",gameTwo,{},HTTP_GET)==200);
  assert(server.body.find("\"game\":2,\"games\":[{\"game\":1,\"state\":\"LOBBY\",\"players\":2},"
      "{\"game\":2,\"state\":\"LOBBY\",\"players\":4}]")!=std::string::npos);
  assert(request("/api/v1/state","",{{"game","2"}},HTTP_GET)==200 && server.body.find("\"game\":2,")!=std::string::npos);
  assert(request("/api/v1/state",gameOne,{},HTTP_GET)==200 && server.body.find("\"game\":1,")!=std::string::npos);
  // Back to Game 1 leaves Game 2's lobby; then over to Game 2 again.
  assert(request("/api/session/game",gameTwo,{{"game","1"}})==200 && tableForProfile(gameTwoId)==-1);
  assert(tables[1].lobby.playerCount()==3);
  assert(request("/api/session/game",gameTwo,{{"game","2"}})==200 && request("/api/session/join",gameTwo)==200);
  // A profile plays at one game at a time.
  {
    TableScope scope(0); String message;
    assert(!handleProfileControl(gameTwoId,WebControl::Join,INVALID_ID,1,message));
  }
  // Both games run independently; Atlas's speaker goes quiet while both do.
  startTable(0,0); assert(!atlasSpeakerShared());
  startTable(1,2); assert(atlasSpeakerShared());
  assert(tables[0].game.playerCount()==2 && tables[1].game.playerCount()==4);
  assert(!allTablesBetweenGames());
  {
    TableScope scope(1);
    const uint8_t active=table().game.activePlayerNumber();
    const uint8_t gameOneActive=tables[0].game.activePlayerNumber();
    const PlayerSeat *seat=table().game.activePlayer();
    if(seat->controllerId<MAX_PHYSICAL_SIGILS) choose(seat->controllerId,SigilAction::Pass);
    else assert(request("/api/control/pass",gameTwo)==200);
    testNow+=PASS_GRACE_MS+1; updatePendingPass(testNow);
    assert(table().game.activePlayerNumber()!=active && tables[0].game.activePlayerNumber()==gameOneActive);
  }
  // Playing Sigils and players can't switch; Game 2's state reaches its phone.
  assert(!menuHas(2,SigilAction::SwitchTable) && !menuHas(0,SigilAction::SwitchTable));
  assert(!dispatchModuleIntent(IntentType::ChooseTable,0,1,1).accepted());
  assert(request("/api/session/game",gameTwo,{{"game","1"}})==409 && tableForProfile(gameTwoId)==1);
  assert(request("/api/session/me",gameTwo,{},HTTP_GET)==200 && server.body.find("\"game\":2")!=std::string::npos);
  const uint8_t activeBefore = tables[1].game.activePlayerNumber();
  assert(request("/api/control/pause",gameTwo,{{"expectedGame","1"}})==409);
  assert(tables[1].hubState==HubState::Running && tables[1].game.activePlayerNumber()==activeBefore);
  assert(request("/api/control/pause",gameTwo,{{"expectedGame","2"}})==200);
  assert(tables[1].hubState==HubState::Paused);
  assert(request("/api/control/pause",gameTwo,{{"expectedGame","2"}})==200);
  assert(request("/api/v1/state",gameTwo,{},HTTP_GET)==200 && server.body.find("\"state\":\"RUNNING\"")!=std::string::npos);
  // Resetting Game 1 leaves Game 2's players and Sigil profiles alone.
  { TableScope scope(0); enterEmptyLobby(); }
  assert(tables[1].game.playerCount()==4 && tableForProfile(gameTwoId)==1 && tableForController(2)==1);
  assert(tables[1].hubState==HubState::Running && !atlasSpeakerShared());
  { TableScope scope(1); enterEmptyLobby(); }
  for(uint8_t id=0;id<MAX_PHYSICAL_SIGILS;++id) sigilTable[id]=0;
  // Later scenarios register phones of their own; the sign-in store is small.
  ProfileFixture::profiles.erase(gameOneId.c_str()); ProfileFixture::profiles.erase(gameTwoId.c_str());
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
  assert(request("/api/game/settings",first,{{"gameProfile","mtg_commander"},{"startingLife","40"},{"autoEliminateAtZeroLife","0"},{"autoEliminateAtCommanderDamage","0"}})==200);
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
  assert(propose(-7)==200&&table().game.lifeTotal(2)==40);
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
  auto id=table().game.lifeChangeFor(2)->id;
  assert(propose(-7)==409); // One pending request per target.
  assert(respond(third,id,true)==409&&respond(first,id,true)==409);
  assert(request("/api/control/life",second,{{"delta","2"}})==200);
  assert(respond(companion,id,true)==200&&table().game.lifeTotal(2)==35); // Delta, not stale total.
  assert(respond(second,id,true)==409&&table().game.lifeTotal(2)==35);
  assert(request("/api/game/counters",third,{},HTTP_GET)==200);
  assert(server.body.find("\"requests\":[]")!=std::string::npos); // No unrelated requests.
  assert(propose(-5)==200);
  const auto replacement=table().game.lifeChangeFor(2)->id;
  assert(replacement!=id&&respond(second,id,true)==409);
  assert(respond(second,replacement,false)==200&&table().game.lifeTotal(2)==35);
  assert(table().game.lifeChangeFor(2)->state==LifeChangeState::Rejected);
  assert(propose(-3)==200);
  testNow+=14999;dispatchSystemIntent(IntentType::ExpireLifeChanges);assert(table().game.lifeTotal(2)==35);
  ++testNow;dispatchSystemIntent(IntentType::ExpireLifeChanges);assert(table().game.lifeTotal(2)==32);
  dispatchSystemIntent(IntentType::ExpireLifeChanges);assert(table().game.lifeTotal(2)==32);
  assert(table().game.lifeChangeFor(2)->state==LifeChangeState::Automatic);
  assert(propose(1)==200);id=table().game.lifeChangeFor(2)->id;
  testNow+=15000;assert(respond(second,id,false)==409&&table().game.lifeTotal(2)==33); // Deadline race.
  for(const char *bad:{"0","-1","1x","4294967296","99999999999999"})
    assert(request("/api/control/life/respond",second,{{"requestId",bad},{"accept","1"}})==400);
  Intent forged;forged.type=IntentType::ExpireLifeChanges;forged.actor.origin=IntentOrigin::Browser;
  assert(!intents.dispatch(forged).accepted());
  assert(request("/api/control/pause",first)==200);
  assert(propose(2)==200);testNow+=15000;dispatchSystemIntent(IntentType::ExpireLifeChanges);
  assert(table().game.lifeTotal(2)==35&&table().hubState==HubState::Paused);
  // Automatic application must revalidate after intervening changes.
  assert(propose(1)==200);
  assert(request("/api/control/life",second,{{"delta","99964"}})==200);
  testNow+=15000;dispatchSystemIntent(IntentType::ExpireLifeChanges);
  assert(table().game.lifeTotal(2)==99999&&table().game.lifeChangeFor(2)->state==LifeChangeState::Failed);
  assert(request("/api/control/life",second,{{"delta","-99959"}})==200);
  assert(propose(-1)==200);
  const uint32_t beforeRollover=testNow;
  table().game.cancelLifeChanges();
  testNow=UINT32_MAX-10000;
  IntentPayload payload;payload.targetPlayer=2;payload.value=-1;String message;
  assert(changeCounter(table().game.playerByNumber(1)->controllerId,1,IntentType::RequestLifeChange,payload,message));
  testNow+=14999;dispatchSystemIntent(IntentType::ExpireLifeChanges);assert(table().game.lifeTotal(2)==40);
  ++testNow;dispatchSystemIntent(IntentType::ExpireLifeChanges);assert(table().game.lifeTotal(2)==39);
  testNow=beforeRollover;
  const auto damage=[&](const String &token,int source,int commander,int delta){return request("/api/control/commander",token,{{"source",String(source)},{"commander",String(commander)},{"delta",String(delta)},{"target","1"}});};
  assert(damage("",1,1,2)==401);
  assert(damage(second,1,1,-2)==409);
  assert(server.body.find("Cannot remove more Commander damage") != std::string::npos);
  assert(table().game.commanderDamage(2,1,1)==0&&table().game.lifeTotal(2)==39);
  assert(damage(second,1,1,21)==200&&table().game.commanderDamage(2,1,1)==21&&table().game.lifeTotal(2)==18);
  assert(!table().game.isEliminated(2)); // This scenario explicitly disables Commander-loss adjudication.
  assert(damage(companion,1,2,3)==200&&table().game.commanderDamage(2,1,2)==3&&table().game.lifeTotal(2)==15);
  assert(damage(second,3,1,5)==200&&table().game.commanderDamage(2,3,1)==5&&table().game.lifeTotal(2)==10);
  assert(table().game.lifeTotal(1)==40); // Forged target ignored: received damage belongs to caller.
  assert(damage(second,1,1,-2)==200&&table().game.commanderDamage(2,1,1)==19&&table().game.lifeTotal(2)==12);
  assert(damage(second,1,1,-20)==409&&table().game.commanderDamage(2,1,1)==19&&table().game.lifeTotal(2)==12);
  assert(request("/api/control/life",second,{{"delta","99987"}})==200);
  assert(damage(second,1,1,-1)==409&&table().game.lifeTotal(2)==99999&&table().game.commanderDamage(2,1,1)==19);
  assert(request("/api/control/life",second,{{"delta","-99987"}})==200);
  assert(damage(second,16,1,2)==409);
  assert(damage(second,1,3,2)==400);
  assert(damage(second,1,1,0)==400);
  assert(!table().game.changeCommanderDamage(2,1,1,INT32_MIN));
  assert(request("/api/control/life",second,{{"delta","-99999"}})==200);
  assert(damage(second,1,1,13)==409&&table().game.lifeTotal(2)==-99987&&table().game.commanderDamage(2,1,1)==19);
  assert(request("/api/control/life",second,{{"delta","99999"}})==200);
  assert(request("/api/game/counters",second,{},HTTP_GET)==200);
  assert(server.body.find("\"commanders\":[19,3]")!=std::string::npos);
  assert(propose(-1)==200);
  assert(request("/api/control/win",first)==200);
  assert(table().game.lifeChangeFor(2)->state==LifeChangeState::Cancelled);
  assert(damage(second,1,1,1)==409&&propose(-1)==409);
  assert(request("/api/control/deny",second)==200);
  assert(propose(-1)==200);id=table().game.lifeChangeFor(2)->id;
  assert(request("/api/control/concede",second)==200);
  assert(table().game.lifeChangeFor(2)->state==LifeChangeState::Cancelled&&damage(second,1,1,1)==409);
  assert(propose(-1)==409);
  assert(request("/api/control/life/request",third,{{"target","1"},{"delta","-1"}})==200);
  assert(request("/api/control/concede",third)==200&&table().hubState==HubState::GameOver);
  assert(table().game.lifeChangeFor(1)->state==LifeChangeState::Cancelled);
  assert(request("/api/control/rematch",first)==200);
  assert(request("/api/game/settings",first,{{"gameProfile","mtg"},{"startingLife","20"}})==200);
  assert(request("/api/control/start",first)==200);testNow+=3000;updateCountdown(testNow);
  assert(table().game.lifeTotal(2)==20&&table().game.commanderDamage(2,1,1)==0&&!table().game.lifeChangeFor(2)->id);
  assert(damage(second,1,1,1)==409); // Only Commander supports these counters.
  assert(propose(-1)==200&&table().game.lifeChangeFor(2)->id>id);
  assert(respond(second,id,true)==409&&table().game.lifeTotal(2)==20);
  enterEmptyLobby();
}

static void physicalPlayerControls() {
  using namespace TurnHub; using namespace TurnHubProtocol;
  freshLobby(3,true); table().nextGameSettings.profile=GameProfile::Commander;
  table().nextGameSettings.startingLife=40; startFromHost(); syncSigilMenus(testNow);
  auto change=[&](uint8_t sigil,uint8_t player,uint8_t control,int8_t value,uint8_t revision=255) {
    handlePlayerControl(sigil,encodePlayerControl(player,control,value,revision==255 ? sigilMenuRevision(sigil) : revision));
  };
  change(0,1,0,1); assert(table().game.countersForPlayer(1)->poison==1);
  change(1,1,0,1); assert(table().game.countersForPlayer(1)->poison==1); // Other controller.
  change(0,1,0,1,(sigilMenuRevision(0)+1)&7); assert(table().game.countersForPlayer(1)->poison==1);
  change(0,2,1,1); assert(table().game.countersForPlayer(2)->energy==1); // Own shared seat.
  change(0,1,5,1); assert(table().game.countersForPlayer(1)->commanderCasts[0]==1);
  change(0,1,6,1); assert(table().game.countersForPlayer(1)->commanderCasts[1]==0); // No partner.
  change(0,1,10,1); assert(table().game.hasPartner(1));
  change(0,1,6,1); change(0,1,10,0); assert(table().game.hasPartner(1)); // Cannot drop recorded partner.
  change(0,1,7,1); change(0,1,8,1); change(0,1,9,1);
  assert(table().game.countersForPlayer(1)->cityBlessing && table().game.monarchPlayerNumber()==1 && table().game.initiativePlayerNumber()==1);
  const auto *seat=table().game.playerByNumber(1);
  assert(dispatchSeatIntent(IntentType::Pause,IntentOrigin::PhysicalSigil,*seat).accepted());
  syncSigilMenus(testNow); change(0,1,2,1); assert(table().game.countersForPlayer(1)->experience==1);
  change(0,1,2,-1); change(0,1,2,-1); assert(table().game.countersForPlayer(1)->experience==0);
  assert(dispatchSeatIntent(IntentType::ClaimWin,IntentOrigin::PhysicalSigil,*seat).accepted());
  change(0,1,1,1); assert(table().game.countersForPlayer(1)->energy==0); // Decision blocks edits.
  enterEmptyLobby(); change(0,1,0,1); assert(!table().game.hasPlayers());
}

static void commanderFlow() {
  using namespace TurnHub;
  using namespace TurnHubProtocol;
  using A = SigilAction;
  resetCommanderPickers();
  freshLobby(3,true);
  table().nextGameSettings.profile = GameProfile::Commander; table().nextGameSettings.startingLife = 40;
  startFromHost();
  leds.render(table().hubState,table().lobby,table().game,0,0,0,testNow);
  assert(leds.switchShownSeat(0,table().game));
  leds.render(table().hubState,table().lobby,table().game,0,0,0,testNow);
  auto open = [&](bool undo=false, uint8_t target=2) {
    syncSigilMenus(testNow);
    handleSelectAction(0,encodeSelectAction(undo ? A::UndoCommanderHit : A::CommanderDamage,sigilMenuRevision(0),target));
  };
  auto key = [&](uint8_t k) { handleCommanderKey(0,encodeCommanderKey(k,commanderPage(0).revision),testNow); };
  // Partners are off by default: a hit from commander 2 needs them turned on.
  assert(!table().game.hasPartner(3) && !table().game.recordCommanderHit(2,3,2,1));
  open(); auto p=commanderPage(0);
  assert(validCommanderFlow(p) && p.stage==CommanderStage::Source && p.recipient==2 && p.recipientSlot==2);
  // Without partners, Select skips the Commander step; Up goes back past it.
  key(4); assert(commanderPage(0).stage==CommanderStage::Amount && commanderPage(0).commander==1);
  key(0); assert(commanderPage(0).stage==CommanderStage::Source);
  key(1); assert(commanderPage(0).stage==CommanderStage::Closed);
  // Player 3 turns partners on from their own Sigil's menu (seat on Sigil 1).
  {
    syncSigilMenus(testNow);
    leds.render(table().hubState,table().lobby,table().game,0,0,0,testNow);
    const PlayerSeat *three = table().game.playerByNumber(3);
    assert(three && (sigilMenuFor(three->controllerId).actions & sigilActionBit(A::AddPartner)));
    handleSelectAction(three->controllerId,encodeSelectAction(A::AddPartner,sigilMenuRevision(three->controllerId),3));
    assert(table().game.hasPartner(3));
  }
  open(); p=commanderPage(0);
  // Sources skip the recipient (2): 1 -> 3, then back past 2 to 1.
  key(3); assert(commanderPage(0).source==3);
  key(2); assert(commanderPage(0).source==1);
  key(3); assert(commanderPage(0).source==3);
  key(4); key(3); assert(commanderPage(0).commander==2);
  key(4); for (int i=0;i<4;++i) key(3);
  assert(commanderPage(0).amount==5 && table().game.lifeTotal(2)==40);
  key(4); p=commanderPage(0);
  assert(p.stage==CommanderStage::Confirm && p.life==40 && p.damage==0);
  // Turn progression does not change the frozen recipient.
  assert(table().game.passTurn(0,testNow));
  const int32_t confirm=encodeCommanderKey(4,p.revision);
  handleCommanderKey(0,confirm,testNow);
  assert(table().game.lifeTotal(2)==35 && table().game.commanderDamage(2,3,2)==5 && table().game.lifeTotal(1)==40);
  handleCommanderKey(0,confirm,testNow);
  assert(table().game.lifeTotal(2)==35 && table().game.commanderDamage(2,3,2)==5);
  key(4); assert(commanderPage(0).stage==CommanderStage::Closed);
  open(true); p=commanderPage(0);
  assert(p.stage==CommanderStage::UndoConfirm && p.amount==5 && p.life==35 && p.damage==5);
  key(4); assert(table().game.lifeTotal(2)==40 && table().game.commanderDamage(2,3,2)==0 && !table().game.lastCommanderHit(2));
  key(4);
  // Fresh totals must be seen and reconfirmed after a concurrent life change.
  open(); key(4); key(4); p=commanderPage(0);  // Player 1 has no partner: Source, Amount, Confirm.
  assert(table().game.changeLife(2,-2));
  handleCommanderKey(0,encodeCommanderKey(4,p.revision),testNow);
  assert(table().game.lifeTotal(2)==38 && table().game.commanderDamage(2,1,1)==0 && commanderPage(0).revision!=p.revision);
  key(4); assert(table().game.lifeTotal(2)==37 && table().game.commanderDamage(2,1,1)==1);
  // A correction to that cell invalidates Undo, rather than reversing the correction.
  assert(table().game.changeCommanderDamage(2,1,1,1) && !table().game.lastCommanderHit(2));
  key(4); open(); key(4); key(4); key(1);
  assert(commanderPage(0).stage==CommanderStage::Closed && table().game.lifeTotal(2)==36);
  // Player source selection reaches every other participant, including the
  // shared seat's partner, and wraps past the recipient.
  open(); for (int i=0;i<2;++i) key(3);
  assert(commanderPage(0).source==4); key(3); assert(commanderPage(0).source==1);
  key(2); key(2); assert(commanderPage(0).source==3);
  key(1);
  Intent forbidden; forbidden.type=IntentType::RecordCommanderHit;
  forbidden.actor={IntentOrigin::PhysicalSigil,1,1,3}; forbidden.payload.targetPlayer=2;
  forbidden.payload.counterSource=3; forbidden.payload.counterSlot=1; forbidden.payload.value=5;
  assert(intents.dispatch(forbidden).status==IntentStatus::Unauthorized);
  // Bounds failures are atomic; nothing changes and no Undo receipt is created.
  assert(table().game.changeLife(2,-99036));
  const int32_t before=table().game.lifeTotal(2), damage=table().game.commanderDamage(2,3,1);
  assert(!table().game.recordCommanderHit(2,3,1,9999) && table().game.lifeTotal(2)==before &&
      table().game.commanderDamage(2,3,1)==damage && !table().game.lastCommanderHit(2));
  // Idle, offline, elimination and table decisions close the disposable flow.
  open(); syncCommanderPickers(testNow+COMMANDER_IDLE_MS);
  assert(commanderPage(0).stage==CommanderStage::Closed);
  open(); fixtureRadio=false; syncCommanderPickers(testNow); fixtureRadio=true;
  assert(commanderPage(0).stage==CommanderStage::Closed);
  assert(dispatchSeatIntent(IntentType::Pause,IntentOrigin::PhysicalSigil,*table().game.playerByNumber(2)).accepted());
  open(); assert(commanderPage(0).stage==CommanderStage::Source); key(1);
  assert(dispatchSeatIntent(IntentType::Resume,IntentOrigin::PhysicalSigil,*table().game.playerByNumber(2)).accepted());
  // Starting another match with the same seats cannot retain the old entry.
  PlayerSeat sameSeats[4]; for (uint8_t i=0;i<4;++i) sameSeats[i]=*table().game.playerAt(i);
  const GameSettings settings=table().game.settings();
  open(); assert(table().game.start(sameSeats,4,sameSeats[0],testNow,settings));
  syncCommanderPickers(testNow); assert(commanderPage(0).stage==CommanderStage::Closed && !table().game.lastCommanderHit(2));
  open(); assert(dispatchSeatIntent(IntentType::Concede,IntentOrigin::PhysicalSigil,*table().game.playerByNumber(2)).accepted());
  assert(table().game.isEliminated(2));
  syncCommanderPickers(testNow); assert(commanderPage(0).stage==CommanderStage::Closed);
  // A remaining seat's flow closes for a table-wide win decision too.
  open(false,1); dispatchSeatIntent(IntentType::ClaimWin,IntentOrigin::PhysicalSigil,*table().game.activePlayer());
  syncCommanderPickers(testNow); assert(commanderPage(0).stage==CommanderStage::Closed);
  enterEmptyLobby();
  table().nextGameSettings = GameSettings{};
  freshLobby(2); startFromHost(); syncSigilMenus(testNow);
  assert(!(sigilMenuFor(0).actions & sigilActionBit(A::CommanderDamage)));
  openCommanderPicker(0,1,false,testNow); assert(commanderPage(0).stage==CommanderStage::Closed);
  enterEmptyLobby(); resetCommanderPickers();
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
  assert(first.controls.allowed & 1);
  assert(engine.changePlayerCounter(1,CounterKind::Energy,3));
  render(); assert(sentGameDisplays[0].controls.values[1]==3);
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
  // Negative life shows as is (the automatic zero-life rule is off here).
  engine.setAutoEliminateAtZeroLife(false);
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
  fixtureRecords[6].helloInfoValid = false;
  assert(pickerSigil(1) && pickerSigil(2) && pickerSigil(3) && !pickerSigil(6));
  fixtureRecords[6].helloInfoValid = true;
  const auto pick = [](uint8_t id, A a) { handleSelectAction(id, encodeSelectAction(a, sigilMenuRevision(id))); };
  const auto key = [](uint8_t id, PickerKeyCode k) {
    handlePickerKey(id, encodePickerKey(k, profilePickerPage(id).revision), testNow);
  };

  // Join opens the picker; browsing changes nothing at the table.
  pick(1, A::Join);
  assert(pickerOpen(1) && !table().lobby.isJoined(1) && table().lobby.playerCount() == 1);
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
  assert(pickerOpen(1) && !table().lobby.isJoined(1) && profilePickerPage(1).revision == page.revision);
  syncProfilePickers(testNow); assert(pickerSends == quiet + 1);

  // Click: next page. Archived Eve is left out; Dan needs a phone sign-in.
  key(1, PickerKeyCode::Select);
  page = profilePickerPage(1);
  assert(page.page == 1 && page.itemCount == 2 && !strcmp(page.items[0].name, "Carol"));
  assert(!strcmp(page.items[1].name, "Dan") && (page.items[1].flags & PICKER_ITEM_LOCKED));
  key(1, PickerKeyCode::Right);
  page = profilePickerPage(1);
  assert(page.mode == PickerMode::List && page.notice == PickerNotice::NeedsPhone && !table().lobby.isJoined(1));
  // The handler enforces the same policy, whatever the page said.
  Intent sneaky; sneaky.type = IntentType::PickProfile; sneaky.actor.origin = IntentOrigin::PhysicalSigil;
  sneaky.actor.controllerId = 1; sneaky.actor.slot = 1; strcpy(sneaky.payload.profileId, "0000000D");
  assert(intents.dispatch(sneaky).status == IntentStatus::Unauthorized && !table().lobby.isJoined(1));

  // Left: back a page. Right: Alice, then confirm; Left backs out, click joins.
  key(1, PickerKeyCode::Left); assert(profilePickerPage(1).page == 0);
  key(1, PickerKeyCode::Right);
  page = profilePickerPage(1);
  assert(page.mode == PickerMode::Confirm && page.itemCount == 1 && !strcmp(page.items[0].name, "Alice"));
  key(1, PickerKeyCode::Left); assert(profilePickerPage(1).mode == PickerMode::List && !table().lobby.isJoined(1));
  key(1, PickerKeyCode::Right); key(1, PickerKeyCode::Select);
  assert(table().lobby.isJoined(1) && TurnHubControllers::profileForSeat(1, 1) == "0000000A");
  assert(!pickerOpen(1) && profilePickerPage(1).mode == PickerMode::Closed);
  syncProfilePickers(testNow); assert(sentPickers[1].mode == PickerMode::Closed);

  // Leave: the whole Sigil leaves the lobby.
  resetSigilMenus(); syncSigilMenus(testNow);
  assert((sigilMenuFor(1).actions & sigilActionBit(A::Leave)) != 0);
  assert((decodeMenuState2(fixtureMenuState2[1]).actions & sigilActionBit(A::Leave)) != 0);
  pick(1, A::Leave);
  assert(!table().lobby.isJoined(1) && TurnHubControllers::profileForSeat(1, 1).length() == 0);
  pick(1, A::Join); key(1, PickerKeyCode::Right); key(1, PickerKeyCode::Select);
  assert(table().lobby.isJoined(1) && TurnHubControllers::profileForSeat(1, 1) == "0000000A");

  // Playtest 2026-09-29 item 8: Add seat B opens the picker for seat B, and a
  // picked profile adds seat B in one step. Seat A's Alice is not offered.
  pick(1, A::AddSeatB);
  assert(pickerOpen(1) && !table().lobby.hasSecondary(1));
  page = profilePickerPage(1);
  assert(!strcmp(page.items[1].name, "bob") && !strcmp(page.items[2].name, "Carol"));
  key(1, PickerKeyCode::Right); key(1, PickerKeyCode::Select);
  assert(table().lobby.hasSecondary(1) && TurnHubControllers::profileForSeat(1, 2) == "0000000B" && !pickerOpen(1));
  pick(1, A::RemoveSeatB); assert(!table().lobby.hasSecondary(1));
  { // A profile already at the table cannot take seat B, whatever the page said.
    Intent twice; twice.type = IntentType::PickProfile; twice.actor.origin = IntentOrigin::PhysicalSigil;
    twice.actor.controllerId = 1; twice.actor.slot = 2; strcpy(twice.payload.profileId, "0000000A");
    assert(intents.dispatch(twice).status == IntentStatus::Conflict && !table().lobby.hasSecondary(1));
  }
  // Removing seat B freed bob's seat; leaving from a phone frees Alice's seat
  // A too, so the Sigil no longer shows her and its next Join is not her.
  assert(TurnHubControllers::profileForSeat(1, 2).length() == 0);
  {
    Intent leave; leave.type = IntentType::LeaveProfile; leave.actor.origin = IntentOrigin::Browser;
    strcpy(leave.payload.profileId, "0000000A");
    assert(intents.dispatch(leave).accepted() && !table().lobby.isJoined(1) &&
        TurnHubControllers::profileForSeat(1, 1).length() == 0);
  }
  pick(1, A::Join); key(1, PickerKeyCode::Right); key(1, PickerKeyCode::Select);
  assert(table().lobby.isJoined(1) && TurnHubControllers::profileForSeat(1, 1) == "0000000A");

  // The OLED Sigil gets the same picker (it draws it as a list); Guest joins.
  pick(2, A::Join); assert(pickerOpen(2) && !table().lobby.isJoined(2));
  key(2, PickerKeyCode::Up); assert(table().lobby.isJoined(2) && !pickerOpen(2));
  pick(4, A::Join); key(4, PickerKeyCode::Up);
  assert(table().lobby.isJoined(4) && TurnHubControllers::profileForSeat(4, 1).length() == 0 && !pickerOpen(4));
  pick(4, A::AddSeatB); key(4, PickerKeyCode::Up);  // Guest in seat B.
  assert(table().lobby.hasSecondary(4) && TurnHubControllers::profileForSeat(4, 2).length() == 0 && !pickerOpen(4));
  pick(4, A::RemoveSeatB); assert(!table().lobby.hasSecondary(4));

  // Alice now plays on a Sigil, so she is no longer offered elsewhere.
  pick(5, A::Join);
  page = profilePickerPage(5);
  assert(page.pageCount == 2 && !strcmp(page.items[1].name, "bob") && !strcmp(page.items[2].name, "Carol"));
  // Left on the first page cancels.
  key(5, PickerKeyCode::Left); assert(!pickerOpen(5) && !table().lobby.isJoined(5));

  // Idle for a minute closes it.
  pick(5, A::Join); assert(pickerOpen(5));
  testNow += PICKER_IDLE_MS; syncProfilePickers(testNow); assert(!pickerOpen(5));
  // Leaving the lobby closes it too.
  pick(5, A::Join); assert(pickerOpen(5));
  pick(0, A::StartGame); assert(table().hubState == HubState::Starting);
  syncProfilePickers(testNow); assert(!pickerOpen(5));

  // Owner request 2026-10-07: during a game a Sigil outside it may take over a
  // player seated from a phone or the tablet; only those players are offered.
  enterEmptyLobby(); resetProfilePickers(); resetSigilMenus();
  ProfileFixture::bindings.clear();
  { Intent join; join.type = IntentType::JoinProfile; join.actor.origin = IntentOrigin::Browser;
    strcpy(join.payload.profileId, "0000000B"); assert(intents.dispatch(join).accepted()); }
  pick(1, A::Join); key(1, PickerKeyCode::Up);  // Guest on Sigil 1.
  assert(table().lobby.playerCount() == 2);
  pick(1, A::StartGame); testNow += START_COUNTDOWN_MS; updateCountdown(testNow);
  assert(table().hubState == HubState::Running);
  uint8_t bobController = INVALID_ID, bobSlot = 1;
  assert(resolveProfileParticipant("0000000B", bobController, bobSlot) && bobController >= MAX_PHYSICAL_SIGILS);
  PlayerSeat bobSeat[2]; assert(table().game.playersForController(bobController, bobSeat, 2) == 1);
  const int32_t bobLife = table().game.lifeTotal(bobSeat[0].playerNumber);
  resetSigilMenus();
  assert((sigilMenuFor(3).actions & sigilActionBit(A::Join)) != 0);
  assert((sigilMenuFor(1).actions & sigilActionBit(A::Join)) == 0);
  pick(3, A::Join);
  page = profilePickerPage(3);
  assert(page.mode == PickerMode::List && page.pageCount == 1 && page.itemCount == 1 && !strcmp(page.items[0].name, "bob"));
  key(3, PickerKeyCode::Up); assert(profilePickerPage(3).mode == PickerMode::Confirm);
  key(3, PickerKeyCode::Select);
  assert(!pickerOpen(3) && table().game.controllerInGame(3) && !table().game.controllerInGame(bobController) && table().lobby.isJoined(3));
  assert(TurnHubControllers::profileForSeat(3, 1) == "0000000B");
  assert(table().game.playerByNumber(bobSeat[0].playerNumber)->controllerId == 3 && table().game.lifeTotal(bobSeat[0].playerNumber) == bobLife);
  { // Nobody new joins mid-game, and a Sigil already playing cannot take another player.
    Intent late; late.type = IntentType::PickProfile; late.actor.origin = IntentOrigin::PhysicalSigil;
    late.actor.controllerId = 5; late.actor.slot = 1; strcpy(late.payload.profileId, "0000000C");
    assert(intents.dispatch(late).status == IntentStatus::InvalidActor && !table().game.controllerInGame(5));
  }
  // With nobody left to take over, Join is not offered; an open picker says so.
  resetSigilMenus(); assert((sigilMenuFor(5).actions & sigilActionBit(A::Join)) == 0);
  openProfilePicker(5, testNow);
  page = profilePickerPage(5);
  assert(page.itemCount == 0 && page.pageCount == 1 && page.page == 0 && page.notice == PickerNotice::Unavailable);
  // The picker closes when the game ends.
  enterEmptyLobby(); syncProfilePickers(testNow); assert(!pickerOpen(5));

  // Names a Sigil would show alike are numbered by profile ID, cut to fit.
  enterEmptyLobby(); resetProfilePickers();
  ProfileFixture::bindings.clear(); ProfileFixture::profiles.clear();
  add("00000002", "Sam"); add("00000001", "sam"); add("00000003", "Samantha Jones");
  add("00000004", "Samantha Jonesy"); add("00000005", "Ty");
  pick(5, A::Join);
  page = profilePickerPage(5);
  assert(page.pageCount == 2 && !strcmp(page.items[1].name, "sam 1") &&
         !strcmp(page.items[2].name, "Sam 2"));
  key(5, PickerKeyCode::Select);
  page = profilePickerPage(5);
  assert(!strcmp(page.items[0].name, "Samantha J 1") && !strcmp(page.items[1].name, "Samantha J 2") &&
         !strcmp(page.items[2].name, "Ty"));
  key(5, PickerKeyCode::Left); key(5, PickerKeyCode::Left); assert(!pickerOpen(5));

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
  const PlayerSeat a = *table().game.playerByNumber(table().lobby.playerNumber(0, 1));
  const PlayerSeat b = *table().game.playerByNumber(table().lobby.playerNumber(1, 1));
  const int32_t start = table().game.lifeTotal(a.playerNumber);
  handleLifeAdjust(0, encodeLifeAdjust(a.playerNumber, -39));
  assert(table().game.lifeTotal(a.playerNumber) == start - 39);
  handleLifeAdjust(0, encodeLifeAdjust(a.playerNumber, 11));
  assert(table().game.lifeTotal(a.playerNumber) == start - 28);
  handleLifeAdjust(0, encodeLifeAdjust(b.playerNumber, -5));  // Not this Sigil's player.
  assert(table().game.lifeTotal(b.playerNumber) == start);

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
  assert(table().game.lifeTotal(a.playerNumber) == start - 28);
  handleLifeResponse(1, encodeLifeResponse(a.playerNumber, true, shown.tag));  // Not the target's Sigil.
  assert(table().game.lifeTotal(a.playerNumber) == start - 28);
  handleLifeResponse(0, encodeLifeResponse(a.playerNumber, true, shown.tag));
  assert(table().game.lifeTotal(a.playerNumber) == start - 31 && sigilLifeRequestFor(0) == 0);
  syncSigilMenus(testNow); assert(fixtureLifeRequest[0] == 0);
  assert(intents.dispatch(ask).accepted());
  handleLifeResponse(0, encodeLifeResponse(a.playerNumber, false, decodeLifeRequest(sigilLifeRequestFor(0)).tag));
  assert(table().game.lifeTotal(a.playerNumber) == start - 31);

  // Every menu Sigil learns the starting life and sees a pending pass, not
  // only the passer (turntest, 2026-09-26).
  syncSigilMenus(testNow);
  assert(fixtureStartingLife[0] == start && fixtureStartingLife[1] == start);
  const PlayerSeat *passer = table().game.activePlayer();
  assert(passer != nullptr);
  Intent pass; pass.type = IntentType::Pass; pass.actor.origin = IntentOrigin::PhysicalSigil;
  pass.actor.controllerId = passer->controllerId; pass.actor.slot = passer->slot;
  pass.actor.playerNumber = passer->playerNumber;
  assert(intents.dispatch(pass).accepted() && table().pendingPass.active);
  syncSigilMenus(testNow);
  assert(fixturePassPending[0] == passer->playerNumber && fixturePassPending[1] == passer->playerNumber);
  assert(intents.dispatch(pass).accepted() && !table().pendingPass.active);  // Pressed again: undone.
  syncSigilMenus(testNow);
  assert(fixturePassPending[0] == 0 && fixturePassPending[1] == 0);

  // An elimination selection blocks life changes.
  assert(dispatchSeatIntent(IntentType::Pause, IntentOrigin::PhysicalSigil, a).accepted());
  assert(offered(0));
  dispatchModuleIntent(IntentType::BeginElimination, 0);
  assert(table().eliminationTargetPlayer != 0 && !offered(0));
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
  for (const char *bad : {"13", "128", "257", "-1", "x", "1000"})
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

static void artworkUploads() {
  enterEmptyLobby();
  uploadApprovalRequired = true;
  String playerId, adminId, otherId;
  const String player = registerPhone("Image player", playerId);
  const String admin = registerPhone("Image admin", adminId);
  const String other = registerPhone("Image other", otherId);
  TurnHubAccounts::Account account; account.permissions = TurnHubAccounts::Admin;
  assert(TurnHubAccounts::save(adminId, account));
  std::ifstream input("fixtures/avatar-64.jpg", std::ios::binary);
  std::string jpeg((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  assert(jpeg.size() > TurnHubArtwork::CHUNK && jpeg.size() < TurnHubArtwork::MAX_BYTES);
  auto start = [&]() {
    assert(request("/api/session/avatar",player,{{"action","start"},{"size",String(jpeg.size())}})==200);
    return responseField("revision");
  };
  auto chunk = [&](const String &revision, size_t index) {
    static const char *digits = "0123456789abcdef"; String hex;
    for (size_t i = index * TurnHubArtwork::CHUNK; i < jpeg.size() && i < (index+1)*TurnHubArtwork::CHUNK; ++i) {
      const uint8_t b = jpeg[i]; hex += digits[b >> 4]; hex += digits[b & 15];
    }
    return request("/api/session/avatar",player,{{"action","chunk"},{"revision",revision},{"index",String(index)},{"data",hex}});
  };
  auto submit = [&](const String &revision) {
    return request("/api/session/avatar",player,{{"action","submit"},{"revision",revision},{"thumbnail",String(std::string(512,'a'))}});
  };
  auto upload = [&]() {
    String revision = start();
    for (size_t i = 0; i < (jpeg.size()+TurnHubArtwork::CHUNK-1)/TurnHubArtwork::CHUNK; ++i) assert(chunk(revision,i)==200);
    assert(submit(revision)==200); return revision;
  };
  assert(request("/api/session/avatar","",{{"action","start"},{"size","1000"}})==401);
  assert(request("/api/session/avatar",player,{{"action","start"},{"size","49153"}})==400);
  ProfileFixture::artworkAvailable=false;
  assert(request("/api/session/avatar",player,{{"action","start"},{"size","1000"}})==503);
  ProfileFixture::artworkAvailable=true;
  assert(request("/api/upload-approval","",{},HTTP_GET)==401);
  assert(request("/api/upload-approval",player,{{"required","0"}})==403);
  assert(request("/api/upload-approval",admin,{{"required","2"}})==400);
  assert(request("/api/upload-approval",admin,{},HTTP_GET)==200 && server.body.find("true")!=std::string::npos);
  ProfileFixture::gameSettingsWritable=false;
  assert(request("/api/upload-approval",admin,{{"required","0"}})==409 && uploadApprovalRequired);
  ProfileFixture::gameSettingsWritable=true;
  String revision=start();
  assert(chunk(revision,1)==400); // Out-of-order and incomplete transfers cannot publish.
  assert(submit(revision)==400);
  assert(request("/api/session/avatar",other,{{"action","chunk"},{"revision",revision},{"index","0"},{"data","aa"}})==409);
  ProfileFixture::artworkWritable=false; assert(chunk(revision,0)==503);
  ProfileFixture::artworkWritable=true; assert(chunk(revision,0)==200);
  assert(chunk(revision,0)==400); // No blind retry after acknowledgement.
  revision=upload(); // Starting again cleans the unfinished transfer.
  assert(TurnHubProfiles::avatarForProfile(playerId)==0);
  assert(request("/api/avatar","",{{"profileId",playerId},{"revision",revision}},HTTP_GET)==404);
  assert(request("/api/avatar",other,{{"profileId",playerId},{"revision",revision},{"pending","1"}},HTTP_GET)==403);
  assert(request("/api/avatar",player,{{"profileId",playerId},{"revision",revision},{"pending","1"}},HTTP_GET)==200);
  assert(server.body.size()==jpeg.size() && std::string(server.body.data(),server.body.size())==jpeg);
  assert(request("/api/avatar/review",player,{{"profileId",playerId},{"revision",revision},{"action","approve"}})==403);
  assert(request("/api/avatar/review",admin,{{"profileId",playerId},{"revision","00000000"},{"action","approve"}})==409);
  assert(request("/api/avatar/review",admin,{{"profileId",playerId},{"revision",revision},{"action","approve"}})==200);
  assert(TurnHubProfiles::avatarForProfile(playerId)==TurnHubAvatars::AVATAR_CUSTOM);
  assert(request("/api/avatar","",{{"profileId",playerId},{"revision",revision}},HTTP_GET)==200 && server.body.size()==jpeg.size());
  uint32_t thumbRevision=0; assert(TurnHubProfiles::artworkThumbnail(playerId,thumbRevision)!=nullptr && thumbRevision!=0);
  assert(request("/api/session/join",player)==200);
  AtlasScreen screen; buildAtlasScreen(testNow,screen);
  assert(screen.playerCount==1 && screen.players[0].avatar==TurnHubAvatars::AVATAR_CUSTOM && screen.players[0].avatarPixels);
  assert(request("/api/seats","",{},HTTP_GET)==200 && server.body.find("customAvatar")!=std::string::npos);
  const String approved=revision;
  // Simulate interrupted approval cleanup: pending still references approved chunks.
  auto *artworkStore = TurnHubProfiles::profileArtworkStore();
  TurnHubArtwork::Image retained;
  assert(TurnHubArtwork::metadata(*artworkStore, (String("a")+playerId).c_str(), retained)==TurnHubStorage::Status::Ok);
  assert(TurnHubArtwork::publish(*artworkStore, (String("p")+playerId).c_str(), retained)==TurnHubStorage::Status::Ok);
  revision=upload();
  assert(request("/api/avatar","",{{"profileId",playerId},{"revision",approved}},HTTP_GET)==200);
  assert(request("/api/avatar/review",admin,{{"profileId",playerId},{"revision",revision},{"action","reject"}})==200);
  assert(request("/api/avatar","",{{"profileId",playerId},{"revision",approved}},HTTP_GET)==200);
  // Off takes effect at submission, but does not approve previously pending images.
  String pending=upload();
  assert(request("/api/upload-approval",admin,{{"required","0"}})==200 && !uploadApprovalRequired);
  assert(request("/api/avatar","",{{"profileId",playerId},{"revision",pending}},HTTP_GET)==404);
  String immediate=upload();
  assert(request("/api/avatar","",{{"profileId",playerId},{"revision",immediate}},HTTP_GET)==200);
  assert(request("/api/avatar",player,{{"profileId",playerId},{"revision",pending},{"pending","1"}},HTTP_GET)==200);
  assert(request("/api/session/personalization",player,{},HTTP_GET)==200 && server.body.find("\"uploadApprovalRequired\":false")!=std::string::npos);
  assert(request("/api/upload-approval",admin,{{"required","1"}})==200 && uploadApprovalRequired);
  revision=upload();
  assert(request("/api/avatar","",{{"profileId",playerId},{"revision",revision}},HTTP_GET)==404);
  assert(request("/api/avatar","",{{"profileId",playerId},{"revision",immediate}},HTTP_GET)==200);
  assert(request("/api/upload-approval",admin,{{"required","0"}})==200);
  // The JPEG validator rejects a truncated envelope even with all declared chunks.
  const char last=jpeg.back(); jpeg.back()=0; revision=start();
  for(size_t i=0;i<(jpeg.size()+TurnHubArtwork::CHUNK-1)/TurnHubArtwork::CHUNK;++i) assert(chunk(revision,i)==200);
  assert(submit(revision)==400); jpeg.back()=last;
  assert(request("/api/upload-approval",admin,{{"required","1"}})==200);
  assert(request("/api/session/avatar",player,{{"action","remove"}})==200);
  assert(TurnHubProfiles::avatarForProfile(playerId)==0);
  assert(request("/api/avatar","",{{"profileId",playerId},{"revision",approved}},HTTP_GET)==404);
  enterEmptyLobby();
  for (const String &id : {playerId,adminId,otherId}) { ProfileFixture::profiles.erase(id.c_str()); TurnHubAccounts::accounts.erase(id.c_str()); }
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
    // Switch game (venue tables) has its own scenario.
    return (sigilMenuFor(id).actions & ~sigilActionBit(A::SwitchTable)) == mask;
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
  assert(sent.actions == (sigilActionBit(A::Join) | sigilActionBit(A::SwitchTable)) &&
      sent.defaultAction == SIGIL_ACTION_NONE);
  syncSigilMenus(testNow); assert(fixtureMenuStateSends == sends + MAX_PHYSICAL_SIGILS);
  invalidateSigilMenu(2); syncSigilMenus(testNow); assert(fixtureMenuStateSends == sends + MAX_PHYSICAL_SIGILS + 1);

  // Joining changes Sigil 2's menu: its revision moves on.
  const uint8_t oldRevision = sigilMenuRevision(2);
  pick(2, A::Join); assert(pickerOpen(2));  // Every Sigil picks who joins; Guest is Up.
  handlePickerKey(2, encodePickerKey(PickerKeyCode::Up, profilePickerPage(2).revision), testNow);
  assert(table().lobby.isJoined(2) && table().lobby.playerCount() == 3);
  syncSigilMenus(testNow);
  assert(sigilMenuRevision(2) != oldRevision && decodeMenuState2(fixtureMenuState2[2]).revision == sigilMenuRevision(2));
  // A choice from the old menu is dropped (and the menu resent).
  handleSelectAction(2, encodeSelectAction(A::Join, oldRevision));
  assert(table().lobby.playerCount() == 3);
  // Unoffered actions are dropped even at the current revision.
  pick(1, A::Rematch); assert(table().hubState == HubState::Lobby && table().lobby.playerCount() == 3);

  pick(1, A::AddSeatB);  // Seat B opens the picker too; Guest.
  handlePickerKey(1, encodePickerKey(PickerKeyCode::Up, profilePickerPage(1).revision), testNow);
  assert(table().lobby.hasSecondary(1) && has(1, A::RemoveSeatB));
  pick(1, A::RemoveSeatB); assert(!table().lobby.hasSecondary(1));

  // Start from the menu: one choice arms and starts; anyone seated may cancel.
  pick(0, A::StartGame); assert(table().hubState == HubState::Starting);
  assert(only(1, {A::CancelStart}));
  pick(1, A::CancelStart); assert(table().hubState == HubState::Lobby);
  pick(0, A::StartGame); assert(table().hubState == HubState::Starting);
  testNow += 3000; updateCountdown(testNow); assert(table().hubState == HubState::Running);

  // Running: the active Sigil passes, claims or pauses; others may pause.
  // Every living seat can change its own life.
  const uint8_t activeId = table().game.activePlayer()->controllerId;
  const uint8_t otherId = activeId == 0 ? 1 : 0;
  assert(only(activeId, {A::Pass, A::ClaimWin, A::Pause, A::AdjustLife}));
  assert(sigilMenuFor(activeId).defaultAction == static_cast<uint8_t>(A::Pass));
  assert(only(otherId, {A::Pause, A::AdjustLife}));
  pick(activeId, A::Pass); assert(table().pendingPass.active);
  assert(has(activeId, A::CancelPass) && !has(activeId, A::Pass));
  pick(activeId, A::CancelPass); assert(!table().pendingPass.active);

  // Paused: resume, "I'm out", and a claim for the active player.
  pick(otherId, A::Pause); assert(table().hubState == HubState::Paused);
  assert(only(otherId, {A::Resume, A::BeginElimination, A::AdjustLife}));
  assert(only(activeId, {A::Resume, A::BeginElimination, A::ClaimWin, A::AdjustLife}));
  pick(otherId, A::BeginElimination);
  assert(table().eliminationTargetPlayer != 0 && table().game.playerByNumber(table().eliminationTargetPlayer)->controllerId == otherId);
  assert(only(otherId, {A::Eliminate, A::CancelElimination}));
  assert(sigilMenuFor(otherId).defaultAction == static_cast<uint8_t>(A::Eliminate));
  assert(only(activeId, {A::CancelElimination}));
  pick(activeId, A::CancelElimination); assert(table().eliminationTargetPlayer == 0);
  pick(otherId, A::Resume); assert(table().hubState == HubState::Running);

  // A win claim asks the other players to confirm or deny.
  pick(activeId, A::ClaimWin); assert(table().game.hasWinClaim());
  const PlayerSeat *confirmer = table().game.playerByNumber(table().game.nextWinConfirmationPlayerNumber());
  assert(confirmer != nullptr);
  assert(only(confirmer->controllerId, {A::ConfirmWin, A::DenyWin}));
  assert(sigilMenuFor(confirmer->controllerId).defaultAction == static_cast<uint8_t>(A::ConfirmWin));
  pick(confirmer->controllerId, A::DenyWin); assert(!table().game.hasWinClaim());

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

  // A denied/withdrawn win claim resumes on the same millis() clock. Use
  // both an ordinary nonzero start and a start crossing the 32-bit wrap.
  for (uint32_t began : {123456u, UINT32_MAX - 2000u}) {
    assert(engine.start(seats,2,seats[0],began,settings));
    assert(engine.beginWinClaim(1,true,began + 1000));
    assert(engine.gameElapsedMs(began + 2000) == 1000);
    assert(engine.denyWinClaim(2,began + 2000));
    assert(!engine.paused() && engine.gameElapsedMs(began + 2500) == 1500);
    assert(engine.turnRemainingMs(began + 2500) == 58500);
    assert(engine.beginWinClaim(1,true,began + 3000));
    assert(engine.cancelWinClaim(1,began + 4000));
    assert(engine.gameElapsedMs(began + 4500) == 2500);
  }

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
    return selectSigilLedState(id,HubState::Lobby,table().lobby,table().game,0,0,0,now);
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
  table().nextGameSettings.turnTimerMs = 60000;
  startFromHost();
  assert(table().game.turnTimerMs() == 60000);
  const uint8_t active = table().game.activeController();
  resetBuzzes();
  // loop() samples millis() before the countdown stamps the new turn; that
  // stale sample must not raise a spurious EXPIRED cue at game start.
  updateTurnTimerCues(testNow - 1); drainAudio();
  assert(totalBuzzes() == 0 && table().turnTimerCue.phase == TurnTimerPhase::Normal);
  testNow += 49000; updateTurnTimerCues(testNow); drainAudio();
  assert(totalBuzzes() == 0);
  testNow += 1000; updateTurnTimerCues(testNow); updateTurnTimerCues(testNow); drainAudio();
  assert(fixtureBuzzes[active] == 1 && totalBuzzes() == 1);  // One chirp, once.
  testNow += 10000; updateTurnTimerCues(testNow); drainAudio();
  assert(fixtureBuzzes[active] == 3 && totalBuzzes() == 3);  // Two-note expiry, once.
  testNow += 600000; updateTurnTimerCues(testNow); drainAudio();
  assert(totalBuzzes() == 3 && table().hubState == HubState::Running && table().game.activeController() == active);
  // Pause/resume does not replay the timer cue.
  assert(web(active,1,WebControl::PauseResume) && web(active,1,WebControl::PauseResume));
  resetBuzzes(); updateTurnTimerCues(testNow); drainAudio();
  assert(totalBuzzes() == 0);
  // The next turn re-arms, and muted audio stays silent while LEDs still render.
  choose(active,SigilAction::Pass); testNow += 3000; updatePendingPass(testNow);
  const uint8_t next = table().game.activeController();
  assert(next != active);
  resetBuzzes();
  AudioCueProfile muted = defaultAudioCueProfile();
  muted.enabled = false;
  audio.setProfile(muted);
  testNow += 51000; updateTurnTimerCues(testNow); drainAudio();
  assert(totalBuzzes() == 0 && table().turnTimerCue.phase == TurnTimerPhase::Warning);
  assert(selectSigilLedState(next,table().hubState,table().lobby,table().game,0,0,0,testNow).has(LedOverlay::TurnWarning));
  audio.setProfile(defaultAudioCueProfile());
  // Settings stay lobby-only while the captured timer runs.
  table().nextGameSettings = GameSettings{};
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
  const uint8_t first = player(table().game.nextWinConfirmationPlayerNumber()).controllerId;
  assert(heardActionRequired(first) && !heardActionRequired(0));
  for (uint8_t id = 0; id < 3; ++id) if (id != first) assert(!heardActionRequired(id));
  clearTones();
  choose(first,SigilAction::ConfirmWin); drainAudio();
  const uint8_t second = player(table().game.nextWinConfirmationPlayerNumber()).controllerId;
  assert(second != first && heardActionRequired(second) && !heardActionRequired(first));
  clearTones();
  choose(second,SigilAction::ConfirmWin); drainAudio();
  assert(table().game.gameOver());
  for (uint8_t id = 0; id < 3; ++id) assert(!heardActionRequired(id));  // Nothing left to decide.
  // A life change request reaches only the recipient, who must approve it.
  choose(0,SigilAction::Rematch); assert(table().hubState == HubState::Lobby);
  startFromHost();
  clearTones();
  TurnHub::IntentPayload payload; payload.targetPlayer = 2; payload.value = -3; String message;
  const uint8_t requester = table().game.playerByNumber(1)->controllerId;
  const uint8_t recipient = table().game.playerByNumber(2)->controllerId;
  assert(changeCounter(requester, 1, IntentType::RequestLifeChange, payload, message)); drainAudio();
  assert(heardActionRequired(recipient) && !heardActionRequired(requester));
  table().game.cancelLifeChanges();
  enterEmptyLobby();
}

static void turnTimerSettingsHttp() {
  enterEmptyLobby(); TurnHub::fixtureRadio = false; testNow = 1000;
  table().nextGameSettings = TurnHub::GameSettings{};
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
  assert(table().nextGameSettings.turnTimerMs == 90000);
  assert(request("/api/game/settings", guest, {{"turnTimerMs", "0"}}) == 200);
  assert(table().nextGameSettings.turnTimerMs == 0);
  // Partial update: only the timer changes.
  table().nextGameSettings.profile = TurnHub::GameProfile::Magic; table().nextGameSettings.startingLife = 20;
  assert(request("/api/game/settings", host, {{"turnTimerMs", "90000"}}) == 200);
  assert(table().nextGameSettings.turnTimerMs == 90000 && table().nextGameSettings.profile == TurnHub::GameProfile::Magic &&
      table().nextGameSettings.startingLife == 20);
  assert(request("/api/v1/state", "", {}, HTTP_GET) == 200);
  assert(server.body.find("\"turnTimerMs\":90000") != std::string::npos);
  assert(server.body.find("\"turnTimer\":{\"phase\":\"NORMAL\",\"remainingMs\":null}") != std::string::npos);
  assert(request("/api/control/start", host) == 200);
  testNow += 3000; updateCountdown(testNow);
  assert(table().hubState == HubState::Running);
  testNow += 81000;
  assert(request("/api/v1/state", "", {}, HTTP_GET) == 200);
  assert(server.body.find("\"phase\":\"WARNING\",\"remainingMs\":9000") != std::string::npos);
  saveClientFixture("timer-warning", server.body);
  server.on("/api/status", HTTP_GET, handleStatus);  // Registered by startNetworking() on hardware.
  assert(request("/api/status", "", {}, HTTP_GET) == 200);
  assert(server.body.find("\"turnTimerMs\":90000") != std::string::npos &&
      server.body.find("\"timerPhase\":\"WARNING\"") != std::string::npos);
  assert(request("/api/game/settings", host, {{"turnTimerMs", "60000"}}) == 409);  // Lobby only.
  enterEmptyLobby(); table().nextGameSettings = TurnHub::GameSettings{};
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
  table().clientState.setNameLookup(displayNameForTableSeat);  // As setup() does.
  enterEmptyLobby(); testNow = 1000;
  table().nextGameSettings = TurnHub::GameSettings{};
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
  assert(table().hubState == HubState::Running);
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
  assert(!table().pendingPass.active);
  assert(request("/api/control/pass", first, {{"expectedRevision", String(runningRevision)}, {"expectedBootId", epoch}}) == 200);
  assert(table().pendingPass.active && clientRevision() > runningRevision);
  saveClientFixture("pass-result", server.body);
  const auto armedRevision = clientRevision();
  // Retry with the old revision cannot toggle/cancel the pending PASS.
  assert(request("/api/control/pass", first, {{"expectedRevision", String(runningRevision)}, {"expectedBootId", epoch}}) == 409);
  assert(table().pendingPass.active && clientRevision() == armedRevision);
  saveClientFixture("conflict", server.body);
  testNow += 3000; updatePendingPass(testNow);
  assert(table().game.activePlayerNumber() == 2 && clientRevision() > armedRevision);
  const String reconnected = loginPhone(firstId);
  assert(request("/api/session/me", reconnected, {}, HTTP_GET) == 200);
  assert(request("/api/v1/state", reconnected, {}, HTTP_GET) == 200);
  assert(server.body.find("\"activePlayer\":2") != std::string::npos);
  saveClientFixture("reconnected", server.body);

  freshLobby(2);
  table().nextGameSettings.profile = TurnHub::GameProfile::Commander;
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
  assert(table().clientState.revision() > pendingRevision && table().game.lifeTotal(2) == 38);
  const auto settledRevision = table().clientState.revision();
  dispatchSystemIntent(IntentType::ExpireLifeChanges);
  assert(table().clientState.revision() == settledRevision);
  testNow = UINT32_MAX - 10000;
  assert(changeCounter(0, 1, IntentType::RequestLifeChange, payload, message));
  const auto rolloverRevision = table().clientState.revision();
  testNow += TurnHub::LIFE_APPROVAL_MS;
  dispatchSystemIntent(IntentType::ExpireLifeChanges);
  assert(table().clientState.revision() > rolloverRevision && table().game.lifeTotal(2) == 36);
  // A recipient's longer window (accessibility) holds the request past 15 s.
  assert(table().game.requestLifeChange(1, 2, -1, testNow, 60000));
  assert(table().game.lifeChangeFor(2)->windowMs == 60000);
  testNow += TurnHub::LIFE_APPROVAL_MS; table().game.expireLifeChanges(testNow);
  assert(table().game.lifeChangeFor(2)->state == TurnHub::LifeChangeState::Pending && table().game.lifeTotal(2) == 36);
  testNow += 60000 - TurnHub::LIFE_APPROVAL_MS; table().game.expireLifeChanges(testNow);
  assert(table().game.lifeChangeFor(2)->state == TurnHub::LifeChangeState::Automatic && table().game.lifeTotal(2) == 35);

  // Exercise the largest snapshot with every Commander source populated.
  GameEngine fullGame; Lobby fullLobby; TurnHub::ClientState full;
  PlayerSeat seats[MAX_PLAYERS];
  for (uint8_t i = 0; i < MAX_PLAYERS; ++i)
    seats[i] = PlayerSeat(i + 1, i / 2, i % 2 + 1);
  assert(fullGame.start(seats, MAX_PLAYERS, seats[0], testNow, table().nextGameSettings));
  for (uint8_t i = 1; i <= MAX_PLAYERS; ++i)
    for (uint8_t j = 1; j <= MAX_PLAYERS; ++j)
      for (uint8_t c = 1; c <= 2; ++c)
        assert(i == j ? !fullGame.changeCommanderDamage(i, j, c, 1) : fullGame.changeCommanderDamage(i, j, c, 1));
  full.observe(HubState::Running, fullLobby, fullGame, table().nextGameSettings, {});
  const auto fullRevision = full.revision();
  full.observe(HubState::Running, fullLobby, fullGame, table().nextGameSettings, {});
  assert(full.revision() == fullRevision);
  const String fullJson = full.json("THA-TEST", epoch.c_str(), fullGame, testNow, PASS_GRACE_MS);
  assert(fullJson.length() > 10000);
  saveClientFixture("full", fullJson);
  enterEmptyLobby(); table().nextGameSettings = TurnHub::GameSettings{};
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
static void touchAt(int16_t x,int16_t y) { updateTouchControls(testNow,true,x,y); }
static void touchRelease() { testNow+=TOUCH_RELEASE_MS; updateTouchControls(testNow,false,0,0); }
static void pressButton(TouchAction action) {
  const AtlasScreen screen=currentScreen();  // keep it alive: b points into it
  const TouchButton *b=screenButton(screen,action); assert(b!=nullptr);
  touchAt(b->x+b->w/2,b->y+b->h/2);
}
static void tapButton(TouchAction action) { pressButton(action); testNow+=30; pressButton(action); touchRelease(); }
// Pair, QR codes, Theme and Device live under the Settings tab.
static void openMenuScreen() {
  if (currentScreen().kind!=ScreenKind::Menu) tapButton(TouchAction::OpenSettings);
  assert(currentScreen().kind==ScreenKind::Menu);
}
// End match and Master pass live on the in-game Table screen.
static void openTableScreen() {
  if (currentScreen().kind!=ScreenKind::Status && currentScreen().kind!=ScreenKind::Table)
    tapButton(TouchAction::OpenGame);
  if (!screenButton(currentScreen(),TouchAction::EndMatch)) tapButton(TouchAction::OpenTable);
}
// A hazardous button asks first: tap it, then Confirm in the popup.
static void tapConfirmed(TouchAction action) {
  tapButton(action);
  assert(currentScreen().kind==ScreenKind::Confirm);
  tapButton(TouchAction::ConfirmYes);
}
// Taps the on-screen End match button and confirms it.
static void holdEndMatch() {
  openTableScreen();
  tapConfirmed(TouchAction::EndMatch);
}
// A player's own PASS from their Sigil (the touchscreen no longer passes).
static void seatPass() {
  assert(dispatchSeatIntent(IntentType::Pass,IntentOrigin::PhysicalSigil,*table().game.activePlayer()).accepted());
}

// A Sigil can join Seat B and start a shared-seat match.
static void sigilSeatsTwoPlayers() {
  freshLobby(2);
  choose(1,SigilAction::AddSeatB);
  assert(table().lobby.hasSecondary(1) && table().lobby.playerCount()==3);
  assert(dispatchModuleIntent(IntentType::Leave,1,2).accepted());
  assert(dispatchModuleIntent(IntentType::Join,1,2).accepted() && table().lobby.hasSecondary(1));
  assert(dispatchModuleIntent(IntentType::Join,0,2).accepted() && table().lobby.hasSecondary(0));
  assert(dispatchModuleIntent(IntentType::ArmStart,0).accepted());
  enterEmptyLobby();
}

static void touchControls() {
  resetTouchControls(); freshLobby(2); TurnHub::fixtureRadio=true; pairingActive=false;
  AtlasScreen s=currentScreen();
  // The lobby row: Add player, Start (New game) and Clear; the rest is under
  // the Settings tab, its only path.
  assert(String(s.title)=="Lobby" && s.buttonCount==7 && screenButton(s,TouchAction::OpenNewGame) &&
      screenButton(s,TouchAction::OpenSeat) && screenButton(s,TouchAction::ClearLobby)->confirm &&
      !screenButton(s,TouchAction::StartGame) && !screenButton(s,TouchAction::Pair) && !screenButton(s,TouchAction::OpenQr));
  assert(s.round==0 && s.gameClock[0]=='\0' && s.players[0].turnTime[0]=='\0');
  // Every button fits on screen and meets the 44 px minimum target size.
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.y>=0 && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);
  tapButton(TouchAction::OpenSettings); s=currentScreen();
  assert(s.kind==ScreenKind::Menu && String(s.badge)=="SETTINGS" && screenButton(s,TouchAction::Pair) &&
      screenButton(s,TouchAction::OpenQr) && screenButton(s,TouchAction::OpenThemes) && screenButton(s,TouchAction::OpenDevice) &&
      !screenButton(s,TouchAction::CloseScreen) && table().hubState==HubState::Lobby);
  // Settings is one list: full-width rows of one height, touching, then the tabs.
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.y>=SCREEN_BODY_Y && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);
  for (uint8_t i=1;i<s.buttonCount && s.buttons[i].y<432;++i)
    assert(s.buttons[i].y==s.buttons[i-1].y+s.buttons[i-1].h && s.buttons[i].h==s.buttons[0].h && s.buttons[i].w==s.buttons[0].w);

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
  // A refused pairing leaves Settings up; the Game tab returns to the lobby.
  assert(currentScreen().kind==ScreenKind::Menu);
  tapButton(TouchAction::OpenGame); assert(currentScreen().kind==ScreenKind::Status);
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

  // Running: Pass (the active player's pass, as on the app's shared table),
  // Pause and Actions.
  startFromHost(); s=currentScreen();
  assert(startsWith(s.title,"Player ") && s.buttonCount==7 && screenButton(s,TouchAction::PassTurn) &&
      screenButton(s,TouchAction::Pause) && screenButton(s,TouchAction::OpenTable) &&
      !screenButton(s,TouchAction::EndMatch) && !screenButton(s,TouchAction::MasterPass));
  seatPass(); assert(String(currentScreen().detail)=="Passing in 3s: that seat can cancel");
  testNow+=PASS_GRACE_MS; updatePendingPass(testNow);
  tapButton(TouchAction::Pause); assert(table().hubState==HubState::Paused);
  s=currentScreen();
  assert(String(s.title)=="Paused" && s.buttonCount==6 && screenButton(s,TouchAction::Resume) &&
      screenButton(s,TouchAction::OpenTable));
  // Paused, the Table screen offers End match but not the master pass.
  tapButton(TouchAction::OpenTable); s=currentScreen();
  assert(s.kind==ScreenKind::Table && String(s.detail)=="Resume to use Master pass" &&
      !screenButton(s,TouchAction::MasterPass) && screenButton(s,TouchAction::EndMatch)->confirm);
  tapButton(TouchAction::CloseScreen); tapButton(TouchAction::Resume); assert(table().hubState==HubState::Running);

  // The app's table on Atlas (2026-10-28): a tile's lower halves take one
  // life from that player or give one back; its name band opens their screen
  // (below). Pass is the active player's own pass, a tap with its grace period.
  {
    const PlayerSeat second=*table().game.playerAt(1);
    const int32_t life=table().game.lifeTotal(second.playerNumber);
    int16_t cx,cy,cw,ch; screenChipCell(1,table().game.playerCount(),cx,cy,cw,ch);
    touchAt(cx+cw/4,cy+ch-8); touchRelease();
    assert(table().game.lifeTotal(second.playerNumber)==life-1 && currentScreen().kind==ScreenKind::Status);
    touchAt(cx+cw*3/4,cy+ch-8); touchRelease(); touchAt(cx+cw*3/4,cy+ch-8); touchRelease();
    assert(table().game.lifeTotal(second.playerNumber)==life+1);
    touchAt(cx+cw/4,cy+ch-8); touchRelease();
    assert(table().game.lifeTotal(second.playerNumber)==life);
    s=currentScreen();
    const TouchButton *pass=screenButton(s,TouchAction::PassTurn);
    assert(pass && !pass->confirm && pass->w>=44);
    const uint8_t before=table().game.activePlayer()->playerNumber;
    tapButton(TouchAction::PassTurn);
    assert(table().pendingPass.active);
    testNow+=PASS_GRACE_MS+1; updatePendingPass(testNow);
    assert(!table().pendingPass.active && table().game.activePlayer()->playerNumber!=before);
  }

  // Playtest 2026-09-29 item 9: a player's chip opens their screen, which
  // changes their own life and concedes only after the confirm popup.
  {
    const PlayerSeat second=*table().game.playerAt(1);
    const int32_t life=table().game.lifeTotal(second.playerNumber);
    int16_t cx,cy,cw,ch; screenChipCell(1,table().game.playerCount(),cx,cy,cw,ch);
    touchAt(cx+cw/2,cy+10); testNow+=30; touchAt(cx+cw/2,cy+10); touchRelease();
    s=currentScreen();
    assert(s.kind==ScreenKind::Player && String(s.badge)=="PLAYER" && s.buttonCount==11 &&
        String(screenButton(s,TouchAction::LifeMinus5)->label)=="-5" && screenButton(s,TouchAction::CloseScreen) &&
        screenButton(s,TouchAction::Concede)->confirm);
    for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None) assert(b.w>=44 && b.h>=44);
    tapButton(TouchAction::LifeMinus5); tapButton(TouchAction::LifePlus1);
    assert(table().game.lifeTotal(second.playerNumber)==life-4);
    tapButton(TouchAction::Concede); s=currentScreen();
    assert(!table().game.isEliminated(second.playerNumber) && s.kind==ScreenKind::Confirm && s.buttonCount==2 &&
        screenButton(s,TouchAction::ConfirmYes) && screenButton(s,TouchAction::ConfirmNo) && startsWith(s.title,"Concede for "));
    for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None) assert(b.w>=44 && b.h>=44 && b.y>=CONFIRM_CARD_Y && b.y+b.h<=CONFIRM_CARD_Y+CONFIRM_CARD_H);
    tapButton(TouchAction::ConfirmNo);
    assert(!table().game.isEliminated(second.playerNumber) && currentScreen().kind==ScreenKind::Player);
    tapButton(TouchAction::CloseScreen); assert(currentScreen().kind==ScreenKind::Status);
  }

  // End match asks in the popup first, and acts once on Confirm.
  tapButton(TouchAction::OpenTable); s=currentScreen();
  assert(s.kind==ScreenKind::Table && String(s.badge)=="TABLE" && s.buttonCount==7 &&
      screenButton(s,TouchAction::MasterPass)->confirm &&
      screenButton(s,TouchAction::EndMatch)->confirm && screenButton(s,TouchAction::CloseScreen));
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.y>=SCREEN_BODY_Y && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);
  completedGames=0;
  // A long press is just a tap: it opens the popup, nothing more.
  pressButton(TouchAction::EndMatch); testNow+=10000; pressButton(TouchAction::EndMatch); touchRelease();
  s=currentScreen();
  assert(table().hubState==HubState::Running && s.kind==ScreenKind::Confirm && String(s.title)=="End the match?" &&
      String(screenButton(s,TouchAction::ConfirmYes)->label)=="End match");
  // Cancel goes back to the screen that asked.
  tapButton(TouchAction::ConfirmNo); assert(currentScreen().kind==ScreenKind::Table && table().hubState==HubState::Running);
  tapButton(TouchAction::EndMatch); tapButton(TouchAction::ConfirmYes);
  assert(table().hubState==HubState::GameOver && table().game.endedInDraw() && completedGames==1);
  touchAt(160,120); testNow+=5000; touchAt(160,120); touchRelease();
  assert(completedGames==1);
  s=currentScreen();
  assert(s.kind==ScreenKind::Status && String(s.title)=="Game over" && String(s.detail)=="The match ended in a draw" &&
      s.buttonCount==6 && screenButton(s,TouchAction::Rematch) && screenButton(s,TouchAction::ResetTable)->confirm);
  // After a game Settings has no Pair; the round and match length stay up.
  assert(s.round>=1 && s.gameClock[0]!='\0');
  tapButton(TouchAction::OpenSettings); s=currentScreen();
  assert(s.kind==ScreenKind::Menu && !screenButton(s,TouchAction::Pair) && screenButton(s,TouchAction::OpenQr));
  tapButton(TouchAction::OpenGame);

  // Four players across three Sigils: third is A on the last shared Sigil.
  enterEmptyLobby(); freshLobby(3);
  {
    PlayerSeat joined[4]; table().lobby.buildPlayers(joined, 4);
    const uint8_t shared = joined[2].controllerId;
    bool added; PlayerSeat b;
    assert(table().lobby.toggleSecondary(shared, added, b) && added);
    PlayerSeat starter; assert(table().lobby.selectStarterSeat(shared, 1, starter));
    table().lobby.buildPlayers(joined, 4);
    const uint32_t aParticipant = joined[2].participantId;
    const uint32_t bParticipant = joined[3].participantId;
    int16_t cx,cy,cw,ch; screenChipCell(2,4,cx,cy,cw,ch);
    touchAt(cx+cw/2,cy+ch/2); touchRelease();
    s=currentScreen();
    assert(startsWith(s.detail,"Turn order: 3 of 4") && screenButton(s,TouchAction::SeatBLeft));
    table().lobby.setStartArmedBy(shared);
    tapButton(TouchAction::MoveLater);
    assert(table().lobby.playerNumber(shared,1)==4 && table().lobby.playerNumber(shared,2)==3 &&
        table().lobby.startArmedBy()==INVALID_ID);
    assert(table().lobby.selectedStarter(starter) && starter.slot==1 && starter.playerNumber==4);
    assert(startsWith(currentScreen().detail,"Turn order: 4 of 4"));
    tapButton(TouchAction::MoveLater);
    assert(startsWith(currentScreen().notice,"Already last"));
    tapButton(TouchAction::SeatBRight);
    assert(table().lobby.playerNumber(shared,1)==3 && !table().lobby.secondaryFirst(shared));
    tapButton(TouchAction::SeatBLeft);
    assert(table().lobby.playerNumber(shared,2)==3 && table().lobby.secondaryFirst(shared));
    table().lobby.buildPlayers(joined,4);
    assert(joined[2].slot==2 && joined[2].participantId==bParticipant &&
        joined[3].slot==1 && joined[3].participantId==aParticipant);
    Intent side; side.type=IntentType::SetSeatSide; side.payload.targetPlayer=3;
    side.actor.origin=IntentOrigin::Browser;
    assert(intents.dispatch(side).status==IntentStatus::Unauthorized);
    side.actor.origin=IntentOrigin::AtlasHardware; side.payload.value=2;
    assert(intents.dispatch(side).status==IntentStatus::Conflict);
    side.payload.value=1; side.payload.targetPlayer=1;
    assert(intents.dispatch(side).status==IntentStatus::Conflict);
    tapButton(TouchAction::CloseScreen);
    startFromHost();
    assert(table().game.playerAt(2)->slot==2 && table().game.playerAt(3)->slot==1);
    side.payload.targetPlayer=3;
    assert(intents.dispatch(side).status==IntentStatus::InvalidState);
    TurnHub::GameCheckpoint saved; table().game.checkpoint(saved,testNow);
    assert(TurnHub::validCheckpoint(saved));
    Lobby restored;
    assert(restored.restorePlayers(saved.players,saved.count,saved.starter));
    PlayerSeat recovered[4]; restored.buildPlayers(recovered,4);
    for (uint8_t i=0;i<4;++i) assert(recovered[i].sameSeat(saved.players[i]) &&
        recovered[i].participantId==saved.players[i].participantId);
    restored.resetForRematch(); assert(restored.secondaryFirst(shared));
    assert(restored.moveController(shared,-1));
    restored.buildPlayers(recovered,4);
    assert(recovered[1].slot==2 && recovered[2].slot==1);
    assert(restored.toggleSecondary(shared,added,b) && !added);
    assert(restored.toggleSecondary(shared,added,b) && added && !restored.secondaryFirst(shared));
    // Also cover four players on two shared Sigils, and B/A at the start.
    TurnHub::Lobby pairs;
    assert(pairs.join(0) && pairs.toggleSecondary(0,added,b));
    assert(pairs.join(1) && pairs.toggleSecondary(1,added,b));
    assert(pairs.moveSeat(1,1,1) && pairs.playerNumber(1,1)==4);
    assert(pairs.setSecondaryFirst(0,true));
    assert(pairs.starterOrDefault(starter) && starter.slot==2 && starter.playerNumber==1);
    TurnHub::GameEngine pairGame;
    pairs.buildPlayers(recovered,4);
    assert(pairGame.start(recovered,4,starter,testNow));
    TurnHub::GameCheckpoint pairSaved; pairGame.checkpoint(pairSaved,testNow);
    assert(TurnHub::validCheckpoint(pairSaved));
    TurnHub::Lobby pairRestored;
    assert(pairRestored.restorePlayers(pairSaved.players,4,pairSaved.starter));
    assert(pairRestored.secondaryFirst(0) && pairRestored.secondaryFirst(1));
    // Atlas sends physical B identity even when B has the lower number.
    LedRenderer renderer(sigilBus);
    renderer.render(HubState::Running,table().lobby,table().game,0,0,0,testNow);
    assert(!TurnHubProtocol::hasDisplayFlag(TurnHub::sentGameDisplays[shared].state,
        TurnHubProtocol::DISPLAY_FLAG_PRIMARY_B)); // starter is A
    assert(table().game.passTurn(shared,testNow+1));
    assert(table().game.passTurn(table().game.activeController(),testNow+1));
    assert(table().game.passTurn(table().game.activeController(),testNow+1));
    renderer.render(HubState::Running,table().lobby,table().game,0,0,0,testNow+1);
    assert(TurnHubProtocol::hasDisplayFlag(TurnHub::sentGameDisplays[shared].state,
        TurnHubProtocol::DISPLAY_FLAG_PRIMARY_B));
    // Reject recovery with a nonadjacent B/A pair.
    std::swap(saved.players[1].controllerId,saved.players[2].controllerId);
    assert(!TurnHub::validCheckpoint(saved));
  }

  // Staged Changes 2026-10-06: a confirmed Remove on a lobby Player screen
  // takes that one seat out (seat A takes its Sigil's seat B too); Cancel does not.
  enterEmptyLobby(); freshLobby(3);
  {
    PlayerSeat seats[3]; table().lobby.buildPlayers(seats,3);
    const uint8_t gone=seats[1].controllerId;
    int16_t cx,cy,cw,ch; screenChipCell(1,3,cx,cy,cw,ch);
    touchAt(cx+cw/2,cy+ch/2); touchRelease();
    assert(screenButton(currentScreen(),TouchAction::RemoveSeat));
    tapButton(TouchAction::RemoveSeat); assert(table().lobby.playerCount()==3 && currentScreen().kind==ScreenKind::Confirm);
    tapButton(TouchAction::ConfirmNo); assert(table().lobby.playerCount()==3 && currentScreen().kind==ScreenKind::Player);
    tapConfirmed(TouchAction::RemoveSeat);
    assert(table().lobby.playerCount()==2 && !table().lobby.isJoined(gone) && currentScreen().kind==ScreenKind::Status);
    assert(logHas("ATLAS|LOBBY|REMOVE|CONTROLLER|"));
    // Only the Atlas screen, and only in the lobby.
    Intent fromPhone; fromPhone.type=IntentType::RemoveSeat; fromPhone.actor.origin=IntentOrigin::Browser;
    fromPhone.actor.controllerId=seats[0].controllerId; fromPhone.actor.slot=1;
    assert(intents.dispatch(fromPhone).status==IntentStatus::Unauthorized && table().lobby.playerCount()==2);
    Intent missing; missing.type=IntentType::RemoveSeat; missing.actor.origin=IntentOrigin::AtlasHardware;
    missing.actor.controllerId=gone; missing.actor.slot=1;
    assert(intents.dispatch(missing).status==IntentStatus::InvalidActor);
    // Seat B alone, then seat A with a seat B.
    const uint8_t shared=seats[0].controllerId;
    Intent addB; addB.type=IntentType::Join; addB.actor.origin=IntentOrigin::PhysicalSigil;
    addB.actor.controllerId=shared; addB.actor.slot=2;
    assert(intents.dispatch(addB).accepted() && table().lobby.hasSecondary(shared) && table().lobby.playerCount()==3);
    Intent removeB=missing; removeB.actor.controllerId=shared; removeB.actor.slot=2;
    assert(intents.dispatch(removeB).accepted() && !table().lobby.hasSecondary(shared) && table().lobby.isJoined(shared));
    assert(intents.dispatch(addB).accepted());
    Intent removeA=removeB; removeA.actor.slot=1;
    assert(intents.dispatch(removeA).accepted() && !table().lobby.isJoined(shared) && table().lobby.playerCount()==1);
  }

  // Playtest 2026-09-29 item 11: in the lobby a chip opens that seat's turn
  // order; any player may move it, from the Atlas screen only.
  enterEmptyLobby(); freshLobby(3);
  {
    PlayerSeat seats[3]; table().lobby.buildPlayers(seats,3);
    const uint8_t mover=seats[2].controllerId;
    int16_t cx,cy,cw,ch; screenChipCell(2,3,cx,cy,cw,ch);
    touchAt(cx+cw/2,cy+ch/2); touchRelease();
    s=currentScreen();
    assert(s.kind==ScreenKind::Player && String(s.badge)=="ORDER" && startsWith(s.detail,"Turn order: 3 of 3") &&
        screenButton(s,TouchAction::MoveEarlier) && screenButton(s,TouchAction::MoveLater) &&
        !screenButton(s,TouchAction::Concede));
    tapButton(TouchAction::MoveEarlier);
    assert(table().lobby.playerNumber(mover)==2 && startsWith(currentScreen().detail,"Turn order: 2 of 3"));
    tapButton(TouchAction::MoveEarlier); assert(table().lobby.playerNumber(mover)==1);
    tapButton(TouchAction::MoveEarlier);
    assert(table().lobby.playerNumber(mover)==1 && startsWith(currentScreen().notice,"Already first"));
    Intent fromPhone; fromPhone.type=IntentType::MoveSeat; fromPhone.actor.origin=IntentOrigin::Browser;
    fromPhone.payload.targetPlayer=1; fromPhone.payload.value=1;
    assert(intents.dispatch(fromPhone).status==IntentStatus::Unauthorized && table().lobby.playerNumber(mover)==1);
    tapButton(TouchAction::CloseScreen);
    // The new order is the game's turn order.
    startFromHost(); assert(table().game.playerAt(0)->controllerId==mover);
    Intent late; late.type=IntentType::MoveSeat; late.actor.origin=IntentOrigin::AtlasHardware;
    late.payload.targetPlayer=1; late.payload.value=1;
    assert(intents.dispatch(late).status==IntentStatus::InvalidState);
  }

  // A confirmed concession from the Player screen goes through Concede: the
  // player is out, the match goes on, and their screen closes.
  {
    const uint8_t third=table().game.playerAt(2)->playerNumber;
    int16_t cx,cy,cw,ch; screenChipCell(2,table().game.playerCount(),cx,cy,cw,ch);
    touchAt(cx+cw/2,cy+10); touchRelease();
    tapConfirmed(TouchAction::Concede);
    assert(table().game.isEliminated(third) && table().hubState==HubState::Running && currentScreen().kind==ScreenKind::Status);
    // An eliminated player's chip opens nothing.
    touchAt(cx+cw/2,cy+ch/2); touchRelease(); assert(currentScreen().kind==ScreenKind::Status);
  }

  // A press whose button disappears before release does nothing.
  enterEmptyLobby(); freshLobby(2); startFromHost();
  pressButton(TouchAction::Pause); assert(web(0,1,WebControl::PauseResume) && table().hubState==HubState::Paused);
  touchRelease(); assert(table().hubState==HubState::Paused);
  resetTouchControls(); enterEmptyLobby();
}

// The status screen's chips, the NO SD CARD warning, and the Info and QR
// screens, which change no table state.
extern bool fixtureSdCardReady;
extern TurnHub::BatteryReading fixtureBattery;
static void atlasScreens() {
  resetTouchControls(); enterEmptyLobby(); pairingActive=false;
  AtlasScreen s=currentScreen();
  assert(s.kind==ScreenKind::Status && String(s.badge)=="LOBBY" && s.playerCount==0 && !s.sdMissing);
  assert(s.qr[0]=='\0' && s.lineCount>0);  // Empty table: how to join, no QR code (it is under Settings).
  fixtureSdCardReady=false; assert(currentScreen().sdMissing); fixtureSdCardReady=true;
  fixtureBattery.present=true; fixtureBattery.percent=12; fixtureBattery.low=true;
  s=currentScreen(); assert(s.batteryLow && s.batteryPercent==12);
  fixtureBattery=TurnHub::BatteryReading(); assert(!currentScreen().batteryLow && currentScreen().batteryPercent==-1);

  freshLobby(3); s=currentScreen();
  assert(s.playerCount==3 && s.qr[0]=='\0' && !s.showLife);
  assert(String(s.players[0].name)=="Player 1");
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);

  // Home is the table at a glance, with the tabs and Mana Tap to lead on:
  // no clocks and no device details (the header and QR codes hold those).
  tapButton(TouchAction::OpenHome); s=currentScreen();
  assert(s.kind==ScreenKind::Home && String(s.title)=="TurnHub" && String(s.badge)=="LOBBY" && s.lineCount==4 &&
      String(s.lines[0])=="3 players at the table" && String(s.lines[1])=="Start a game from the Game tab" &&
      s.lines[2][0] && s.playerCount==3 && s.clock[0]=='\0' &&
      s.buttonCount==5 && screenButton(s,TouchAction::OpenGame) && screenButton(s,TouchAction::OpenSettings) &&
      screenButton(s,TouchAction::OpenManaTap));
  for (uint8_t i=0;i<s.lineCount;++i) assert(!strstr(s.lines[i],"Wi-Fi") && !strstr(s.lines[i],"Portal"));
  fixtureBattery.present=true; fixtureBattery.percent=64; fixtureBattery.charging=true;
  assert(currentScreen().batteryPercent==64 && currentScreen().batteryCharging);
  openMenuScreen(); tapButton(TouchAction::OpenDevice);
  assert(startsWith(currentScreen().detail,"TurnHub Atlas v"));
  fixtureBattery=TurnHub::BatteryReading();
  openMenuScreen(); tapButton(TouchAction::OpenQr); s=currentScreen();
  assert(s.kind==ScreenKind::Qr && String(s.qr)=="http://192.168.4.1/portal" &&
      screenButton(s,TouchAction::QrPortal)->selected && !screenButton(s,TouchAction::QrWifi)->selected);
  tapButton(TouchAction::QrWifi); s=currentScreen();
  // The shipped default password is public, so its Wi-Fi code shows freely.
  assert(String(s.qr)=="WIFI:T:WPA;S:TurnHub-Atlas;P:TurnHub-Setup;;" && screenButton(s,TouchAction::QrWifi)->selected);
  tapButton(TouchAction::QrSignIn); assert(String(currentScreen().qr)=="http://192.168.4.1/login");
  assert(table().hubState==HubState::Lobby && table().lobby.playerCount()==3);  // Screens change no table state.
  // Back steps out of QR codes to Settings; the Game tab goes to the lobby.
  tapButton(TouchAction::CloseScreen); assert(currentScreen().kind==ScreenKind::Menu);
  tapButton(TouchAction::OpenGame); assert(currentScreen().kind==ScreenKind::Status);
  // Settings remains available when a game starts; Game returns to live play.
  openMenuScreen();

  startFromHost();
  assert(currentScreen().kind==ScreenKind::Menu);
  tapButton(TouchAction::OpenGame); s=currentScreen();
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
    assert(s.players[i].life==table().game.lifeTotal(s.players[i].number));
    if (s.players[i].flags&CHIP_ACTIVE) { ++active; assert(s.players[i].number==table().game.activePlayerNumber()); }
  }
  assert(active==1);
  char title[32]; snprintf(title,sizeof(title),"Player %u's turn",static_cast<unsigned>(table().game.activePlayerNumber()));
  assert(String(s.title)==title);
  resetTouchControls(); enterEmptyLobby();
}

// Newer firmware (update_notice.h): the app reports the release feed's
// versions; Atlas counts itself and each paired Sigil that runs something
// older, and says so on the Menu and Info screens.
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
  // Two e-ink Sigils on 0.9.2 and an OLED on 0.9.3.
  auto hello=[](uint8_t id,uint8_t caps,uint8_t patch){
    auto &record=TurnHub::fixtureRecords[id];
    record.helloInfoValid=true; record.capabilities=caps;
    record.firmwareMajor=0; record.firmwareMinor=9; record.firmwarePatch=patch;
  };
  hello(0,0,2); hello(1,0,2); hello(2,0,3);
  assert(request("/api/updates/latest","",{{"sigil","0.9.3"}})==200);
  assert(firmwareUpdatesAvailable()==2 && server.body.find("\"updatesAvailable\":2")!=std::string::npos &&
      server.body.find("\"atlas\":null")!=std::string::npos);
  assert(request("/api/updates/latest","",{{"atlas",newer},{"sigil","0.9.3"}})==200);
  assert(firmwareUpdatesAvailable()==3);  // Atlas and the two Sigils on 0.9.2.
  openMenuScreen(); AtlasScreen s=currentScreen();
  assert(firmwareUpdateKind()==TurnHub::UpdateKind::Sigils && s.update==TurnHub::UpdateKind::Sigils);
  assert(String(s.detail)=="Update available: use the app");
  tapButton(TouchAction::OpenHome); assert(currentScreen().update==TurnHub::UpdateKind::Sigils);
  // Only Atlas behind: the words name it, and every Sigil is told the same.
  assert(request("/api/updates/latest","",{{"atlas",newer},{"sigil","0.9.2"}})==200);
  assert(firmwareUpdatesAvailable()==1 && firmwareUpdateKind()==TurnHub::UpdateKind::AtlasOnly);
  assert(String(TurnHub::updateKindText(firmwareUpdateKind()))=="Update available for Atlas");
  openMenuScreen(); assert(String(currentScreen().detail)=="Update available for Atlas: use the app");
  tapButton(TouchAction::OpenHome);
  AtlasScreen header; buildAtlasScreen(testNow,header);
  assert(header.update==TurnHub::UpdateKind::AtlasOnly);
  syncSigilMenus(testNow);
  assert(TurnHub::fixtureUpdateNotice[0]==static_cast<int32_t>(TurnHub::UpdateKind::AtlasOnly));
  assert(request("/api/updates/latest","",{{"atlas",newer},{"sigil","0.9.4-dev"}})==200);
  syncSigilMenus(testNow);
  assert(TurnHub::fixtureUpdateNotice[0]==static_cast<int32_t>(TurnHub::UpdateKind::Sigils));
  // Up to date again: the header notice goes.
  assert(request("/api/updates/latest","",{{"sigil","0.9.2"}})==200 && firmwareUpdatesAvailable()==0);
  assert(currentScreen().update==TurnHub::UpdateKind::None);
  for (auto &record : TurnHub::fixtureRecords) { record.helloInfoValid=false; record.capabilities=0; }
  resetTouchControls();
}

// The lobby's Start opens New game; Start game there starts the match.
static void tapStartGame() {
  if (!screenButton(currentScreen(),TouchAction::StartGame)) tapButton(TouchAction::OpenNewGame);
  assert(currentScreen().kind==ScreenKind::NewGame);
  tapButton(TouchAction::StartGame);
}

// The touchscreen between games: Start (two or more players), Cancel start,
// Rematch and Reset, all as table actions from the Atlas hardware.
static void touchTableLifecycle() {
  resetTouchControls(); freshLobby(1); pairingActive=false;
  tapButton(TouchAction::OpenNewGame);
  assert(currentScreen().kind==ScreenKind::NewGame && !screenButton(currentScreen(),TouchAction::StartGame) &&
      String(currentScreen().detail)=="Seat two players to start");
  tapButton(TouchAction::CloseScreen);
  Intent start; start.type=IntentType::StartGame; start.actor.origin=IntentOrigin::AtlasHardware;
  assert(intents.dispatch(start).status==IntentStatus::InvalidState && table().hubState==HubState::Lobby);
  freshLobby(3); tapStartGame();
  assert(table().hubState==HubState::Starting);
  AtlasScreen s=currentScreen();
  assert(s.buttonCount==5 && screenButton(s,TouchAction::CancelStart));
  tapButton(TouchAction::CancelStart); assert(table().hubState==HubState::Lobby && table().lobby.playerCount()==3);
  // Only the touchscreen skips the seat and arming: an unseated browser cannot.
  Intent stranger=start; stranger.actor.origin=IntentOrigin::Browser;
  stranger.actor.controllerId=MAX_PHYSICAL_SIGILS; stranger.actor.slot=1;
  assert(!intents.dispatch(stranger).accepted() && table().hubState==HubState::Lobby);
  tapStartGame(); testNow+=START_COUNTDOWN_MS; updateCountdown(testNow);
  assert(table().hubState==HubState::Running);
  // Rematch and Reset only after a game.
  for (IntentType type : {IntentType::Rematch, IntentType::ResetGame}) {
    Intent early; early.type=type; early.actor.origin=IntentOrigin::AtlasHardware;
    assert(intents.dispatch(early).status==IntentStatus::InvalidState && table().hubState==HubState::Running);
  }
  holdEndMatch(); assert(table().hubState==HubState::GameOver);
  tapButton(TouchAction::Rematch); assert(table().hubState==HubState::Lobby && table().lobby.playerCount()==3);
  tapStartGame(); testNow+=START_COUNTDOWN_MS; updateCountdown(testNow);
  holdEndMatch(); assert(table().hubState==HubState::GameOver);
  // Reset asks first; Cancel keeps the finished game up.
  tapButton(TouchAction::ResetTable); assert(currentScreen().kind==ScreenKind::Confirm && table().hubState==HubState::GameOver);
  tapButton(TouchAction::ConfirmNo); assert(table().hubState==HubState::GameOver);
  tapConfirmed(TouchAction::ResetTable); assert(table().hubState==HubState::Lobby && table().lobby.playerCount()==0);
  // The lobby's Clear: shown only with someone joined, and confirmed.
  assert(!screenButton(currentScreen(),TouchAction::ClearLobby));
  freshLobby(3); resetTouchControls();
  assert(screenButton(currentScreen(),TouchAction::ClearLobby));
  tapButton(TouchAction::ClearLobby); assert(table().lobby.playerCount()==3);
  // The table moving on closes the popup: a code over it, or a match.
  assert(requestPresenceCode(String("ABCDEFGH"),false,testNow) && currentScreen().kind==ScreenKind::Code);
  tapButton(TouchAction::CancelCode); assert(currentScreen().kind==ScreenKind::Status);
  tapConfirmed(TouchAction::ClearLobby);
  assert(table().hubState==HubState::Lobby && table().lobby.playerCount()==0);
  assert(!screenButton(currentScreen(),TouchAction::ClearLobby));
  resetTouchControls(); enterEmptyLobby();
}

// New game (owner 2026-10-30): the app's game options on the touchscreen.
// Each row steps one setting through ConfigureGame from the table, seated
// players or not; official games keep their life until House rules.
static void touchNewGame() {
  using TurnHub::GameProfile;
  resetTouchControls(); enterEmptyLobby();
  const TurnHub::GameSettings saved=table().nextGameSettings;
  table().nextGameSettings=TurnHub::GameSettings{};
  table().nextGameSettings.profile=GameProfile::Commander;
  tapButton(TouchAction::OpenNewGame);
  AtlasScreen s=currentScreen();
  assert(s.kind==ScreenKind::NewGame && String(s.title)=="New game" && screenButton(s,TouchAction::CloseScreen));
  assert(String(screenButton(s,TouchAction::GameType)->label)=="Game: Commander" &&
      String(screenButton(s,TouchAction::GameMode)->label)=="Mode: Free-for-all" &&
      String(screenButton(s,TouchAction::GameLife)->label)=="Starting life: 40 (rules)" &&
      String(screenButton(s,TouchAction::GameTimer)->label)=="Turn timer: off" &&
      screenButton(s,TouchAction::GameHouseRules));
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.y>=SCREEN_BODY_Y && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);
  // Official life is fixed: the row says why, and nothing changes.
  tapButton(TouchAction::GameLife);
  assert(table().nextGameSettings.startingLife==40 && startsWith(currentScreen().notice,"Official rules set it"));
  // Commander modes, in the app's order.
  tapButton(TouchAction::GameMode); assert(table().nextGameSettings.twoHeadedGiant && table().nextGameSettings.startingLife==60);
  tapButton(TouchAction::GameMode); assert(table().nextGameSettings.profile==GameProfile::DoomCommander && !table().nextGameSettings.twoHeadedGiant);
  tapButton(TouchAction::GameMode); assert(table().nextGameSettings.profile==GameProfile::StarCommander);
  assert(String(screenButton(currentScreen(),TouchAction::GameMode)->label)=="Mode: Star Commander" &&
      !screenButton(currentScreen(),TouchAction::GameHouseRules));
  tapButton(TouchAction::GameMode); assert(table().nextGameSettings.profile==GameProfile::Commander);
  // Games: Standard takes Magic's official 20, Yu-Gi-Oh! 8000, Other is free.
  tapButton(TouchAction::GameType);
  assert(table().nextGameSettings.profile==GameProfile::Magic && table().nextGameSettings.startingLife==20);
  tapButton(TouchAction::GameType);
  assert(table().nextGameSettings.profile==GameProfile::Yugioh && table().nextGameSettings.startingLife==8000 &&
      !screenButton(currentScreen(),TouchAction::GameMode));
  tapButton(TouchAction::GameHouseRules); assert(table().nextGameSettings.houseRules);
  tapButton(TouchAction::GameLife); assert(table().nextGameSettings.startingLife==16000);
  tapButton(TouchAction::GameType); assert(table().nextGameSettings.profile==GameProfile::Generic);
  tapButton(TouchAction::GameLife); assert(table().nextGameSettings.startingLife==25);
  tapButton(TouchAction::GameTimer); assert(table().nextGameSettings.turnTimerMs==60000);
  assert(String(screenButton(currentScreen(),TouchAction::GameTimer)->label)=="Turn timer: 1 min");
  tapButton(TouchAction::GameType); assert(table().nextGameSettings.profile==GameProfile::Commander);
  // A setting change is no notice: the row shows it.
  testNow+=TOUCH_NOTICE_MS; tapButton(TouchAction::GameTimer); assert(currentScreen().notice[0]=='\0');
  // Only the touchscreen skips the seat: an unseated browser cannot.
  Intent stranger; stranger.type=IntentType::ConfigureGame; stranger.actor.origin=IntentOrigin::Browser;
  stranger.actor.controllerId=MAX_PHYSICAL_SIGILS; stranger.actor.slot=1;
  assert(!intents.dispatch(stranger).accepted());
  // The settings start the match.
  freshLobby(2); tapStartGame(); testNow+=START_COUNTDOWN_MS; updateCountdown(testNow);
  assert(table().hubState==HubState::Running && table().game.settings().profile==GameProfile::Commander &&
      table().game.settings().turnTimerMs==120000);
  // New game belongs to the lobby.
  assert(currentScreen().kind==ScreenKind::Status);
  holdEndMatch();
  table().nextGameSettings=saved;
  resetTouchControls(); enterEmptyLobby();
}

// A key or list row on the touchscreen, by its action and arg.
static void tapKey(TouchAction action,uint8_t arg) {
  const AtlasScreen screen=currentScreen();
  const TouchButton *b=nullptr;
  for (uint8_t i=0;i<screen.buttonCount;++i) if (screen.buttons[i].action==action && screen.buttons[i].arg==arg) b=&screen.buttons[i];
  assert(b!=nullptr);
  const int16_t x=b->x+b->w/2, y=b->y+b->h/2;
  touchAt(x,y); testNow+=30; touchAt(x,y); touchRelease();
}
static void touchPlayerCounters() {
  freshLobby(2); table().nextGameSettings.profile=TurnHub::GameProfile::Commander;
  table().nextGameSettings.startingLife=40; startFromHost(); resetTouchControls();
  int16_t x,y,w,h; screenChipCell(0,2,x,y,w,h);
  touchAt(x+w/2,y+10); touchRelease(); tapButton(TouchAction::OpenPlayerControls);
  auto s=currentScreen(); assert(s.showPlayerControls && s.controls.allowed);
  for(uint8_t i=0;i<s.buttonCount;++i) assert(s.buttons[i].w>=44 && s.buttons[i].h>=44);
  tapKey(TouchAction::ControlPlus,0); assert(table().game.countersForPlayer(1)->poison==1);
  tapKey(TouchAction::ControlMinus,0); assert(table().game.countersForPlayer(1)->poison==0);
  tapButton(TouchAction::PlayerControlsNext); assert(currentScreen().controlsPage==1);
  tapKey(TouchAction::ControlPlus,5); assert(table().game.countersForPlayer(1)->commanderCasts[0]==1);
  tapButton(TouchAction::PlayerControlsBack); assert(!currentScreen().showPlayerControls);
  enterEmptyLobby(); resetTouchControls();
}

// The Add player row whose label starts with `name`, paging with More.
static bool tapSeatRow(const char *name) {
  for (uint8_t page=0;page<20;++page) {
    const AtlasScreen screen=currentScreen();
    assert(screen.kind==ScreenKind::Seat);
    for (uint8_t i=0;i<screen.buttonCount;++i) {
      const TouchButton &b=screen.buttons[i];
      if (b.action==TouchAction::SeatProfile && startsWith(b.label,name)) { tapKey(TouchAction::SeatProfile,b.arg); return true; }
    }
    if (!screenButton(screen,TouchAction::SeatMore)) return false;
    tapButton(TouchAction::SeatMore);
  }
  return false;
}
static void typeName(const char *text) {
  static const char keys[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ-' ";
  for (const char *c=text;*c;++c) {
    const char upper=(*c>='a'&&*c<='z')?static_cast<char>(*c-'a'+'A'):*c;
    tapKey(TouchAction::NameKey,static_cast<uint8_t>(strchr(keys,upper)-keys));
  }
}
static bool keysFit(const AtlasScreen &s) {
  for (uint8_t i=0;i<s.buttonCount;++i) {
    const TouchButton &b=s.buttons[i];
    if (b.w<44 || b.h<44 || b.x<0 || b.y<SCREEN_BODY_Y || b.x+b.w>ATLAS_SCREEN_WIDTH || b.y+b.h>ATLAS_SCREEN_HEIGHT) return false;
    for (uint8_t j=0;j<i;++j) {
      const TouchButton &o=s.buttons[j];
      if (b.x<o.x+o.w && o.x<b.x+b.w && b.y<o.y+o.h && o.y<b.y+b.h) return false;
    }
  }
  return true;
}

// Add player on the Atlas screen, as the app's shared table seats people:
// a saved profile that allows it sits at a tap, one that does not asks for
// its PIN on a keypad, and New player types a name for a PIN-less profile.
// Each seat is a JoinProfile Intent from AtlasHardware.
static void touchSeatsPlayers() {
  resetTouchControls(); resetPresence(); enterEmptyLobby(); TurnHub::fixtureRadio=false; testNow+=60000;
  const String open=TurnHubProfiles::createProfileWithName("Zed Touch");
  String lockedId;
  const String locked=registerPhone("Pia Touch",lockedId);
  ProfileFixture::profiles[lockedId].policy.allowPhysicalWithoutPin=false;
  assert(request("/api/session/logout",locked)==200);

  tapButton(TouchAction::OpenSeat);
  assert(currentScreen().kind==ScreenKind::Seat && keysFit(currentScreen()));
  assert(tapSeatRow("Zed Touch") && table().lobby.playerCount()==1 && tableForProfile(open)==0);
  assert(currentScreen().kind==ScreenKind::Seat && !tapSeatRow("Zed Touch"));

  // A PIN-protected profile: the keypad, a wrong PIN refused, then its own.
  assert(tapSeatRow("Pia Touch (PIN)") && currentScreen().kind==ScreenKind::Pin && keysFit(currentScreen()));
  for (uint8_t d : {9,9,9,9}) tapKey(TouchAction::PinKey,d);
  assert(String(currentScreen().detail)=="* * * *");
  tapButton(TouchAction::PinDone);
  assert(table().lobby.playerCount()==1 && String(currentScreen().notice)=="That PIN was not accepted");
  for (uint8_t d : {1,2,3,5}) tapKey(TouchAction::PinKey,d);
  tapButton(TouchAction::PinBack); tapKey(TouchAction::PinKey,4);
  tapButton(TouchAction::PinDone);
  assert(table().lobby.playerCount()==2 && tableForProfile(lockedId)==0 && currentScreen().kind==ScreenKind::Seat);

  // New player: words start with capitals; the profile has no PIN.
  tapButton(TouchAction::NewPlayer);
  assert(currentScreen().kind==ScreenKind::Name && keysFit(currentScreen()));
  tapButton(TouchAction::NameDone);
  assert(table().lobby.playerCount()==2 && String(currentScreen().notice)=="Type the player's name");
  typeName("  jo  bx"); tapButton(TouchAction::NameBack); typeName("y");
  assert(String(currentScreen().title)=="Jo By");
  tapButton(TouchAction::NameDone);
  assert(table().lobby.playerCount()==3 && currentScreen().kind==ScreenKind::Seat);
  // A name already used is refused; pick that profile instead.
  tapButton(TouchAction::NewPlayer); typeName("jo by"); tapButton(TouchAction::NameDone);
  assert(table().lobby.playerCount()==3 && String(currentScreen().notice)=="That name has a profile; pick it");
  tapButton(TouchAction::CloseScreen); assert(currentScreen().kind==ScreenKind::Seat);
  tapButton(TouchAction::CloseScreen); assert(currentScreen().kind==ScreenKind::Status);

  // Add player belongs to the lobby: a started game closes it.
  tapButton(TouchAction::OpenSeat);
  Intent start; start.type=IntentType::StartGame; start.actor.origin=IntentOrigin::AtlasHardware;
  assert(intents.dispatch(start).accepted());
  assert(currentScreen().kind==ScreenKind::Status);
  resetTouchControls(); enterEmptyLobby();
  // Later scenarios need room in the profile store.
  for (auto it=ProfileFixture::profiles.begin();it!=ProfileFixture::profiles.end();) {
    if (it->first==open.c_str() || it->first==lockedId.c_str() || it->second.name=="Jo By") it=ProfileFixture::profiles.erase(it);
    else ++it;
  }
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
  const uint8_t first=table().game.activePlayerNumber();
  // Master pass asks first; Confirm passes once, immediately, and returns to
  // the status screen.
  tapButton(TouchAction::OpenTable);
  char detail[48]; snprintf(detail,sizeof(detail),"Stuck turn? Master pass skips Player %u",static_cast<unsigned>(first));
  assert(String(currentScreen().detail)==detail);
  tapButton(TouchAction::MasterPass);
  { char title[32]; snprintf(title,sizeof(title),"Pass for Player %u?",static_cast<unsigned>(first));
    assert(table().game.activePlayerNumber()==first && String(currentScreen().title)==title); }
  const size_t activityBefore=TurnHub::Diagnostics::activityLog().count;
  tapButton(TouchAction::ConfirmYes);
  const uint8_t second=table().game.activePlayerNumber();
  assert(second!=first && !table().pendingPass.active && table().hubState==HubState::Running);
  assert(currentScreen().kind==ScreenKind::Status && String(currentScreen().notice)=="Master pass: turn passed");
  assert(table().game.activePlayerNumber()==second);
  { const TurnHub::Diagnostics::ActivityLog &log=TurnHub::Diagnostics::activityLog();
    assert(log.count==activityBefore+1 || log.count==TurnHub::Diagnostics::ACTIVITY_CAPACITY);
    const TurnHub::Diagnostics::ActivityEvent &last=log.entries[(log.next+TurnHub::Diagnostics::ACTIVITY_CAPACITY-1)%TurnHub::Diagnostics::ACTIVITY_CAPACITY];
    assert(String(last.kind)=="master_pass" && startsWith(last.message,"player=")); }
  // It overrides a queued PASS: one turn passes, not two.
  seatPass(); assert(table().pendingPass.active);
  assert(intents.dispatch(master).accepted());
  const uint8_t third=table().game.activePlayerNumber();
  assert(third!=second && !table().pendingPass.active);
  testNow+=PASS_GRACE_MS; updatePendingPass(testNow); assert(table().game.activePlayerNumber()==third);
  // Not while paused, nor over a win claim.
  assert(web(0,1,WebControl::PauseResume) && table().hubState==HubState::Paused);
  assert(intents.dispatch(master).status==IntentStatus::InvalidState);
  assert(web(0,1,WebControl::PauseResume) && table().hubState==HubState::Running);
  const PlayerSeat claimant=*table().game.activePlayer();
  assert(web(claimant.controllerId,claimant.slot,WebControl::ClaimWin) && table().game.hasWinClaim());
  assert(!intents.dispatch(master).accepted() && table().game.hasWinClaim());
  // The Table screen closes with the match.
  holdEndMatch(); assert(table().hubState==HubState::GameOver && currentScreen().kind==ScreenKind::Status);
  resetTouchControls(); enterEmptyLobby();
}

static void endMatchAsDraw() {
  using TurnHubProfiles::LastGameResult;
  // Only a match in progress, and only the Atlas hardware, can end it.
  resetTouchControls(); freshLobby(2);
  Intent end; end.type=IntentType::EndMatch; end.actor.origin=IntentOrigin::AtlasHardware;
  assert(!intents.dispatch(end).accepted() && table().hubState==HubState::Lobby);
  assert(table().lobby.playerCount()==2 && !screenButton(currentScreen(),TouchAction::EndMatch));
  startFromHost();
  for (auto origin : {IntentOrigin::Browser, IntentOrigin::AndroidApp, IntentOrigin::PhysicalSigil,
                      IntentOrigin::Simulator, IntentOrigin::System}) {
    Intent other=end; other.actor.origin=origin;
    assert(intents.dispatch(other).status==IntentStatus::Unauthorized);
  }
  assert(table().hubState==HubState::Running && !table().game.gameOver());

  // Confirmed, it overrides a queued PASS and ends the match once, as a draw.
  seatPass(); assert(table().pendingPass.active);
  openTableScreen(); tapButton(TouchAction::EndMatch);
  assert(table().hubState==HubState::Running && table().pendingPass.active);
  tapButton(TouchAction::ConfirmYes);
  assert(table().hubState==HubState::GameOver && table().game.endedInDraw() && table().game.winnerPlayerNumber()==0);
  assert(!table().pendingPass.active && completedGames==1);
  assert(table().hubState==HubState::GameOver && completedGames==1 && !table().pendingPass.active);
  assert(!intents.dispatch(end).accepted() && completedGames==1);

  // A draw survives recovery validation (it used to be rejected as corrupt,
  // which would have locked the recovery store) and restores as a draw.
  TurnHub::GameCheckpoint saved; table().game.checkpoint(saved,testNow);
  assert(saved.over && saved.winner==0 && TurnHub::validCheckpoint(saved));
  GameEngine restored; assert(restored.restoreCheckpoint(saved,testNow));
  assert(restored.gameOver() && restored.endedInDraw());
  saved.over=false; saved.winner=1; assert(!TurnHub::validCheckpoint(saved));

  // It overrides an open win claim, and works from a paused (e.g. recovered) match.
  freshLobby(2); startFromHost();
  assert(web(0,1,WebControl::ClaimWin) && table().game.hasWinClaim() && table().hubState==HubState::Paused);
  holdEndMatch();
  assert(table().game.endedInDraw() && !table().game.hasWinClaim() && completedGames==1);

  // Statistics: every player gets a game played and a Draw, not a win or loss;
  // a player who conceded first keeps Conceded.
  enterEmptyLobby(); TurnHub::fixtureRadio=false; completedGames=0;
  String aId,bId,cId;
  const String a=registerPhone("Draw one",aId),b=registerPhone("Draw two",bId),c=registerPhone("Draw three",cId);
  assert(request("/api/session/join",a)==200 && request("/api/session/join",b)==200 &&
      request("/api/session/join",c)==200);
  assert(request("/api/control/start",a)==200); testNow+=3000; updateCountdown(testNow);
  assert(table().hubState==HubState::Running);
  assert(request("/api/control/concede",c)==200 && table().hubState==HubState::Running);
  assert(request("/api/control/pause",b)==200 && table().hubState==HubState::Paused);
  holdEndMatch();
  assert(table().hubState==HubState::GameOver && table().game.endedInDraw() && completedGames==1);
  for (const String *id : {&aId,&bId,&cId}) {
    const auto &stats=ProfileFixture::profiles[id->c_str()].stats;
    assert(stats.gamesPlayed==1 && stats.gamesWon==0);
  }
  assert(ProfileFixture::profiles[aId.c_str()].stats.lastGameResult==LastGameResult::Draw);
  assert(ProfileFixture::profiles[bId.c_str()].stats.lastGameResult==LastGameResult::Draw);
  assert(ProfileFixture::profiles[cId.c_str()].stats.lastGameResult==LastGameResult::Conceded);
  assert(String(TurnHubProfileStats::resultName(LastGameResult::Draw))=="Draw");
  assert(request("/api/v1/state",a,{},HTTP_GET)==200);
  assert(server.body.find("\"state\":\"GAME_OVER\"")!=std::string::npos &&
      server.body.find("\"winnerPlayer\":null")!=std::string::npos);
  assert(request("/api/control/rematch",a)==200 && table().hubState==HubState::Lobby);
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
  assert(helloCapabilities(encodeHelloInfo(0,9,0,0x10)) == 0x10);
  assert(helloCapabilities(encodeHelloInfo(0,9,0,0)) == 0);
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
  assert(screenButton(screen,TouchAction::SkipSetup) && screenButton(screen,TouchAction::OpenSettings));
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
  tapButton(TouchAction::OpenGame);

  // Sigils paired during the phone's steps: nothing is left for "You're all
  // set" to say, so finishing goes straight to Complete.
  TurnHub::fixtureRadio=true;
  TurnHub::SigilRecord savedRecords[MAX_PHYSICAL_SIGILS]; memcpy(savedRecords,TurnHub::fixtureRecords,sizeof(savedRecords));
  bool savedForgotten[MAX_PHYSICAL_SIGILS]; memcpy(savedForgotten,TurnHub::fixtureForgotten,sizeof(savedForgotten));
  for (bool &forgotten : TurnHub::fixtureForgotten) forgotten=true;
  setupStage=SetupStage::Welcome; Preferences::strings().erase(AtlasConfig::WIFI_PREF_KEY);
  assert(request("/api/setup/finish",owner,{{"password","table-pass-3"}})==200);
  assert(setupStage==SetupStage::Finished && TurnHub::fixtureSetupStageSaved==1);
  TurnHub::fixtureForgotten[1]=false;
  setupStage=SetupStage::Welcome; Preferences::strings().erase(AtlasConfig::WIFI_PREF_KEY);
  assert(request("/api/setup/finish",owner,{{"password","table-pass-3"},{"uploadApprovalRequired","invalid"}})==400);
  assert(uploadApprovalRequired);
  assert(request("/api/setup/finish",owner,{{"password","table-pass-3"},{"uploadApprovalRequired","0"}})==200);
  assert(!uploadApprovalRequired);
  assert(server.body.find("\"restarting\":true")!=std::string::npos);
  assert(setupStage==SetupStage::Complete && TurnHub::fixtureSetupStageSaved==2);
  assert(request("/api/upload-approval",owner,{{"required","1"}})==200);
  assert(currentScreen().kind==ScreenKind::Status);
  memcpy(TurnHub::fixtureRecords,savedRecords,sizeof(savedRecords)); memcpy(TurnHub::fixtureForgotten,savedForgotten,sizeof(savedForgotten)); TurnHub::fixtureRadio=previousRadio;

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
  const PlayerSeat *active=table().game.activePlayer(); assert(active);
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
  assert(table().hubState==HubState::Running); playQueuedAudio();
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

// Tablet mode: one shared screen seats every player (new PIN-less profiles or
// existing ones, by their PIN-use choice) and acts for each seat as its own
// controller would. A player's table presence code turns it on, and grants
// nothing an Admin needs.
static void tabletMode() {
  TurnHubWebApi::configure(resolveWebSeat,handleWebControl,handleProfileControl,resolveProfileParticipant);
  TurnHubWebApi::configureGameControls(readGameSettings,configureGame,changeLife);
  TurnHubWebApi::configureCounterControls(readCounters,changeCounter);
  TurnHubWebApi::configureDevices(manageDevices, []() { return pairingWindowMs; });
  TurnHubWebApi::configurePresence(presenceHooks());
  resetPresence(); enterEmptyLobby(); TurnHub::fixtureRadio=false; testNow=1000;
  String ownerId,lockedId;
  String tablet=registerPhone("Tablet owner",ownerId);
  const String locked=registerPhone("Locked Lee",lockedId);
  ProfileFixture::profiles[lockedId].policy.allowPhysicalWithoutPin=false;
  assert(request("/api/session/logout",locked)==200);

  // Off until a presence code from the Atlas screen; a player may ask for one
  // only for tablet mode, and it grants no Admin action.
  assert(request("/api/tablet/seat",tablet,{{"name","Ava"}})==403);
  assert(server.body.find("tabletRequired")!=std::string::npos);
  assert(request("/api/tablet/enable","")==401);
  assert(request("/api/tablet/enable",tablet)==403 && server.body.find("presenceRequired")!=std::string::npos);
  assert(request("/api/presence/request",tablet)==403);
  assert(request("/api/presence/request",tablet,{{"purpose","tablet"}})==200);
  const PresenceRequest *shown=pendingPresenceCode(testNow); assert(shown!=nullptr);
  char digits[8]; snprintf(digits,sizeof(digits),"%06lu",static_cast<unsigned long>(shown->code));
  assert(request("/api/presence/confirm",tablet,{{"code",digits}})==200);
  assert(request("/api/table/reset",tablet)==403);
  const String personalToken = tablet;
  assert(request("/api/tablet/enable",tablet)==200);
  tablet = responseField("token");
  assert(tablet.length() == 32 && tablet != personalToken);
  assert(request("/api/session/me",personalToken,{},HTTP_GET)==401);
  assert(request("/api/session/personalization",tablet,{},HTTP_GET)==403);
  assert(request("/api/session/me",tablet,{},HTTP_GET)==200 && server.body.find("\"tablet\":true")!=std::string::npos);
  assert(server.body.find("\"participating\":false")!=std::string::npos);

  // The Tablet access role skips the code, e.g. for a tablet account that
  // never joins the table; without the role the code is needed again.
  {
    String roleId; String roleTablet=registerPhone("Table tablet",roleId);
    TurnHubAccounts::Account account; account.permissions=TurnHubAccounts::TabletAccess;
    assert(TurnHubAccounts::save(roleId,account));
    assert(request("/api/tablet/enable",roleTablet)==200);
    roleTablet = responseField("token");
    assert(request("/api/session/me",roleTablet,{},HTTP_GET)==200 && server.body.find("\"tablet\":true")!=std::string::npos);
    assert(server.body.find("\"participating\":false")!=std::string::npos);
    assert(request("/api/table/reset",roleTablet)==403);
    assert(request("/api/tablet/disable",roleTablet)==200);
    account.permissions=0; assert(TurnHubAccounts::save(roleId,account));
    assert(request("/api/session/me",roleTablet,{},HTTP_GET)==401);
    assert(request("/api/session/login","",{{"profileId",roleId},{"pin","1234"}})==200);
    roleTablet = responseField("token");
    assert(request("/api/tablet/enable",roleTablet)==403 && server.body.find("presenceRequired")!=std::string::npos);
    assert(request("/api/session/logout",roleTablet)==200);
    ProfileFixture::profiles.erase(roleId.c_str()); TurnHubAccounts::accounts.erase(roleId.c_str());
  }

  // New names become PIN-less profiles at the table; names stay unique.
  assert(request("/api/tablet/seat",tablet,{{"name","  "}})==400);
  assert(request("/api/tablet/seat",tablet,{{"name","Ava"}})==200);
  const String avaId=responseField("profileId");
  assert(ProfileFixture::profiles[avaId].name=="Ava" && !TurnHubProfiles::hasPinForProfile(avaId));
  assert(request("/api/tablet/seat",tablet,{{"name","ava"}})==409 && table().lobby.playerCount()==1);
  assert(request("/api/tablet/seat",tablet,{{"name","Ben"}})==200 && table().lobby.playerCount()==2);
  const String benId=responseField("profileId");
  // An existing profile: its own PIN unless it allows use without one.
  assert(request("/api/tablet/seat",tablet,{{"profileId","FFFFFFFF"}})==404);
  assert(request("/api/tablet/seat",tablet,{{"profileId",lockedId}})==403);
  assert(server.body.find("pinRequired")!=std::string::npos && table().lobby.playerCount()==2);
  assert(request("/api/tablet/seat",tablet,{{"profileId",lockedId},{"pin","9999"}})==401);
  assert(request("/api/tablet/seat",tablet,{{"profileId",lockedId},{"pin","1234"}})==200 && table().lobby.playerCount()==3);
  assert(request("/api/tablet/unseat",tablet,{{"profileId",lockedId}})==200 && table().lobby.playerCount()==2);
  assert(request("/api/tablet/seat",tablet,{{"profileId",ownerId}})==200 && table().lobby.playerCount()==3);
  assert(request("/api/tablet/unseat",tablet,{{"profileId",ownerId}})==200 && table().lobby.playerCount()==2);

  // Settings and Start for the table, through any seat.
  PlayerSeat seats[MAX_PLAYERS]; assert(table().lobby.buildPlayers(seats,MAX_PLAYERS)==2);
  const String m1=String(seats[0].controllerId), m2=String(seats[1].controllerId);
  // Turn order, from the tablet as from the Atlas screen (lobby only); a phone may not.
  const uint8_t c1=seats[0].controllerId, c2=seats[1].controllerId;
  assert(request("/api/tablet/control",tablet,{{"module",m1},{"slot","1"},{"action","move-earlier"}})==409);
  assert(request("/api/tablet/control",tablet,{{"module",m2},{"slot","1"},{"action","move-earlier"}})==200);
  assert(table().lobby.buildPlayers(seats,MAX_PLAYERS)==2 && seats[0].controllerId==c2 && seats[1].controllerId==c1);
  assert(request("/api/tablet/control",tablet,{{"module",m2},{"slot","1"},{"action","move-later"}})==200);
  assert(table().lobby.buildPlayers(seats,MAX_PLAYERS)==2 && seats[0].controllerId==c1);
  { Intent fromPhone; fromPhone.type=IntentType::MoveSeat; fromPhone.actor.origin=IntentOrigin::Browser;
    fromPhone.payload.targetPlayer=2; fromPhone.payload.value=-1;
    assert(!intents.dispatch(fromPhone).accepted() && table().lobby.buildPlayers(seats,MAX_PLAYERS)==2 && seats[0].controllerId==c1); }
  assert(request("/api/tablet/settings",tablet,{{"module",m1},{"slot","1"},{"gameProfile","mtg_commander"},{"startingLife","40"}})==200);
  assert(request("/api/tablet/control",tablet,{{"module","99"},{"slot","1"},{"action","start"}})==400);
  assert(request("/api/tablet/control",tablet,{{"module",m1},{"slot","1"},{"action","fly"}})==400);
  assert(request("/api/tablet/control",tablet,{{"module",m1},{"slot","1"},{"action","start"}})==200);
  testNow+=3000; updateCountdown(testNow); assert(table().hubState==HubState::Running);
  assert(request("/api/tablet/control",tablet,{{"module",m1},{"slot","1"},{"action","move-later"}})==409);
  assert(request("/api/tablet/control",tablet,{{"module",m1},{"slot","1"},{"action","monarch"}})==200 && table().game.monarchPlayerNumber()==1);
  assert(request("/api/tablet/control",tablet,{{"module",m2},{"slot","1"},{"action","monarch"}})==200 && table().game.monarchPlayerNumber()==2);
  assert(request("/api/tablet/counter",tablet,{{"module",m1},{"slot","1"},{"counter","poison"},{"delta","2"}})==200 && table().game.countersForPlayer(1)->poison==2);
  assert(request("/api/tablet/counter",tablet,{{"module",m2},{"slot","1"},{"counter","initiative"},{"value","1"}})==200 && table().game.initiativePlayerNumber()==2);
  assert(request("/api/tablet/counter",tablet,{{"module",m1},{"slot","1"},{"counter","commander-casts"},{"commander","2"},{"delta","1"}})==200 && table().game.countersForPlayer(1)->commanderCasts[1]==1);
  assert(String(table().game.playerAt(0)->profileId)==avaId && String(table().game.playerAt(1)->profileId)==benId);

  // Each panel changes its own seat's life and records damage it received.
  assert(request("/api/tablet/life",tablet,{{"module",m2},{"slot","1"},{"delta","-3"}})==200);
  assert(table().game.lifeTotal(2)==37 && table().game.lifeTotal(1)==40);
  assert(request("/api/tablet/life",tablet,{{"module",m2},{"slot","1"},{"delta","0"}})==400);
  assert(request("/api/tablet/commander",tablet,{{"module",m2},{"slot","1"},{"source","1"},{"commander","1"},{"delta","5"}})==200);
  assert(table().game.commanderDamage(2,1,1)==5 && table().game.lifeTotal(2)==32);
  // The active panel passes; a pass from the other seat is refused.
  const String active=String(table().game.activePlayer()->controllerId);
  const String waiting=active==m1?m2:m1;
  assert(request("/api/tablet/control",tablet,{{"module",waiting},{"slot","1"},{"action","pass"}})==409);
  assert(request("/api/tablet/control",tablet,{{"module",active},{"slot","1"},{"action","pass"}})==200 && table().pendingPass.active);
  testNow+=PASS_GRACE_MS+1; updatePendingPass(testNow);
  assert(String(table().game.activePlayer()->controllerId)==waiting);
  // A win claim (only the active player's) is confirmed on the other panel.
  assert(request("/api/tablet/control",tablet,{{"module",waiting},{"slot","1"},{"action","win"}})==200);
  assert(request("/api/tablet/control",tablet,{{"module",active},{"slot","1"},{"action","confirm"}})==200);
  assert(table().hubState==HubState::GameOver);
  const String winnerId=waiting==m1?avaId:benId;
  assert(ProfileFixture::profiles[winnerId].stats.gamesPlayed==1 && ProfileFixture::profiles[winnerId].stats.gamesWon==1);

  // Shared-table credit uses the same authenticated intent as the player's phone.
  assert(request("/api/tablet/control",tablet,{{"module",m1},{"slot","1"},{"action","rematch"}})==200);
  assert(request("/api/tablet/control",tablet,{{"module",m1},{"slot","1"},{"action","start"}})==200);
  testNow+=3000; updateCountdown(testNow);
  assert(request("/api/tablet/control",tablet,{{"module",m1},{"slot","1"},{"action","eliminate"},{"eliminatedBy","1"}})==409);
  assert(request("/api/tablet/control",tablet,{{"module",m1},{"slot","1"},{"action","eliminate"},{"eliminatedBy","2"}})==200);
  assert(table().game.eliminatedBy(1)==2 && table().game.eliminationsBy(2)==1);
  assert(ProfileFixture::profiles[benId].stats.lastGameEliminationsMade==1);

  // Leaving tablet mode revokes the shared credential.
  assert(request("/api/tablet/disable",tablet)==200);
  assert(request("/api/tablet/control",tablet,{{"module",m1},{"slot","1"},{"action","rematch"}})==401);
  assert(request("/api/session/me",tablet,{},HTTP_GET)==401);
  assert(request("/api/session/logout",tablet)==200);
  // Free the fixture's profile capacity for later scenarios.
  for (const String &id:{ownerId,lockedId,avaId,benId}) ProfileFixture::profiles.erase(id.c_str());
  resetPresence(); enterEmptyLobby();
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
  assert(request("/api/table/reset",player)==403 && table().hubState==HubState::Running);
  assert(request("/api/table/reset",admin)==403 && table().hubState==HubState::Running);
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
  assert(request("/api/table/reset",player)==403 && table().hubState==HubState::Running);
  Intent forged; forged.type=IntentType::ResetTable; forged.actor.origin=IntentOrigin::Browser;
  strncpy(forged.payload.moderatorId,playerId.c_str(),8);
  assert(intents.dispatch(forged).status==IntentStatus::Unauthorized && table().hubState==HubState::Running);

  // A paused match ends as a draw once, and the table empties.
  assert(web(0,1,WebControl::PauseResume) && table().hubState==HubState::Paused);
  assert(request("/api/table/reset",admin)==200);
  assert(server.body.find("ended as a draw")!=std::string::npos);
  assert(table().hubState==HubState::Lobby && table().lobby.playerCount()==0 && !table().game.hasPlayers() && completedGames==1);

  // From a lobby (or a countdown) it just empties the table.
  choose(0,SigilAction::Join); choose(1,SigilAction::Join); completedGames=0; assert(table().lobby.playerCount()==2);
  choose(0,SigilAction::StartGame); assert(table().hubState==HubState::Starting);
  assert(request("/api/table/reset",admin)==200 && table().hubState==HubState::Lobby && table().lobby.playerCount()==0);
  assert(completedGames==0);

  // Verification lapses after PRESENCE_GRANT_MS, even for the Admin.
  choose(0,SigilAction::Join); choose(1,SigilAction::Join); testNow+=PRESENCE_GRANT_MS; updatePairingWindow(testNow);
  assert(request("/api/table/reset",admin)==403 && table().lobby.playerCount()==2);
  strncpy(forged.payload.moderatorId,adminId.c_str(),8);
  assert(intents.dispatch(forged).status==IntentStatus::Unauthorized && table().lobby.playerCount()==2);
  enterEmptyLobby();
}

// The table code is optional (off by default, 2026-10-08): while off, a
// signed-in Admin counts as at the table with no code. An Admin turns it on
// freely; turning it off again takes a code that Admin actually entered.
static void tableCodeOptional() {
  TurnHubWebApi::configureDevices(manageDevices, []() { return pairingWindowMs; });
  TurnHubWebApi::configurePresence(presenceHooks());
  resetPresence(); resetTouchControls();
  tableCodeRequired=false;
  freshLobby(2);
  String adminId,playerId;
  const String admin=registerPhone("Code admin",adminId),player=registerPhone("Code player",playerId);
  TurnHubAccounts::Account account; account.permissions=TurnHubAccounts::Admin;
  assert(TurnHubAccounts::save(adminId,account));

  // Off: no code asked, but Admin still is.
  assert(request("/api/presence",admin,{},HTTP_GET)==200);
  assert(server.body.find("\"verified\":true")!=std::string::npos && server.body.find("\"codeRequired\":false")!=std::string::npos);
  assert(request("/api/table-code",player,{},HTTP_GET)==403 && request("/api/table-code",player,{{"required","1"}})==403);
  assert(request("/api/table-code",admin,{},HTTP_GET)==200 && server.body.find("\"required\":false")!=std::string::npos);
  assert(request("/api/table/reset",player)==403);
  choose(0,SigilAction::Join); choose(1,SigilAction::Join);
  assert(request("/api/table/reset",admin)==200 && table().lobby.playerCount()==0 && pendingPresenceCode(testNow)==nullptr);
  assert(request("/api/table-code",admin,{{"required","2"}})==400 && !tableCodeRequired);

  // On: protected actions need a code again.
  assert(request("/api/table-code",admin,{{"required","1"}})==200 && tableCodeRequired);
  choose(0,SigilAction::Join); choose(1,SigilAction::Join);
  assert(request("/api/table/reset",admin)==403 && server.body.find("presenceRequired")!=std::string::npos);
  assert(table().lobby.playerCount()==2);
  // Off again needs that code: a forged Intent or a request without one fails.
  Intent forged; forged.type=IntentType::ConfigureTableCode; forged.actor.origin=IntentOrigin::Browser;
  strncpy(forged.payload.moderatorId,adminId.c_str(),8); forged.payload.value=0;
  assert(intents.dispatch(forged).status==IntentStatus::Unauthorized && tableCodeRequired);
  assert(request("/api/table-code",admin,{{"required","0"}})==403 && server.body.find("presenceRequired")!=std::string::npos);
  assert(tableCodeRequired);
  verifyAtTable(admin);
  assert(request("/api/table-code",admin,{{"required","0"}})==200 && !tableCodeRequired);
  assert(request("/api/table/reset",admin)==200 && table().lobby.playerCount()==0);

  // The rest of this suite exercises the code path.
  tableCodeRequired=true;
  for (const String &id:{adminId,playerId}) {
    ProfileFixture::profiles.erase(id.c_str()); TurnHubAccounts::accounts.erase(id.c_str());
  }
  resetPresence(); enterEmptyLobby();
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

// Settings > Device on the touchscreen (owner 2026-10-02): the BOOT button's
// Unpair and Factory reset, confirmed in the popup, between games only.
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
  assert(unpair->confirm && reset->confirm);
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.y>=SCREEN_BODY_Y && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);

  // A tap only asks; Cancel does nothing.
  tapButton(TouchAction::UnpairSigils);
  assert(sigilBus.record(0) && sigilBus.record(3) && String(currentScreen().title)=="Unpair every Sigil?");
  tapButton(TouchAction::ConfirmNo); assert(sigilBus.record(0) && currentScreen().kind==ScreenKind::Device);
  // Confirmed: the handler still keeps a Sigil with seated players.
  tapConfirmed(TouchAction::UnpairSigils);
  assert(sigilBus.record(0) && sigilBus.record(3) && TurnHub::fixtureUnpairs[3]==0);
  // With everyone out of the lobby, every Sigil is forgotten.
  enterEmptyLobby(); resetTouchControls(); openMenuScreen(); tapButton(TouchAction::OpenDevice);
  tapConfirmed(TouchAction::UnpairSigils);
  assert(!sigilBus.record(0) && !sigilBus.record(3) && TurnHub::fixtureUnpairs[3]==1);
  assert(String(currentScreen().notice)=="All Sigils forgotten" && !factoryResetScheduled());
  // Back returns to Settings.
  tapButton(TouchAction::CloseScreen); assert(currentScreen().kind==ScreenKind::Menu);

  // After a game: no Unpair (lobby only), but Factory reset, confirmed.
  freshLobby(2); startFromHost(); holdEndMatch();
  assert(table().hubState==HubState::GameOver);
  openMenuScreen(); tapButton(TouchAction::OpenDevice); s=currentScreen();
  assert(s.kind==ScreenKind::Device && !screenButton(s,TouchAction::UnpairSigils) &&
      screenButton(s,TouchAction::FactoryResetAtlas));
  tapButton(TouchAction::FactoryResetAtlas);
  assert(!factoryResetScheduled() && String(currentScreen().title)=="Erase this Atlas?");
  tapButton(TouchAction::ConfirmYes);
  assert(factoryResetScheduled());
  serviceFactoryReset(millis()+2000);
  assert(fixtureFactoryResets==1 && !factoryResetScheduled());
  // Device settings stays open during play, without reset/unpair/sleep actions.
  enterEmptyLobby(); freshLobby(2); resetTouchControls(); openMenuScreen(); tapButton(TouchAction::OpenDevice);
  startFromHost(); assert(currentScreen().kind==ScreenKind::Device);
  assert(!screenButton(currentScreen(),TouchAction::FactoryResetAtlas) &&
      !screenButton(currentScreen(),TouchAction::UnpairSigils) && !screenButton(currentScreen(),TouchAction::SleepAtlas));
  enterEmptyLobby(); resetTouchControls();
  TurnHub::fixtureUnpairs[3]=0;
}

// Menu > Device Sleep (owner 2026-10-02): a tap, between games, from the
// touchscreen only; Atlas sleeps once the notice has been shown.
extern unsigned fixtureSleeps;
static void touchThemeChoices() {
  freshLobby(2); resetTouchControls(); openMenuScreen();
  tapButton(TouchAction::OpenThemes);
  const TouchAction choices[] = {TouchAction::ThemeGraphite, TouchAction::ThemeDaylight, TouchAction::ThemeBrass, TouchAction::ThemeContrast};
  for (uint8_t i = 0; i < 4; ++i) {
    const auto before = table().hubState;
    tapButton(choices[i]);
    const auto screen = currentScreen();
    assert(screen.kind == ScreenKind::Themes && table().hubState == before);
    assert(static_cast<uint8_t>(atlasDisplayTheme()) == i);
    assert(screenButton(screen, choices[i])->selected);
    for (uint8_t b = 0; b < screen.buttonCount; ++b) assert(screen.buttons[b].w >= 44 && screen.buttons[b].h >= 44);
  }
  const auto before = table().hubState;
  tapButton(TouchAction::InvertDisplay);
  assert(atlasDisplayInverted() && screenButton(currentScreen(), TouchAction::InvertDisplay)->selected);
  assert(table().hubState == before);
  tapButton(TouchAction::ThemeDaylight);
  assert(atlasDisplayInverted()); // Theme changes retain independent inversion.
  tapButton(TouchAction::InvertDisplay);
  assert(!atlasDisplayInverted() && !screenButton(currentScreen(), TouchAction::InvertDisplay)->selected);
  tapButton(TouchAction::CloseScreen); assert(currentScreen().kind == ScreenKind::Menu);
  assert(chooseAtlasDisplayTheme(TurnHubTheme::Id::Graphite));
}

static void touchDeviceSleep() {
  resetPresence(); resetTouchControls();
  freshLobby(2); fixtureSleeps=0; fixtureFactoryResets=0;
  openMenuScreen(); tapButton(TouchAction::OpenDevice);
  AtlasScreen s=currentScreen();
  const TouchButton *sleep=screenButton(s,TouchAction::SleepAtlas);
  assert(sleep && !sleep->confirm && screenButton(s,TouchAction::CloseScreen) && s.buttonCount<=MAX_TOUCH_BUTTONS);
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
  startFromHost(); holdEndMatch(); assert(table().hubState==HubState::GameOver);
  openMenuScreen(); tapButton(TouchAction::OpenDevice);
  assert(screenButton(currentScreen(),TouchAction::SleepAtlas));
  enterEmptyLobby(); freshLobby(2); startFromHost();
  Intent touch; touch.type=IntentType::Sleep; touch.actor.origin=IntentOrigin::AtlasHardware;
  assert(intents.dispatch(touch).status==IntentStatus::InvalidState && !sleepScheduled());
  enterEmptyLobby(); resetTouchControls();
  assert(fixtureFactoryResets==0);
}

// Auto sleep on the cell (owner 2026-10-08): idle between games, or an empty
// charge at any time. On USB Atlas never sleeps by itself.
extern bool fixtureOnBattery;
extern bool fixtureSleptEmpty;
static void autoSleepOnBattery() {
  resetPresence(); resetTouchControls();
  freshLobby(2); fixtureSleeps=0; fixtureOnBattery=false;
  fixtureBattery=TurnHub::BatteryReading(); fixtureBattery.present=true; fixtureBattery.percent=50;
  const uint32_t idle=AtlasConfig::BATTERY_IDLE_SLEEP_MS;
  serviceAutoSleep(testNow); testNow+=idle*2; serviceAutoSleep(testNow);
  assert(!sleepScheduled());
  // Unplugged: the idle count starts now, and any player action restarts it.
  fixtureOnBattery=true; serviceAutoSleep(testNow);
  testNow+=idle-60000; serviceAutoSleep(testNow); assert(!sleepScheduled());
  Intent web; web.type=IntentType::Sleep; web.actor.origin=IntentOrigin::Browser;
  assert(intents.dispatch(web).status==IntentStatus::Unauthorized);
  testNow+=idle-60000; serviceAutoSleep(testNow); assert(!sleepScheduled());
  testNow+=60000; serviceAutoSleep(testNow); assert(sleepScheduled());
  serviceSleep(testNow+3000); assert(fixtureSleeps==1 && !fixtureSleptEmpty && !sleepScheduled());
  // Mid-game, idle never sleeps it.
  enterEmptyLobby(); freshLobby(2); startFromHost();
  testNow+=idle*2; serviceAutoSleep(testNow); assert(!sleepScheduled());
  // Voltage protection works while cosmetic charge remains high. Inferred
  // USB alone does not request critical-low sleep without emergency evidence.
  fixtureBattery.percent=80; fixtureBattery.low=true; fixtureBattery.charging=true;
  fixtureBattery.criticalLow=true;
  fixtureOnBattery=false;
  serviceAutoSleep(testNow); testNow+=AtlasConfig::BATTERY_EMPTY_CONFIRM_MS*2; serviceAutoSleep(testNow);
  assert(!sleepScheduled());
  fixtureOnBattery=true; fixtureBattery.charging=false;
  serviceAutoSleep(testNow); testNow+=AtlasConfig::BATTERY_EMPTY_CONFIRM_MS/2; serviceAutoSleep(testNow);
  assert(!sleepScheduled());
  testNow+=AtlasConfig::BATTERY_EMPTY_CONFIRM_MS/2; serviceAutoSleep(testNow);
  assert(sleepScheduled() && table().hubState==HubState::Running);
  serviceSleep(testNow+3000); assert(fixtureSleeps==2 && fixtureSleptEmpty);
  // Severe voltage protects even a cold boot still assuming USB. High
  // cosmetic percent and an isolated critical indication cannot prevent sleep.
  fixtureOnBattery=false; serviceAutoSleep(testNow); // cold boot clears old confirmations
  fixtureBattery.percent=90; fixtureBattery.emergencyLow=true;
  serviceAutoSleep(testNow);
  testNow+=AtlasConfig::BATTERY_EMERGENCY_CONFIRM_MS-1; serviceAutoSleep(testNow);
  assert(!sleepScheduled());
  ++testNow; serviceAutoSleep(testNow); assert(sleepScheduled());
  serviceSleep(testNow+3000); assert(fixtureSleeps==3 && fixtureSleptEmpty);
  fixtureOnBattery=false; fixtureBattery=TurnHub::BatteryReading();
  enterEmptyLobby(); resetTouchControls();
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
  assert(TurnHub::beginGameRecovery(table().game, table().lobby, testNow) == Status::NotFound);
  assert(!table().game.hasPlayers());

  // 2) Start a match and accept a semantic transition (a committed pass).
  // main.cpp's observer persists a checkpoint after every dispatched intent;
  // no code here calls checkpointGame() directly.
  startFromHost();
  choose(0,SigilAction::Pass);
  testNow += PASS_GRACE_MS; updatePendingPass(testNow);
  assert(!table().pendingPass.active && table().game.activePlayerNumber() == 2);
  assert(testBlobs.count("checkpoint") == 1);
  testNow += 45000; // 45s of real play before "power loss".
  choose(1,SigilAction::Pass);
  testNow += PASS_GRACE_MS; updatePendingPass(testNow);
  const uint32_t elapsedBeforeLoss = table().game.gameElapsedMs(testNow);
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

// Two-Headed Giant (owner request 2026-10-06): teams of neighbours share one
// life total, one turn and one elimination.
static void twoHeadedGiantEngine() {
  using namespace TurnHub;
  GameEngine engine;
  PlayerSeat seats[6] = {{1,0,1},{2,1,1},{3,2,1},{4,3,1},{5,4,1},{6,5,1}};
  GameSettings settings;
  settings.profile = GameProfile::Magic;
  settings.startingLife = TEAM_LIFE_MAGIC;
  settings.twoHeadedGiant = true;
  settings.poisonLimit = 15;
  assert(!engine.start(seats,3,seats[0],1000,settings));  // Odd table.
  assert(!engine.start(seats,2,seats[0],1000,settings));  // One team.
  GameSettings generic = settings; generic.profile = GameProfile::Generic;
  assert(!validGameSettings(generic) && !engine.start(seats,4,seats[0],1000,generic));
  assert(engine.start(seats,4,seats[0],1000,settings));
  assert(engine.twoHeadedGiant());
  assert(engine.teamOf(1)==1 && engine.teamOf(2)==1 && engine.teamOf(3)==2 && engine.teamOf(4)==2);
  assert(engine.teammateOf(1)==2 && engine.teammateOf(4)==3 && engine.sameTeam(3,4) && !engine.sameTeam(2,3));
  assert(engine.hasTurn(1) && engine.hasTurn(2) && !engine.hasTurn(3));
  assert(engine.turnSeatForController(1)->playerNumber==2 && !engine.turnSeatForController(2));
  // Either teammate passes the team's turn; it goes to the next team.
  assert(!engine.passTurn(2,2000));
  assert(engine.passTurn(1,2000) && engine.activePlayerNumber()==3);
  assert(engine.statsForPlayer(1)->turnsCompleted==1 && engine.statsForPlayer(2)->turnsCompleted==1);
  assert(engine.statsForPlayer(2)->totalTurnMs==1000 && engine.currentRound()==1);
  assert(!engine.passTurn(0,2500));
  assert(engine.passTurn(3,3000) && engine.activePlayerNumber()==1 && engine.currentRound()==2);
  // One shared life total.
  assert(engine.changeLife(2,-5) && engine.lifeTotal(1)==25 && engine.lifeTotal(2)==25 && engine.lifeTotal(3)==30);
  // A teammate's life is not a request; an opponent's is, and either opponent answers it.
  assert(!engine.requestLifeChange(1,2,-1,3000));
  assert(engine.requestLifeChange(1,3,-3,3000));
  assert(!engine.requestLifeChange(2,4,-1,3000));  // The team already has one pending.
  const uint32_t id = engine.lifeChangeFor(3)->id;
  assert(engine.respondLifeChange(4,id,true,3100));
  assert(engine.lifeTotal(3)==27 && engine.lifeTotal(4)==27);
  // A win claim waits only for the other team.
  assert(engine.beginWinClaim(1,true,4000) && engine.nextWinConfirmationPlayerNumber()==3);
  bool finished = false;
  assert(!engine.confirmWinClaim(2,4000,finished));
  assert(!engine.denyWinClaim(4,4100) && engine.denyWinClaim(3,4100) && engine.running() && !engine.paused());
  // The checkpoint keeps the mode; teammates must agree.
  GameCheckpoint saved; engine.checkpoint(saved,5000);
  uint8_t bytes[GAME_CHECKPOINT_CAPACITY];
  const size_t size = encodeCheckpoint(saved,bytes,sizeof(bytes));
  GameCheckpoint decoded;
  assert(size && decodeCheckpoint(bytes,size,decoded)==TurnHubStorage::Status::Ok);
  assert(decoded.settings.twoHeadedGiant && decoded.settings.profile==GameProfile::Magic);
  GameEngine restored; assert(restored.restoreCheckpoint(decoded,9000) && restored.teamOf(4)==2);
  decoded.life[1] = 1; assert(!validCheckpoint(decoded));
  decoded.life[1] = decoded.life[0]; decoded.eliminated[3] = true; assert(!validCheckpoint(decoded));
  // Eliminating one teammate eliminates the team; the last team wins together.
  assert(engine.pause(6000) && engine.eliminatePlayer(4,6000,finished) && finished);
  assert(engine.isEliminated(3) && engine.isEliminated(4) && engine.winnerPlayerNumber()==1);
  assert(engine.isWinner(1) && engine.isWinner(2) && !engine.isWinner(3));

  // Three teams: the turn skips the starter's teammate, and an eliminated team
  // leaves play going. Official Two-Headed Giant is two teams of two.
  assert(!engine.start(seats,6,seats[1],1000,settings));
  settings.houseRules = true;
  // The chosen starter's team goes first, its first player holding the turn (the app's rule).
  assert(engine.start(seats,6,seats[1],1000,settings) && engine.activePlayerNumber()==1);
  assert(engine.passTurn(0,2000) && engine.activePlayerNumber()==3);
  assert(engine.pause(2500) && engine.eliminatePlayer(3,2500,finished) && !finished);
  assert(engine.isEliminated(4) && engine.activePlayerNumber()==5 && engine.resume(2600));
  assert(engine.passTurn(5,3000) && engine.activePlayerNumber()==1);

  // Commander: per-player damage comes off the team's life.
  settings.profile = GameProfile::Commander;
  settings.startingLife = TEAM_LIFE_COMMANDER;
  settings.autoEliminateAtCommanderDamage = false;
  assert(engine.start(seats,4,seats[0],1000,settings));
  assert(engine.changeCommanderDamage(3,1,1,21));
  assert(engine.lifeTotal(3)==39 && engine.lifeTotal(4)==39);
  // A teammate is never a source of Commander damage.
  assert(!engine.changeCommanderDamage(4,3,1,1) && !engine.recordCommanderHit(1,2,1,1) && engine.lifeTotal(1)==60);
  assert(engine.commanderDamage(3,1,1)==21 && engine.commanderDamage(4,1,1)==0);
  assert(engine.recordCommanderHit(4,2,1,5) && engine.lifeTotal(3)==34 && engine.undoCommanderHit(4));
  assert(engine.lifeTotal(3)==39);

  // The profile byte carries the mode in saved settings and records.
  GameSettings read;
  readProfileByte(profileByte(settings),read);
  assert(read.profile==GameProfile::Commander && read.twoHeadedGiant);
  readProfileByte(static_cast<uint8_t>(GameProfile::Magic),read);
  assert(read.profile==GameProfile::Magic && !read.twoHeadedGiant);
}

static void twoHeadedGiantTable() {
  using namespace TurnHub;
  enterEmptyLobby(); TurnHub::fixtureRadio=false;
  String ids[4]; String tokens[4];
  const char *names[4] = {"Giant A1","Giant A2","Giant B1","Giant B2"};
  for (int i=0;i<4;++i) tokens[i]=registerPhone(names[i],ids[i]);
  for (int i=0;i<3;++i) assert(request("/api/session/join",tokens[i])==200);
  // Teams need Magic or Commander, and an even table of four or more.
  assert(request("/api/game/settings",tokens[0],{{"gameProfile","generic"},{"twoHeadedGiant","1"}})==409);
  assert(request("/api/game/settings",tokens[0],{{"twoHeadedGiant","yes"}})==400);
  assert(request("/api/game/settings",tokens[0],
      {{"gameProfile","mtg"},{"startingLife","30"},{"twoHeadedGiant","1"}})==200);
  assert(table().nextGameSettings.twoHeadedGiant);
  GameSettings saved; assert(loadGameSettings(saved)==TurnHubStorage::Status::Ok && saved.twoHeadedGiant);
  assert(request("/api/game/settings",tokens[0],{},HTTP_GET)==200);
  assert(server.body.find("\"twoHeadedGiant\":true")!=std::string::npos);
  assert(request("/api/v1/state","",{},HTTP_GET)==200);
  assert(server.body.find("\"twoHeadedGiant\":true")!=std::string::npos);
  assert(server.body.find("\"team\":2")!=std::string::npos);
  assert(request("/api/control/start",tokens[0])==409);  // Three players.
  assert(request("/api/session/join",tokens[3])==200);
  assert(request("/api/control/start",tokens[0])==200);
  testNow+=3000; updateCountdown(testNow);
  assert(table().hubState==HubState::Running && table().game.twoHeadedGiant() && table().game.lifeTotal(4)==30);
  const uint8_t starter = table().game.activePlayerNumber();
  const uint8_t mate = table().game.teammateOf(starter);
  auto token = [&](uint8_t player) {
    for (int i=0;i<4;++i) if (String(table().game.playerByNumber(player)->profileId)==ids[i]) return tokens[i];
    assert(false); return String();
  };
  // The teammate passes the team's turn and changes the shared life.
  assert(request("/api/control/life",token(mate),{{"delta","-4"}})==200);
  assert(table().game.lifeTotal(starter)==26 && table().game.lifeTotal(mate)==26);
  assert(request("/api/control/pass",token(mate))==200 && table().pendingPass.active);
  testNow+=PASS_GRACE_MS; updatePendingPass(testNow);
  assert(!table().game.sameTeam(table().game.activePlayerNumber(),starter));
  // A conceding player takes the teammate along, and the other team wins together.
  assert(request("/api/control/concede",token(starter))==200 && table().hubState==HubState::GameOver);
  assert(table().game.isEliminated(mate) && table().game.isWinner(table().game.activePlayerNumber()));
  assert(table().game.isWinner(table().game.teammateOf(table().game.activePlayerNumber())));
  // Leaving Magic for a profile without teams turns the mode off.
  assert(request("/api/control/reset",tokens[0])==200);
  assert(request("/api/session/join",tokens[0])==200);
  assert(request("/api/game/settings",tokens[0],{{"gameProfile","generic"}})==200);
  assert(!table().nextGameSettings.twoHeadedGiant);
  // Later scenarios register their own accounts against the profile limit.
  assert(request("/api/control/reset",tokens[0])==200);
  for (const String &id : ids) ProfileFixture::profiles.erase(id.c_str());
}

// Parity with the standalone app (2026-10-14): official modes keep official
// life, poison and player counts unless house rules are on; nobody chosen to
// go first means a random starter, and choosing the chosen player again clears it.
static void officialRulesAndStarter() {
  using namespace TurnHub;
  enterEmptyLobby(); TurnHub::fixtureRadio=false;
  String ids[6]; String tokens[6];
  const char *names[6] = {"Rules A","Rules B","Rules C","Rules D","Rules E","Rules F"};
  for (int i=0;i<6;++i) { tokens[i]=registerPhone(names[i],ids[i]); assert(request("/api/session/join",tokens[i])==200); }
  // Two-Headed Giant: 30 life and 15 shared poison, two teams of two.
  assert(request("/api/game/settings",tokens[0],{{"gameProfile","mtg"},{"twoHeadedGiant","1"},{"startingLife","50"},{"poisonLimit","9"}})==200);
  assert(table().nextGameSettings.startingLife==30 && table().nextGameSettings.poisonLimit==15);
  assert(request("/api/v1/state",tokens[0],{},HTTP_GET)==200 && server.body.find("\"houseRules\":false")!=std::string::npos);
  assert(request("/api/control/start",tokens[0])==409 && server.body.find("two teams of two")!=std::string::npos);
  // House rules unlock the life, the limit and the table size.
  assert(request("/api/game/settings",tokens[0],{{"houseRules","1"},{"startingLife","50"},{"poisonLimit","9"}})==200);
  assert(table().nextGameSettings.startingLife==50 && table().nextGameSettings.poisonLimit==9 && table().nextGameSettings.houseRules);
  assert(request("/api/game/settings",tokens[0],{},HTTP_GET)==200 && server.body.find("\"houseRules\":true")!=std::string::npos);
  // Turning them off puts the official values back.
  assert(request("/api/game/settings",tokens[0],{{"houseRules","0"}})==200);
  assert(table().nextGameSettings.startingLife==30 && table().nextGameSettings.poisonLimit==15);
  // Yu-Gi-Oh!: a Duel is two players.
  assert(request("/api/game/settings",tokens[0],{{"gameProfile","yugioh"},{"twoHeadedGiant","0"}})==200);
  assert(table().nextGameSettings.startingLife==8000);
  assert(request("/api/control/start",tokens[0])==409 && server.body.find("two players")!=std::string::npos);
  // Commander: 40 life, poison limit fixed during play without house rules.
  for (int i=2;i<6;++i) assert(request("/api/session/leave",tokens[i])==200);
  assert(request("/api/game/settings",tokens[0],{{"gameProfile","mtg_commander"},{"startingLife","30"}})==200);
  assert(table().nextGameSettings.startingLife==40 && table().nextGameSettings.poisonLimit==10);
  // Nobody chosen: a random player starts. Choosing the chosen player again clears the choice.
  PlayerSeat chosen;
  assert(request("/api/control/starter",tokens[1])==200 && table().lobby.selectedStarter(chosen) && chosen.playerNumber==2);
  assert(request("/api/control/starter",tokens[1])==200 && !table().lobby.selectedStarter(chosen));
  randomStarterByDefault=true;
  bool starters[3] = {};
  for (int round=0; round<16; ++round) {
    assert(request("/api/control/start",tokens[0])==200);
    testNow+=3000; updateCountdown(testNow);
    assert(table().hubState==HubState::Running && table().game.lifeTotal(1)==40);
    starters[table().game.starterPlayerNumber()]=true;
    if (round==0) {
      assert(request("/api/game/settings",tokens[0],{{"poisonLimit","12"}})==409);
      assert(request("/api/game/settings",tokens[0],{{"houseRules","1"}})==409);
      assert(request("/api/game/settings",tokens[0],{{"autoEliminateAtPoisonLimit","0"}})==200);
    }
    assert(request("/api/control/concede",tokens[1])==200 && table().hubState==HubState::GameOver);
    assert(request("/api/control/rematch",tokens[0])==200);
  }
  randomStarterByDefault=false;
  assert(starters[1] && starters[2]);
  assert(request("/api/control/reset",tokens[0])==200);
  for (int i=0;i<6;++i) { assert(request("/api/session/logout",tokens[i])==200); ProfileFixture::profiles.erase(ids[i].c_str()); TurnHubAccounts::accounts.erase(ids[i].c_str()); }
  table().nextGameSettings=GameSettings{};
}

// Parity with the standalone app (2026-10-14): Back to setup within three
// minutes of game time records nothing; a match under a minute waits for the
// table to count it.
static void backToSetupAndShortGames() {
  using namespace TurnHub;
  enterEmptyLobby(); TurnHub::fixtureRadio=false;
  String ids[2]; String tokens[2];
  tokens[0]=registerPhone("Undo one",ids[0]); tokens[1]=registerPhone("Undo two",ids[1]);
  for (const String &t : tokens) assert(request("/api/session/join",t)==200);
  auto played = [&](int i) { return ProfileFixture::profiles[ids[i]].stats.gamesPlayed; };
  auto start = [&]() {
    assert(request("/api/control/start",tokens[0])==200); testNow+=3000; updateCountdown(testNow);
    assert(table().hubState==HubState::Running);
  };
  TurnHubProfileStats::shortGameMinimumMs=SHORT_GAME_MS;
  const int baseGames=completedGames;
  assert(request("/api/control/back-to-setup",tokens[0])==409);  // Lobby.
  start();
  assert(request("/api/control/back-to-setup","")==401);
  testNow+=UNDO_START_MS-1000; assert(request("/api/control/life",tokens[1],{{"delta","-3"}})==200);
  assert(request("/api/control/back-to-setup",tokens[1])==200);
  assert(table().hubState==HubState::Lobby && table().lobby.playerCount()==2 && !table().game.hasPlayers());
  assert(completedGames==baseGames && played(0)==0 && played(1)==0);
  // Paused time doesn't count; past three minutes of play it is gone.
  start();
  assert(request("/api/control/pause",tokens[0])==200); testNow+=10*60000;
  assert(request("/api/control/pause",tokens[0])==200);
  assert(request("/api/tablet/control",tokens[0],{{"action","back-to-setup"}})!=200);  // Not a tablet.
  testNow+=UNDO_START_MS+1;
  assert(request("/api/control/back-to-setup",tokens[0])==409 && table().hubState==HubState::Running);
  // A long game counts at once.
  assert(request("/api/control/concede",tokens[1])==200 && table().hubState==HubState::GameOver);
  assert(played(0)==1 && played(1)==1);
  assert(request("/api/v1/state",tokens[0],{},HTTP_GET)==200 && server.body.find("\"shortGameUncounted\":false")!=std::string::npos);
  assert(request("/api/control/count-game",tokens[0])==409);
  // A game under a minute waits; the table counts it once.
  assert(request("/api/control/rematch",tokens[0])==200); start();
  testNow+=SHORT_GAME_MS-1;
  assert(request("/api/control/concede",tokens[1])==200 && table().hubState==HubState::GameOver);
  assert(completedGames==baseGames+2 && played(0)==1 && played(1)==1);
  assert(request("/api/v1/state",tokens[0],{},HTTP_GET)==200 && server.body.find("\"shortGameUncounted\":true")!=std::string::npos);
  assert(request("/api/control/count-game","")==401);
  assert(request("/api/control/count-game",tokens[1])==200 && played(0)==2 && played(1)==2);
  assert(ProfileFixture::profiles[ids[0]].stats.gamesWon==2);
  assert(request("/api/control/count-game",tokens[0])==409 && played(0)==2);
  assert(request("/api/v1/state",tokens[0],{},HTTP_GET)==200 && server.body.find("\"shortGameUncounted\":false")!=std::string::npos);
  // Left uncounted, it never reaches the stats.
  assert(request("/api/control/rematch",tokens[0])==200); start();
  assert(request("/api/control/concede",tokens[1])==200 && shortGameUncounted());
  assert(request("/api/control/rematch",tokens[0])==200 && !shortGameUncounted() && played(0)==2);
  TurnHubProfileStats::shortGameMinimumMs=0;
  enterEmptyLobby();
  for (int i=0;i<2;++i) { assert(request("/api/session/logout",tokens[i])==200); ProfileFixture::profiles.erase(ids[i].c_str()); TurnHubAccounts::accounts.erase(ids[i].c_str()); }
}

static void twoHeadedGiantSigils() {
  using namespace TurnHub;
  using namespace TurnHubProtocol;
  freshLobby(4);
  table().nextGameSettings.profile=GameProfile::Magic; table().nextGameSettings.startingLife=30;
  table().nextGameSettings.twoHeadedGiant=true; table().nextGameSettings.poisonLimit=15;
  startFromHost();
  assert(table().game.activePlayerNumber()==1);
  // Both teammates' Sigils show the turn and offer Pass; the other team's don't.
  syncSigilMenus(testNow);
  assert(sigilMenuFor(1).actions & sigilActionBit(SigilAction::Pass));
  assert(!(sigilMenuFor(2).actions & sigilActionBit(SigilAction::Pass)));
  leds.render(table().hubState,table().lobby,table().game,0,0,0,testNow);
  assert(hasDisplayFlag(sentGameDisplays[1].state,DISPLAY_FLAG_ACTIVE));
  assert(!hasDisplayFlag(sentGameDisplays[2].state,DISPLAY_FLAG_ACTIVE));
  assert(table().game.changeLife(1,-2) && table().game.lifeTotal(2)==28);
  leds.render(table().hubState,table().lobby,table().game,0,0,0,testNow);
  assert(sentGameDisplays[1].primary.life==28);
  choose(1,SigilAction::Pass); assert(table().pendingPass.active);
  syncSigilMenus(testNow);
  assert(sigilMenuFor(0).actions & sigilActionBit(SigilAction::CancelPass));
  choose(0,SigilAction::CancelPass); assert(!table().pendingPass.active);
  choose(1,SigilAction::Pass);
  testNow+=PASS_GRACE_MS; updatePendingPass(testNow);
  assert(table().game.activePlayerNumber()==3 && table().game.hasTurn(4));
  table().nextGameSettings=GameSettings{};
}

// A standalone tablet game's finished record: imported once, credited to the
// explicit Atlas-scoped profiles only; invalid mappings never take a receipt.
static void standaloneImport() {
  MemoryBlobs blobs; TurnHubStandalone::useImportStore(&blobs);
  TurnHubWebApi::configure(resolveWebSeat,handleWebControl,handleProfileControl,resolveProfileParticipant);
  TurnHubWebApi::configureGameControls(readGameSettings,configureGame,changeLife);
  TurnHubWebApi::configureCounterControls(readCounters,changeCounter);
  enterEmptyLobby(); TurnHub::fixtureRadio=false; testNow=1000;
  String anaId,benId; const String ana=registerPhone("Ana Import",anaId); const String ben=registerPhone("Ben Import",benId);
  std::map<std::string,String> record={
    {"recordId","0f8e5c2a-1111-4222-8333-444455556666"},{"atlasId",TurnHubWebApi::internal::atlasHardwareId()},
    {"gameProfile","mtg_commander"},{"durationMs","1800000"},{"players","2"},{"starter","1"},{"winner","0"},
    {"name0","Ana"},{"profile0",anaId},{"turns0","6"},{"turnMs0","600000"},{"fastest0","30000"},{"longest0","200000"},{"out0","0"},
    {"eliminatedBy1","0"},{"name1","ben import"},{"profile1",benId},{"turns1","6"},{"turnMs1","500000"},{"fastest1","20000"},{"longest1","150000"},{"out1","1"},
  };
  const auto anaBefore=ProfileFixture::profiles[anaId.c_str()].stats;
  const auto benBefore=ProfileFixture::profiles[benId.c_str()].stats;
  assert(request("/api/standalone/import","",record)==401);
  auto bad=record; bad["eliminatedBy1"]="1"; assert(request("/api/standalone/import",ana,bad)==400);
  bad=record; bad["conceded1"]="1"; assert(request("/api/standalone/import",ana,bad)==400);
  bad=record; bad["eliminatedBy0"]="1"; assert(request("/api/standalone/import",ana,bad)==400);
  bad=record; bad["winner"]="2"; assert(request("/api/standalone/import",ana,bad)==400);
  bad=record; bad["recordId"]="x"; assert(request("/api/standalone/import",ana,bad)==400);
  bad=record; bad["players"]="1"; assert(request("/api/standalone/import",ana,bad)==400);
  bad=record; bad["atlasId"]="THA-OTHER"; assert(request("/api/standalone/import",ana,bad)==400);
  bad=record; bad.erase("atlasId"); assert(request("/api/standalone/import",ana,bad)==400);
  // A same-name account must never rescue missing, deleted or foreign identity.
  bad=record; bad["profile0"]=""; bad["profile1"]=""; assert(request("/api/standalone/import",ana,bad)==400);
  bad=record; bad["profile1"]="000000000000"; assert(request("/api/standalone/import",ana,bad)==400);
  bad=record; bad["profile1"]=anaId; assert(request("/api/standalone/import",ana,bad)==400);
  TurnHubAccounts::accounts[benId.c_str()].archived=true;
  assert(request("/api/standalone/import",ana,record)==400);
  TurnHubAccounts::accounts[benId.c_str()].archived=false;
  assert(blobs.blobs.empty());
  assert(ProfileFixture::profiles[anaId.c_str()].stats.gamesPlayed==anaBefore.gamesPlayed);
  assert(ProfileFixture::profiles[benId.c_str()].stats.gamesPlayed==benBefore.gamesPlayed);

  assert(request("/api/standalone/import",ana,record)==200);
  assert(server.body.find("\"duplicate\":false")!=std::string::npos);
  assert(server.body.find("\"credited\":2")!=std::string::npos);
  const auto &anaStats=ProfileFixture::profiles[anaId.c_str()].stats;
  assert(anaStats.gamesPlayed==anaBefore.gamesPlayed+1 && anaStats.gamesWon==anaBefore.gamesWon+1);
  assert(anaStats.lastGameResult==TurnHubProfiles::LastGameResult::Win && anaStats.lastGameTurns==6);
  assert(anaStats.lastGameProfile==static_cast<uint8_t>(TurnHub::GameProfile::Commander));
  assert(anaStats.eliminationsMade==anaBefore.eliminationsMade+1 && anaStats.lastGameEliminationsMade==1);
  const auto &benStats=ProfileFixture::profiles[benId.c_str()].stats;
  assert(benStats.gamesPlayed==benBefore.gamesPlayed+1 && benStats.gamesWon==benBefore.gamesWon);
  assert(benStats.gamesEliminated==benBefore.gamesEliminated+1 && benStats.gamesStarted==benBefore.gamesStarted+1);

  // Duplicate receipt is acknowledged even if a profile was subsequently archived.
  TurnHubAccounts::accounts[benId.c_str()].archived=true;
  assert(request("/api/standalone/import",ana,record)==200);
  assert(server.body.find("\"duplicate\":true")!=std::string::npos);
  assert(ProfileFixture::profiles[anaId.c_str()].stats.gamesPlayed==anaBefore.gamesPlayed+1);
  TurnHubAccounts::accounts[benId.c_str()].archived=false;

  auto next=record; next["recordId"]="0f8e5c2a-2222-4222-8333-444455556666";
  blobs.failWrites=true; assert(request("/api/standalone/import",ana,next)==503);
  assert(ProfileFixture::profiles[anaId.c_str()].stats.gamesPlayed==anaBefore.gamesPlayed+1);
  blobs.failWrites=false;
  next["winner"]="";
  assert(request("/api/standalone/import",ana,next)==200 && server.body.find("\"credited\":2")!=std::string::npos);
  assert(ProfileFixture::profiles[anaId.c_str()].stats.lastGameResult==TurnHubProfiles::LastGameResult::Draw);

  // A guest seat (empty profile) is not credited. Sending the record again
  // after that player is linked credits only them; Ana is not counted twice.
  auto partial=record; partial["recordId"]="0f8e5c2a-3333-4222-8333-444455556666"; partial["profile1"]="";
  const uint32_t anaPlayed=ProfileFixture::profiles[anaId.c_str()].stats.gamesPlayed;
  const uint32_t benPlayed=ProfileFixture::profiles[benId.c_str()].stats.gamesPlayed;
  assert(request("/api/standalone/import",ana,partial)==200);
  assert(server.body.find("\"credited\":1,\"already\":0,\"guests\":1")!=std::string::npos);
  assert(ProfileFixture::profiles[anaId.c_str()].stats.gamesPlayed==anaPlayed+1);
  assert(ProfileFixture::profiles[benId.c_str()].stats.gamesPlayed==benPlayed);
  assert(request("/api/standalone/import",ana,partial)==200 && server.body.find("\"duplicate\":true")!=std::string::npos);
  partial["profile1"]=benId;
  assert(request("/api/standalone/import",ana,partial)==200);
  assert(server.body.find("\"duplicate\":false,\"credited\":1,\"already\":1,\"guests\":0")!=std::string::npos);
  assert(ProfileFixture::profiles[anaId.c_str()].stats.gamesPlayed==anaPlayed+1);
  assert(ProfileFixture::profiles[benId.c_str()].stats.gamesPlayed==benPlayed+1);
  assert(request("/api/standalone/import",ana,partial)==200 && server.body.find("\"duplicate\":true")!=std::string::npos);
  // A ring from before per-player receipts is discarded, not an error.
  blobs.blobs[TurnHubStandalone::IMPORTED_KEY]={1,0,0};
  auto later=record; later["recordId"]="0f8e5c2a-4444-4222-8333-444455556666";
  assert(request("/api/standalone/import",ana,later)==200 && server.body.find("\"credited\":2")!=std::string::npos);
  assert(blobs.blobs[TurnHubStandalone::IMPORTED_KEY][0]==2);

  assert(request("/api/session/logout",ana)==200 && request("/api/session/logout",ben)==200);
  ProfileFixture::profiles.erase(anaId.c_str()); ProfileFixture::profiles.erase(benId.c_str());
  TurnHubAccounts::accounts.erase(anaId.c_str()); TurnHubAccounts::accounts.erase(benId.c_str());
  TurnHubStandalone::useImportStore(nullptr);
}

// Atlas's match history (match_archive.h): a phone's game arrives with its
// whole record; Atlas keeps it once on the card, with its own profile IDs,
// plus CSV rows, and serves it back to that profile's phone.
struct MemoryArchive : TurnHubHistory::ArchiveFiles {
  std::map<std::string,std::string> files;
  bool size(const char *name,size_t &size) override {
    auto f=files.find(name); if(f==files.end()) return false; size=f->second.size(); return true;
  }
  bool append(const char *name,const char *data,size_t size) override { files[name].append(data,size); return true; }
  bool readAt(const char *name,size_t offset,char *data,size_t capacity,size_t &got) override {
    auto f=files.find(name); if(f==files.end()) return false;
    got=offset<f->second.size()?std::min(capacity,f->second.size()-offset):0;
    memcpy(data,f->second.data()+offset,got); return true;
  }
};
static void matchHistory() {
  MemoryBlobs blobs; TurnHubStandalone::useImportStore(&blobs);
  enterEmptyLobby(); TurnHub::fixtureRadio=false; testNow=1000;
  String anaId,benId; const String ana=registerPhone("Ana History",anaId); const String ben=registerPhone("Ben History",benId);
  auto phoneRecord=[](const char *id){
    return String("{\"recordId\":\"")+id+"\",\"gameProfile\":\"mtg_commander\",\"startingLife\":40,\"startedAtMs\":1793131200000,"
      "\"durationMs\":1800000,\"starter\":1,\"winner\":0,\"winners\":[0],\"players\":["
      "{\"name\":\"Ana\",\"profileId\":\"AAAA0000\",\"localId\":\"phone-uuid\",\"finalLife\":12,\"turnsCompleted\":6,\"demerits\":0,"
      "\"extensionsUsed\":0,\"turnMs\":600000,\"fastestTurnMs\":30000,\"longestTurnMs\":200000,\"outOrder\":null,\"conceded\":false,"
      "\"eliminatedBy\":null,\"bagDrawn\":false,\"selfEliminated\":false,\"commanderDamageReceived\":5,\"counters\":{\"poison\":1,"
      "\"energy\":0,\"experience\":0,\"commanderCasts\":[2,0],\"cityBlessing\":false},\"commanderBySource\":{\"1\":[5,0]}},"
      "{\"name\":\"Guest\",\"profileId\":null,\"localId\":null,\"finalLife\":0,\"turnsCompleted\":6,\"demerits\":0,\"extensionsUsed\":0,"
      "\"turnMs\":500000,\"fastestTurnMs\":20000,\"longestTurnMs\":150000,\"outOrder\":1,\"conceded\":false,\"eliminatedBy\":0,"
      "\"bagDrawn\":false,\"selfEliminated\":false,\"commanderDamageReceived\":0,\"counters\":{\"poison\":0,\"energy\":0,"
      "\"experience\":0,\"commanderCasts\":[0,0],\"cityBlessing\":false},\"commanderBySource\":{}}]}";
  };
  std::map<std::string,String> record={
    {"recordId","1a2b3c4d-1111-4222-8333-444455556666"},{"atlasId",TurnHubWebApi::internal::atlasHardwareId()},
    {"gameProfile","mtg_commander"},{"durationMs","1800000"},{"players","2"},{"starter","1"},{"winner","0"},
    {"name0","Ana"},{"profile0",anaId},{"turns0","6"},{"turnMs0","600000"},{"fastest0","30000"},{"longest0","200000"},{"out0","0"},
    {"eliminatedBy1","0"},{"name1","Guest"},{"profile1",""},{"turns1","6"},{"turnMs1","500000"},{"fastest1","20000"},{"longest1","150000"},{"out1","1"},
    {"record",phoneRecord("1a2b3c4d-1111-4222-8333-444455556666")},
  };
  // No card: statistics still count, the history says so.
  TurnHubHistory::useFiles(nullptr);
  assert(request("/api/standalone/import",ana,record)==200 && server.body.find("\"history\":\"no_card\"")!=std::string::npos);
  MemoryArchive archive; TurnHubHistory::useFiles(&archive);
  // A record of another game, or not a record, is refused before any credit.
  auto bad=record; bad["recordId"]="1a2b3c4d-2222-4222-8333-444455556666"; assert(request("/api/standalone/import",ana,bad)==400);
  bad=record; bad["record"]="{not json"; assert(request("/api/standalone/import",ana,bad)==400);
  assert(archive.files.empty());
  // The duplicate receipt still lets the record into the history once.
  assert(request("/api/standalone/import",ana,record)==200 && server.body.find("\"history\":\"kept\"")!=std::string::npos);
  assert(request("/api/standalone/import",ana,record)==200 && server.body.find("\"history\":\"already_kept\"")!=std::string::npos);
  const std::string &lines=archive.files[TurnHubHistory::RECORDS_FILE];
  assert(std::count(lines.begin(),lines.end(),'\n')==1);
  assert(lines.find(std::string("\"profileId\":\"")+anaId.c_str()+"\"")!=std::string::npos);
  assert(lines.find("AAAA0000")==std::string::npos && lines.find("phone-uuid")==std::string::npos);
  const std::string &csv=archive.files[TurnHubHistory::CSV_FILE];
  assert(csv.rfind(turnhub::RECORD_CSV_HEADER,0)==0 && std::count(csv.begin(),csv.end(),'\n')==3);
  assert(csv.find(std::string(",1,Ana,")+anaId.c_str()+",false,true,false,,false,,false,12,6,600000,100000,")!=std::string::npos);
  // A torn last line (power cut mid-write) is not a record, and the next one starts on its own line.
  archive.files[TurnHubHistory::RECORDS_FILE]+="{\"recordId\":\"torn";
  auto second=record; second["recordId"]="1a2b3c4d-3333-4222-8333-444455556666";
  second["record"]=phoneRecord("1a2b3c4d-3333-4222-8333-444455556666");
  assert(request("/api/standalone/import",ana,second)==200 && server.body.find("\"history\":\"kept\"")!=std::string::npos);

  // Served back to the profile's own session or its linked phone, oldest first.
  assert(request("/api/profiles/history","",{{"profileId",anaId}},HTTP_GET)==401);
  assert(request("/api/profiles/history",ben,{{"profileId",anaId}},HTTP_GET)==401);
  assert(request("/api/profiles/history",ana,{{"profileId",anaId},{"limit","0"}},HTTP_GET)==400);
  assert(request("/api/profiles/history",ana,{{"profileId",anaId},{"limit","1"}},HTTP_GET)==200);
  assert(server.body.find("1a2b3c4d-1111")!=std::string::npos && server.body.find("1a2b3c4d-3333")==std::string::npos);
  assert(server.body.find("\"more\":true")!=std::string::npos);
  assert(request("/api/profiles/link-key",ana)==200); const String key=responseField("linkKey");
  assert(request("/api/profiles/history","",{{"profileId",anaId},{"linkKey",key},{"skip","1"}},HTTP_GET)==200);
  assert(server.body.find("1a2b3c4d-3333")!=std::string::npos && server.body.find("\"more\":false")!=std::string::npos);
  assert(request("/api/profiles/history",ben,{{"profileId",benId}},HTTP_GET)==200 && server.body.find("\"records\":[]")!=std::string::npos);
  // A game played on Atlas itself joins the same history, as the core's record.
  assert(request("/api/session/join",ana)==200 && request("/api/session/join",ben)==200);
  assert(request("/api/control/start",ana)==200); testNow+=3000; updateCountdown(testNow);
  assert(table().hubState==HubState::Running);
  // Power loss preserves the record ID and names, including guest labels:
  // otherwise every boot's first restored game used "atlas-restored-2"
  // and the history silently deduplicated unrelated games.
  const std::string matchId = table().game.core().gameId;
  TurnHub::GameCheckpoint saved, decoded;
  table().game.checkpoint(saved,testNow);
  uint8_t bytes[TurnHub::GAME_CHECKPOINT_CAPACITY];
  const size_t size = TurnHub::encodeCheckpoint(saved,bytes,sizeof(bytes));
  assert(size && TurnHub::decodeCheckpoint(bytes,size,decoded)==TurnHubStorage::Status::Ok);
  assert(table().game.restoreCheckpoint(decoded,testNow+1000));
  assert(table().game.core().gameId==matchId && table().game.core().players[0].name=="Ana History" &&
      table().game.core().players[1].name=="Ben History");
  assert(table().game.resume(testNow+1000));
  testNow+=1000;
  testNow+=TurnHub::SHORT_GAME_MS+1000;
  assert(request("/api/control/concede",ben)==200 && table().hubState==HubState::GameOver);
  assert(request("/api/profiles/history",ben,{{"profileId",benId}},HTTP_GET)==200);
  assert(server.body.find("\"recordId\":\"atlas-")!=std::string::npos && server.body.find("Ana History")!=std::string::npos);
  assert(archive.files[TurnHubHistory::CSV_FILE].find("Ben History")!=std::string::npos);
  enterEmptyLobby();
  TurnHubHistory::useFiles(nullptr);
  assert(request("/api/profiles/history",ana,{{"profileId",anaId}},HTTP_GET)==503);
  assert(request("/api/session/logout",ana)==200 && request("/api/session/logout",ben)==200);
  ProfileFixture::profiles.erase(anaId.c_str()); ProfileFixture::profiles.erase(benId.c_str());
  TurnHubAccounts::accounts.erase(anaId.c_str()); TurnHubAccounts::accounts.erase(benId.c_str());
  TurnHubStandalone::useImportStore(nullptr);
}

// Battery charge from the cell voltage: the LiPo curve, smoothing, the shown
// percent's hysteresis, the low warning, and /api/devices.
static void batteryGauge() {
  using TurnHub::lipoPercentFromMillivolts;
  assert(lipoPercentFromMillivolts(4250)==100 && lipoPercentFromMillivolts(4180)==100);
  assert(lipoPercentFromMillivolts(3450)==0 && lipoPercentFromMillivolts(3000)==0);
  assert(lipoPercentFromMillivolts(3830)==46);
  // Not linear: the flat middle holds most of the charge.
  assert(lipoPercentFromMillivolts(3815)>40 && lipoPercentFromMillivolts(3815)<46);
  for (uint32_t mv=3400; mv<4300; mv+=5) assert(lipoPercentFromMillivolts(mv)<=lipoPercentFromMillivolts(mv+5));

  TurnHub::BatteryGauge gauge;
  assert(!gauge.reading().present);
  gauge.addSample(3900);
  assert(gauge.reading().present && gauge.reading().millivolts==3900 && gauge.reading().percent==62 && !gauge.reading().low);
  // Median rejects a one-second sag before either voltage filter sees it.
  gauge.addSample(3700);
  assert(gauge.reading().millivolts==3900 && gauge.reading().percent==62);
  gauge.addSample(3900);
  // Sustained real discharge cannot be hidden behind cosmetic smoothing.
  for (int i=0;i<360;++i) gauge.addSample(3700);
  assert(gauge.reading().millivolts>=3700 && gauge.reading().millivolts<=3702 && gauge.reading().percent<=15 && gauge.reading().low);
  for (int i=0;i<120;++i) gauge.addSample(3720);
  assert(gauge.reading().low);
  // A compelling persistent correction can rise slowly on battery.
  for (int i=0;i<1000;++i) gauge.addSample(3900);
  assert(!gauge.reading().low && gauge.reading().percent>40);
  for (int i=0;i<3;++i) gauge.addSample(100);
  assert(!gauge.reading().present);
  gauge.addSample(4100);
  assert(gauge.reading().millivolts==4100 && gauge.reading().percent==92);

  assert(request("/api/devices", "", {}, HTTP_GET) == 200);
  assert(server.body.find("\"battery\":null") != std::string::npos);
  fixtureBattery.present=true; fixtureBattery.percent=12; fixtureBattery.millivolts=3705; fixtureBattery.low=true;
  assert(request("/api/devices", "", {}, HTTP_GET) == 200);
  assert(server.body.find("\"battery\":{\"percent\":12,\"millivolts\":3705,\"low\":true,\"charging\":false}") != std::string::npos);
  fixtureBattery.charging=true;
  assert(request("/api/devices", "", {}, HTTP_GET) == 200);
  assert(server.body.find("\"low\":true,\"charging\":true}") != std::string::npos);
  fixtureBattery=TurnHub::BatteryReading();
  gauge.reseed(3800);
  assert(gauge.reading().present && gauge.reading().millivolts==3800 && gauge.reading().percent==38);
}

// USB or the cell, from the steps in the cell voltage (four samples a
// second; the numbers are from the bench trace in battery_gauge.h).
// Charging: the percent counts up from the last on-battery percent instead
// of reading the charger's voltage (battery_gauge.h).
static void chargingEstimate() {
  using TurnHub::lipoPercentFromMillivolts;
  TurnHub::BatteryGauge gauge;
  for (int i=0;i<30;++i) gauge.addSample(3860);
  const uint8_t before=gauge.reading().percent;
  assert(before==54 && !gauge.reading().charging);
  // USB in: the reading jumps 210 mV, the percent doesn't.
  gauge.addSample(4070, true);
  assert(gauge.reading().charging && gauge.reading().percent==before);
  // One point per CHARGE_MS_PER_POINT (30 s at 300 mA into 250 mAh), never
  // down, whatever the reading does.
  assert(TurnHub::CHARGE_MS_PER_POINT==30000);
  uint8_t last=before;
  uint32_t mv=4070;
  for (int s=1;s<=300;++s) {
    mv+=s%2;
    gauge.addSample(mv-(s%7==0 ? 40 : 0), true);
    assert(gauge.reading().percent>=last && gauge.reading().percent<=last+1);
    last=gauge.reading().percent;
  }
  assert(last==before+10);
  // Short of the charger's limit it stops at 90.
  for (int s=0;s<1800;++s) gauge.addSample(s%2 ? 4010 : 3990, true);
  assert(gauge.reading().percent==TurnHub::CHARGE_BELOW_LIMIT_MAX_PERCENT);
  // At the limit (steady at 4.08 V or more for 4 minutes): half the rate, and
  // 100 only after 20 minutes there.
  for (int s=0;s<1300;++s) gauge.addSample(4110, true);
  assert(gauge.reading().percent==99 && gauge.reading().charging);
  for (int s=0;s<200;++s) gauge.addSample(4110, true);
  // Full is shown as full: no bolt at 100.
  assert(gauge.reading().percent==100 && !gauge.reading().low && !gauge.reading().charging);
  // A lower charger plateau for ten seconds cancels full/taper evidence.
  TurnHub::BatteryGauge taper;
  taper.addSample(4110,true); taper.restorePercent(99);
  for (int s=0;s<900;++s) taper.addSample(4110,true);
  for (int s=0;s<30;++s) taper.addSample(4000,true);
  for (int s=0;s<900;++s) taper.addSample(4110,true);
  assert(taper.reading().percent==99 && taper.reading().charging);
  // USB out: retain the last percentage, reconcile sustained evidence later.
  gauge.addSample(3900);
  assert(!gauge.reading().charging && gauge.reading().percent==100);

  // Started on USB: nothing to count from, so the cell is taken as 210 mV
  // below the charger's reading...
  TurnHub::BatteryGauge cold;
  cold.addSample(4070, true);
  assert(cold.reading().charging && cold.reading().percent==lipoPercentFromMillivolts(3860));
  // No jump to an invented 85% when the charger reaches its limit.
  TurnHub::BatteryGauge full;
  for (int s=0;s<241;++s) full.addSample(4120, true);
  assert(full.reading().percent>=62 && full.reading().percent<85);
}

static void powerSource() {
  using TurnHub::PowerSource;
  TurnHub::PowerSourceTracker power;
  assert(power.source()==PowerSource::Usb);
  for (int i=0;i<200;++i) assert(!power.addSample(i%2 ? 4106 : 4096));
  auto step = [&](uint32_t mv, PowerSource expected) {
    assert(!power.addSample(mv)); assert(!power.addSample(mv));
    assert(power.addSample(mv) && power.source()==expected);
  };
  step(3866, PowerSource::Battery);
  uint32_t mv=3860;
  for (int i=0;i<180;++i) { mv-=1; assert(!power.addSample(mv + (i%3==0 ? 30 : 0))); }
  step(mv+220, PowerSource::Usb);
  for (int i=0;i<400;++i) assert(!power.addSample(mv+220+i/4));
  step(3700, PowerSource::Battery);
  for (int i=0;i<8;++i) assert(!power.addSample(3730));
  step(100, PowerSource::Usb);
  assert(!power.addSample(100));

  TurnHub::PowerSourceTracker cold;
  int seen=-1;
  for (int i=0;i<240 && seen<0;++i) if (cold.addSample(3860-i/2)) seen=i;
  assert(seen>0 && seen<240 && cold.source()==PowerSource::Battery);
  mv=3860-seen/2; seen=-1;
  for (int i=0;i<400 && seen<0;++i) if (cold.addSample(mv+i/2)) seen=i;
  assert(seen>0 && cold.source()==PowerSource::Usb);
}

static void batteryStability() {
  using namespace TurnHub;
  BatteryGauge noisy;
  noisy.addSample(3800);
  for (int i=0;i<300;++i) noisy.addSample(i%2 ? 3808 : 3792);
  assert(noisy.reading().percent>=36 && noisy.reading().percent<=38);
  const auto before=noisy.reading().percent;
  for (int i=0;i<120;++i) {
    noisy.addSample(i%10==0 ? 3520 : 3800); // radio/backlight/speaker pulses
    assert(noisy.reading().percent>=before-1 && !noisy.reading().criticalLow);
  }
  // Load removal is not a reason to report rising charge on battery.
  for (int i=0;i<180;++i) noisy.addSample(3820);
  assert(noisy.reading().percent<=before);

  BatteryGauge slow;
  slow.addSample(3860);
  slow.restorePercent(55);
  for (int i=0;i<29;++i) slow.addSample(3850);
  assert(slow.reading().percent==55);
  for (int i=0;i<31;++i) slow.addSample(3850);
  assert(slow.reading().percent>=53 && slow.reading().percent<=54);
  // Measured small cell can discharge quickly: accelerate a sustained gap.
  for (int i=0;i<180;++i) slow.addSample(3700);
  assert(slow.reading().percent<=20 && slow.reading().corrections>0);

  BatteryGauge underreported; underreported.addSample(4100); underreported.restorePercent(10);
  for (int i=0;i<3000;++i) underreported.addSample(4100);
  assert(underreported.reading().percent>=90 && underreported.reading().percent<=92);

  BatteryGauge cycling; cycling.addSample(3860);
  const auto unplugged=cycling.reading().percent;
  for (int cycle=0;cycle<5;++cycle) {
    cycling.addSample(4070,true);
    assert(cycling.reading().percent==unplugged);
    for (int i=0;i<10;++i) cycling.addSample(4070,true);
    cycling.addSample(3860,false);
    assert(cycling.reading().percent==unplugged);
  }
  // Reboot restores credible percent, never elapsed charge or full timers.
  const auto saved=makeBatteryCheckpoint(cycling.reading(),false);
  assert(validBatteryCheckpoint(saved) && usableBatteryCheckpoint(saved,3860));
  assert(usableBatteryCheckpoint(saved,4070)); // legitimate charger step
  BatteryGauge reboot; reboot.addSample(4070,true); reboot.restorePercent(saved.bytes[5]);
  assert(reboot.reading().percent==unplugged && reboot.reading().charging);
  for (int i=0;i<29;++i) reboot.addSample(4070,true);
  assert(reboot.reading().percent==unplugged);
  reboot.addSample(4070,true); assert(reboot.reading().percent==unplugged+1);
  // RTC restore uses exactly the same codec/plausibility, not charging state.
  BatteryGauge wake; wake.addSample(3860); wake.restorePercent(saved.bytes[5]);
  assert(wake.reading().percent==unplugged && !wake.reading().charging);
  auto corrupt=saved; corrupt.bytes[5]^=1; assert(!validBatteryCheckpoint(corrupt));
  corrupt=saved; corrupt.bytes[4]=2; assert(!validBatteryCheckpoint(corrupt));
  corrupt=saved; corrupt.bytes[9]^=1; assert(!validBatteryCheckpoint(corrupt));
  assert(!usableBatteryCheckpoint(saved,3500) && !usableBatteryCheckpoint(saved,100));
  BatteryGauge high; high.addSample(4180);
  assert(!usableBatteryCheckpoint(makeBatteryCheckpoint(high.reading(),false),3700));
  BatteryGauge empty; empty.addSample(3500);
  assert(!usableBatteryCheckpoint(makeBatteryCheckpoint(empty.reading(),false),4150));
  cycling.addSample(100); assert(cycling.reading().present); // isolated disconnect noise
  cycling.addSample(100); cycling.addSample(100); assert(!cycling.reading().present);
  const auto absent=makeBatteryCheckpoint(cycling.reading(),true);
  assert(validBatteryCheckpoint(absent) && !usableBatteryCheckpoint(absent,4100));
  cycling.addSample(4100); assert(cycling.reading().percent==92);

  BatteryGauge safety; safety.addSample(3900); safety.restorePercent(95);
  safety.addSample(3400); safety.addSample(3900);
  assert(!safety.reading().criticalLow);
  for (int i=0;i<12;++i) safety.addSample(3400);
  assert(safety.reading().criticalLow && safety.reading().emergencyLow && safety.reading().percent>80 && safety.reading().low);
  for (int i=0;i<80;++i) safety.addSample(3650);
  assert(!safety.reading().criticalLow && !safety.reading().emergencyLow);
  // Even incorrectly inferred USB must not fabricate charging in undervoltage.
  BatteryGauge coldLow; for (int i=0;i<8;++i) coldLow.addSample(3400,true);
  assert(coldLow.reading().criticalLow && coldLow.reading().emergencyLow && !coldLow.reading().charging);

  BatteryGauge wrapped; uint32_t now=0xfffff000UL;
  wrapped.addTimedSample(3860,true,now); wrapped.restorePercent(50);
  for (int i=0;i<30;++i) { now+=1000; wrapped.addTimedSample(4070,true,now); }
  assert(wrapped.reading().percent==51);
  now+=600000; wrapped.addTimedSample(4070,true,now);
  assert(wrapped.reading().percent==51); // delayed service cannot invent ten minutes of charge
  BatteryCheckpointPolicy policy; const uint32_t start=0xffff0000UL;
  policy.begin(start,&saved);
  auto changed=saved; BatteryReading r=cycling.reading(); r.percent=70;
  changed=makeBatteryCheckpoint(r,false);
  assert(!policy.due(changed,start+59999)); assert(policy.due(changed,start+60000));
  policy.attempted(changed,start+60000,false);
  assert(!policy.due(changed,start+60001)); assert(policy.due(changed,start+120000));
  policy.attempted(changed,start+120000,true);
  assert(!policy.due(changed,start+900000)); // unchanged does not wear flash
  r.percent=71; const auto small=makeBatteryCheckpoint(r,false);
  assert(!policy.due(small,start+180000)); assert(policy.due(small,start+720000));
  assert(policy.due(absent,start+180000));

  PowerSourceTracker pulses;
  for (int i=0;i<40;++i) pulses.addSample(4100);
  for (int i=0;i<100;++i) assert(!pulses.addSample(i%10==0 ? 3850 : 4100));
  assert(pulses.source()==PowerSource::Usb);
  for (int i=0;i<3;++i) pulses.addSample(3850);
  assert(pulses.source()==PowerSource::Battery);
  for (int i=0;i<40;++i) pulses.addSample(3850);
  for (int i=0;i<100;++i) assert(!pulses.addSample(i%10==0 ? 4100 : 3850));
  assert(pulses.source()==PowerSource::Battery);
  // Weaker steps need 1.5 seconds of corroboration.
  for (int i=0;i<5;++i) assert(!pulses.addSample(3970));
  assert(pulses.addSample(3970) && pulses.source()==PowerSource::Usb);
  for (int i=0;i<8;++i) pulses.addSample(3970);
  for (int i=0;i<6;++i) pulses.addSample(3850);
  assert(pulses.source()==PowerSource::Battery);
  // Two-sample load events don't change the source or flip on rebound.
  for (int i=0;i<10;++i) { assert(!pulses.addSample(4050)); assert(!pulses.addSample(4050));
    for (int j=0;j<8;++j) assert(!pulses.addSample(3850)); }
}

static void magicCountersAndRules() {
  using namespace TurnHub;
  enterEmptyLobby(); fixtureRadio=false;
  String aId,bId,cId;
  const String a=registerPhone("Magic one",aId), b=registerPhone("Magic two",bId), c=registerPhone("Magic three",cId);
  for (const String *token : {&a,&b,&c}) assert(request("/api/session/join",*token)==200);
  auto counter = [&](const String &token, const char *kind, int value, int commander=1) {
    return request("/api/control/counter",token,{{"counter",kind},{"delta",String(value)},{"value",String(value)},{"commander",String(commander)},{"target","3"}});
  };
  assert(counter(a,"energy",1)==409 && counter("","poison",1)==401);
  assert(request("/api/game/settings",a,{{"gameProfile","mtg_commander"},{"startingLife","40"},{"twoHeadedGiant","0"},
      {"autoEliminateAtZeroLife","0"},{"autoEliminateAtPoisonLimit","1"},{"autoEliminateAtCommanderDamage","1"},{"poisonLimit","10"},{"houseRules","1"}})==200);
  assert(request("/api/control/start",a)==200); testNow+=3000; updateCountdown(testNow);
  assert(counter(a,"fly",1)==400 && counter(a,"energy",-1)==409 && counter(a,"city-blessing",2)==400);
  assert(counter(a,"commander-casts",1,3)==400);
  assert(counter(a,"energy",5)==200 && counter(a,"energy",-2)==200);
  assert(counter(a,"experience",7)==200 && table().game.countersForPlayer(1)->energy==3 && table().game.countersForPlayer(3)->experience==0);
  assert(counter(a,"commander-casts",2,1)==200 && counter(a,"commander-casts",1,2)==200);
  assert(table().game.countersForPlayer(1)->commanderCasts[0]==2 && table().game.countersForPlayer(1)->commanderCasts[1]==1);
  assert(request("/api/control/monarch",c)==200);
  assert(counter(a,"initiative",1)==200 && counter(b,"initiative",1)==200 && table().game.initiativePlayerNumber()==2 && table().game.monarchPlayerNumber()==3);
  assert(counter(a,"city-blessing",1)==200 && counter(b,"city-blessing",1)==200);
  assert(request("/api/control/pause",a)==200 && counter(a,"energy",1)==200);
  assert(request("/api/game/settings",a,{{"autoEliminateAtPoisonLimit","0"},{"autoEliminateAtCommanderDamage","0"},{"poisonLimit","17"}})==200);
  assert(request("/api/game/settings",a,{{"poisonLimit","0"}})==400);
  assert(request("/api/game/settings",a,{{"autoEliminateAtCommanderDamage","yes"}})==400);
  assert(counter(a,"poison",16)==200 && !table().game.isEliminated(1));
  GameCheckpoint saved; table().game.checkpoint(saved,testNow); uint8_t bytes[GAME_CHECKPOINT_CAPACITY];
  const size_t size=encodeCheckpoint(saved,bytes,sizeof(bytes)); assert(size && decodeCheckpoint(bytes,size,saved)==TurnHubStorage::Status::Ok);
  GameEngine restored; assert(restored.restoreCheckpoint(saved,testNow));
  assert(restored.initiativePlayerNumber()==2 && restored.monarchPlayerNumber()==3 && restored.countersForPlayer(1)->energy==4);
  assert(restored.countersForPlayer(1)->commanderCasts[1]==1 && restored.countersForPlayer(1)->cityBlessing && restored.hasPartner(1));
  assert(!restored.settings().autoEliminateAtCommanderDamage && restored.settings().poisonLimit==17);
  saved.counters[0].poison=-1; assert(!validCheckpoint(saved)); saved.counters[0].poison=16;
  assert(request("/api/v1/state",a,{},HTTP_GET)==200 && server.body.find("\"initiativePlayer\":2")!=std::string::npos && server.body.find("\"commanderCasts\":[2,1]")!=std::string::npos);
  assert(request("/api/game/settings",a,{{"autoEliminateAtPoisonLimit","1"},{"poisonLimit","16"}})==200 && table().game.isEliminated(1));
  assert(counter(a,"energy",1)==409);
  assert(request("/api/control/commander",b,{{"source","3"},{"commander","1"},{"delta","20"}})==200);
  assert(request("/api/control/commander",b,{{"source","3"},{"commander","2"},{"delta","20"}})==200 && !table().game.isEliminated(2));
  assert(request("/api/control/commander",b,{{"source","3"},{"commander","2"},{"delta","1"}})==200 && !table().game.isEliminated(2));
  assert(request("/api/game/settings",c,{{"autoEliminateAtCommanderDamage","1"}})==200 && table().game.isEliminated(2));
  // CR 725.4: the initiative passes to the surviving active player.
  assert(table().hubState==HubState::GameOver && table().game.winnerPlayerNumber()==3 && table().game.initiativePlayerNumber()==3);
  assert(counter(c,"initiative",1)==409);
  enterEmptyLobby();
  for (const String *token : {&a,&b,&c}) assert(request("/api/session/logout",*token)==200);
  for (const String *id : {&aId,&bId,&cId}) { ProfileFixture::profiles.erase(id->c_str()); TurnHubAccounts::accounts.erase(id->c_str()); }
  table().nextGameSettings=GameSettings{};
  PlayerSeat seats[4]; for (uint8_t i=0;i<4;++i) { seats[i].playerNumber=i+1; seats[i].controllerId=i; seats[i].slot=1; }
  GameEngine engine; GameSettings settings; settings.profile=GameProfile::Magic; settings.twoHeadedGiant=true; settings.poisonLimit=15; settings.houseRules=true;
  assert(engine.start(seats,4,seats[0],testNow,settings));
  assert(engine.changePlayerCounter(1,CounterKind::Poison,14) && engine.countersForPlayer(2)->poison==14);
  assert(!engine.resolveEliminationRules(testNow));
  assert(engine.changePlayerCounter(2,CounterKind::Poison,1) && engine.resolveEliminationRules(testNow));
  assert(engine.isEliminated(1) && engine.isEliminated(2) && engine.isWinner(3));
  settings.twoHeadedGiant=false; assert(engine.start(seats,4,seats[0],testNow,settings) && engine.countersForPlayer(1)->poison==0);
  assert(engine.initiativePlayerNumber()==0 && !engine.changePlayerCounter(1,CounterKind::CommanderCast,1));
  settings.profile=GameProfile::Generic; assert(engine.start(seats,4,seats[0],testNow,settings) && !engine.changePlayerCounter(1,CounterKind::Poison,1));
}

// App parity (2026-10-28): treasure and rad, the rad trigger, day/night,
// reversed turns and Everyone's counters, over HTTP and through recovery.
static void magicTableCounters() {
  using namespace TurnHub;
  enterEmptyLobby(); fixtureRadio=false;
  String aId,bId,cId;
  const String a=registerPhone("Table one",aId), b=registerPhone("Table two",bId), c=registerPhone("Table three",cId);
  for (const String *token : {&a,&b,&c}) assert(request("/api/session/join",*token)==200);
  auto counter = [&](const String &token, const char *kind, int value, const char *target="") {
    return request("/api/control/counter",token,{{"counter",kind},{"delta",String(value)},{"value",String(value)},{"kind",target}});
  };
  assert(request("/api/game/settings",a,{{"gameProfile","mtg"},{"startingLife","20"},{"twoHeadedGiant","0"},
      {"autoEliminateAtZeroLife","1"},{"houseRules","1"}})==200);
  assert(request("/api/control/start",a)==200); testNow+=3000; updateCountdown(testNow);
  auto &game = table().game;
  const uint8_t first = game.activePlayerNumber();
  const String *tokens[] = {&a,&b,&c};
  const String &activeToken = *tokens[first-1];
  const uint8_t next = first % 3 + 1;
  const String &nextToken = *tokens[next-1];
  // Treasure and rad are ordinary per-player counters.
  assert(counter(a,"treasure",3)==200 && game.countersForPlayer(1)->treasure==3);
  assert(counter(a,"treasure",-4)==409 && counter(a,"rad",0)==400);
  assert(counter(nextToken,"rad",4)==200 && game.countersForPlayer(next)->rad==4 && !game.radDue(next));
  // The state carries them, with day/night and the turn order.
  assert(request("/api/v1/state",a,{},HTTP_GET)==200 && server.body.find("\"treasure\":3")!=std::string::npos &&
      server.body.find("\"dayNight\":\"NEITHER\"")!=std::string::npos && server.body.find("\"turnOrderReversed\":false")!=std::string::npos);
  // No rad trigger until that player's turn begins.
  assert(counter(nextToken,"rad-resolve",1)==409);
  assert(request("/api/control/pass",activeToken)==200); testNow+=PASS_GRACE_MS+1; updatePendingPass(testNow);
  assert(game.activePlayerNumber()==next && game.radDue(next));
  assert(request("/api/v1/state",a,{},HTTP_GET)==200 && server.body.find("\"radDue\":true")!=std::string::npos);
  assert(counter(nextToken,"rad-resolve",5)==409);  // More than their rad.
  const int32_t life = game.lifeTotal(next);
  assert(counter(nextToken,"rad-resolve",3)==200 && game.lifeTotal(next)==life-3 && game.countersForPlayer(next)->rad==1 && !game.radDue(next));
  assert(counter(nextToken,"rad-resolve",0)==409);  // Answered once.
  // Day and night: a change only, and back to neither as a correction.
  assert(counter(a,"day-night",1)==200 && game.dayNight()==DayNight::Day && counter(b,"day-night",1)==409);
  assert(counter(b,"day-night",2)==200 && counter(b,"day-night",3)==400 && game.dayNight()==DayNight::Night);
  // Reversed turns go counterclockwise from the active player.
  assert(counter(c,"turn-order",1)==200 && game.turnOrderReversed() && counter(c,"turn-order",1)==409);
  const uint8_t before = game.activePlayerNumber();
  assert(request("/api/control/pass",*tokens[before-1])==200); testNow+=PASS_GRACE_MS+1; updatePendingPass(testNow);
  assert(game.activePlayerNumber()==(before+1)%3+1);
  // Everyone's counters: one each for every living player, each within its own bounds.
  assert(counter(a,"everyone",1,"treasure")==200 && game.countersForPlayer(1)->treasure==4 && game.countersForPlayer(2)->treasure==1 && game.countersForPlayer(3)->treasure==1);
  assert(counter(a,"everyone",2,"treasure")==400 && counter(a,"everyone",1,"initiative")==400);
  assert(counter(a,"everyone",-1,"rad")==200 && game.countersForPlayer(next)->rad==0);
  assert(counter(a,"everyone",-1,"rad")==409);  // Nobody had any left.
  // Recovery keeps the table state and the new counters.
  assert(request("/api/control/pause",a)==200);
  GameCheckpoint saved; game.checkpoint(saved,testNow);
  uint8_t bytes[GAME_CHECKPOINT_CAPACITY]; const size_t size=encodeCheckpoint(saved,bytes,sizeof(bytes));
  assert(size && decodeCheckpoint(bytes,size,saved)==TurnHubStorage::Status::Ok);
  GameEngine restored; assert(restored.restoreCheckpoint(saved,testNow));
  assert(restored.dayNight()==DayNight::Night && restored.turnOrderReversed() && restored.countersForPlayer(1)->treasure==4);
  saved.radDue=1u<<5; assert(!validCheckpoint(saved)); saved.radDue=0;
  saved.dayNight=DayNight::Count; assert(!validCheckpoint(saved));
  // A new match starts clockwise, in neither day nor night.
  enterEmptyLobby();
  assert(!game.turnOrderReversed() && game.dayNight()==DayNight::Neither);
  for (const String *token : {&a,&b,&c}) assert(request("/api/session/logout",*token)==200);
  for (const String *id : {&aId,&bId,&cId}) { ProfileFixture::profiles.erase(id->c_str()); TurnHubAccounts::accounts.erase(id->c_str()); }
  table().nextGameSettings=GameSettings{};
}

static void magicMonarch() {
  using namespace TurnHub;
  enterEmptyLobby(); fixtureRadio=false;
  String aId,bId,cId;
  const String a=registerPhone("Monarch one",aId), b=registerPhone("Monarch two",bId), c=registerPhone("Monarch three",cId);
  for (const String *token : {&a,&b,&c}) assert(request("/api/session/join",*token)==200);
  assert(request("/api/control/monarch",a)==409); // Lobby cannot hold the crown.
  assert(request("/api/game/settings",a,{{"gameProfile","mtg"},{"startingLife","20"},{"twoHeadedGiant","0"},{"autoEliminateAtZeroLife","1"}})==200);
  assert(request("/api/control/start",a)==200); testNow+=3000; updateCountdown(testNow);
  assert(request("/api/control/monarch","")==401);
  observeClientState(); const uint32_t before=table().clientState.revision();
  assert(request("/api/control/monarch",a)==200 && table().game.monarchPlayerNumber()==1);
  assert(table().clientState.revision()>before);
  assert(request("/api/control/monarch",b)==200 && table().game.monarchPlayerNumber()==2);
  assert(request("/api/v1/state",b,{},HTTP_GET)==200 && server.body.find("\"monarchPlayer\":2")!=std::string::npos);
  assert(request("/api/control/pause",a)==200);
  assert(request("/api/control/monarch",c)==200 && table().game.monarchPlayerNumber()==3);
  GameCheckpoint saved; table().game.checkpoint(saved,testNow);
  uint8_t bytes[GAME_CHECKPOINT_CAPACITY]; const size_t size=encodeCheckpoint(saved,bytes,sizeof(bytes));
  assert(size && decodeCheckpoint(bytes,size,saved)==TurnHubStorage::Status::Ok && saved.monarch==3);
  GameEngine restored; assert(restored.restoreCheckpoint(saved,testNow) && restored.monarchPlayerNumber()==3);
  saved.monarch=4; assert(!validCheckpoint(saved)); saved.monarch=3;
  // CR 724.4: the crown passes to the active player, or the next in turn
  // order when the monarch is the active player.
  const uint8_t heir = table().game.activePlayerNumber()==3 ? 1 : table().game.activePlayerNumber();
  assert(request("/api/control/concede",c)==200 && table().game.monarchPlayerNumber()==heir);
  assert(request("/api/control/monarch",c)==409);
  assert(request("/api/control/monarch",a)==200);
  assert(request("/api/control/life",a,{{"delta","-20"}})==200 && table().game.monarchPlayerNumber()==2);
  assert(request("/api/control/monarch",b)==409); // Finished game.
  enterEmptyLobby();
  for (const String *token : {&a,&b,&c}) assert(request("/api/session/logout",*token)==200);
  for (const String *id : {&aId,&bId,&cId}) {
    ProfileFixture::profiles.erase(id->c_str()); TurnHubAccounts::accounts.erase(id->c_str());
  }
  PlayerSeat seats[4]; for (uint8_t i=0;i<4;++i) { seats[i].playerNumber=i+1; seats[i].controllerId=i; seats[i].slot=1; }
  GameEngine engine; GameSettings settings; settings.houseRules=true;
  for (GameProfile profile : {GameProfile::Generic, GameProfile::Yugioh}) {
    settings.profile=profile; assert(engine.start(seats,4,seats[0],testNow,settings)); assert(!engine.claimMonarch(1));
  }
  settings.profile=GameProfile::Commander; settings.twoHeadedGiant=true;
  assert(engine.start(seats,4,seats[0],testNow,settings));
  assert(engine.claimMonarch(1) && engine.claimMonarch(2) && engine.monarchPlayerNumber()==2);
  bool finished=false; assert(engine.pause(testNow) && engine.eliminatePlayer(1,testNow,finished));
  assert(engine.monarchPlayerNumber()==3); // The leaving team's crown goes to the next team's active player.
  assert(engine.start(seats,4,seats[0],testNow,settings) && engine.monarchPlayerNumber()==0);
  assert(engine.beginWinClaim(1,false,testNow) && !engine.claimMonarch(3)); // Not while a win claim waits.
}

static void zeroLifeAndExitReasons() {
  using namespace TurnHub;
  using TurnHubProfiles::LastGameResult;
  enterEmptyLobby(); fixtureRadio=false;
  String aId,bId,cId;
  const String a=registerPhone("Zero one",aId), b=registerPhone("Zero two",bId), c=registerPhone("Zero three",cId);
  for (const String *token : {&a,&b,&c}) assert(request("/api/session/join",*token)==200);
  assert(request("/api/game/settings",a,{{"gameProfile","mtg_commander"},{"startingLife","40"},{"autoEliminateAtZeroLife","1"},{"twoHeadedGiant","0"}})==200);
  assert(request("/api/control/start",a)==200); testNow+=3000; updateCountdown(testNow);
  assert(table().game.settings().autoEliminateAtZeroLife);
  assert(request("/api/game/settings",a,{{"autoEliminateAtZeroLife","no"}})==400);
  assert(request("/api/game/settings",a,{{"startingLife","20"}})==409);
  assert(request("/api/game/settings",a,{{"autoEliminateAtZeroLife","0"}})==200);
  assert(request("/api/control/life",a,{{"delta","-40"}})==200 && !table().game.isEliminated(1));
  assert(request("/api/control/life",b,{{"delta","-41"}})==200 && !table().game.isEliminated(2));
  assert(request("/api/control/pause",c)==200);
  assert(request("/api/game/settings",c,{{"autoEliminateAtZeroLife","1"}})==200);
  assert(table().hubState==HubState::GameOver && table().game.winnerPlayerNumber()==3);
  assert(table().game.isEliminated(1) && table().game.isEliminated(2) && !table().game.hasConceded(1));
  assert(ProfileFixture::profiles[aId.c_str()].stats.lastGameResult==LastGameResult::Eliminated);
  assert(request("/api/control/rematch",c)==200);
  assert(request("/api/control/start",c)==200); testNow+=3000; updateCountdown(testNow);
  assert(request("/api/control/concede",a)==200);
  assert(table().game.hasConceded(1) && table().hubState==HubState::Running);
  assert(request("/api/control/pass",b)==200 && table().pendingPass.active);
  assert(request("/api/control/eliminate",b,{{"eliminatedBy","2"}})==409 && table().pendingPass.active);
  assert(request("/api/control/eliminate",b,{{"eliminatedBy","17"}})==400);
  assert(request("/api/control/eliminate",b,{{"eliminatedBy","3"}})==200 && !table().game.hasConceded(2));
  assert(table().game.eliminatedBy(2)==3 && ProfileFixture::profiles[cId.c_str()].stats.lastGameEliminationsMade==1);
  assert(request("/api/session/stats",c,{},HTTP_GET)==200 && server.body.find("\"eliminationsMade\":1")!=std::string::npos);
  assert(ProfileFixture::profiles[aId.c_str()].stats.lastGameResult==LastGameResult::Conceded);
  assert(ProfileFixture::profiles[aId.c_str()].stats.gamesConceded==1);
  assert(ProfileFixture::profiles[bId.c_str()].stats.lastGameResult==LastGameResult::Eliminated);
  GameCheckpoint saved; table().game.checkpoint(saved,testNow);
  uint8_t bytes[GAME_CHECKPOINT_CAPACITY]; const size_t size=encodeCheckpoint(saved,bytes,sizeof(bytes));
  assert(size && decodeCheckpoint(bytes,size,saved)==TurnHubStorage::Status::Ok);
  GameEngine restored; assert(restored.restoreCheckpoint(saved,testNow));
  assert(restored.hasConceded(1) && !restored.hasConceded(2) && restored.settings().autoEliminateAtZeroLife);

  // Resolve an approved lethal request, and Commander life loss, through real HTTP.
  assert(request("/api/control/rematch",c)==200);
  assert(request("/api/control/start",c)==200); testNow+=3000; updateCountdown(testNow);
  assert(request("/api/control/life/request",a,{{"target","2"},{"delta","-40"}})==200);
  const uint32_t id=table().game.lifeChangeFor(2)->id;
  assert(request("/api/control/life/respond",b,{{"requestId",String(id)},{"accept","1"}})==200);
  assert(table().game.isEliminated(2) && table().hubState==HubState::Running);
  assert(request("/api/control/commander",a,{{"source","3"},{"commander","1"},{"delta","40"}})==200);
  assert(table().hubState==HubState::GameOver && table().game.winnerPlayerNumber()==3);

  // A simultaneous check must not crown a player who also has nonpositive life.
  PlayerSeat players[4]; for (uint8_t i=0;i<4;++i) { players[i].playerNumber=i+1; players[i].controllerId=i; players[i].slot=1; }
  GameEngine engine; GameSettings settings; settings.houseRules=true; settings.startingLife=20; settings.autoEliminateAtZeroLife=false;
  assert(engine.start(players,3,players[0],testNow,settings));
  for (uint8_t i=1;i<=3;++i) assert(engine.changeLife(i,-20));
  engine.setAutoEliminateAtZeroLife(true); assert(engine.resolveEliminationRules(testNow));
  assert(engine.endedInDraw()); engine.checkpoint(saved,testNow); assert(validCheckpoint(saved));
  settings.profile=GameProfile::Magic; settings.twoHeadedGiant=true; settings.autoEliminateAtZeroLife=true;
  assert(engine.start(players,4,players[0],testNow,settings));
  assert(engine.changeLife(2,-20) && engine.resolveEliminationRules(testNow));
  assert(engine.isEliminated(1) && engine.isEliminated(2) && engine.isWinner(3) && engine.isWinner(4));
  enterEmptyLobby();
  for (const String *token : {&a,&b,&c}) assert(request("/api/session/logout",*token)==200);
  for (const String *id : {&aId,&bId,&cId}) {
    ProfileFixture::profiles.erase(id->c_str()); TurnHubAccounts::accounts.erase(id->c_str());
  }
}

// --- Star Commander -------------------------------------------------------------------------

// Seat geometry: for player p (i = p - 1) the neighbours (i+1, i+4) are allies
// and the two across the table (i+2, i+3) are enemies.
static void starCommanderRelationships() {
  using namespace TurnHub;
  const uint8_t expectedAllies[5][2] = {{2,5},{3,1},{4,2},{5,3},{1,4}};
  const uint8_t expectedEnemies[5][2] = {{3,4},{4,5},{5,1},{1,2},{2,3}};
  for (uint8_t p = 1; p <= STAR_PLAYERS; ++p) {
    uint8_t allies[2] = {}, enemies[2] = {};
    assert(starAllies(p,allies) && starEnemies(p,enemies));
    assert(allies[0]==expectedAllies[p-1][0] && allies[1]==expectedAllies[p-1][1]);
    assert(enemies[0]==expectedEnemies[p-1][0] && enemies[1]==expectedEnemies[p-1][1]);
    assert(!starAllied(p,p) && !starEnemy(p,p));
    for (uint8_t q = 1; q <= STAR_PLAYERS; ++q) {
      if (q == p) continue;
      assert(starAllied(p,q) != starEnemy(p,q));  // Every other seat is exactly one.
      assert(starAllied(p,q) == starAllied(q,p) && starEnemy(p,q) == starEnemy(q,p));
    }
  }
  uint8_t out[2] = {};
  assert(!starAllies(0,out) && !starEnemies(6,out) && !starAllied(1,6));
  GameProfile parsed = GameProfile::Generic;
  assert(parseGameProfile("mtg_star_commander",parsed) && parsed==GameProfile::StarCommander);
  assert(!strcmp(gameProfileKey(GameProfile::StarCommander),"mtg_star_commander"));
  assert(commanderRules(GameProfile::StarCommander) && magicRules(GameProfile::StarCommander));
  assert(!teamsAllowed(GameProfile::StarCommander) && !commanderRules(GameProfile::Magic));
}

static void starCommanderEngine() {
  using namespace TurnHub;
  PlayerSeat seats[6];
  for (uint8_t i = 0; i < 6; ++i) { seats[i].playerNumber = i + 1; seats[i].controllerId = i; seats[i].slot = 1; }
  GameSettings star; star.profile = GameProfile::StarCommander; star.startingLife = STAR_DEFAULT_LIFE;
  GameEngine engine;
  // Exactly five players, and never Two-Headed Giant.
  assert(validGameSettings(star) && !validStarTable(star,4) && validStarTable(star,5));
  assert(!engine.start(seats,4,seats[0],1000,star) && !engine.start(seats,6,seats[0],1000,star));
  GameSettings teams = star; teams.twoHeadedGiant = true;
  assert(!validGameSettings(teams) && !engine.start(seats,5,seats[0],1000,teams));
  GameSettings read; readProfileByte(profileByte(star),read);
  assert(read.profile==GameProfile::StarCommander && !read.twoHeadedGiant);
  // Normal Commander with five players has no relationships.
  GameSettings commander = star; commander.profile = GameProfile::Commander;
  assert(engine.start(seats,5,seats[0],1000,commander) && !engine.starCommander() && !engine.starAllies(1,2));
  assert(engine.changeCommanderDamage(1,2,1,1));

  assert(engine.start(seats,5,seats[0],1000,star) && engine.starCommander() && engine.lifeTotal(5)==40);
  // Allies never deal each other Commander damage; enemies do.
  for (uint8_t to = 1; to <= 5; ++to)
    for (uint8_t from = 1; from <= 5; ++from) {
      if (to == from) continue;
      assert(engine.starAllies(to,from) == starAllied(to,from));
      assert(engine.changeCommanderDamage(to,from,1,1) == !starAllied(to,from));
    }
  for (uint8_t p = 1; p <= 5; ++p) assert(engine.lifeTotal(p)==38);
  assert(engine.setPartner(1,true) && !engine.recordCommanderHit(1,2,1,3) && !engine.recordCommanderHit(1,5,2,3));
  assert(engine.recordCommanderHit(1,3,1,3) && engine.undoCommanderHit(1) && engine.lifeTotal(1)==38);
  // Ordinary life changes are never restricted, also between allies.
  assert(engine.changeLife(2,-5) && engine.requestLifeChange(1,2,-1,1000));
  assert(engine.respondLifeChange(2,engine.lifeChangeFor(2)->id,true,1100) && engine.lifeTotal(2)==32);
  assert(engine.changePlayerCounter(3,CounterKind::CommanderCast,1) && engine.claimMonarch(4));

  // One enemy out is no victory; the second is, whoever removed them. A
  // concession counts like any elimination.
  bool finished = false;
  int before = completedGames;
  assert(engine.pause(2000) && engine.eliminatePlayer(3,2000,finished) && !finished);
  assert(engine.starEnemiesEliminated(1)==1 && !engine.starVictoryPending(1) && !engine.gameOver());
  assert(engine.eliminatePlayer(4,2000,finished,true) && finished);
  assert(engine.gameOver() && engine.winnerPlayerNumber()==1 && engine.winnerMask()==1u && !engine.endedInDraw());
  assert(engine.isWinner(1) && !engine.isWinner(2) && !engine.isWinner(5) && completedGames==before+1);

  // The whole batch first: 4 is out, then 3 and 5 fall together. Allies 1
  // (enemies 3, 4) and 2 (enemies 4, 5) share the victory; it is no draw.
  assert(engine.start(seats,5,seats[0],3000,star));
  assert(engine.pause(3000) && engine.eliminatePlayer(4,3000,finished) && !finished && engine.resume(3000));
  // Rules apply as each change lands; holding them shows a batch landing together.
  engine.setAutoEliminateAtZeroLife(false);
  assert(engine.changeLife(3,-40) && engine.changeLife(5,-40));
  engine.setAutoEliminateAtZeroLife(true);
  before = completedGames;
  assert(engine.resolveEliminationRules(3100) && engine.gameOver() && completedGames==before+1);
  assert(engine.isWinner(1) && engine.isWinner(2) && !engine.isWinner(3) && !engine.isWinner(4) && !engine.isWinner(5));
  assert(engine.winnerPlayerNumber()==1 && engine.winnerMask()==3u && !engine.endedInDraw());
  uint8_t winners[MAX_PLAYERS] = {};
  assert(engine.winnerPlayers(winners,MAX_PLAYERS)==2 && winners[0]==1 && winners[1]==2);
  for (uint8_t id = 0; id < 5; ++id) {
    const SigilLedState cue = selectSigilLedState(id,HubState::GameOver,table().lobby,engine,0,0,0,3200);
    assert(cue.has(LedOverlay::Winner) == (id < 2));
  }
  // A finished shared victory survives the checkpoint, without replaying completion.
  GameCheckpoint saved; engine.checkpoint(saved,3200);
  uint8_t bytes[GAME_CHECKPOINT_CAPACITY];
  size_t size = encodeCheckpoint(saved,bytes,sizeof(bytes));
  GameCheckpoint decoded;
  assert(size && decodeCheckpoint(bytes,size,decoded)==TurnHubStorage::Status::Ok && decoded.winners==3);
  GameEngine restored; before = completedGames;
  assert(restored.restoreCheckpoint(decoded,9000) && restored.gameOver() && completedGames==before);
  assert(restored.isWinner(1) && restored.isWinner(2) && restored.settings().profile==GameProfile::StarCommander);
  decoded.winners = 1 | 4; assert(!validCheckpoint(decoded));  // Player 3 is out.
  decoded.winners = 2; assert(!validCheckpoint(decoded));      // Must include the first winner.
  decoded.winners = 3; decoded.settings.profile = GameProfile::Commander;
  assert(!validCheckpoint(decoded));                           // Only Star Commander shares.

  // An interrupted Star match restores paused, with its profile and seats.
  assert(engine.start(seats,5,seats[2],4000,star) && engine.changeCommanderDamage(1,3,1,7));
  engine.checkpoint(saved,4500);
  size = encodeCheckpoint(saved,bytes,sizeof(bytes));
  assert(size && decodeCheckpoint(bytes,size,decoded)==TurnHubStorage::Status::Ok);
  assert(restored.restoreCheckpoint(decoded,9000) && restored.paused() && restored.starCommander());
  assert(restored.activePlayerNumber()==3 && restored.commanderDamage(1,3,1)==7 && restored.lifeTotal(1)==33);
  assert(!restored.changeCommanderDamage(1,2,1,1) && restored.changeCommanderDamage(1,4,1,1));
  decoded.damage[0][1][0] = 1; assert(!validCheckpoint(decoded));  // Ally damage is impossible.
  decoded.damage[0][1][0] = 0; decoded.count = 4; assert(!validCheckpoint(decoded));

  // An eliminated player never wins, and nobody qualifying keeps play going.
  assert(engine.start(seats,5,seats[0],5000,star));
  assert(engine.pause(5000) && engine.eliminatePlayer(1,5000,finished) && !finished && engine.resume(5000));
  assert(engine.changeLife(3,-40) && engine.changeLife(4,-40) && engine.resolveEliminationRules(5100));
  assert(!engine.gameOver() && engine.running() && !engine.paused() && engine.livingPlayerCount()==2);
  assert(!engine.starVictoryPending(1) && !engine.isWinner(1));
  // A batch leaving nobody, and so no winner, is a draw.
  engine.setAutoEliminateAtZeroLife(false);
  assert(engine.changeLife(2,-40) && engine.changeLife(5,-40));
  engine.setAutoEliminateAtZeroLife(true);
  assert(engine.resolveEliminationRules(5200));
  assert(engine.gameOver() && engine.endedInDraw() && engine.winnerMask()==0 && !engine.isWinner(1));

  // Poison and Commander damage feed the same victory policy.
  assert(engine.start(seats,5,seats[0],6000,star));
  assert(engine.changePlayerCounter(3,CounterKind::Poison,10) && engine.changeCommanderDamage(4,1,1,21));
  assert(engine.resolveEliminationRules(6100) && engine.gameOver() && engine.winnerMask()==1u);

  // An alternate win claim keeps its single-claimant confirmation workflow.
  assert(engine.start(seats,5,seats[0],7000,star) && engine.beginWinClaim(2,true,7000));
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t next = engine.nextWinConfirmationPlayerNumber();
    assert(next && next != 2 && engine.confirmWinClaim(next,7100,finished));
  }
  assert(finished && engine.winnerMask()==2u && engine.isWinner(2) && !engine.isWinner(1) && !engine.isWinner(3));
  engine.reset(); assert(engine.winnerMask()==0 && !engine.starCommander());
}

// Sigils at a Star Commander table: five seats, relationships from the
// lobby order (fixed in play), the Commander picker and refused ally hits.
static void starCommanderSigils() {
  using namespace TurnHub;
  using namespace TurnHubProtocol;
  using A = SigilAction;
  resetCommanderPickers();
  freshLobby(4);
  table().nextGameSettings.profile = GameProfile::StarCommander;
  table().nextGameSettings.startingLife = STAR_DEFAULT_LIFE;
  const IntentResult four = dispatchModuleIntent(IntentType::StartGame,0);
  assert(!four.accepted() && !strcmp(four.message,"Star Commander needs exactly five players."));
  assert(table().hubState==HubState::Lobby);
  choose(4,A::Join); assert(table().lobby.playerCount()==5);
  // Moving a seat in the lobby changes its relationships: Sigil 4 becomes player 4.
  assert(table().lobby.playerNumber(4,1)==5);
  assert(web(4,1,WebControl::MoveEarlier) && table().lobby.playerNumber(4,1)==4 && table().lobby.playerNumber(3,1)==5);
  startFromHost();
  assert(table().game.starCommander() && table().game.playerByNumber(4)->controllerId==4);
  // Fixed once the game starts.
  assert(!web(4,1,WebControl::MoveLater) && table().game.playerByNumber(5)->controllerId==3);
  assert(table().game.starAllies(4,5) && table().game.starAllies(4,3) && !table().game.starAllies(4,1));
  // The Sigil Commander picker offers only enemies (player 1: 3 and 4).
  leds.render(table().hubState,table().lobby,table().game,0,0,0,testNow);
  syncSigilMenus(testNow);
  assert(sigilMenuFor(0).actions & sigilActionBit(A::CommanderDamage));
  handleSelectAction(0,encodeSelectAction(A::CommanderDamage,sigilMenuRevision(0),1));
  auto key = [&](uint8_t k) { handleCommanderKey(0,encodeCommanderKey(k,commanderPage(0).revision),testNow); };
  assert(commanderPage(0).stage==CommanderStage::Source && commanderPage(0).source==3);
  key(3); assert(commanderPage(0).source==4);
  key(3); assert(commanderPage(0).source==3);
  key(2); assert(commanderPage(0).source==4);
  key(1); assert(commanderPage(0).stage==CommanderStage::Closed);
  // An ally hit is refused by the handler for every adapter.
  Intent hit; hit.type = IntentType::RecordCommanderHit; hit.actor.origin = IntentOrigin::PhysicalSigil;
  hit.actor.controllerId = 0; hit.actor.slot = 1; hit.actor.playerNumber = 1;
  hit.payload.targetPlayer = 1; hit.payload.counterSource = 2; hit.payload.counterSlot = 1; hit.payload.value = 3;
  IntentResult result = intents.dispatch(hit);
  assert(result.status==IntentStatus::Conflict &&
      !strcmp(result.message,"Allies can't deal Commander damage to each other in Star Commander."));
  hit.payload.counterSource = 3; assert(intents.dispatch(hit).accepted() && table().game.lifeTotal(1)==37);
  // Enemies 3 (Sigil 2) and 4 (Sigil 4) concede: player 1 wins, not by last standing.
  assert(dispatchSeatIntent(IntentType::Concede,IntentOrigin::PhysicalSigil,player(3)).accepted());
  assert(table().hubState==HubState::Running);
  assert(dispatchSeatIntent(IntentType::Concede,IntentOrigin::PhysicalSigil,player(4)).accepted());
  assert(table().hubState==HubState::GameOver && table().game.livingPlayerCount()==3);
  assert(table().game.isWinner(1) && !table().game.isWinner(2) && !table().game.isWinner(5));
  enterEmptyLobby();
  table().nextGameSettings = GameSettings{};
}

// Two Atlas games stay separate: Star Commander in Game 1, Commander in Game 2.
static void starCommanderVenue() {
  using namespace TurnHub;
  using TurnHubProtocol::SigilAction;
  registerWebCallbacks();
  freshLobby(5);
  choose(5,SigilAction::SwitchTable); choose(6,SigilAction::SwitchTable);
  choose(5,SigilAction::Join); choose(6,SigilAction::Join);
  assert(tables[1].lobby.playerCount()==2 && tables[0].lobby.playerCount()==5);
  tables[0].nextGameSettings.profile = GameProfile::StarCommander;
  tables[1].nextGameSettings.profile = GameProfile::Commander;
  for (uint8_t index : {0, 1}) {
    choose(index ? 5 : 0,SigilAction::StartGame);
    TableScope scope(index);
    testNow += 3000; updateCountdown(testNow); assert(table().hubState==HubState::Running);
  }
  assert(tables[0].game.starCommander() && tables[1].game.settings().profile==GameProfile::Commander);
  auto hit = [](uint8_t sigil, uint8_t recipient, uint8_t source) {
    Intent intent; intent.type = IntentType::RecordCommanderHit; intent.actor.origin = IntentOrigin::PhysicalSigil;
    intent.actor.controllerId = sigil; intent.actor.slot = 1; intent.actor.playerNumber = recipient;
    intent.payload.targetPlayer = recipient; intent.payload.counterSource = source;
    intent.payload.counterSlot = 1; intent.payload.value = 2;
    TableScope scope(tableForController(sigil));
    return intents.dispatch(intent).accepted();
  };
  assert(!hit(1,2,1) && hit(1,2,4));         // Game 1: allies refused, enemies counted.
  assert(hit(6,2,1) && !tables[1].game.starCommander());  // Game 2: a neighbour is just an opponent.
  assert(tables[1].game.commanderDamage(2,1,1)==2 && tables[0].game.commanderDamage(2,1,1)==0);
  { TableScope scope(0); enterEmptyLobby(); }
  { TableScope scope(1); enterEmptyLobby(); }
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) sigilTable[id] = 0;
  tables[0].nextGameSettings = GameSettings{}; tables[1].nextGameSettings = GameSettings{};
}

// Phones at a Star Commander table: settings, start, the 409 reasons, a
// shared victory after one batch, state, statistics and rematch.
static void starCommanderTable() {
  using namespace TurnHub;
  using TurnHubProfiles::LastGameResult;
  enterEmptyLobby(); fixtureRadio = false; testNow = 1000;
  registerWebCallbacks();
  String ids[5], tokens[5];
  const char *names[5] = {"Star One","Star Two","Star Three","Star Four","Star Five"};
  for (int i = 0; i < 5; ++i) tokens[i] = registerPhone(names[i],ids[i]);
  for (int i = 0; i < 4; ++i) assert(request("/api/session/join",tokens[i])==200);
  // Star Commander never allows Two-Headed Giant; choosing it turns teams off.
  assert(request("/api/game/settings",tokens[0],{{"gameProfile","mtg"},{"twoHeadedGiant","1"}})==200);
  assert(request("/api/game/settings",tokens[0],{{"gameProfile","mtg_star_commander"},{"twoHeadedGiant","1"}})==409);
  assert(server.body.find("Two-Headed Giant isn't available in Star Commander")!=std::string::npos);
  assert(table().nextGameSettings.profile==GameProfile::Magic);
  assert(request("/api/game/settings",tokens[0],
      {{"gameProfile","mtg_star_commander"},{"startingLife","40"},{"autoEliminateAtZeroLife","0"}})==200);
  assert(table().nextGameSettings.profile==GameProfile::StarCommander && !table().nextGameSettings.twoHeadedGiant);
  assert(request("/api/game/settings",tokens[0],{},HTTP_GET)==200 &&
      server.body.find("\"gameProfile\":\"mtg_star_commander\"")!=std::string::npos);
  assert(request("/api/control/start",tokens[0])==409);
  assert(server.body.find("Star Commander needs exactly five players.")!=std::string::npos);
  assert(request("/api/session/join",tokens[4])==200);
  assert(request("/api/control/start",tokens[0])==200);
  testNow += 3000; updateCountdown(testNow);
  assert(table().hubState==HubState::Running && table().game.starCommander() && table().game.lifeTotal(5)==40);
  String seatProfile[6];
  for (uint8_t p = 1; p <= 5; ++p) seatProfile[p] = table().game.playerByNumber(p)->profileId;
  auto token = [&](uint8_t p) {
    for (int i = 0; i < 5; ++i) if (ids[i] == seatProfile[p]) return tokens[i];
    assert(false); return String();
  };
  auto statsOf = [&](uint8_t p) { return ProfileFixture::profiles[seatProfile[p].c_str()].stats; };
  TurnHubProfiles::ProfileStats before[6];
  for (uint8_t p = 1; p <= 5; ++p) before[p] = statsOf(p);
  // Allied Commander damage is refused with its reason; enemy damage and any
  // life change work.
  assert(request("/api/control/commander",token(1),{{"source","2"},{"commander","1"},{"delta","3"}})==409);
  assert(server.body.find("Allies can't deal Commander damage to each other in Star Commander.")!=std::string::npos);
  assert(table().game.commanderDamage(1,2,1)==0 && table().game.lifeTotal(1)==40);
  assert(request("/api/control/commander",token(1),{{"source","3"},{"commander","1"},{"delta","3"}})==200);
  assert(table().game.lifeTotal(1)==37);
  // Player 4 concedes: no one has both enemies out yet.
  assert(request("/api/control/concede",token(4))==200 && table().hubState==HubState::Running);
  // A blanket effect takes 3 and 5 (an ally of 1) to zero; zero-life
  // elimination is off, so they stay in until it is turned on.
  assert(request("/api/control/life",token(3),{{"delta","-40"}})==200);
  assert(request("/api/control/life",token(5),{{"delta","-40"}})==200);
  assert(!table().game.isEliminated(3) && !table().game.isEliminated(5));
  // One batch eliminates both: allies 1 and 2 share the victory.
  assert(request("/api/game/settings",token(1),{{"autoEliminateAtZeroLife","1"}})==200);
  assert(table().hubState==HubState::GameOver && !table().game.endedInDraw());
  assert(table().game.isWinner(1) && table().game.isWinner(2) && table().game.winnerPlayerNumber()==1);
  assert(request("/api/v1/state",token(1),{},HTTP_GET)==200);
  assert(server.body.find("\"winnerPlayer\":1,\"winnerPlayers\":[1,2]")!=std::string::npos);
  assert(server.body.find("\"profile\":\"mtg_star_commander\"")!=std::string::npos);
  saveClientFixture("star-commander-game-over",server.body);
  // Every winner is credited, and Star Commander games are counted apart.
  for (uint8_t p = 1; p <= 5; ++p) {
    const auto after = statsOf(p);
    const bool won = p <= 2;
    assert(after.gamesPlayed==before[p].gamesPlayed+1 && after.gamesWon==before[p].gamesWon+(won ? 1 : 0));
    assert(after.starGamesPlayed==before[p].starGamesPlayed+1 && after.starGamesWon==before[p].starGamesWon+(won ? 1 : 0));
    assert(after.lastGameProfile==static_cast<uint8_t>(GameProfile::StarCommander));
    assert(after.lastGameResult==(won ? LastGameResult::Win : p==4 ? LastGameResult::Conceded : LastGameResult::Eliminated));
  }
  assert(request("/api/session/stats",token(2),{},HTTP_GET)==200);
  assert(server.body.find("\"starGamesPlayed\":1,\"starGamesWon\":1")!=std::string::npos);
  assert(server.body.find("\"lastGameType\":\"mtg_star_commander\"")!=std::string::npos);
  assert(TurnHubProfileStats::buildTextReport("x","y",statsOf(2)).find("Star Commander games won: 1")!=std::string::npos);
  // Rematch keeps the profile and the seats; life, eliminations and the
  // victory start over.
  assert(request("/api/control/rematch",token(1))==200);
  assert(request("/api/control/start",token(1))==200);
  testNow += 3000; updateCountdown(testNow);
  assert(table().hubState==HubState::Running && table().game.starCommander() && table().game.winnerMask()==0);
  for (uint8_t p = 1; p <= 5; ++p) {
    assert(String(table().game.playerByNumber(p)->profileId)==seatProfile[p]);
    assert(table().game.lifeTotal(p)==40 && !table().game.isEliminated(p) && !table().game.isWinner(p));
  }
  assert(request("/api/v1/state",token(1),{},HTTP_GET)==200 && server.body.find("\"winnerPlayers\":[]")!=std::string::npos);
  enterEmptyLobby();
  table().nextGameSettings = GameSettings{};
  for (int i = 0; i < 5; ++i) assert(request("/api/session/logout",tokens[i])==200);
  for (const String &id : ids) { ProfileFixture::profiles.erase(id.c_str()); TurnHubAccounts::accounts.erase(id.c_str()); }
}

// A standalone Star Commander record credits every listed winner.
static void starStandaloneImport() {
  using TurnHubProfiles::LastGameResult;
  MemoryBlobs blobs; TurnHubStandalone::useImportStore(&blobs);
  enterEmptyLobby(); TurnHub::fixtureRadio = false; testNow = 1000;
  String ids[5], tokens[5];
  const char *names[5] = {"Import A","Import B","Import C","Import D","Import E"};
  for (int i = 0; i < 5; ++i) tokens[i] = registerPhone(names[i],ids[i]);
  std::map<std::string,String> record = {
    {"recordId","5a7c0c3e-1111-4222-8333-444455556666"},{"atlasId",TurnHubWebApi::internal::atlasHardwareId()},
    {"gameProfile","mtg_star_commander"},{"durationMs","3600000"},{"players","5"},{"starter","0"},
    {"winner","0"},{"winners","0,1"},
  };
  for (int i = 0; i < 5; ++i) {
    const String n(i);
    record[("name"+n).c_str()] = names[i]; record[("profile"+n).c_str()] = ids[i];
    record[("turns"+n).c_str()] = "8"; record[("turnMs"+n).c_str()] = "800000";
    record[("fastest"+n).c_str()] = "20000"; record[("longest"+n).c_str()] = "200000";
    record[("out"+n).c_str()] = i >= 2 ? String(i - 1) : String("0");
  }
  TurnHubProfiles::ProfileStats before[5];
  for (int i = 0; i < 5; ++i) before[i] = ProfileFixture::profiles[ids[i].c_str()].stats;
  for (const char *bad : {"1,2", "0,5", "0,0", "0,x", ",", "1"}) {
    auto invalid = record; invalid["winners"] = bad;
    assert(request("/api/standalone/import",tokens[0],invalid)==400);
  }
  assert(blobs.blobs.empty());
  assert(request("/api/standalone/import",tokens[0],record)==200 && server.body.find("\"credited\":5")!=std::string::npos);
  for (int i = 0; i < 5; ++i) {
    const auto &after = ProfileFixture::profiles[ids[i].c_str()].stats;
    const bool won = i < 2;
    assert(after.gamesPlayed==before[i].gamesPlayed+1 && after.gamesWon==before[i].gamesWon+(won ? 1 : 0));
    assert(after.starGamesPlayed==before[i].starGamesPlayed+1 && after.starGamesWon==before[i].starGamesWon+(won ? 1 : 0));
    assert(after.lastGameResult==(won ? LastGameResult::Win : LastGameResult::Eliminated));
  }
  // Without "winner", the lowest listed index is the first winner.
  auto next = record; next["recordId"] = "5a7c0c3e-2222-4222-8333-444455556666";
  next["winner"] = ""; next["winners"] = "4,3"; next["out0"] = "1"; next["out1"] = "2"; next["out2"] = "3";
  next["out3"] = "0"; next["out4"] = "0";
  assert(request("/api/standalone/import",tokens[0],next)==200);
  assert(ProfileFixture::profiles[ids[3].c_str()].stats.lastGameResult==LastGameResult::Win);
  assert(ProfileFixture::profiles[ids[4].c_str()].stats.lastGameResult==LastGameResult::Win);
  assert(ProfileFixture::profiles[ids[0].c_str()].stats.lastGameResult==LastGameResult::Eliminated);
  for (int i = 0; i < 5; ++i) {
    assert(request("/api/session/logout",tokens[i])==200);
    ProfileFixture::profiles.erase(ids[i].c_str()); TurnHubAccounts::accounts.erase(ids[i].c_str());
  }
  TurnHubStandalone::useImportStore(nullptr);
}

static void tenPlayerImportCredit() {
  using namespace TurnHubStandalone;
  MemoryBlobs blobs; useImportStore(&blobs);
  ImportedGame record; record.recordId="ten-player-credit"; record.playerCount=10;
  record.winner=9; record.winners=1u<<9; record.durationMs=90000;
  for (uint8_t i=0;i<10;++i) {
    record.players[i].profileId=TurnHubProfiles::createProfile();
    record.players[i].name=String("Ten ")+String(i);
    if(i<9) { record.players[i].outOrder=i+1; record.players[i].eliminatedBy=9; }
  }
  auto invalid=record; invalid.players[0].eliminatedBy=0;
  assert(importGame(invalid).status==ImportStatus::Invalid && blobs.blobs.empty());
  const auto result=importGame(record); assert(result.status==ImportStatus::Imported && result.credited==10);
  const auto &stats=ProfileFixture::profiles[record.players[9].profileId.c_str()].stats;
  assert(stats.gamesWon==1 && stats.eliminationsMade==9 && stats.lastGameEliminationsMade==9);
  assert(importGame(record).status==ImportStatus::Duplicate && stats.eliminationsMade==9);
  // Each credit takes its own receipt; the ring keeps the newest 128.
  for (uint8_t round=0;round<13;++round) {
    auto again=record; again.recordId=String("ten-player-round-")+String(round);
    assert(importGame(again).status==ImportStatus::Imported);
  }
  assert(blobs.blobs[IMPORTED_KEY].size()==3u+IMPORTED_CAPACITY*8u);
  for (uint8_t i=0;i<10;++i) ProfileFixture::profiles.erase(record.players[i].profileId.c_str());
  useImportStore(nullptr);
}

static void eliminationCredit() {
  using namespace TurnHub;
  GameEngine::setGameCompletedCallback(nullptr);
  PlayerSeat seats[] = {PlayerSeat{1,0}, PlayerSeat{2,1}, PlayerSeat{3,2}};
  GameEngine g;
  assert(g.start(seats,3,seats[0],1000));
  assert(g.pause(2000));
  bool finished=false;
  assert(!g.eliminatePlayer(2,2000,finished,false,2)); // self
  assert(!g.eliminatePlayer(2,2000,finished,false,4)); // unknown
  assert(!g.eliminatePlayer(2,2000,finished,true,1)); // concession
  assert(g.eliminatePlayer(2,2000,finished,false,1) && !finished);
  assert(g.eliminatedBy(2)==1 && g.eliminationsBy(1)==1);
  assert(!g.eliminatePlayer(2,2000,finished,false,1)); // no duplicate
  GameCheckpoint checkpoint; g.checkpoint(checkpoint,2100);
  uint8_t bytes[GAME_CHECKPOINT_CAPACITY];
  const auto size=encodeCheckpoint(checkpoint,bytes,sizeof(bytes)); assert(size);
  GameCheckpoint decoded; assert(decodeCheckpoint(bytes,size,decoded)==TurnHubStorage::Status::Ok);
  GameEngine restored; assert(restored.restoreCheckpoint(decoded,5000));
  assert(restored.paused() && restored.eliminatedBy(2)==1 && restored.eliminationsBy(1)==1);
  auto bad=decoded; bad.eliminatedBy[0]=2; assert(!validCheckpoint(bad));
  bad=decoded; bad.eliminatedBy[1]=2; assert(!validCheckpoint(bad));
  assert(restored.eliminatePlayer(3,6000,finished,false,1) && finished);
  assert(restored.eliminationsBy(1)==2);
  TurnHubProfiles::ProfileStats totals;
  TurnHubProfileStats::GameResult result; result.eliminationsMade=restored.eliminationsBy(1);
  TurnHubProfileStats::applyGameResult(totals,result);
  assert(totals.eliminationsMade==2 && totals.lastGameEliminationsMade==2);
  result.eliminationsMade=0; TurnHubProfileStats::applyGameResult(totals,result);
  assert(totals.eliminationsMade==2 && totals.lastGameEliminationsMade==0);
  PlayerSeat teams[] = {PlayerSeat{1,0},PlayerSeat{2,1},PlayerSeat{3,2},PlayerSeat{4,3}};
  GameSettings settings; settings.profile=GameProfile::Commander; settings.twoHeadedGiant=true;
  assert(g.start(teams,4,teams[0],1000,settings) && g.pause(2000));
  assert(!g.eliminatePlayer(3,2000,finished,false,4));
  assert(g.eliminatePlayer(3,2000,finished,false,1) && finished);
  assert(g.eliminatedBy(3)==1 && g.eliminatedBy(4)==1 && g.eliminationsBy(1)==2);
  g.checkpoint(checkpoint,2000); checkpoint.eliminatedBy[3]=0; assert(!validCheckpoint(checkpoint));
  GameEngine::setGameCompletedCallback(completed);
}

static void doomCommanderEngine() {
  using namespace TurnHub;
  using TurnHubStorage::Status;
  PlayerSeat seats[10] = {};
  for (uint8_t i=0;i<10;++i) { seats[i].playerNumber=i+1; seats[i].controllerId=i; seats[i].slot=1; if(i>=8) snprintf(seats[i].profileId,sizeof(seats[i].profileId),"%08x",i+1); }
  GameSettings settings; settings.profile=GameProfile::DoomCommander; settings.startingLife=40;
  GameEngine game;
  assert(commanderRules(settings.profile) && !teamsAllowed(settings.profile));
  assert(!game.start(seats,4,seats[0],1000,settings));
  assert(game.start(seats,10,seats[2],1000,settings));
  assert(game.doomBag().atRisk==1023 && game.doomBag().boundary==2 && game.doomBag().warmup==4);
  // A fetchland/other life payment on this turn saves the turn owner, not its payer.
  assert(game.changeLife(3,-1) && (game.doomBag().atRisk & doomBit(3)));
  assert(game.changeLife(4,-1) && !(game.doomBag().atRisk & doomBit(3)));
  assert(game.changeLife(4,1) && !(game.doomBag().atRisk & doomBit(3)));
  // Commander damage is life loss, regardless of whose commander dealt it.
  assert(game.passTurn(2,1100) && game.changeCommanderDamage(5,6,1,1));
  assert(!(game.doomBag().atRisk & doomBit(4)));
  // A delayed life approval belongs to the turn on which it was requested.
  assert(game.requestLifeChange(4,6,-1,1200));
  uint32_t request=game.lifeChangeFor(6)->id;
  assert(game.passTurn(3,1300) && game.respondLifeChange(6,request,true,1400));
  assert(game.doomBag().atRisk & doomBit(5));
  uint32_t now=2000;
  for(int i=2;i<40;++i) { assert(!game.paused()); assert(game.passTurn(game.activePlayerNumber()-1,now++)); }
  assert(game.paused() && game.doomBag().pending && game.doomBag().pendingTurn==2 && game.activePlayerNumber()==3);
  assert(!game.resume(now) && !game.passTurn(2,now) && !game.beginWinClaim(1,false,now));
  assert(!game.prepareDoomDraw(99,0));
  // Approvals block sealing; an accepted life change can still correct the last turn.
  assert(game.requestLifeChange(2,7,-1,now));
  assert(!game.prepareDoomDraw(1,0));
  assert(game.respondLifeChange(7,game.lifeChangeFor(7)->id,true,++now));
  assert(!(game.doomBag().atRisk & doomBit(2)));
  assert(game.prepareDoomDraw(1,0));
  uint8_t chosen=game.doomBag().selected;
  assert(chosen && game.prepareDoomDraw(1,7) && game.doomBag().selected==chosen);
  assert(!game.changeLife(8,-1) && !game.setDoomEligibility(8,false,1));
  bool finished=false; assert(!game.eliminatePlayer(8,now,finished));
  // A failed durable save reveals nothing. A saved reservation survives reboot.
  MemoryBlobs blobs; GameEngine empty; Lobby lobby; GameRecovery recovery(blobs);
  assert(recovery.load(empty,lobby,now)==Status::NotFound);
  blobs.failWrites=true; assert(recovery.save(game,now)==Status::IoError);
  assert(game.doomBag().selected==chosen && !game.isEliminated(chosen));
  blobs.failWrites=false; assert(recovery.save(game,now)==Status::Ok);
  GameEngine restored; Lobby restoredLobby; GameRecovery reboot(blobs);
  assert(reboot.load(restored,restoredLobby,now+10000)==Status::Ok);
  assert(restored.doomBag().selected==chosen && restored.prepareDoomDraw(1,0));
  assert(restored.resolveDoomDraw(now+10000) && restored.isEliminated(chosen));
  assert(restored.eliminatedBy(chosen)==0 && restored.doomBag().lastDraw==chosen && restored.doomBag().cycle==2);
  assert(restored.doomBag().atRisk==(1023 & ~doomBit(chosen)) && !restored.doomBag().pending);
  assert(!restored.prepareDoomDraw(1,0) && !restored.creditDoomElimination(chosen,2,2));
  GameCheckpoint saved, decoded; restored.checkpoint(saved,now+10000);
  uint8_t bytes[GAME_CHECKPOINT_CAPACITY]; size_t size=encodeCheckpoint(saved,bytes,sizeof(bytes));
  assert(size && decodeCheckpoint(bytes,size,decoded)==Status::Ok && sameDoomBag(saved.doom,decoded.doom));
  decoded.eliminatedBy[chosen-1]=2; assert(!validCheckpoint(decoded));
  decoded=saved; decoded.doom.atRisk|=doomBit(chosen); assert(!validCheckpoint(decoded));
  decoded=saved; decoded.doom.drawn|=doomBit(10); assert(!validCheckpoint(decoded));
  // Empty checks also reset every survivor's immunity.
  assert(restored.resume(now));
  for(uint8_t p=1;p<=10;++p) if(!restored.isEliminated(p)) assert(restored.setDoomEligibility(p,false,2));
  do { assert(restored.passTurn(restored.activePlayerNumber()-1,++now)); } while(!restored.paused());
  assert(restored.prepareDoomDraw(2,0) && restored.resolveDoomDraw(now));
  assert(restored.doomBag().lastDraw==0 && restored.doomBag().lastCheck==2 && restored.doomBag().atRisk==(1023 & ~doomBit(chosen)));
  // Early credited kills remove the grace period and move the boundary.
  assert(game.start(seats,7,seats[0],now,settings));
  assert(game.pause(now) && game.eliminatePlayer(4,now,finished,false,2));
  assert(game.doomBag().cycle==2 && game.doomBag().warmup==0 && game.doomBag().boundary==1);
  assert(game.doomBag().atRisk==(127 & ~doomBit(4) & ~doomBit(2)) && game.eliminationsBy(2)==1);
  assert(game.eliminatePlayer(6,now,finished));
  assert(game.creditDoomElimination(6,3,2) && !game.creditDoomElimination(6,2,3));
  assert(game.doomBag().boundary==2 && !(game.doomBag().atRisk & doomBit(3)));
  assert(game.eliminatePlayer(7,now,finished,true));
  // The bag stays in play until its last two survivors (the app's DoomRules).
  assert(game.livingPlayerCount()==4 && game.doomActive() && game.doomBag().atRisk);
  assert(game.eliminatePlayer(5,now,finished,true) && game.eliminatePlayer(3,now,finished,true));
  assert(game.livingPlayerCount()==2 && !game.doomActive() && !game.doomBag().atRisk && !game.doomBag().boundary);
  game.checkpoint(saved,now); assert(validCheckpoint(saved));
  game.reset(); assert(sameDoomBag(game.doomBag(),DoomBag{}));
}

static void doomCommanderHttp() {
  using namespace TurnHub;
  enterEmptyLobby(); fixtureRadio=false; testNow=1000; registerWebCallbacks();
  testBlobs.erase("checkpoint"); checkpointReadError=openError=setError=injectedCommitError=ESP_OK;
  assert(beginGameRecovery(table().game,table().lobby,testNow)==TurnHubStorage::Status::NotFound);
  String ids[6],tokens[6];
  for(int i=0;i<6;++i) tokens[i]=registerPhone((String("Bag Seat ")+String(i)).c_str(),ids[i]);
  for(int i=0;i<4;++i) assert(request("/api/session/join",tokens[i])==200);
  assert(request("/api/game/settings",tokens[0],{{"gameProfile","mtg_bag_of_doom"},{"startingLife","40"}})==200);
  assert(request("/api/control/start",tokens[0])==409 && server.body.find("at least five")!=std::string::npos);
  for(int i=4;i<6;++i) assert(request("/api/session/join",tokens[i])==200);
  assert(request("/api/control/start",tokens[0])==200);
  testNow+=3000; updateCountdown(testNow);
  auto token=[&](uint8_t player) {
    for(int i=0;i<6;++i) if(ids[i]==table().game.playerByNumber(player)->profileId) return tokens[i];
    assert(false);return String();
  };
  assert(table().game.activePlayerNumber()==1);
  assert(request("/api/control/life",token(2),{{"delta","-1"}})==200);
  assert(!(table().game.doomBag().atRisk & doomBit(1)));
  for(int i=0;i<24;++i) {
    assert(request("/api/control/pass",token(table().game.activePlayerNumber()))==200);
    testNow+=PASS_GRACE_MS; updatePendingPass(testNow);
  }
  assert(table().hubState==HubState::Paused && table().game.doomBag().pending);
  assert(request("/api/v1/state",token(1),{},HTTP_GET)==200);
  saveClientFixture("bag-of-doom-pending",server.body);
  auto fields=[&]() { return std::map<std::string,String>{{"expectedBootId",TurnHubWebApi::internal::bootId},
      {"expectedRevision",String(TurnHubWebApi::internal::readClientRevision())},{"cycle",String(table().game.doomBag().cycle)},{"action","draw"}}; };
  auto old=fields();
  assert(request("/api/control/doom","",old)==401);
  assert(request("/api/control/doom",token(1),{{"action","draw"},{"cycle","1"}})==409);
  auto bad=fields();bad["expectedBootId"]="old";
  assert(request("/api/control/doom",token(1),bad)==409);
  bad=fields();bad["cycle"]="4294967296";
  assert(request("/api/control/doom",token(1),bad)==400);
  bad=fields();bad["action"]="eligibility";bad["player"]="2";bad["inBag"]="2";
  assert(request("/api/control/doom",token(1),bad)==400);
  assert(request("/api/tablet/doom",token(1),fields())==403);
  // Failed reservation save reveals no name and leaves the table paused.
  setError=ESP_ERR_NVS_INVALID_HANDLE;
  assert(request("/api/control/doom",token(1),fields())==409);
  const uint8_t reserved=table().game.doomBag().selected;
  assert(reserved && !table().game.isEliminated(reserved));
  setError=ESP_OK;
  assert(request("/api/v1/state",token(1),{},HTTP_GET)==200);
  assert(server.body.find("\"sealed\":true")!=std::string::npos && server.body.find("\"selected\"")==std::string::npos);
  saveClientFixture("bag-of-doom-sealed",server.body);
  assert(request("/api/control/doom",token(1),old)==409);
  auto retry=fields();assert(request("/api/control/doom",token(1),retry)==200);
  assert(table().game.doomBag().lastDraw==reserved && table().game.isEliminated(reserved));
  assert(table().game.eliminatedBy(reserved)==0 && !table().game.doomBag().pending);
  assert(request("/api/control/doom",token(1),retry)==409);
  assert(request("/api/v1/state",token(1),{},HTTP_GET)==200);
  saveClientFixture("bag-of-doom-result",server.body);
  auto credit=fields();credit["action"]="credit";credit["player"]=String(reserved);credit["source"]="1";
  assert(request("/api/control/doom",token(1),credit)==409);
  assert(request("/api/control/pause",token(1))==200 && table().hubState==HubState::Running);

  uint8_t natural=2;while(table().game.isEliminated(natural)) ++natural;
  assert(request("/api/control/life",token(natural),{{"delta","-40"}})==200);
  assert(table().game.eliminationPending(natural) && table().hubState==HubState::Paused);
  auto answer=fields();answer.erase("action");answer.erase("cycle");answer["player"]=String(natural);answer["source"]="0";answer["selfEliminated"]="1";
  auto stale=answer;stale["expectedBootId"]="old";
  assert(request("/api/control/elimination",token(natural),stale)==409);
  assert(request("/api/control/elimination",token(natural),answer)==200);
  assert(table().game.selfEliminated(natural) && !table().game.eliminationPending(natural));
  assert(request("/api/control/elimination",token(natural),answer)==409);
  assert(request("/api/v1/state",token(1),{},HTTP_GET)==200);saveClientFixture("elimination-self",server.body);
  enterEmptyLobby();table().nextGameSettings=GameSettings{};
  for(int i=0;i<6;++i) {
    request("/api/session/logout",tokens[i]);
    ProfileFixture::profiles.erase(ids[i].c_str());TurnHubAccounts::accounts.erase(ids[i].c_str());
  }
}

static void explicitEliminationAttribution() {
  using namespace TurnHub;
  GameEngine::setGameCompletedCallback([](const GameEngine &) { ++completedGames; });
  PlayerSeat seats[6] = {};
  for(uint8_t i=0;i<6;++i) { seats[i].controllerId=i; seats[i].slot=1; seats[i].playerNumber=i+1; }
  GameSettings settings; settings.profile=GameProfile::Commander;settings.askEliminationSource=true;
  GameEngine g; bool finished=false;
  assert(g.start(seats,3,seats[0],1000,settings));
  GameSettings live = g.settings(); live.askEliminationSource = false; live.autoEliminateAtCommanderDamage = false;
  assert(g.setEliminationSettings(live) && g.settings().askEliminationSource);
  assert(g.changeLife(2,-40) && g.resolveEliminationRules(2000));
  assert(g.paused() && g.eliminationPending(2) && !g.gameOver() && g.eliminatedBy(2)==0);
  assert(!g.resume(2000) && !g.beginWinClaim(1,false,2000));
  assert(!g.resolveElimination(2,2,false,2000) && !g.resolveElimination(2,1,true,2000));
  // Player 3 earns the kill, although Player 1 owns the turn.
  assert(g.resolveElimination(2,3,false,2100));
  assert(g.eliminatedBy(2)==3 && g.eliminationsBy(3)==1 && g.eliminationsBy(1)==0);
  assert(!g.resolveElimination(2,1,false,2100));
  assert(g.resume(2200) && g.changeLife(1,-40) && g.resolveEliminationRules(3000));
  const int before=completedGames;
  assert(!g.gameOver() && completedGames==before && g.eliminationPending(1));
  GameCheckpoint saved,decoded;g.checkpoint(saved,3000);
  uint8_t bytes[GAME_CHECKPOINT_CAPACITY];const size_t size=encodeCheckpoint(saved,bytes,sizeof(bytes));
  assert(size && decodeCheckpoint(bytes,size,decoded)==TurnHubStorage::Status::Ok);
  GameEngine restored;assert(restored.restoreCheckpoint(decoded,9000) && restored.eliminationPending(1));
  assert(restored.resolveElimination(1,0,true,9100) && restored.gameOver() && completedGames==before+1);
  assert(restored.selfEliminated(1) && restored.eliminatedBy(1)==0 && restored.winnerPlayerNumber()==3);
  restored.checkpoint(saved,9200);assert(validCheckpoint(saved));
  decoded=saved;decoded.eliminationPending[0]=true;assert(!validCheckpoint(decoded));
  // Unknown is an explicit answer and does not award speculative credit.
  assert(g.start(seats,3,seats[0],1000,settings) && g.changeLife(2,-40) && g.resolveEliminationRules(2000));
  assert(g.resolveElimination(2,0,false,2100) && !g.eliminationPending(2) && !g.selfEliminated(2));
  // The whole batch can leave nobody; it still waits for every source, even all-out.
  assert(g.changeLife(1,-40) && g.changeLife(3,-40) && g.resolveEliminationRules(2200));
  g.checkpoint(saved,2300);assert(validCheckpoint(saved));
  assert(g.resolveElimination(1,0,true,2400) && !g.gameOver());
  assert(g.resolveElimination(3,0,false,2500) && g.gameOver() && g.endedInDraw());
  // Self gives no bag reset; an off-turn credited player receives the reset.
  settings.profile=GameProfile::DoomCommander;
  assert(g.start(seats,6,seats[0],1000,settings) && g.changeLife(2,-40) && g.resolveEliminationRules(2000));
  assert(g.resolveElimination(2,0,true,2100) && g.doomBag().cycle==1 && !g.creditDoomElimination(2,3,1));
  assert(g.resume(2200) && g.changeLife(4,-40) && g.resolveEliminationRules(2300));
  assert(g.resolveElimination(4,3,false,2400) && g.livingPlayerCount()==4 && g.doomActive() && g.eliminationsBy(3)==1);
  // Explicit manual Self is recorded before the normal completion path.
  settings.profile=GameProfile::Commander;assert(g.start(seats,2,seats[0],1000,settings) && g.pause(2000));
  assert(g.eliminatePlayer(2,2000,finished,false,0,true) && finished && g.selfEliminated(2));
  GameEngine::setGameCompletedCallback(completed);
}

static void explicitAttributionHttp() {
  using namespace TurnHub;
  enterEmptyLobby();table().nextGameSettings=GameSettings{};fixtureRadio=false;testNow=1000;registerWebCallbacks();
  String ids[2],tokens[2];
  tokens[0]=registerPhone("Explicit One",ids[0]);tokens[1]=registerPhone("Explicit Two",ids[1]);
  for(auto &token:tokens) assert(request("/api/session/join",token)==200);
  assert(request("/api/control/start",tokens[0],{{"askEliminationSource","1"}})==200);
  testNow+=3000;updateCountdown(testNow);assert(table().game.settings().askEliminationSource);
  const auto p1=table().game.playerByNumber(1);const bool first=String(p1->profileId)==ids[0];
  const String winnerToken=tokens[first?0:1],loserToken=tokens[first?1:0];
  const String winnerId=ids[first?0:1];
  const auto before=ProfileFixture::profiles[winnerId.c_str()].stats;
  assert(request("/api/control/life",loserToken,{{"delta","-40"}})==200);
  assert(table().game.eliminationPending(2) && !table().game.gameOver() && table().hubState==HubState::Paused);
  assert(ProfileFixture::profiles[winnerId.c_str()].stats.gamesPlayed==before.gamesPlayed);
  assert(request("/api/v1/state",loserToken,{},HTTP_GET)==200);saveClientFixture("elimination-final-pending",server.body);
  std::map<std::string,String> answer={{"expectedBootId",TurnHubWebApi::internal::bootId},
      {"expectedRevision",String(TurnHubWebApi::internal::readClientRevision())},{"player","2"},{"source","1"},{"selfEliminated","0"}};
  assert(request("/api/control/elimination",loserToken,answer)==200 && table().game.gameOver());
  const auto after=ProfileFixture::profiles[winnerId.c_str()].stats;
  assert(after.gamesPlayed==before.gamesPlayed+1 && after.eliminationsMade==before.eliminationsMade+1);
  assert(request("/api/control/elimination",loserToken,answer)==409);
  assert(ProfileFixture::profiles[winnerId.c_str()].stats.eliminationsMade==after.eliminationsMade);
  assert(request("/api/control/rematch",winnerToken)==200);
  assert(request("/api/control/start",winnerToken,{{"askEliminationSource","1"}})==200);
  testNow+=3000;updateCountdown(testNow);
  assert(request("/api/control/eliminate",loserToken,{{"eliminatedBy","0"},{"selfEliminated","1"}})==200);
  assert(table().game.gameOver() && table().game.selfEliminated(2) && !table().game.hasPendingElimination());
  assert(ProfileFixture::profiles[winnerId.c_str()].stats.eliminationsMade==after.eliminationsMade);
  enterEmptyLobby();table().nextGameSettings=GameSettings{};
  for(int i=0;i<2;++i) { request("/api/session/logout",tokens[i]);ProfileFixture::profiles.erase(ids[i].c_str());TurnHubAccounts::accounts.erase(ids[i].c_str()); }
}

// Runs the clock until one of Mana Tap's own orbs is well inside the field.
static const TurnHub::ManaOrb *awaitManaOrb() {
  static AtlasScreen s;
  for (int n=0;n<2000;++n) {
    s=currentScreen();
    for (uint8_t i=0;i<TurnHub::MANA_ORBS;++i) {
      const TurnHub::ManaOrb &o=s.mana.orb(i);
      if (o.live && o.target && o.y()<s.mana.field().bottom-2*s.mana.field().radius) return &o;
    }
    testNow+=20; updateTouchControls(testNow,false,0,0);
  }
  return nullptr;
}

// Mana Tap on the Atlas screen: opened from Home, played by touch, paused
// when its screen closes or a turn passes; it never changes table state.
static void manaTapOnAtlas() {
  freshLobby(3); pairingActive=false; resetTouchControls(true);
  tapButton(TouchAction::OpenManaTap);
  AtlasScreen s=currentScreen();
  assert(s.kind==ScreenKind::ManaTap && s.mana.phase()==TurnHub::ManaPhase::Ready &&
      screenButton(s,TouchAction::ManaAction) && screenButton(s,TouchAction::ManaQuit) &&
      screenButton(s,TouchAction::OpenHome)->selected);
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);
  tapButton(TouchAction::ManaAction);
  s=currentScreen(); assert(s.mana.phase()==TurnHub::ManaPhase::Playing && !screenButton(s,TouchAction::ManaQuit));
  // The Pause button sits clear of the field.
  const TouchButton *pause=screenButton(s,TouchAction::ManaAction);
  assert(pause->y+pause->h<=s.mana.field().top);
  const TurnHub::ManaOrb *orb=awaitManaOrb(); assert(orb);
  touchAt(orb->x,orb->y()); touchRelease();
  assert(currentScreen().mana.score()==1);
  assert(table().hubState==HubState::Lobby && table().lobby.playerCount()==3);  // No table state changes.
  // Leaving the screen pauses it; Home's button carries on the same run.
  tapButton(TouchAction::OpenGame); updateTouchControls(testNow,false,0,0);
  tapButton(TouchAction::OpenHome); tapButton(TouchAction::OpenManaTap);
  s=currentScreen(); assert(s.mana.phase()==TurnHub::ManaPhase::Paused && s.mana.score()==1 && !s.manaTurnPaused);
  tapButton(TouchAction::ManaAction); assert(currentScreen().mana.phase()==TurnHub::ManaPhase::Playing);
  // A turn passing in the match pauses it and shows the Game tab.
  tapButton(TouchAction::ManaAction); tapButton(TouchAction::OpenGame); startFromHost();
  tapButton(TouchAction::OpenHome); tapButton(TouchAction::OpenManaTap); tapButton(TouchAction::ManaAction);
  assert(currentScreen().mana.phase()==TurnHub::ManaPhase::Playing);
  seatPass(); testNow+=PASS_GRACE_MS; updatePendingPass(testNow); updateTouchControls(testNow,false,0,0);
  s=currentScreen(); assert(s.kind==ScreenKind::Status);
  tapButton(TouchAction::OpenHome); tapButton(TouchAction::OpenManaTap);
  s=currentScreen(); assert(s.mana.phase()==TurnHub::ManaPhase::Paused && s.manaTurnPaused &&
      String(s.detail)=="Paused: the turn passed");
  // Quit keeps the record, shown on Home's button.
  tapButton(TouchAction::ManaQuit); s=currentScreen();
  assert(s.kind==ScreenKind::Home && String(screenButton(s,TouchAction::OpenManaTap)->label)=="Play Mana Tap (best 1)");
  resetTouchControls(); enterEmptyLobby();
}

// Home Wi-Fi's pure parts (home_wifi.h) and its Settings row: the release
// feed the app reads, when a check-in is due, and the on/off toggle.
static void homeWifiCheckIns() {
  using namespace TurnHub;
  LatestFirmware latest;
  assert(parseReleaseFeed("{\"schema\": 1, \"release\": \"0.9.1\", \"packages\": ["
      "{\"product\": \"atlas\", \"version\": \"0.7.20\", \"file\": \"a.thfw\"},"
      "{\"product\":\"portal\",\"version\":\"9.9.9\"},"
      "{\"version\":\"0.5.3-dev\",\"product\":\"sigil\"}]}", latest));
  assert(latest.atlas.known && latest.atlas.minor == 7 && latest.atlas.patch == 20);
  assert(latest.sigil.known && latest.sigil.patch == 3);
  LatestFirmware untouched;
  assert(!parseReleaseFeed("{\"schema\":2,\"packages\":[{\"product\":\"atlas\",\"version\":\"1.0.0\"}]}", untouched));
  assert(!parseReleaseFeed("{\"schema\":1,\"packages\":[{\"product\":\"atlas\",\"version\":\"x\"}]}", untouched));
  assert(!parseReleaseFeed("not json", untouched) && !parseReleaseFeed(nullptr, untouched) && !untouched.atlas.known);

  HomeWifiSchedule schedule;
  assert(!schedule.due(HOME_WIFI_FIRST_CHECK_MS, false, true, true));  // Off by default.
  assert(!schedule.due(HOME_WIFI_FIRST_CHECK_MS, true, false, true));  // No network.
  assert(!schedule.due(HOME_WIFI_FIRST_CHECK_MS - 1, true, true, true));
  assert(!schedule.due(HOME_WIFI_FIRST_CHECK_MS, true, true, false));  // A match is running.
  assert(schedule.due(HOME_WIFI_FIRST_CHECK_MS, true, true, true));
  schedule.finished(500000, true);
  assert(!schedule.due(500000 + HOME_WIFI_CHECK_INTERVAL_MS - 1, true, true, true));
  assert(schedule.due(500000 + HOME_WIFI_CHECK_INTERVAL_MS, true, true, true));
  schedule.finished(600000, false);  // A failed check-in retries sooner.
  assert(schedule.due(600000 + HOME_WIFI_RETRY_MS, true, true, true));
  schedule.finished(700000, true);
  schedule.requestNow();  // Check now.
  assert(schedule.due(700001, true, true, true) && !schedule.due(700001, true, true, false));

  enterEmptyLobby();
  TurnHubAtlas::forgetHomeWifiNetwork();
  if (currentScreen().kind != ScreenKind::Menu) tapButton(TouchAction::OpenSettings);
  auto s = currentScreen();
  assert(s.kind == ScreenKind::Menu && String(screenButton(s, TouchAction::HomeWifi)->label) == "Home Wi-Fi: off");
  tapButton(TouchAction::HomeWifi);  // No network saved: it stays off and says where to add one.
  s = currentScreen();
  assert(!TurnHubAtlas::homeWifiEnabled() && std::string(s.notice).find("home network") != std::string::npos);
  assert(TurnHubAtlas::storeHomeWifiNetwork("Home", "password123"));
  tapButton(TouchAction::HomeWifi);
  s = currentScreen();
  assert(TurnHubAtlas::homeWifiEnabled() && screenButton(s, TouchAction::HomeWifi)->selected &&
      String(screenButton(s, TouchAction::HomeWifi)->label) == "Home Wi-Fi: on");
  for (uint8_t b = 0; b < s.buttonCount; ++b) assert(s.buttons[b].h >= 44);
  tapButton(TouchAction::HomeWifi);
  assert(!TurnHubAtlas::homeWifiEnabled());
  TurnHubAtlas::forgetHomeWifiNetwork();
}

static void deviceNavigation() {
  freshLobby(3); pairingActive=false; resetTouchControls(true);
  assert(currentScreen().kind==ScreenKind::Home);
  for (TouchAction action : {TouchAction::OpenHome,TouchAction::OpenGame,TouchAction::OpenOverview,TouchAction::OpenSettings})
    assert(screenButton(currentScreen(),action));
  tapButton(TouchAction::OpenGame);
  assert(currentScreen().kind==ScreenKind::Status);
  startFromHost();
  const auto player=*table().game.playerAt(0);
  const auto life=table().game.lifeTotal(player.playerNumber);
  const auto active=table().game.activePlayerNumber();
  tapButton(TouchAction::OpenOverview);
  assert(currentScreen().kind==ScreenKind::Overview && currentScreen().playerCount==3);
  int16_t x,y,w,h;screenChipCell(0,3,x,y,w,h);
  touchAt(x+w/4,y+h-8);touchRelease();touchAt(x+3*w/4,y+h-8);touchRelease();
  assert(currentScreen().kind==ScreenKind::Overview && table().game.lifeTotal(player.playerNumber)==life &&
      table().game.activePlayerNumber()==active);
  tapButton(TouchAction::OpenSettings);tapButton(TouchAction::OpenDevice);
  assert(!screenButton(currentScreen(),TouchAction::UnpairSigils) &&
      !screenButton(currentScreen(),TouchAction::FactoryResetAtlas) && !screenButton(currentScreen(),TouchAction::SleepAtlas));
  LedRenderer presentation(sigilBus);
  presentation.render(HubState::Running,table().lobby,table().game,0,0,0,testNow);
  const auto &roster=sentTableDisplays[0][0];
  assert(roster.total==3 && roster.game==1 && roster.rows[0].player==player.playerNumber && roster.rows[0].life==life);
  tapButton(TouchAction::OpenGame);
  assert(currentScreen().kind==ScreenKind::Status && table().game.activePlayerNumber()==active);
  enterEmptyLobby();resetTouchControls();
}

int main() {
  // Most scenarios inspect the interactive Game destination explicitly.
  resetTouchControls();
  // These scenarios exercise the presence code; tableCodeOptional() covers
  // the default (off).
  tableCodeRequired=true;
  TurnHubProfileStats::shortGameMinimumMs=0;  // Scenario games are short; backToSetupAndShortGames() checks the threshold.
  randomStarterByDefault=false;  // Player 1 starts unless a scenario chooses; officialRulesAndStarter() checks random.
  sigilReceivePackets(); std::cout<<"PASS Sigil radio queue preserves legacy and game display packets\n";
  assert(configureIntentHandlers());
  deviceNavigation(); std::cout<<"PASS device navigation, read-only table, live settings and Atlas roster projection\n";
  homeWifiCheckIns(); std::cout<<"PASS home Wi-Fi: release feed, check-in schedule, Settings toggle\n";
  manaTapOnAtlas(); std::cout<<"PASS Mana Tap on Atlas: touch play, pause on leaving or a pass, record, no table state\n";
  observeClientState(); intents.setObserver(observeIntent);
  GameEngine::setGameCompletedCallback(completed);
  eliminationCredit(); std::cout<<"PASS elimination credit, validation, team counts, recovery and shared statistics\n";
  doomCommanderEngine(); std::cout<<"PASS Bag of Doom rules, turn ownership, sealed recovery, reset and statistics\n";
  explicitEliminationAttribution(); std::cout<<"PASS explicit off-turn attribution, Self, Unknown, deferred completion and recovery\n";
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
  magicCountersAndRules(); std::cout<<"PASS Magic counters, live loss rules, per-commander casts, status, recovery and shared poison\n";
  magicTableCounters(); std::cout<<"PASS Magic table counters: treasure, rad trigger, day/night, reversed turns, everyone's counters, recovery\n";
  magicMonarch(); std::cout<<"PASS Magic monarch: transfer, permissions, pause, recovery, exit, reset and teams\n";
  zeroLifeAndExitReasons(); std::cout<<"PASS zero-life policy, immediate toggle, separate exits, recovery, lethal requests, Commander and teams\n";
  gameProfilesAndLife(); std::cout<<"PASS game settings, own life, companion state, limits, rematch and authorization\n";
  twoHeadedGiantEngine(); std::cout<<"PASS Two-Headed Giant engine: team turns, shared life, requests, claims, elimination, Commander, recovery\n";
  twoHeadedGiantTable(); std::cout<<"PASS Two-Headed Giant table: settings, state, teammate pass and life, team concession\n";
  officialRulesAndStarter(); std::cout<<"PASS official rules: fixed life, poison and table size, house rules unlock them, random or cleared starter\n";
  backToSetupAndShortGames(); std::cout<<"PASS back to setup in the first three minutes records nothing; short games wait for the table to count them\n";
  twoHeadedGiantSigils(); std::cout<<"PASS Two-Headed Giant Sigils: both teammates active, shared life, either cancels the pass\n";
  starCommanderRelationships(); std::cout<<"PASS Star Commander seats: allies and enemies for every seat, profile key and rule helpers\n";
  starCommanderEngine(); std::cout<<"PASS Star Commander engine: five players, ally damage refused, enemy victory, shared batch winners, draw, claims, recovery\n";
  starCommanderSigils(); std::cout<<"PASS Star Commander Sigils: five-player start, lobby order, fixed seats, enemy-only picker, refused ally hits\n";
  starCommanderVenue(); std::cout<<"PASS Star Commander venue: Game 1 Star Commander and Game 2 Commander stay separate\n";
  starCommanderTable(); std::cout<<"PASS Star Commander table: settings, 409 reasons, shared victory, state, statistics, rematch\n";
  doomCommanderHttp(); std::cout<<"PASS Bag of Doom HTTP authority, freshness, failed seal, retry, projection and result\n";
  explicitAttributionHttp(); std::cout<<"PASS native opt-in, final attribution before stats, duplicate guard and explicit Self\n";
  starStandaloneImport(); std::cout<<"PASS Star Commander standalone import: every listed winner credited, invalid winner sets refused\n";
  lifeApprovalsAndCommander(); std::cout<<"PASS life approval authorization, deadlines, rollover, atomic Commander counters and lifecycle\n";
  accountPermissionsAndModeration(); std::cout<<"PASS account setup, independent permissions, moderation, revocation and private counts\n";
  endMatchAsDraw(); std::cout<<"PASS touchscreen confirmed End match ends it as a draw: authorization, overrides, stats once, recovery\n";
  touchTableLifecycle(); std::cout<<"PASS touchscreen Start, Cancel start, Rematch and Reset between games, confirmed lobby Clear\n";
  touchNewGame(); std::cout<<"PASS touchscreen New game sets the app's game options\n";
  masterPass(); std::cout<<"PASS master pass: confirmed on the Actions screen, Atlas hardware only, immediate, logged, overrides a queued PASS\n";
  touchControls(); std::cout<<"PASS touchscreen: Pair, Start, presence code screen/cancel/expiry, Pause/Resume, Actions screen, confirm popup, slide-off, drop-out, stale press\n";
  updateNotice(); std::cout<<"PASS update notice: app-reported versions, devices behind, Menu and Info text\n";
  atlasScreens(); std::cout<<"PASS Atlas screens: player chips, NO SD CARD, info, QR codes (Wi-Fi, portal, sign in), turn clock" << std::endl;
  sigilSeatsTwoPlayers(); std::cout<<"PASS OLED and e-paper shared seats: chord, join, leave and game start\n";
  atlasSpeaker(); std::cout<<"PASS Atlas speaker: table-wide cues, phone-only table, Sigil mute independence, admin volume setting\n";
  batteryStability(); std::cout<<"PASS persistent battery stability, safety and source debounce\n";
  batteryGauge(); std::cout<<"PASS battery gauge curve, smoothing, low warning and /api/devices\n";
  chargingEstimate(); std::cout<<"PASS charging estimate: no jump on USB, counted climb, never down, full only after the limit\n";
  powerSource(); std::cout<<"PASS power source: USB pulled and plugged in, slow discharge, no cell, started on the cell\n";
  tenPlayerImportCredit(); std::cout<<"PASS ten-player imports, high winner bit, elimination credit and retry deduplication\n";
  standaloneImport(); std::cout<<"PASS standalone import: exact Atlas/profile scope, no name fallback, invalid mapping, guests, per-player receipts, duplicates, storage failure, draw\n";
  matchHistory(); std::cout<<"PASS match history: whole records kept once with Atlas profile IDs, CSV rows, torn lines, history route\n";
  touchPlayerCounters(); std::cout<<"PASS Atlas player counters: shared projection, touch requests, pagination and Done\n";
  touchSeatsPlayers(); std::cout<<"PASS touchscreen Add player: saved profiles, PIN keypad, New player keyboard, unique names\n";
  tabletMode(); std::cout<<"PASS tablet mode: presence code, Tablet access role, PIN-less new players, PIN choice, unique names, turn order, settings, life, Commander, pass, win, off\n";
  resetTableFromPortal(); std::cout<<"PASS admin returns the table to an empty lobby: permission, presence code (wrong, too many, other phone, expiry), draw once, countdown\n";
  tableCodeOptional(); std::cout<<"PASS table code optional: off by default (Admin still required), on needs codes, off again needs one" << std::endl;
  pairConfirmIntent(); std::cout<<"PASS pairing v2 code check: Atlas screen or portal Admin, lobby only, waiting Sigil only, confirm stores, reject and store failure store nothing" << std::endl;
  pairCodeTouchScreen(); std::cout<<"PASS pairing code on the Atlas screen: shown in the lobby after presence codes, Codes match and Reject, one Sigil at a time" << std::endl;
  pairConfirmFromPortal(); std::cout<<"PASS pairing code check from the portal: listed with the code, Admin verified at the table, confirm stores securely, reject stores nothing" << std::endl;
  factoryResetFromPortal(); std::cout<<"PASS factory reset: admin verified at the table, seated/in-game refusal, Sigil told and forgotten, Atlas erase after the reply" << std::endl;
  bootButtonGestures(); std::cout<<"PASS BOOT button: quick press pairs, medium hold forgets all Sigils (seated kept), long hold factory resets Atlas even mid-match" << std::endl;
  touchDeviceScreen(); std::cout<<"PASS touchscreen Device screen: held Unpair Sigils (lobby, seated kept) and Factory reset (between games)" << std::endl;
  touchThemeChoices(); std::cout<<"PASS theme picker: four independent choices, selected state, targets, Back and no game mutation" << std::endl;
  touchDeviceSleep(); std::cout<<"PASS touchscreen Device screen: Sleep (a tap, between games, touchscreen only)" << std::endl;
  autoSleepOnBattery(); std::cout<<"PASS auto sleep on the cell: idle between games, empty at any time" << std::endl;
  deviceManagement(); std::cout<<"PASS admin forget one/all Sigils, seated and in-game refusal, storage failure, pairing window setting\n";
  helloCapabilityLayout(); std::cout<<"PASS Hello capability layout and firmware version fields\n";
  firstRunSetup(); std::cout<<"PASS first-run setup: boot stage, Welcome and Skip, account, table code, private Wi-Fi password, finish, all set, Pair a Sigil\n";
  physicalPlayerControls(); std::cout<<"PASS physical counters/status: ownership, revision, shared seats, partners, bounds and decisions\n";
  commanderFlow(); std::cout<<"PASS Sigil Commander flow: ownership, source/partner, atomic hits, undo, stale/duplicate keys, cancellation and limits\n";
  physicalGameDisplay(); std::cout<<"PASS physical game display snapshots, received damage, shared focus, bounds and deduplication\n";
  turnTimerEngine(); std::cout<<"PASS turn timer phases, no automatic pass, pause freeze, rollover, validation and recovery\n";
  ledCueSelection(); std::cout<<"PASS LED cue selection, default styles and profile-only presentation changes\n";
  ledStateTransport(); std::cout<<"PASS LedState transport: one packet per change, anchor age, style, legacy channel peers\n";
  profilePicker(); std::cout<<"PASS Sigil profile picker: gating, pages by name, locked/blocked profiles, stale keys, guest, confirm, policy, closing\n";
  sigilLife(); std::cout<<"PASS Sigil life: AdjustLife availability, batched own-life changes, requests shown and answered with tag checks\n";
  artworkUploads(); std::cout<<"PASS bounded artwork upload, private review, approved public delivery and Atlas thumbnail\n";
  jewelColors(); std::cout<<"PASS personalization: Jewel color and preset avatars, validation, /api/avatars, /api/seats, TFT chips, SeatColor; custom stays private\n";
  sigilMenus(); std::cout<<"PASS Sigil menus: availability per state, defaults, MenuState2 revisions, stale choices, SelectAction Intents\n";
  turnTimerCuesAndMute(); std::cout<<"PASS one-shot timer audio cues, pause/resume, re-arm and independent mute\n";
  turnTimerSettingsHttp(); std::cout<<"PASS turn timer settings API, partial update, lobby-only edits and state projection\n";
  accessibilityPreferences(); std::cout<<"PASS per-player accessibility: LED profiles, merge rules, API, Sigil mute, hold-timing radio\n";
  actionRequiredCues(); std::cout<<"PASS ActionRequired reaches only the Sigil whose win confirmation is next\n";
  venueTables(); std::cout<<"PASS venue tables: two games, Sigil and phone switching, routing, separate resets, shared speaker\n";
  virtualCapacity(); std::cout<<"PASS virtual capacity and 16-player win confirmation\n";
  serialLogStream();
  serialLogCapture(); std::cout<<"PASS serial log capture, redaction, stream draining, ring overflow and self-describing log lines\n";
  otaServiceScenarios(); std::cout<<"PASS OTA service: authorization, variant checks, jobs, single-use downloads, fresh reboot confirmation\n";
  // Recovery fault injection runs last; see the recovery fixture comment.
  completionRecoveryOrdering(); std::cout<<"PASS completion checkpoint before stats: interrupted profile writes, failed/uncertain commits, protected records, no replay\n";
  gameRecoveryLifecycle(); std::cout<<"PASS interrupted-match recovery: boot load, checkpoint-after-intent, downtime exclusion, corrupt fail-safe\n";
}
