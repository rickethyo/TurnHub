#include <cassert>
#include <iostream>
#include <Arduino.h>
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

// Only hardware/transport/presentation boundaries are replaced.
namespace TurnHub {
SigilBus::SigilBus(uint8_t channel) : wifiChannel_(channel) {}
bool SigilBus::begin() { return true; }
bool SigilBus::poll(SigilEvent&) { return false; }
uint8_t SigilBus::activeCount(uint32_t) const { return 0; }
bool SigilBus::isOnline(uint8_t,uint32_t) const { return true; }
const SigilRecord *SigilBus::record(uint8_t) const { return nullptr; }
AudioController::AudioController(SigilBus &bus) : bus_(bus) {}
uint16_t AudioController::maskForSigil(uint8_t id) { return 1U << id; }
void AudioController::update(uint32_t) {}
void AudioController::clear() {}
void AudioController::countdownTone(uint16_t,uint8_t) {}
#define SOUND(name,type) void AudioController::name(type) {}
SOUND(playerJoined,uint8_t) SOUND(sharedPlayerAdded,uint8_t)
SOUND(sharedPlayerRemoved,uint8_t) SOUND(sameModulePass,uint8_t)
SOUND(starterSelected,uint8_t) SOUND(randomStarter,uint8_t)
SOUND(startArmed,uint8_t) SOUND(countdownCancelled,uint16_t)
SOUND(turnPass,uint8_t) SOUND(pause,uint16_t) SOUND(resume,uint16_t)
SOUND(gameStart,uint16_t) SOUND(gameOver,uint16_t)
SOUND(eliminationArmed,uint8_t) SOUND(eliminationTargetChanged,uint8_t)
SOUND(eliminationCancelled,uint8_t) SOUND(playerEliminated,uint8_t)
SOUND(winClaimed,uint16_t) SOUND(winConfirmed,uint16_t) SOUND(winDenied,uint16_t)
#undef SOUND
LedRenderer::LedRenderer(SigilBus &bus) : bus_(bus) {}
void LedRenderer::invalidate(uint8_t) {}
void LedRenderer::invalidateAll() {}
void LedRenderer::render(HubState,const Lobby&,const GameEngine&,uint32_t,uint8_t,uint8_t,uint32_t) {}
OtaManager::OtaManager(WebServer &webServer,AllowedCallback allowed) : server_(webServer),allowedCallback_(allowed) {}
void OtaManager::begin() {}
void OtaManager::update(uint32_t) {}
bool OtaManager::inProgress() const { return false; }
}
namespace TurnHubWebApi {
void configure(ResolveSeatCallback,ControlCallback) {}
void notePhysicalAction(uint8_t) {}
}

static int completedGames=0;
static void completed(const GameEngine&) { ++completedGames; }
static void freshLobby(int modules=3,bool shared=false) {
  // Fixture reset; all actions under test go through adapters/dispatcher.
  enterEmptyLobby();
  testNow=1000;
  completedGames=0;
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
  assert(lobby.hostModule()==0);
  assert(!dispatchModuleIntent(IntentType::Join,255).accepted());
  assert(!dispatchModuleIntent(IntentType::ArmStart,1).accepted());
  assert(web(0,2,WebControl::SelectStarter));
  PlayerSeat selected; assert(lobby.selectedStarter(selected)&&selected.slot==2);
  handleActionShort(0); assert(lobby.selectedStarter(selected)&&selected.slot==1);
  handlePass(1); assert(lobby.selectedStarter(selected)&&selected.moduleId==0);
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
  assert(lobby.hostModule()==1&&lobby.playerCount()==1);
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
int main() {
  assert(configureIntentHandlers());
  GameEngine::setGameCompletedCallback(completed);
  dispatcherContract(); std::cout<<"PASS dispatcher contract\n";
  lobbyLifecycle(); std::cout<<"PASS lobby and lifecycle\n";
  winDecisions(); std::cout<<"PASS win decisions and shared-seat order\n";
  eliminationAndConcession(); std::cout<<"PASS elimination versus concession\n";
  passTimingAndActors(); std::cout<<"PASS pass timing, cancellation, rollover, actors\n";
  optionalStorage(); std::cout<<"PASS optional storage error policy\n";
}
