#include <cassert>
#include <iostream>
#include <fstream>
#include <Arduino.h>
#include "profile_fixture.h"
#include "home_tls.h"
#include "wall_clock.h"
#include "card_cache.h"
#include "profile_statistics.h"
#include "standalone_import.h"
#include "match_archive.h"
#include "game_profile.h"
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
#include "profile_store.h"
#include "web_api_internal.h"
// Compile the actual application entry point; the handler and adapter
// modules it binds are linked from ../../src, not copied rules.
#include "../../src/main.cpp"
#include "hosted_game.h"
#include "hosted_sigils.h"
#include "touch_controls.h"
#include "../../../Sigil/include/received_packet.h"
#include "../../../Sigil/include/display_name.h"

namespace TurnHub { extern uint32_t fixturePairingWindowSaved; extern int fixtureSpeakerVolumeSaved; extern int fixturePowerSettingsSaved; }
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
SigilBus::SigilBus() { fixtureBus=this; }
SigilBus *SigilBus::activeInstance() { return fixtureBus; }
bool SigilBus::begin() { return true; }
static TurnHubProtocol::SigilUpdateOfferPacket fixtureUpdateOffer{};
bool SigilBus::sendUpdateOffer(const TurnHubProtocol::SigilUpdateOfferPacket &p) { fixtureUpdateOffer=p; return true; }
static uint32_t fixturePairingWindowMs=0;
bool SigilBus::openPairing(uint32_t windowMs) { fixturePairingWindowMs=windowMs; return fixtureRadio; }
// Forgotten slots read as unpaired until the next freshTable() re-pairs them.
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
using TurnHubProtocol::SigilAction;
// A Sigil choosing from its menu, through the real adapter (SelectAction on
// the current revision). A choice that isn't on offer is dropped, as on
// hardware.

static void dispatcherContract() {
  IntentDispatcher dispatcher;
  const auto handler=+[](const Intent&,void*) { return IntentResult::accept(); };
  assert(!dispatcher.bind(IntentType::None,handler));
  assert(dispatcher.bind(IntentType::Sleep,handler));
  assert(!dispatcher.bind(IntentType::Sleep,handler));
  Intent intent; intent.type=IntentType::Sleep;
  assert(dispatcher.dispatch(intent).accepted());
  intent.type=IntentType::Count; assert(!dispatcher.dispatch(intent).accepted());
  intent.type=IntentType::ForgetPairing; assert(!dispatcher.dispatch(intent).accepted());
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

static String responseNumber(const char *field) {
  const std::string marker=std::string("\"")+field+"\":";
  auto start=server.body.find(marker); assert(start!=std::string::npos);
  start+=marker.size(); return server.body.substr(start,server.body.find_first_of(",}",start)-start);
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
  server.status=404; return 404;  // As WebServer answers an unknown path.
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
  const int regStatus=request("/api/profiles/register","",{{"name",name},{"pin","1234"}}); if(regStatus!=200) std::cerr<<"REG "<<name<<" "<<regStatus<<" "<<server.body<<std::endl; assert(regStatus==200);
  id=responseField("profileId"); return responseField("token");
}
static String loginPhone(const String &id) {
  assert(request("/api/session/login","",{{"profileId",id},{"pin","1234"}})==200);
  return responseField("token");
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



// Venue tables: two games side by side on one Atlas (PLANNED_DESIGNS.md).









// Every Sigil draws its own light: one semantic LedState packet per change
// (and on invalidation, i.e. every Hello), with the seated player's style.


namespace TurnHubAccounts { extern std::map<std::string, Account> accounts; }
// Profile picker on Sigils: gating, pages by name, locked and
// blocked profiles, stale keys, Guest, confirm/back, policy at the handler,
// and closing when idle or when the game starts.


// Life on Sigils: AdjustLife only while ChangeLife could succeed, a
// batched LifeAdjust for the Sigil's own player only, and life requests
// shown on (and answered from) the target's Sigil, tag-checked.


// Jewel color: the signed-in profile sets or clears it; a seat bound to that
// profile gets a SeatColor, and a guest seat gets none.




// Sigil menus: availability per state, the default action, MenuState2
// transport and revisions, and SelectAction dispatching through Intents.


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



// Which cue each Sigil gets. How a cue looks (colors, cadences, the player's
// style) is the Sigil's job: Sigil/tests/host/led_scenarios.cpp.



// ActionRequired is two 60 ms notes at 1150 Hz; no other cue uses that tone.
static bool heardActionRequired(uint8_t id) {
  const int32_t tone = TurnHubProtocol::encodeTone(1150, 60);
  for (int32_t v : TurnHub::fixtureTones[id]) if (v == tone) return true;
  return false;
}
static void clearTones() { drainAudio(); for (auto &tones : TurnHub::fixtureTones) tones.clear(); }







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


// A Sigil can join Seat B and start a shared-seat match.




// The status screen's chips, the NO SD CARD warning, and the Info and QR
// screens, which change no table state.
extern bool fixtureSdCardReady;
extern TurnHub::BatteryReading fixtureBattery;


// Newer firmware (update_notice.h): the app reports the release feed's
// versions; Atlas counts itself and each paired Sigil that runs something
// older, and says so on the Menu and Info screens.


// The lobby's Start opens New game; Start game there starts the match.
static void tapStartGame() {
  if (!screenButton(currentScreen(),TouchAction::StartGame)) tapButton(TouchAction::OpenNewGame);
  assert(currentScreen().kind==ScreenKind::NewGame);
  tapButton(TouchAction::StartGame);
}

// The touchscreen between games: Start (two or more players), Cancel start,
// Rematch and Reset, all as table actions from the Atlas hardware.


// New game (owner 2026-10-30): the app's game options on the touchscreen.
// Each row steps one setting through ConfigureGame from the table, seated
// players or not; official games keep their life until House rules.


// A key or list row on the touchscreen, by its action and arg.
static void tapKey(TouchAction action,uint8_t arg) {
  const AtlasScreen screen=currentScreen();
  const TouchButton *b=nullptr;
  for (uint8_t i=0;i<screen.buttonCount;++i) if (screen.buttons[i].action==action && screen.buttons[i].arg==arg) b=&screen.buttons[i];
  assert(b!=nullptr);
  const int16_t x=b->x+b->w/2, y=b->y+b->h/2;
  touchAt(x,y); testNow+=30; touchAt(x,y); touchRelease();
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


// Master pass: the Table screen's hold passes a stuck turn at once (no
// grace), logged as a master pass. Atlas hardware only, running games only,
// never over an open table decision.






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


// Atlas's speaker plays table-wide cues, including for a table with no
// Sigil; lobby feedback stays on Sigils. Volume 0 silences only the speaker,
// and muting every Sigil leaves it. Admins set the volume through an Intent.
struct FakeSpeaker final : TurnHub::ToneOutput {
  std::vector<uint8_t> volumes;
  void tone(uint16_t,uint16_t,uint8_t volume) override { volumes.push_back(volume); }
};
static void playQueuedAudio() { for (int i=0;i<300;++i) { testNow+=20; audio.update(testNow); } }







// Factory reset from Device Settings: Admin verified at the table (presence code),
// between games. A Sigil is told to erase itself and is forgotten; Atlas
// erases its NVS and restarts after the reply has gone.
extern unsigned fixtureFactoryResets;
extern unsigned fixtureSdWipes;
extern bool fixtureSdWipedBeforeErase;
// Pairing v2 code check: the Atlas screen or a portal Admin, lobby only,
// only for a Sigil that is actually waiting; confirm stores, reject doesn't.


// The Atlas screen shows a waiting Sigil's code over the lobby, with Codes
// match and Reject acting through PairConfirm; a phone's presence code
// comes first, and the screen closes once nothing is waiting.


// The portal's pairing-code check: listed in /api/devices, answered by an
// Admin verified at the table, through PairConfirm.




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


// Settings > Device on the touchscreen (owner 2026-10-02): the BOOT button's
// Unpair and Factory reset, confirmed in the popup, between games only.


// Menu > Device Sleep (owner 2026-10-02): a tap, between games, from the
// touchscreen only; Atlas sleeps once the notice has been shown.
extern unsigned fixtureSleeps;




// Auto sleep on the cell (owner 2026-10-08): idle between games, or an empty
// charge at any time. On USB Atlas never sleeps by itself.
extern bool fixtureOnBattery;
extern bool fixtureSleptEmpty;


// Screen and sleep (owner 2026-11-03): chosen at the Atlas screen by anyone,
// or by an Admin over HTTP; saved; Off stops idle sleep but not the empty cell.


struct CompletionPowerLoss {};
static unsigned completionWritesBeforeLoss = 0;
static void interruptCompletionWrite() {
  if (--completionWritesBeforeLoss == 0) throw CompletionPowerLoss{};
}







// Fixture reset for device scenarios: radio up, every Sigil paired and
// online, no hub. Games run on the hub phone (hostedTable()).
static void freshTable() {
  testNow=1000;
  TurnHub::fixtureRadio=true;
  for(uint8_t i=0;i<MAX_PHYSICAL_SIGILS;++i) {
    TurnHub::fixtureForgotten[i]=false;
    TurnHub::fixtureRecords[i].id=i; TurnHub::fixtureRecords[i].mac[5]=i;
    TurnHub::fixtureRecords[i].helloInfoValid=false; TurnHub::fixtureRecords[i].capabilities=0;
  }
  TurnHubHosted::hostedTable().clear();
}

// A hub's game on the table: Sigils 0 and 1 seat Ana and Ben (players 1
// and 2), so Atlas is not between games while it runs.
// The fixture hub publishes a two-player game: Ana on Sigil 0 and Ben on
// Sigil 1, seat A each; `active` is whose turn it is.
static void hubPublish(const char *state,int active,uint32_t revision,const String &profileA=String()) {
  std::string error;
  const std::string json=std::string("{\"gameId\":\"fx\",\"revision\":")+std::to_string(revision)+",\"state\":\""+state+
      "\",\"profile\":\"MtgCommander\",\"active\":"+std::to_string(active)+",\"players\":[{\"name\":\"Ana\",\"life\":40,"
      "\"localId\":\"sigil:0:1\""+(profileA.length()?std::string(",\"profileId\":\"")+profileA.c_str()+"\"":std::string())+
      "},{\"name\":\"Ben\",\"life\":40,\"localId\":\"sigil:1:1\"}]}";
  assert(TurnHubHosted::hostedTable().publish("fixture-hub-token",json,testNow,error));
}
static void hostGame(const char *state="RUNNING",const String &profileA=String()) {
  auto &hosted=TurnHubHosted::hostedTable();
  hosted.clear();
  assert(hosted.claim("fixture-hub-token","","Fixture hub",testNow)==TurnHubHosted::Claim::Granted);
  hubPublish(state,0,1,profileA);
}
// Time passes while the fixture hub keeps renewing its lease.
static void hubWait(uint32_t ms) {
  for (uint32_t step; ms; ms-=step) {
    step=ms<20000?ms:20000; testNow+=step;
    assert(TurnHubHosted::hostedTable().claim("fixture-hub-token","","Fixture hub",testNow)==TurnHubHosted::Claim::Granted);
  }
}
static void endHostedGame() { TurnHubHosted::hostedTable().clear(); }

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




// Parity with the standalone app (2026-10-14): official modes keep official
// life, poison and player counts unless house rules are on; nobody chosen to
// go first means a random starter, and choosing the chosen player again clears it.


// Parity with the standalone app (2026-10-14): Back to setup within three
// minutes of game time records nothing; a match under a minute waits for the
// table to count it.




// A standalone tablet game's finished record: imported once, credited to the
// explicit Atlas-scoped profiles only; invalid mappings never take a receipt.


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
  bool remove(const char *name) override { files.erase(name); return true; }
  // A folder is the names with that prefix and a slash; "" is the top.
  bool list(const char *folder,EachFile each,void *context) override {
    const std::string prefix=folder[0] ? std::string(folder)+"/" : std::string();
    for (const auto &f : files) {
      if (f.first.compare(0,prefix.size(),prefix)) continue;
      const std::string rest=f.first.substr(prefix.size());
      if (rest.find('/')==std::string::npos) each(rest.c_str(),context);
    }
    return true;
  }
};
// Offline play: phones keep the game going while Atlas is out of reach and
// send their queues back (turnhub_offline.h, protocol/http-v1.md).
namespace TurnHubWebApi { namespace internal { extern char bootId[33]; } }
// Run inside matchHistory with its two signed-in phones (the profile store is nearly full by then).




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

// Planechase with the players' own plane cards (owner, 2026-10-30): set up
// from a phone or the Atlas screen, the core counting the die and planeswalks.


// The card store (the microSD card's records) for card_cache.cpp's tests.
struct MemoryCardStore : TurnHubStorage::BlobStore {
  std::map<std::string, std::string> records;
  TurnHubStorage::Status read(const char *key, void *data, size_t capacity, size_t &size) override {
    auto found = records.find(key);
    if (found == records.end()) return TurnHubStorage::Status::NotFound;
    size = found->second.size();
    if (!data && !capacity) return TurnHubStorage::Status::Ok;
    if (size > capacity) return TurnHubStorage::Status::Corrupt;
    memcpy(data, found->second.data(), size); return TurnHubStorage::Status::Ok;
  }
  TurnHubStorage::Status write(const char *key, const void *data, size_t size) override {
    if (size > 1024) return TurnHubStorage::Status::InvalidArgument;
    records[key] = std::string(static_cast<const char *>(data), size); return TurnHubStorage::Status::Ok;
  }
  TurnHubStorage::Status remove(const char *key) override {
    return records.erase(key) ? TurnHubStorage::Status::Ok : TurnHubStorage::Status::NotFound;
  }
};

// The app's planar catalog and saved commanders on Atlas (owner, 2026-10-30):
// a digital Planechase deck dealt from the card, and commanders picked from
// each profile's saved ones on the app, the tablet or the Atlas screen.




// App parity (2026-10-28): treasure and rad, the rad trigger, day/night,
// reversed turns and Everyone's counters, over HTTP and through recovery.






// --- Star Commander -------------------------------------------------------------------------

// Seat geometry: for player p (i = p - 1) the neighbours (i+1, i+4) are allies
// and the two across the table (i+2, i+3) are enemies.




// Sigils at a Star Commander table: five seats, relationships from the
// lobby order (fixed in play), the Commander picker and refused ally hits.


// Two Atlas games stay separate: Star Commander in Game 1, Commander in Game 2.


// Phones at a Star Commander table: settings, start, the 409 reasons, a
// shared victory after one batch, state, statistics and rematch.


// A standalone Star Commander record credits every listed winner.


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


// Home Wi-Fi's pure parts (home_wifi.h) and its Settings row: the release
// feed the app reads, when a feed check and a join are due, and the toggle.






// Phone-hosted games (hosted_game.h, protocol/hosted-v1.md): one hub holds
// the table, pushes the core's game JSON, and Atlas shows it without deciding.
static std::string hostedState(const char *gameId,int revision,const char *state,int active,int life) {
  return std::string("{\"gameId\":\"")+gameId+"\",\"revision\":"+std::to_string(revision)+",\"state\":\""+state+
      "\",\"profile\":\"MtgCommander\",\"active\":"+std::to_string(active)+
      ",\"players\":[{\"name\":\"Ana\",\"life\":"+std::to_string(life)+"},{\"name\":\"Ben\",\"life\":40}]}";
}


// Slice 4: a hub that goes away leaves its unfinished game for another phone to resume.


// Slice 2: Sigils bound by localId show the hub's game and queue their keys.


static void utcBookkeepingClock() {
  TurnHubClock::WallClock clock;
  constexpr uint64_t utc = 1700000000000ULL;
  assert(clock.now(100) == 0);
  assert(!clock.observe(0, 100));
  assert(clock.observe(utc, 0xFFFFFFF0UL));
  assert(clock.now(0x20) == utc + 48);
  assert(clock.observe(utc + 1000, 0x20, TurnHubClock::Source::Internet));
  assert(!clock.observe(utc - 100000, 0x30));
  assert(clock.now(0x40) == utc + 1032);
  assert(clock.observe(utc + 21601000, 0x20 + 21600000UL));
  assert(clock.source() == TurnHubClock::Source::Phone);
}


static void deliberatePairing() {
  freshTable();
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
  hostGame(); updatePairingWindow(testNow); assert(!pairingActive);
  assert(!intents.dispatch(intent).accepted());
  endHostedGame();
}

static void virtualProfileFlow() {
  endHostedGame(); TurnHub::fixtureRadio=false; testNow=1000;
  TurnHubWebApi::begin(server);
  String firstId, secondId;
  const String first=registerPhone("Phone One",firstId), second=registerPhone("Phone Two",secondId);
  assert(request("/api/session/me",first,{},HTTP_GET)==200 && responseField("profileId")==firstId);
  assert(request("/api/session/me","not-a-token",{},HTTP_GET)==401);
  // The game routes are gone: games run on the hub phone (hosted-v1.md).
  assert(request("/api/control/pass",first)==404 && request("/api/session/join",first)==404);
  const String companion=loginPhone(firstId);
  assert(request("/api/session/logout",first)==200);
  assert(request("/api/session/me",first,{},HTTP_GET)==401);
  assert(request("/api/session/me",companion,{},HTTP_GET)==200);
  const String returned=loginPhone(firstId);
  assert(request("/api/session/me",returned,{},HTTP_GET)==200);
  // A profile's stats are its own, whatever another profile ID asks for.
  assert(request("/api/session/stats",returned,{{"profileId",secondId}},HTTP_GET)==200);
  assert(responseField("profileId")==firstId);

  for(int i=0;i<5;++i) assert(request("/api/session/login","",{{"profileId",firstId},{"pin","0000"}})==401);
  assert(request("/api/session/login","",{{"profileId",firstId},{"pin","1234"}})==429);
  testNow+=30000; const String afterLimit=loginPhone(firstId);
  assert(request("/api/session/profile",afterLimit,{{"pin","5678"}})==401);
  assert(request("/api/session/profile",afterLimit,{{"pin","5678"},{"currentPin","0000"}})==401);
  assert(request("/api/session/profile",afterLimit,{{"pin","5678"},{"currentPin","1234"}})==200);
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
  for(const char *bad:{"123","pass1","seven77","tab\there!",
      "this password is far too long to be accepted by atlas at all, honestly"})
    assert(request("/api/profiles/register","",{{"name","Bad secret"},{"pin",bad}})==400);
  assert(request("/api/profiles/register","",{{"name","Password user"},{"pin","correct horse battery"}})==200);
  const String passwordId=responseField("profileId");
  assert(request("/api/session/login","",{{"profileId",passwordId},{"pin","correct horse battery"}})==200);
  const String passwordSession=responseField("token");
  assert(request("/api/session/login","",{{"profileId",passwordId},{"pin","correct horse"}})==401);
  assert(request("/api/session/profile",passwordSession,{{"pin","2468"},{"currentPin","correct horse battery"}})==200);
  assert(request("/api/session/login","",{{"profileId",passwordId},{"pin","2468"}})==200);
  assert(request("/api/session/profile",responseField("token"),{{"pin","Ünïcode pass 9"},{"currentPin","2468"}})==200);
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

static void profilePolicyFlow() {
  endHostedGame(); TurnHub::fixtureRadio=true;
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
  assert(TurnHubWebApi::physicalUseAllowed(id) && !TurnHubWebApi::physicalStatsVisible(id));
  owner=loginPhone(id);
  assert(request("/api/session/profile",owner,{{"clearPin","1"},{"currentPin","1234"}})==409);
  const String companion=loginPhone(id);
  assert(request("/api/session/logout",owner)==200);
  assert(TurnHubWebApi::physicalStatsVisible(id));
  assert(request("/api/session/logout",companion)==200);
  assert(!TurnHubWebApi::physicalStatsVisible(id));
  owner=loginPhone(id);
  for (const char *physical : {"0","1"}) for (const char *hidden : {"0","1"}) {
    assert(request("/api/session/policy",owner,{{"allowPhysicalWithoutPin",physical},{"hideStatsWithoutAuthentication",hidden}})==200);
    assert(request("/api/session/logout",owner)==200);
    assert(TurnHubWebApi::physicalUseAllowed(id));
    assert(TurnHubWebApi::physicalStatsVisible(id)==(hidden[0]=='0'));
    owner=loginPhone(id);
  }
  assert(request("/api/session/me",owner,{},HTTP_GET)==200);
  assert(server.body.find("\"policyAvailable\":true")!=std::string::npos);
  owner=loginPhone(id);
  assert(request("/api/session/stats",owner,{},HTTP_GET)==200);
  ProfileFixture::profiles[id].policyReadable=false;
  assert(TurnHubWebApi::physicalUseAllowed(id) && !TurnHubWebApi::physicalStatsVisible(id));
  assert(request("/api/session/policy",owner,{{"allowPhysicalWithoutPin","1"},{"hideStatsWithoutAuthentication","0"}})==503);
  ProfileFixture::profiles[id].policyReadable=true;
  testNow+=8UL*60*60*1000+1;
  assert(!TurnHubWebApi::profileAuthenticated(id));
  assert(!TurnHubWebApi::physicalStatsVisible(id));

  TurnHub::fixtureRadio=false;
}

static void accountPermissionsAndModeration(){
  using namespace TurnHubAccounts;
  endHostedGame();
  TurnHubWebApi::configurePresence(presenceHooks());
  String adminId,gmId,playerId,devId;
  const String admin=registerPhone("Administrator",adminId),gm=registerPhone("Moderator",gmId),player=registerPhone("Participant",playerId),dev=registerPhone("Developer",devId);
  assert(request("/api/accounts/setup","",{},HTTP_GET)==200&&server.body.find("true")!=std::string::npos);
  assert(request("/api/accounts/setup",admin)==403); // Physical confirmation needed.
  verifyAtTable(admin);assert(request("/api/accounts/setup",admin)==200);resetPresence();
  assert(request("/api/session/login","",{{"profileId",adminId}})==200);
  const String adminPlay = responseField("token");
  assert(request("/api/session/me",adminPlay,{},HTTP_GET)==200 && server.body.find("\"permissions\":0")!=std::string::npos);
  assert(request("/api/network",adminPlay,{},HTTP_GET)==403);
  assert(request("/api/session/stats",adminPlay,{},HTTP_GET)==403);
  assert(request("/api/session/logout",adminPlay)==200);

  assert(request("/api/accounts/setup",gm)==409);
  assert(request("/api/accounts/permissions",player,{{"profileId",playerId},{"permissions","31"}})==403);
  assert(request("/api/accounts/permissions",admin,{{"profileId",adminId},{"permissions","0"}})==409);
  assert(request("/api/accounts/permissions",admin,{{"profileId",gmId},{"permissions","26"}})==200);
  assert(request("/api/accounts/permissions",admin,{{"profileId",devId},{"permissions","4"}})==200);
  assert(request("/api/accounts/permissions",admin,{{"profileId",devId},{"permissions","32"}})==400);
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
  assert(request("/api/network/home",admin,{{"ssid","Home Net"},{"password","hunter2hunter2"}})==200);
  assert(TurnHubAtlas::homeWifiEnabled());  // Saving a network turns it on.
  assert(request("/api/network/home",admin,{{"enabled","0"}})==200&&!TurnHubAtlas::homeWifiEnabled());
  assert(request("/api/network/home",admin,{{"enabled","1"}})==200);
  assert(request("/api/network",admin,{},HTTP_GET)==200);
  assert(server.body.find("\"ssid\":\"Home Net\"")!=std::string::npos &&
      server.body.find("\"enabled\":true")!=std::string::npos && server.body.find("hunter2")==std::string::npos &&
      server.body.find("\"address\":null")!=std::string::npos &&
      server.body.find("\"tls\":{\"running\":false,\"port\":443,\"pin\":null}")!=std::string::npos);
  assert(request("/api/network/home/check",gm)==403&&request("/api/network/home/check",admin)==200);
  assert(request("/api/network/home",admin,{{"forget","1"}})==200&&!TurnHubAtlas::homeWifiEnabled()&&
      !TurnHubAtlas::homeWifiConfigured());
  assert(request("/api/network/channel",gm,{{"channel","11"}})==403);
  for(const char *bad:{"0","12","x"}) assert(request("/api/network/channel",admin,{{"channel",bad}})==400);
  assert(request("/api/network/channel",admin,{{"channel","6"}})==200&&server.body.find("\"changed\":false")!=std::string::npos);
  resetPresence();
  assert(request("/api/session/profile",admin,{{"clearPin","1"}})==403);
  // Account listings mark who is in the hub's game, never moderation counts.
  hostGame("RUNNING",playerId);
  for (const String &reader : {admin,gm}) {
    assert(request("/api/accounts",reader,{},HTTP_GET)==200&&server.body.find("connectionResets")==std::string::npos);
    const std::string body=server.body.c_str();
    const auto entry=[&](const String &id){const size_t at=body.find(std::string("\"profileId\":\"")+id.c_str());assert(at!=std::string::npos);return body.substr(at,body.find('}',at)-at);};
    assert(entry(playerId).find("\"atTable\":true")!=std::string::npos&&entry(playerId).find("\"hasPin\":true")!=std::string::npos);
    assert(entry(adminId).find("\"primary\":true")!=std::string::npos&&entry(adminId).find("\"atTable\":false")!=std::string::npos);
  }
  endHostedGame();
  assert(request("/api/accounts/moderate",gm,{{"profileId",playerId},{"action","mute"}})==404);
  assert(request("/api/session/stats",player,{},HTTP_GET)==200&&
      server.body.find("\"moderation\":{\"visible\":true,\"connectionResets\":0,\"gameRemovals\":0}")!=std::string::npos);
  assert(request("/api/accounts/permissions",admin,{{"profileId",adminId},{"permissions","31"}})==200);
  assert(has(adminId,Admin|GameMaster|Developer));
  // The admin portal is gone (owner, 2026-10-30): no pages, only the APIs the
  // app uses, and the firmware upload.
  ota.begin();
  assert(!server.routes.count("0/portal") && !server.routes.count("0/dev") && !server.routes.count("0/update"));
  assert(server.uploads.count("/api/firmware"));
  assert(!server.routes.count("0/api/portal") && !server.uploads.count("/api/portal/install"));
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
  // Equal public labels do not establish account identity.
  {
    String oneId, otherId;
    registerPhone("Same name", oneId); registerPhone("Same name", otherId);
    assert(oneId != otherId);
    assert(TurnHubProfiles::mergedProfileFor(oneId) == oneId);
    assert(TurnHubProfiles::mergedProfileFor(otherId) == otherId);
    ProfileFixture::profiles.erase(oneId.c_str()); ProfileFixture::profiles.erase(otherId.c_str());
  }
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

}

// Jewel color: the signed-in profile sets or clears it; a seat bound to that
// profile gets a SeatColor, and a guest seat gets none.
static void jewelColors() {
  using namespace TurnHubProtocol;
  endHostedGame(); TurnHub::fixtureRadio = true;
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
  hostGame("RUNNING", id);  // The hub seats the profile on Sigil 0, slot 1.
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
  TurnHubProfiles::saveAvatarForProfile(id, TurnHubAvatars::AVATAR_CUSTOM);  // Custom: never public.
  assert(seatAvatar(sigilSeatColorFor(0, 1)) == 0);
  assert(request("/api/session/personalization", owner, {{"avatar", "0"}}) == 200);
  assert(request("/api/session/personalization", owner, {{"color", "none"}}) == 200 && server.body.find("\"color\":null") != std::string::npos);
  assert(!seatColorSet(sigilSeatColorFor(0, 1)));
  // Seat colors are re-read every 500 ms, not every loop pass.
  syncSigilMenus(testNow + 500); assert(!seatColorSet(TurnHub::fixtureSeatColor[0][1]));
  for (auto &record : TurnHub::fixtureRecords) {
    record.helloInfoValid = false; record.capabilities = 0;
  }
  resetSigilMenus(); endHostedGame(); ProfileFixture::bindings.clear();
}

static void artworkUploads() {
  endHostedGame();
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
  hostGame("RUNNING",playerId); resetTouchControls(); tapButton(TouchAction::OpenGame);
  AtlasScreen screen; buildAtlasScreen(testNow,screen);
  assert(screen.playerCount==2 && screen.players[0].avatar==TurnHubAvatars::AVATAR_CUSTOM && screen.players[0].avatarPixels &&
      screen.players[1].avatar==0);
  endHostedGame(); resetTouchControls();
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
  endHostedGame();
  for (const String &id : {playerId,adminId,otherId}) { ProfileFixture::profiles.erase(id.c_str()); TurnHubAccounts::accounts.erase(id.c_str()); }
}

static void atlasScreens() {
  resetTouchControls(); endHostedGame(); pairingActive=false;
  tapButton(TouchAction::OpenGame);
  AtlasScreen s=currentScreen();
  // No hub: the Game tab says how to start one, with no players and no QR code.
  assert(s.kind==ScreenKind::Status && String(s.badge)=="READY" && String(s.title)=="No game yet" &&
      s.playerCount==0 && !s.showLife && !s.sdMissing && s.qr[0]=='\0' && s.lineCount==3);
  fixtureSdCardReady=false; assert(currentScreen().sdMissing); fixtureSdCardReady=true;
  fixtureBattery.present=true; fixtureBattery.percent=12; fixtureBattery.low=true;
  s=currentScreen(); assert(s.batteryLow && s.batteryPercent==12);
  fixtureBattery=TurnHub::BatteryReading(); assert(!currentScreen().batteryLow && currentScreen().batteryPercent==-1);
  for (const TouchButton &b : s.buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.x>=0 && b.x+b.w<=ATLAS_SCREEN_WIDTH && b.y+b.h<=ATLAS_SCREEN_HEIGHT);

  // Home is the table at a glance, with the tabs and Mana Tap to lead on.
  tapButton(TouchAction::OpenHome); s=currentScreen();
  assert(s.kind==ScreenKind::Home && String(s.title)=="TurnHub" && String(s.badge)=="READY" && s.lineCount==4 &&
      String(s.lines[0])=="Ready for a game" && String(s.lines[2])=="No hub" && s.playerCount==0 &&
      screenButton(s,TouchAction::OpenGame) && screenButton(s,TouchAction::OpenSettings) &&
      screenButton(s,TouchAction::OpenManaTap));
  for (uint8_t i=0;i<s.lineCount;++i) assert(!strstr(s.lines[i],"Wi-Fi") && !strstr(s.lines[i],"Portal"));
  fixtureBattery.present=true; fixtureBattery.percent=64; fixtureBattery.charging=true;
  assert(currentScreen().batteryPercent==64 && currentScreen().batteryCharging);
  openMenuScreen(); tapButton(TouchAction::OpenDevice);
  assert(startsWith(currentScreen().detail,"TurnHub Atlas v"));
  // The speaker's volume is set at Atlas itself too, without an Admin sign-in.
  tapButton(TouchAction::OpenPower);
  {
    const uint8_t before=audio.speakerVolume();
    tapButton(TouchAction::SpeakerVolume);
    assert(audio.speakerVolume()==(before+1)%4);
    assert(String(screenButton(currentScreen(),TouchAction::SpeakerVolume)->label)==
        String("Volume: ")+TurnHub::speakerVolumeName(audio.speakerVolume()));
    while (audio.speakerVolume()!=before) tapButton(TouchAction::SpeakerVolume);
  }
  tapButton(TouchAction::CloseScreen);
  fixtureBattery=TurnHub::BatteryReading();
  openMenuScreen(); tapButton(TouchAction::OpenQr); s=currentScreen();
  // The shipped default password is public, so its Wi-Fi code shows freely.
  assert(s.kind==ScreenKind::Qr && String(s.qr)=="WIFI:T:WPA;S:TurnHub-Atlas;P:TurnHub-Setup;;" &&
      !screenButton(s,TouchAction::QrPortal) && !screenButton(s,TouchAction::QrSignIn));
  // Back steps out of QR codes to Settings.
  tapButton(TouchAction::CloseScreen); assert(currentScreen().kind==ScreenKind::Menu);
  openMenuScreen();

  // The hub's game shows as it publishes it; Atlas decides nothing.
  hostGame();
  assert(currentScreen().kind==ScreenKind::Menu);
  tapButton(TouchAction::OpenGame); s=currentScreen();
  assert(s.kind==ScreenKind::Status && String(s.badge)=="PLAYING" && s.showLife && s.playerCount==2 &&
      String(s.title)=="Ana's turn" && String(s.detail)=="Hosted by Fixture hub");
  assert(String(s.players[0].name)=="Ana" && s.players[0].life==40 && (s.players[0].flags&CHIP_ACTIVE) &&
      !(s.players[1].flags&CHIP_ACTIVE));
  tapButton(TouchAction::OpenHome); s=currentScreen();
  assert(String(s.lines[0])=="Ana's turn" && String(s.lines[2])=="Fixture hub" && String(s.lines[3])=="2 players");
  hostGame("LOBBY"); tapButton(TouchAction::OpenGame); s=currentScreen();
  assert(String(s.badge)=="LOBBY" && String(s.title)=="Game setup" && !s.showLife);
  // A hub that stops renewing its lease drops off the screen.
  hostGame(); testNow+=TurnHubHosted::LEASE_MS+1;
  assert(String(currentScreen().title)=="No game yet");
  resetTouchControls(); endHostedGame();
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
  resetTouchControls(); endHostedGame(); pairingActive=false; TurnHub::fixtureRadio=true;
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

static void deviceManagement() {
  TurnHubWebApi::configureDevices(manageDevices, []() { return pairingWindowMs; });
  freshTable();
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

  // A Sigil seated in the hub's game is kept, alone or in "forget all".
  hostGame("LOBBY");
  assert(request("/api/device/forget",admin,{{"module","0"}})==409 && sigilBus.record(0));
  assert(request("/api/device/forget",admin,{{"all","1"}})==409);
  for (uint8_t id=0;id<MAX_PHYSICAL_SIGILS;++id) assert(sigilBus.record(id));
  assert(request("/api/device/forget",admin)==400);
  assert(request("/api/device/forget",admin,{{"module","99"}})==409);

  // A free Sigil is forgotten and told so; a second request finds nothing.
  assert(request("/api/device/forget",admin,{{"module","3"}})==200);
  assert(!sigilBus.record(3) && TurnHub::fixtureUnpairs[3]==1);
  assert(request("/api/device/forget",admin,{{"module","3"}})==409 && TurnHub::fixtureUnpairs[3]==1);

  // Never during a match; a storage failure keeps the pairing.
  hostGame();
  assert(request("/api/device/forget",admin,{{"module","4"}})==409 && sigilBus.record(4));
  endHostedGame();
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

  freshTable(); pairingActive=false;
  Intent pair; pair.type=IntentType::PairRequest; pair.actor.origin=IntentOrigin::AtlasHardware;
  assert(intents.dispatch(pair).accepted() && TurnHub::fixturePairingWindowMs==90000);
  testNow+=89999; updatePairingWindow(testNow); assert(pairingActive);
  ++testNow; updatePairingWindow(testNow); assert(!pairingActive);
  assert(request("/api/pairing",admin,{{"windowMs","60000"}})==200 && pairingWindowMs==60000);
  endHostedGame();
}

static void firstRunSetup() {
  using TurnHub::SetupStage;
  TurnHubWebApi::configureDevices(manageDevices, []() { return pairingWindowMs; });
  TurnHubWebApi::configureSetup([]() { return static_cast<uint8_t>(setupStage); });
  const String previousPrimary=TurnHubAccounts::primary;
  const bool previousRadio=TurnHub::fixtureRadio;
  freshTable(); resetPresence(); resetTouchControls(); testNow+=1000; pairingActive=false;
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

  // Welcome, in words: set up from the app (there is no portal). No QR code.
  AtlasScreen screen=currentScreen();
  assert(screen.kind==ScreenKind::Setup && strcmp(screen.badge,"SETUP")==0);
  assert(strcmp(screen.title,"Welcome to TurnHub")==0 && screen.qr[0]=='\0' && screen.lineCount==4);
  assert(strstr(screen.lines[0],"TurnHub app") && strstr(screen.lines[3],"Set up this Atlas"));
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
  hostGame(); assert(currentScreen().kind==ScreenKind::Status);
  endHostedGame(); { const uint32_t now=testNow; freshTable(); testNow=now; }
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
  { const uint32_t now=testNow; freshTable(); testNow=now; }
  hostGame();
  assert(request("/api/setup/finish",owner,{{"password","table-pass-1"}})==409 && setupStage==SetupStage::Welcome);
  // A refused finish leaves the Wi-Fi as it was (the printed password).
  assert(!TurnHub::validWifiPassword(TurnHub::readStoredWifiPassword()));
  endHostedGame();

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
  resetPresence(); resetTouchControls(); endHostedGame();
}

static void atlasSpeaker() {
  FakeSpeaker speaker;
  audio.clear(); audio.setSpeaker(&speaker); audio.setSpeakerVolume(TurnHub::DEFAULT_SPEAKER_VOLUME);
  // A hub phone plays its own game sounds; Atlas speaks only for itself.
  hostGame(); playQueuedAudio(); assert(speaker.volumes.empty()); endHostedGame();

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
  // Screen and sleep over HTTP (power_settings.h): Admin only; indices
  // checked; a storage failure changes nothing.
  assert(request("/api/power",player,{},HTTP_GET)==403);
  assert(request("/api/power",player,{{"autoSleep","2"}})==403);
  powerSettings=TurnHub::PowerSettings(); powerSettings.screenOff=1; powerSettings.gameScreen=1; powerSettings.autoSleep=0;
  assert(request("/api/power",admin,{},HTTP_GET)==200);
  assert(server.body.find("\"autoSleep\":0")!=std::string::npos &&
      server.body.find("\"screenOffChoices\":[\"Auto\",\"30 s\"")!=std::string::npos);
  for (const char *bad : {"7","99","-1","x",""}) assert(request("/api/power",admin,{{"screenOff",bad}})==400);
  assert(request("/api/power",admin,{{"gameScreen","3"}})==400 && powerSettings.gameScreen==1);
  assert(request("/api/power",admin,{{"autoSleep","3"},{"gameScreen","2"}})==200);
  assert(powerSettings.autoSleep==3 && powerSettings.gameScreen==2 && powerSettings.screenOff==1);
  ProfileFixture::gameSettingsWritable=false;
  assert(request("/api/power",admin,{{"autoSleep","1"}})==409 && powerSettings.autoSleep==3);
  ProfileFixture::gameSettingsWritable=true;
  Intent forgedPower; forgedPower.type=IntentType::ConfigurePower; forgedPower.actor.origin=IntentOrigin::Browser;
  strncpy(forgedPower.payload.moderatorId,playerId.c_str(),8); forgedPower.payload.value=0;
  assert(intents.dispatch(forgedPower).status==IntentStatus::Unauthorized && powerSettings.autoSleep==3);

  powerSettings=TurnHub::PowerSettings(); TurnHub::fixturePowerSettingsSaved=-1;

  audio.setSpeaker(nullptr); audio.setSpeakerVolume(0); audio.clear(); endHostedGame();
}

// An Admin verified at the table (presence code) ends a phone's hold on the
// table, so another phone can host. Atlas has no game of its own to end.
static void resetTableFromPortal() {
  TurnHubWebApi::configureDevices(manageDevices, []() { return pairingWindowMs; });
  TurnHubWebApi::configurePresence(presenceHooks());
  resetPresence(); resetTouchControls();
  freshTable();
  String adminId,playerId;
  const String admin=registerPhone("Reset admin",adminId),player=registerPhone("Reset player",playerId);
  TurnHubAccounts::Account account; account.permissions=TurnHubAccounts::Admin;
  assert(TurnHubAccounts::save(adminId,account));
  hostGame();
  auto &hub=TurnHubHosted::hostedTable();

  // Admin only, and only once that Admin is verified at the table (the code
  // the Atlas screen shows). A player cannot even ask for a code.
  assert(request("/api/table/reset",player)==403 && hub.live(testNow));
  assert(request("/api/table/reset",admin)==403 && hub.live(testNow));
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
  assert(request("/api/table/reset",player)==403 && hub.live(testNow));
  Intent forged; forged.type=IntentType::ResetTable; forged.actor.origin=IntentOrigin::Browser;
  strncpy(forged.payload.moderatorId,playerId.c_str(),8);
  assert(intents.dispatch(forged).status==IntentStatus::Unauthorized && hub.live(testNow));

  // The hub's lease ends; a second phone may claim the table at once.
  assert(request("/api/table/reset",admin)==200 && !hub.live(testNow) && !hub.lease().held);
  assert(logHas("ATLAS|ADMIN|RESET_TABLE|HUB_RELEASED\n"));
  assert(hub.claim("second-hub","","Second",testNow)==TurnHubHosted::Claim::Granted);
  assert(request("/api/table/reset",admin)==200 && logHas("ATLAS|ADMIN|RESET_TABLE|HUB_RELEASED\n"));
  assert(request("/api/table/reset",admin)==200 && logHas("ATLAS|ADMIN|RESET_TABLE|NO_HUB\n"));

  // Verification lapses after PRESENCE_GRANT_MS, even for the Admin.
  hostGame(); testNow+=PRESENCE_GRANT_MS; updatePairingWindow(testNow);
  hostGame();
  assert(request("/api/table/reset",admin)==403 && hub.live(testNow));
  strncpy(forged.payload.moderatorId,adminId.c_str(),8);
  assert(intents.dispatch(forged).status==IntentStatus::Unauthorized && hub.live(testNow));
  endHostedGame();
}

// The table code is optional (off by default, 2026-10-08): while off, a
// signed-in Admin counts as at the table with no code. An Admin turns it on
// freely; turning it off again takes a code that Admin actually entered.
static void tableCodeOptional() {
  TurnHubWebApi::configureDevices(manageDevices, []() { return pairingWindowMs; });
  TurnHubWebApi::configurePresence(presenceHooks());
  resetPresence(); resetTouchControls();
  tableCodeRequired=false;
  freshTable();
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
  hostGame();
  assert(request("/api/table/reset",admin)==200 && !TurnHubHosted::hostedTable().lease().held && pendingPresenceCode(testNow)==nullptr);
  assert(request("/api/table-code",admin,{{"required","2"}})==400 && !tableCodeRequired);

  // On: protected actions need a code again.
  assert(request("/api/table-code",admin,{{"required","1"}})==200 && tableCodeRequired);
  hostGame();
  assert(request("/api/table/reset",admin)==403 && server.body.find("presenceRequired")!=std::string::npos);
  assert(TurnHubHosted::hostedTable().lease().held);
  // Off again needs that code: a forged Intent or a request without one fails.
  Intent forged; forged.type=IntentType::ConfigureTableCode; forged.actor.origin=IntentOrigin::Browser;
  strncpy(forged.payload.moderatorId,adminId.c_str(),8); forged.payload.value=0;
  assert(intents.dispatch(forged).status==IntentStatus::Unauthorized && tableCodeRequired);
  assert(request("/api/table-code",admin,{{"required","0"}})==403 && server.body.find("presenceRequired")!=std::string::npos);
  assert(tableCodeRequired);
  verifyAtTable(admin);
  assert(request("/api/table-code",admin,{{"required","0"}})==200 && !tableCodeRequired);
  assert(request("/api/table/reset",admin)==200 && !TurnHubHosted::hostedTable().lease().held);

  // The rest of this suite exercises the code path.
  tableCodeRequired=true;
  for (const String &id:{adminId,playerId}) {
    ProfileFixture::profiles.erase(id.c_str()); TurnHubAccounts::accounts.erase(id.c_str());
  }
  resetPresence(); endHostedGame();
}

// Pairing v2 code check: the Atlas screen or a portal Admin, lobby only,
// only for a Sigil that is actually waiting; confirm stores, reject doesn't.
static void pairConfirmIntent() {
  freshTable();
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
  hostGame();
  assert(decide(IntentOrigin::AtlasHardware,"",5|accept)==IntentStatus::InvalidState);
  endHostedGame();
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
  endHostedGame();
}

// The Atlas screen shows a waiting Sigil's code over the lobby, with Codes
// match and Reject acting through PairConfirm; a phone's presence code
// comes first, and the screen closes once nothing is waiting.
static void pairCodeTouchScreen() {
  resetTouchControls(); freshTable();
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
  hostGame();
  assert(currentScreen().kind!=ScreenKind::PairCode);
  TurnHub::fixturePending[4]=TurnHubSecureLink::PendingPairing{};
  endHostedGame();
}

// The portal's pairing-code check: listed in /api/devices, answered by an
// Admin verified at the table, through PairConfirm.
static void pairConfirmFromPortal() {
  TurnHubWebApi::configureDevices(manageDevices, []() { return pairingWindowMs; });
  TurnHubWebApi::configurePresence(presenceHooks());
  resetPresence(); resetTouchControls();
  freshTable();
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
  freshTable();
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

  // A Sigil seated in the hub's game is refused; a free one is told and forgotten.
  hostGame("LOBBY");
  assert(request("/api/device/factory-reset",admin,{{"module","0"}})==409 && sigilBus.record(0));
  assert(request("/api/device/factory-reset",admin,{{"module","3"}})==200);
  assert(TurnHub::fixtureFactoryResetSigil==3 &&
      TurnHub::fixtureFactoryResetValue==TurnHubProtocol::FACTORY_RESET_CONFIRM && !sigilBus.record(3) &&
      TurnHub::fixtureUnpairs[3]==1);
  assert(request("/api/device/factory-reset",admin,{{"module","3"}})==409);  // No longer paired.
  assert(fixtureFactoryResets==0);

  // Never during a match, for Sigils or Atlas.
  hostGame();
  assert(request("/api/device/factory-reset",admin,{{"atlas","1"}})==409 && !factoryResetScheduled());
  endHostedGame();

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

static void bootButtonGestures() {
  resetPresence(); resetTouchControls();
  freshTable();
  TurnHub::fixtureUnpairs[3]=0; fixtureFactoryResets=0;

  // Quick press: the pairing window opens (on release).
  pairingActive=false;
  bootPress(500);
  assert(pairingActive);

  // Medium hold: forget all Sigils, but not while players sit on them.
  assert(sigilBus.record(0) && sigilBus.record(3));
  hostGame("LOBBY");
  pairingActive=false;
  bootPress(TurnHubProtocol::UNPAIR_HOLD_MS+500);
  assert(sigilBus.record(0) && sigilBus.record(3) && TurnHub::fixtureUnpairs[3]==0);
  assert(!pairingActive);  // A hold never pairs on release.
  endHostedGame();
  bootPress(TurnHubProtocol::UNPAIR_HOLD_MS+500);
  assert(!sigilBus.record(0) && !sigilBus.record(3) && TurnHub::fixtureUnpairs[3]==1);
  assert(!factoryResetScheduled() && fixtureFactoryResets==0);

  // Long hold in a match: Atlas erases itself, no Admin or table code needed. A
  // Sigil in the match is not forgotten on the way (forget all is lobby-only).
  freshTable(); hostGame();
  assert(!factoryResetScheduled());
  bootPress(TurnHubProtocol::FACTORY_RESET_HOLD_MS+500);
  assert(factoryResetScheduled() && sigilBus.record(0));
  serviceFactoryReset(millis()); assert(fixtureFactoryResets==0);
  serviceFactoryReset(millis()+2000);
  assert(fixtureFactoryResets==1 && !factoryResetScheduled());
  endHostedGame();
  TurnHub::fixtureUnpairs[3]=0;
}

// Settings > Device on the touchscreen (owner 2026-10-02): the BOOT button's
// Unpair and Factory reset, confirmed in the popup, between games only.
static void touchDeviceScreen() {
  resetPresence(); resetTouchControls();
  freshTable();
  TurnHub::fixtureUnpairs[3]=0; fixtureFactoryResets=0;
  hostGame("LOBBY");  // Players seated on Sigils 0 and 1 in the hub's lobby.
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
  endHostedGame(); resetTouchControls(); openMenuScreen(); tapButton(TouchAction::OpenDevice);
  tapConfirmed(TouchAction::UnpairSigils);
  assert(!sigilBus.record(0) && !sigilBus.record(3) && TurnHub::fixtureUnpairs[3]==1);
  assert(String(currentScreen().notice)=="All Sigils forgotten" && !factoryResetScheduled());
  // Back returns to Settings.
  tapButton(TouchAction::CloseScreen); assert(currentScreen().kind==ScreenKind::Menu);

  // After a game (between games): Unpair and Factory reset, confirmed.
  freshTable(); hostGame("GAME_OVER");
  openMenuScreen(); tapButton(TouchAction::OpenDevice); s=currentScreen();
  assert(s.kind==ScreenKind::Device && screenButton(s,TouchAction::UnpairSigils) &&
      screenButton(s,TouchAction::FactoryResetAtlas));
  tapButton(TouchAction::FactoryResetAtlas);
  assert(!factoryResetScheduled() && String(currentScreen().title)=="Erase this Atlas?");
  tapButton(TouchAction::ConfirmYes);
  assert(factoryResetScheduled());
  serviceFactoryReset(millis()+2000);
  assert(fixtureFactoryResets==1 && !factoryResetScheduled());
  // Device settings stays open during play, without reset/unpair/sleep actions.
  endHostedGame(); freshTable(); resetTouchControls(); openMenuScreen(); tapButton(TouchAction::OpenDevice);
  hostGame(); assert(currentScreen().kind==ScreenKind::Device);
  assert(!screenButton(currentScreen(),TouchAction::FactoryResetAtlas) &&
      !screenButton(currentScreen(),TouchAction::UnpairSigils) && !screenButton(currentScreen(),TouchAction::SleepAtlas));
  endHostedGame(); resetTouchControls();
  TurnHub::fixtureUnpairs[3]=0;
}

static void touchThemeChoices() {
  freshTable(); resetTouchControls(); openMenuScreen();
  tapButton(TouchAction::OpenThemes);
  const TouchAction choices[] = {TouchAction::ThemeGraphite, TouchAction::ThemeDaylight, TouchAction::ThemeBrass, TouchAction::ThemeContrast};
  for (uint8_t i = 0; i < 4; ++i) {
    tapButton(choices[i]);
    const auto screen = currentScreen();
    assert(screen.kind == ScreenKind::Themes);
    assert(static_cast<uint8_t>(atlasDisplayTheme()) == i);
    assert(screenButton(screen, choices[i])->selected);
    for (uint8_t b = 0; b < screen.buttonCount; ++b) assert(screen.buttons[b].w >= 44 && screen.buttons[b].h >= 44);
  }
  tapButton(TouchAction::InvertDisplay);
  assert(atlasDisplayInverted() && screenButton(currentScreen(), TouchAction::InvertDisplay)->selected);
  tapButton(TouchAction::ThemeDaylight);
  assert(atlasDisplayInverted()); // Theme changes retain independent inversion.
  tapButton(TouchAction::InvertDisplay);
  assert(!atlasDisplayInverted() && !screenButton(currentScreen(), TouchAction::InvertDisplay)->selected);
  tapButton(TouchAction::CloseScreen); assert(currentScreen().kind == ScreenKind::Menu);
  assert(chooseAtlasDisplayTheme(TurnHubTheme::Id::Graphite));
}

static void touchDeviceSleep() {
  resetPresence(); resetTouchControls();
  freshTable(); fixtureSleeps=0; fixtureFactoryResets=0;
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
  hostGame("GAME_OVER");
  openMenuScreen(); tapButton(TouchAction::OpenDevice);
  assert(screenButton(currentScreen(),TouchAction::SleepAtlas));
  endHostedGame(); freshTable(); hostGame();
  Intent touch; touch.type=IntentType::Sleep; touch.actor.origin=IntentOrigin::AtlasHardware;
  assert(intents.dispatch(touch).status==IntentStatus::InvalidState && !sleepScheduled());
  endHostedGame(); resetTouchControls();
  assert(fixtureFactoryResets==0);
}

static void autoSleepOnBattery() {
  resetPresence(); resetTouchControls();
  freshTable(); fixtureSleeps=0; fixtureOnBattery=false;
  fixtureBattery=TurnHub::BatteryReading(); fixtureBattery.present=true; fixtureBattery.percent=50;
  const uint32_t idle=TurnHub::autoSleepIdleMs(powerSettings);
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
  endHostedGame(); freshTable(); hostGame();
  hubWait(idle*2);
  serviceAutoSleep(testNow); assert(!sleepScheduled());
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
  assert(sleepScheduled());
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
  endHostedGame(); resetTouchControls();
}

// Screen and sleep (owner 2026-11-03): chosen at the Atlas screen by anyone,
// or by an Admin over HTTP; saved; Off stops idle sleep but not the empty cell.
static void powerSettingsChoices() {
  using namespace TurnHub;
  PowerSettings s;  // Defaults keep the 2026-11-02 behavior.
  assert(screenOffIdleMs(s,false,false)==120000 && screenOffIdleMs(s,true,false)==30000);
  assert(screenOffIdleMs(s,false,true)==0 && screenOffIdleMs(s,true,true)==30000);
  assert(autoSleepIdleMs(s)==5UL*60000UL);
  s.gameScreen=1; assert(screenOffIdleMs(s,true,true)==0);
  s.gameScreen=2; s.screenOff=4; assert(screenOffIdleMs(s,false,true)==300000);
  s.screenOff=SCREEN_OFF_NEVER; assert(screenOffIdleMs(s,true,false)==0);
  PowerSettings back; assert(unpackPowerSettings(packPowerSettings(s),back) && back==s);
  assert(!unpackPowerSettings(7,back) && !unpackPowerSettings(3<<8,back) && !unpackPowerSettings(6<<16,back) &&
      !unpackPowerSettings(-1,back) && !unpackPowerSettings(1<<24,back));

  // The Atlas screen: Settings > Device > Sound, screen, sleep, each tap the next choice.
  powerSettings=PowerSettings(); fixturePowerSettingsSaved=-1;
  endHostedGame(); resetTouchControls(); openMenuScreen(); tapButton(TouchAction::OpenDevice);
  tapButton(TouchAction::OpenPower);
  AtlasScreen screen=currentScreen();
  assert(screen.kind==ScreenKind::Power);
  assert(String(screenButton(screen,TouchAction::PowerScreenOff)->label)=="Screen off: Auto");
  assert(String(screenButton(screen,TouchAction::PowerGameScreen)->label)=="Off in games: On battery");
  assert(String(screenButton(screen,TouchAction::PowerAutoSleep)->label)=="Deep sleep: 5 min");
  tapButton(TouchAction::PowerScreenOff); tapButton(TouchAction::PowerGameScreen);
  for (int i=0;i<5;++i) tapButton(TouchAction::PowerAutoSleep);
  assert(powerSettings.screenOff==1 && powerSettings.gameScreen==1 && powerSettings.autoSleep==0);
  assert(fixturePowerSettingsSaved==packPowerSettings(powerSettings));
  assert(String(screenButton(currentScreen(),TouchAction::PowerAutoSleep)->label)=="Deep sleep: Off");
  for (const TouchButton &b : currentScreen().buttons) if (b.action!=TouchAction::None)
    assert(b.w>=44 && b.h>=44 && b.y+b.h<=ATLAS_SCREEN_HEIGHT);
  tapButton(TouchAction::CloseScreen); assert(currentScreen().kind==ScreenKind::Device);

  // Off: idle never sleeps Atlas on the cell.
  fixtureOnBattery=true; fixtureBattery=BatteryReading(); fixtureBattery.present=true; fixtureBattery.percent=50;
  fixtureSleeps=0; serviceAutoSleep(testNow); testNow+=2UL*60UL*60000UL; serviceAutoSleep(testNow);
  assert(!sleepScheduled());
  fixtureOnBattery=false; fixtureBattery=BatteryReading(); serviceAutoSleep(testNow);

  powerSettings=PowerSettings(); fixturePowerSettingsSaved=-1;
  endHostedGame(); resetTouchControls();
}

// A standalone tablet game's finished record: imported once, credited to the
// explicit Atlas-scoped profiles only; invalid mappings never take a receipt.
static void standaloneImport() {
  MemoryBlobs blobs; TurnHubStandalone::useImportStore(&blobs);
  endHostedGame(); TurnHub::fixtureRadio=false; testNow=1000;
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

static void matchHistory() {
  MemoryBlobs blobs; TurnHubStandalone::useImportStore(&blobs);
  endHostedGame(); TurnHub::fixtureRadio=false; testNow=1000;
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
  endHostedGame();
  TurnHubHistory::useFiles(nullptr);
  assert(request("/api/profiles/history",ana,{{"profileId",anaId}},HTTP_GET)==503);
  assert(request("/api/session/logout",ana)==200 && request("/api/session/logout",ben)==200);
  ProfileFixture::profiles.erase(anaId.c_str()); ProfileFixture::profiles.erase(benId.c_str());
  TurnHubAccounts::accounts.erase(anaId.c_str()); TurnHubAccounts::accounts.erase(benId.c_str());
  TurnHubStandalone::useImportStore(nullptr);
}

// The app's planar catalog and saved commanders on Atlas (owner, 2026-10-30):
// a digital Planechase deck dealt from the card, and commanders picked from
// each profile's saved ones on the app, the tablet or the Atlas screen.
static void cardCacheOnAtlas() {
  using namespace TurnHub;
  static MemoryCardStore card;
  card.records.clear();
  TurnHubCards::setStoreForTests(&card);
  endHostedGame(); fixtureRadio=false;
  String tokens[2], ids[2];
  {
    char list[TurnHubProfiles::MAX_LOGIN_PROFILES][TurnHubProfiles::PROFILE_ID_LENGTH + 1];
    const size_t count = TurnHubProfiles::listProfileIds(list, TurnHubProfiles::MAX_LOGIN_PROFILES);
    uint8_t found = 0;
    for (size_t i = 0; i < count && found < 2; ++i) {
      if (request("/api/session/login","",{{"profileId",list[i]},{"pin","1234"}})!=200) continue;
      const String token=responseField("token");
      ids[found]=list[i]; tokens[found++]=token;
    }
    assert(found==2);
  }
  const String &a=tokens[0], &b=tokens[1];
  auto has = [](const char *text) { return server.body.find(text)!=std::string::npos; };
  // The catalog: two planes and a phenomenon, then a reload from the card.
  const String planes = String("11111111-1111-1111-1111-111111111111\tAkoum\t0\n") +
      "22222222-2222-2222-2222-222222222222\tBant\t0\n" + "33333333-3333-3333-3333-333333333333\tChaotic Aether\t1\n";
  assert(request("/api/planes","",{},HTTP_GET)==200 && has("\"count\":0"));
  assert(request("/api/planes","",{{"plain",planes},{"version","v1"}})==401);
  assert(request("/api/planes",a,{{"plain","bad line"},{"version","v1"}})==400);
  // In RAM at once; the SD worker writes the records afterwards.
  assert(request("/api/planes",a,{{"plain",planes},{"version","v1"}})==200 && has("\"queued\":true"));
  assert(TurnHubCards::pendingPlaneJobs()==1 && TurnHubCards::planeCount()==3);
  while (TurnHubCards::servicePlaneStorage(0)) {}
  assert(TurnHubCards::pendingPlaneJobs()==0);
  TurnHubCards::setStoreForTests(&card);  // Forgets RAM, as a reboot does.
  assert(TurnHubCards::planeCount()==0 && TurnHubCards::loadPlanes() && TurnHubCards::planeCount()==3);
  assert(!strcmp(TurnHubCards::planesVersion(),"v1") && !strcmp(TurnHubCards::findPlane("22222222-2222-2222-2222-222222222222")->name,"Bant"));
  // Saved commanders live on the app now; Atlas still lists what a profile kept.
  assert(request("/api/profiles/commanders",a,{},HTTP_GET)==200);
  assert(request("/api/control/commander-choice",a,{{"name","Kenrith"}})==404);
  const String &mine=a;
  {
    // Atlas never waits for the card on the loop (owner, 2026-11-03): the SD
    // worker reads an index of the planes folder once per mount and writes
    // queued cards; the files of the old layout ("pc-<id>.*") go meanwhile.
    const String id="22222222-2222-2222-2222-222222222222";
    MemoryArchive cards;
    cards.files[std::string("pc-")+id.c_str()+".txt"]="old"; cards.files[std::string("pc-")+id.c_str()+".jpg"]="old";
    cards.files["games.jsonl"]="";
    TurnHubCards::setCardFilesForTests(&cards);
    assert(request("/api/planes",mine,{},HTTP_GET)==503);  // The index isn't read yet: no guessing.
    assert(request("/api/planes/card",mine,{{"id",id},{"text","x"},{"plain",""}})==503);
    assert(TurnHubCards::servicePlaneStorage(0) && TurnHubCards::planeIndexReady());
    assert(request("/api/planes",mine,{},HTTP_GET)==200 && has("\"cards\":[]"));
    assert(request("/api/planes/card",mine,{{"id",id},{"type","Phenomenon"},{"artist","A. Painter"},
        {"text","Made-up rules text.\nSecond line."},{"chaos",""},{"plain","/9j/2Q=="}})==202 && has("\"queued\":true") && has("\"art\":true"));
    assert(request("/api/planes/card",mine,{{"id","00000000-0000-0000-0000-000000000000"},{"text","x"},{"plain",""}})==409);
    assert(request("/api/planes/card",mine,{{"id",id},{"text","x"},{"plain","not base64!"}})==400);
    assert(request("/api/planes",mine,{},HTTP_GET)==200 && has("\"cards\":[]"));  // Queued, not written yet.
    while (TurnHubCards::servicePlaneStorage(0)) {}
    char stem[9]; TurnHubCards::planeFileStem(id.c_str(),stem);
    assert(strlen(stem)==8 && cards.files.count(std::string("planes/")+stem+".jpg") && cards.files.count(std::string("planes/")+stem+".txt"));
    assert(!cards.files.count(std::string("pc-")+id.c_str()+".txt") && !cards.files.count(std::string("pc-")+id.c_str()+".jpg") &&
        cards.files.count("games.jsonl"));
    assert(request("/api/planes",mine,{},HTTP_GET)==200 && has((String("\"cards\":[\"")+id+"\"]").c_str()) && has((String("\"art\":[\"")+id+"\"]").c_str()));
    // Three wait at most; the next is told to try again, and nothing is lost.
    for (int i=1;i<=3;++i)
      assert(request("/api/planes/card",mine,{{"id",id},{"text","x"},{"plain",""},{"index",String(i)},{"total","9"}})==202);
    assert(request("/api/planes/card",mine,{{"id",id},{"text","x"},{"plain",""},{"index","4"},{"total","9"}})==503);
    assert(TurnHubCards::pendingPlaneJobs()==3);
    while (TurnHubCards::servicePlaneStorage(0)) {}
    showPlaneCardProgress(testNow);
    assert(String(currentScreen().sync)=="Plane cards: 3 of 9");
    assert(request("/api/planes",mine,{},HTTP_GET)==200 && has("\"art\":[]"));  // Text only now.
    // A fresh index (a remount) finds the same files.
    assert(TurnHubCards::servicePlaneStorage(1) && TurnHubCards::planeIndexReady());
    bool art=true; assert(TurnHubCards::hasPlaneCard(id.c_str(),art) && !art);
    assert(request("/api/planes/card",mine,{{"id",id},{"text","Made-up rules text.\nSecond line."},{"artist","A. Painter"},
        {"type","Phenomenon"},{"plain","/9j/2Q=="},{"index","9"},{"total","9"}})==202);
    while (TurnHubCards::servicePlaneStorage(1)) {}
    showPlaneCardProgress(testNow);
    assert(String(currentScreen().sync)=="All 9 plane cards saved");
    testNow+=SYNC_TOAST_MS+1; assert(!currentScreen().sync[0]);
    TurnHubCards::setCardFilesForTests(nullptr);
  }
  assert(request("/api/atlas/screen",a,{{"view","card"}})==404);
  TurnHubCards::setStoreForTests(nullptr);
  TurnHubCards::SavedCommanders none;
  assert(!TurnHubCards::loadCommanders(ids[0], none));
  endHostedGame();
}

// A standalone Star Commander record credits every listed winner.
static void starStandaloneImport() {
  using TurnHubProfiles::LastGameResult;
  MemoryBlobs blobs; TurnHubStandalone::useImportStore(&blobs);
  endHostedGame(); TurnHub::fixtureRadio = false; testNow = 1000;
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

// Mana Tap on the Atlas screen: opened from Home, played by touch, paused
// when its screen closes or a turn passes; it never changes table state.
static void manaTapOnAtlas() {
  freshTable(); pairingActive=false; resetTouchControls(true);
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
  // Leaving the screen pauses it; Home's button carries on the same run.
  tapButton(TouchAction::OpenGame); updateTouchControls(testNow,false,0,0);
  tapButton(TouchAction::OpenHome); tapButton(TouchAction::OpenManaTap);
  s=currentScreen(); assert(s.mana.phase()==TurnHub::ManaPhase::Paused && s.mana.score()==1 && !s.manaTurnPaused);
  tapButton(TouchAction::ManaAction); assert(currentScreen().mana.phase()==TurnHub::ManaPhase::Playing);
  // A turn passing in the match pauses it and shows the Game tab.
  tapButton(TouchAction::ManaAction); tapButton(TouchAction::OpenGame); hostGame();
  tapButton(TouchAction::OpenHome); tapButton(TouchAction::OpenManaTap); tapButton(TouchAction::ManaAction);
  assert(currentScreen().mana.phase()==TurnHub::ManaPhase::Playing);
  hubPublish("RUNNING",1,2); updateTouchControls(testNow,false,0,0);  // The hub passes the turn.
  s=currentScreen(); assert(s.kind==ScreenKind::Status);
  tapButton(TouchAction::OpenHome); tapButton(TouchAction::OpenManaTap);
  s=currentScreen(); assert(s.mana.phase()==TurnHub::ManaPhase::Paused && s.manaTurnPaused &&
      String(s.detail)=="Paused: the turn passed");
  // Quit keeps the record, shown on Home's button.
  tapButton(TouchAction::ManaQuit); s=currentScreen();
  assert(s.kind==ScreenKind::Home && String(screenButton(s,TouchAction::OpenManaTap)->label)=="Play Mana Tap (best 1)");
  resetTouchControls(); endHostedGame();
}

// Home Wi-Fi's pure parts (home_wifi.h) and its Settings row: the release
// feed the app reads, when a feed check and a join are due, and the toggle.
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

  // Joining: soon after start-up, then backing off after failures, never
  // hopping channels during a match.
  HomeWifiRejoin rejoin;
  assert(!rejoin.due(HOME_WIFI_START_JOIN_MS, false, true) && !rejoin.due(HOME_WIFI_START_JOIN_MS, true, false));
  assert(!rejoin.due(HOME_WIFI_START_JOIN_MS - 1, true, true) && rejoin.due(HOME_WIFI_START_JOIN_MS, true, true));
  rejoin.failed(20000);
  assert(rejoin.waitMs() == HOME_WIFI_REJOIN_MIN_MS && !rejoin.due(20000 + HOME_WIFI_REJOIN_MIN_MS - 1, true, true));
  assert(rejoin.due(20000 + HOME_WIFI_REJOIN_MIN_MS, true, true));
  rejoin.failed(60000);
  assert(rejoin.waitMs() == 2 * HOME_WIFI_REJOIN_MIN_MS);
  for (int i = 0; i < 20; ++i) rejoin.failed(100000);
  assert(rejoin.waitMs() == HOME_WIFI_REJOIN_MAX_MS);
  rejoin.requestNow();  // Network saved or setting turned on.
  assert(rejoin.due(100001, true, true));
  rejoin.joined();
  rejoin.lost(900000);  // The router dropped Atlas: retry quickly.
  assert(rejoin.waitMs() == HOME_WIFI_START_JOIN_MS && rejoin.due(900000 + HOME_WIFI_START_JOIN_MS, true, true));
  const uint8_t digest[32] = {0x00, 0x01, 0xab, 0xff};
  char pinHex[HOME_TLS_PIN_HEX_SIZE];
  homeTlsPinHex(digest, pinHex);
  assert(std::string(pinHex).rfind("0001abff", 0) == 0 && std::string(pinHex).size() == 64);
  assert(homeWifiJoinScan(true) == HomeWifiJoinScan::AllChannels);
  assert(homeWifiJoinScan(false) == HomeWifiJoinScan::CurrentChannelOnly);

  endHostedGame();
  TurnHubAtlas::forgetHomeWifiNetwork();
  if (currentScreen().kind != ScreenKind::Menu) tapButton(TouchAction::OpenSettings);
  auto s = currentScreen();
  assert(s.kind == ScreenKind::Menu && String(screenButton(s, TouchAction::HomeWifi)->label) == "Home Wi-Fi: off");
  tapButton(TouchAction::HomeWifi);  // No network saved: it stays off and says where to add one.
  s = currentScreen();
  assert(!TurnHubAtlas::homeWifiEnabled() && std::string(s.notice).find("home network") != std::string::npos);
  assert(TurnHubAtlas::storeHomeWifiNetwork("Home", "password123"));  // Saving turns it on.
  tapButton(TouchAction::HomeWifi);
  assert(!TurnHubAtlas::homeWifiEnabled());
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
  freshTable(); pairingActive=false; resetTouchControls(true);
  assert(currentScreen().kind==ScreenKind::Home);
  for (TouchAction action : {TouchAction::OpenHome,TouchAction::OpenGame,TouchAction::OpenSettings})
    assert(screenButton(currentScreen(),action));
  assert(!screenButton(currentScreen(),TouchAction::OpenOverview));
  tapButton(TouchAction::OpenGame);
  assert(currentScreen().kind==ScreenKind::Status);
  hostGame();
  // Touching the players changes nothing: the hub owns the game.
  const std::string before=TurnHubHosted::hostedTable().stateJson();
  int16_t x,y,w,h;screenChipCell(0,2,x,y,w,h);
  touchAt(x+w/4,y+h-8);touchRelease();touchAt(x+3*w/4,y+h-8);touchRelease();
  assert(TurnHubHosted::hostedTable().stateJson()==before);
  tapButton(TouchAction::OpenSettings);tapButton(TouchAction::OpenDevice);
  assert(!screenButton(currentScreen(),TouchAction::UnpairSigils) &&
      !screenButton(currentScreen(),TouchAction::FactoryResetAtlas) && !screenButton(currentScreen(),TouchAction::SleepAtlas));
  // Each Sigil's roster comes from the hub's game.
  LedRenderer presentation(sigilBus);
  presentation.renderHosted(TurnHubHosted::hostedTable().game(),testNow);
  const auto &roster=sentTableDisplays[0][0];
  assert(roster.total==2 && roster.rows[0].life==40);
  tapButton(TouchAction::OpenGame);
  assert(currentScreen().kind==ScreenKind::Status && TurnHubHosted::hostedTable().stateJson()==before);
  endHostedGame();resetTouchControls();
}

static void phoneHostedGame() {
  freshTable(); testNow=50000;
  auto &hosted=TurnHubHosted::hostedTable(); hosted.clear();
  String anaId,benId;
  const String ana=registerPhone("Hub Ana",anaId), ben=registerPhone("Hub Ben",benId);
  assert(request("/api/hosted/claim","")==401);
  assert(request("/api/hosted/state",ana,{{"state",hostedState("g1",1,"RUNNING",0,40).c_str()}})==409);  // claim first
  assert(request("/api/hosted/claim",ana)==200 && server.body.find("\"host\":\"Hub Ana\"")!=std::string::npos);
  assert(request("/api/hosted/claim",ben)==409 && server.body.find("Hub Ana")!=std::string::npos);
  assert(!hosted.live(testNow));
  // Nothing shows before a game arrives; then the hub's game is the screen.
  assert(request("/api/hosted/state",ana,{{"state","not json"}})==400);
  assert(request("/api/hosted/state",ana,{{"state","{\"revision\":1}"}})==400);
  assert(request("/api/hosted/state",ana,{{"state",hostedState("",5,"LOBBY",0,40).c_str()}})==200);  // setup, no gameId yet
  assert(request("/api/hosted/state",ana,{{"state",hostedState("",2,"LOBBY",0,40).c_str()}})==200);
  assert(request("/api/hosted/state",ana,{{"state",hostedState("g1",3,"RUNNING",1,37).c_str()}})==200);
  assert(hosted.live(testNow) && hosted.game().players.size()==2 && hosted.game().players[0].life==37);
  resetTouchControls(false);
  AtlasScreen screen; buildAtlasScreen(testNow,screen);
  assert(screen.kind==ScreenKind::Status && !strcmp(screen.badge,"PLAYING") && !strcmp(screen.title,"Ben's turn"));
  assert(!strcmp(screen.detail,"Hosted by Hub Ana") && screen.playerCount==2 && screen.showLife);
  assert(screen.players[0].life==37 && (screen.players[1].flags&CHIP_ACTIVE) && !(screen.players[0].flags&CHIP_ACTIVE));
  // Slice 3: the screen's Pass and Pause queue inputs for Ben (the turn); Atlas changes nothing.
  {
    bool pass=false,pause=false;
    for(uint8_t i=0;i<screen.buttonCount;++i) {
      pass|=screen.buttons[i].action==TouchAction::PassTurn; pause|=screen.buttons[i].action==TouchAction::Pause;
      assert(screen.buttons[i].action!=TouchAction::StartGame && screen.buttons[i].action!=TouchAction::ChipLifeMinus);
    }
    assert(pass && pause);
    assert(TurnHubAtlas::queueHostedScreenAction(TurnHubAtlas::HostedScreenAction::Pass));
    assert(!TurnHubAtlas::queueHostedScreenAction(TurnHubAtlas::HostedScreenAction::Start));
    assert(!TurnHubAtlas::queueHostedScreenAction(TurnHubAtlas::HostedScreenAction::Resume));
    const auto queued=hosted.inputsAfter(0);
    assert(queued.size()==1 && queued[0].player==2 && !strncmp(queued[0].id.c_str(),"atlas-",6) &&
        queued[0].action=="{\"kind\":\"pass\",\"value\":0,\"slot\":0,\"source\":0}");
    assert(hosted.game().active && *hosted.game().active==1);
  }
  const uint32_t base=hosted.lastSeq();
  // Only newer revisions of the same game replace it; a retry is fine.
  assert(request("/api/hosted/state",ana,{{"state",hostedState("g1",2,"RUNNING",0,40).c_str()}})==400);
  assert(request("/api/hosted/state",ana,{{"state",hostedState("g1",3,"RUNNING",0,10).c_str()}})==400);
  assert(request("/api/hosted/state",ana,{{"state",hostedState("g1",3,"RUNNING",1,37).c_str()}})==200);
  assert(request("/api/hosted/state",ben,{{"state",hostedState("g1",9,"RUNNING",1,1).c_str()}})==409);
  assert(hosted.game().revision==3);
  // Watchers read it; only the hub reads the input queue.
  assert(request("/api/hosted/state",ben,{},HTTP_GET)==200 && server.body.find("\"live\":true")!=std::string::npos &&
      server.body.find("\"gameId\":\"g1\"")!=std::string::npos);
  assert(hosted.queue(2,"act-1","{\"do\":\"pass\"}",testNow)==base+1);
  assert(hosted.queue(2,"act-1","{\"do\":\"pass\"}",testNow)==base+1);  // same action once
  assert(hosted.queue(3,"act-2","{\"do\":\"pass\"}",testNow)==0);  // no such player
  assert(hosted.queue(1,"act-3","nope",testNow)==0);
  assert(request("/api/hosted/inputs",ben,{},HTTP_GET)==409);
  assert(request("/api/hosted/inputs",ana,{{"after",std::to_string(base).c_str()}},HTTP_GET)==200 &&
      server.body.find("\"id\":\"act-1\"")!=std::string::npos &&
      server.body.find("\"last\":"+std::to_string(base+1))!=std::string::npos);
  assert(request("/api/hosted/inputs",ana,{{"after",std::to_string(base+1).c_str()}},HTTP_GET)==200 && server.body.find("act-1")==std::string::npos);
  // A new game id starts a fresh queue.
  assert(request("/api/hosted/state",ana,{{"state",hostedState("g2",1,"LOBBY",0,40).c_str()}})==200);
  assert(hosted.inputsAfter(0).empty());
  buildAtlasScreen(testNow,screen); assert(!strcmp(screen.badge,"LOBBY") && !strcmp(screen.title,"Game setup") && !screen.showLife);
  {  // Two players in setup: the screen offers Start, which only queues it.
    bool start=false; for(uint8_t i=0;i<screen.buttonCount;++i) start|=screen.buttons[i].action==TouchAction::StartGame;
    assert(start && TurnHubAtlas::queueHostedScreenAction(TurnHubAtlas::HostedScreenAction::Start));
    const auto queued=hosted.inputsAfter(0);
    assert(queued.size()==1 && queued[0].player==0 && queued[0].action=="{\"kind\":\"start\"}");
    assert(hosted.game().state==turnhub::TableState::Lobby);
  }
  // A hub that goes quiet loses the table; the screen goes back to Atlas's own.
  testNow+=TurnHubHosted::LEASE_MS;
  assert(!hosted.live(testNow));
  buildAtlasScreen(testNow,screen); assert(strncmp(screen.detail,"Hosted by",9)!=0);
  assert(request("/api/hosted/claim",ben)==200 && !hosted.hasGame());  // the last hub's game is not Ben's
  assert(request("/api/hosted/release",ana)==409);
  assert(request("/api/hosted/release",ben)==200 && !hosted.lease().held);
  ProfileFixture::profiles.erase(anaId.c_str()); ProfileFixture::profiles.erase(benId.c_str());
}

// Slice 4: a hub that goes away leaves its unfinished game for another phone to resume.
static void hostedResume() {
  freshTable(); testNow=120000;
  auto &hosted=TurnHubHosted::hostedTable(); hosted.clear();
  String anaId,benId;
  const String ana=registerPhone("Resume Ana",anaId), ben=registerPhone("Resume Ben",benId);
  const auto resumeOf=[&](const String &who){ assert(request("/api/hosted/state",who,{},HTTP_GET)==200); return server.body; };
  assert(request("/api/hosted/claim",ana)==200);
  assert(request("/api/hosted/state",ana,{{"state",hostedState("g7",4,"RUNNING",0,33).c_str()}})==200);
  assert(resumeOf(ben).find("\"resume\":null")!=std::string::npos);  // a live hub: nothing to resume
  // Ana's phone goes quiet: Ben sees her game to resume, then takes the table and resumes it.
  testNow+=TurnHubHosted::LEASE_MS;
  std::string body=resumeOf(ben);
  assert(body.find("\"live\":false")!=std::string::npos && body.find("\"resume\":{\"host\":\"Resume Ana\"")!=std::string::npos &&
      body.find("\"gameId\":\"g7\"")!=std::string::npos);
  assert(request("/api/hosted/claim",ben)==200 && !hosted.hasGame());
  assert(hosted.resumeJson(testNow).empty());  // Ben holds the table now
  assert(request("/api/hosted/state",ben,{{"state",hostedState("g7",5,"RUNNING",1,33).c_str()}})==200);
  assert(request("/api/hosted/release",ben)==200);
  body=resumeOf(ana);  // Ben stopped mid-game: still resumable, now Ben's
  assert(body.find("\"resume\":{\"host\":\"Resume Ben\"")!=std::string::npos && body.find("\"revision\":5")!=std::string::npos);
  // A finished game, or setup, is not offered.
  assert(request("/api/hosted/claim",ana)==200);
  assert(request("/api/hosted/state",ana,{{"state",hostedState("g7",6,"GAME_OVER",1,33).c_str()}})==200);
  assert(request("/api/hosted/release",ana)==200 && hosted.resumeJson(testNow).empty());
  assert(resumeOf(ben).find("\"resume\":null")!=std::string::npos);
  hosted.clear();
  ProfileFixture::profiles.erase(anaId.c_str()); ProfileFixture::profiles.erase(benId.c_str());
}

// Slice 2: Sigils bound by localId show the hub's game and queue their keys.
static void hostedSigils() {
  freshTable(); testNow=90000; TurnHub::fixtureRadio=true;
  auto &hosted=TurnHubHosted::hostedTable(); hosted.clear();
  String anaId; const String ana=registerPhone("Sigil Hub",anaId);
  assert(request("/api/hosted/claim",ana)==200);
  const std::string lobby="{\"gameId\":\"\",\"revision\":1,\"state\":\"LOBBY\",\"profile\":\"MTG_COMMANDER\",\"players\":[]}";
  assert(request("/api/hosted/state",ana,{{"state",lobby.c_str()}})==200);
  // An unbound Sigil may join the hub's setup; the hub decides.
  assert(TurnHubAtlas::hostedForSigil(0,testNow));
  assert(sigilMenuFor(0).actions==TurnHubProtocol::sigilActionBit(TurnHubProtocol::SigilAction::Join));
  syncSigilMenus(testNow);
  TurnHubAtlas::handleHostedSigilEvent(0,TurnHubProtocol::PacketType::SelectAction,
      TurnHubProtocol::encodeSelectAction(TurnHubProtocol::SigilAction::Join,sigilMenuRevision(0)));
  auto inputs=hosted.inputsAfter(0); assert(inputs.size()==1 && inputs[0].player==0);
  assert(inputs[0].action=="{\"kind\":\"sigil-join\",\"sigil\":0,\"slot\":1}");
  // The hub seats Sigils 0 and 1 and starts; Ana (Sigil 0) has the turn.
  const std::string running="{\"gameId\":\"g5\",\"revision\":7,\"state\":\"RUNNING\",\"profile\":\"MTG_COMMANDER\",\"active\":0,"
      "\"players\":[{\"name\":\"Ana\",\"life\":38,\"localId\":\"sigil:0:1\",\"commanderDamage\":{\"1\":[2,0]}},"
      "{\"name\":\"Ben\",\"life\":40,\"localId\":\"sigil:1:1\"}]}";
  assert(request("/api/hosted/state",ana,{{"state",running.c_str()}})==200);
  assert(TurnHubHosted::sigilPlayer(hosted.game(),0,1)==0 && TurnHubHosted::sigilPlayer(hosted.game(),1,1)==1);
  leds.invalidateAll(); leds.renderHosted(hosted.game(),testNow,0x3);
  const auto &shown=TurnHub::sentGameDisplays[0];
  assert(shown.primary.life==38 && !strcmp(shown.primary.name,"Ana") && shown.commander==1);
  assert(TurnHubProtocol::displayPrimaryPlayer(shown.state)==1);
  assert(TurnHub::sentGameDisplays[1].primary.life==40 && !strcmp(TurnHub::sentGameDisplays[1].primary.name,"Ben"));
  using TurnHubProtocol::SigilAction; using TurnHubProtocol::sigilActionBit;
  assert(sigilMenuFor(0).actions & sigilActionBit(SigilAction::Pass));
  assert(!(sigilMenuFor(1).actions & sigilActionBit(SigilAction::Pass)) && (sigilMenuFor(1).actions & sigilActionBit(SigilAction::Pause)));
  syncSigilMenus(testNow);
  TurnHubAtlas::handleHostedSigilEvent(0,TurnHubProtocol::PacketType::SelectAction,
      TurnHubProtocol::encodeSelectAction(SigilAction::Pass,sigilMenuRevision(0)));
  // Life keys only for this Sigil's own seat; Ben's Sigil can't move Ana's life.
  TurnHubAtlas::handleHostedSigilEvent(1,TurnHubProtocol::PacketType::LifeAdjust,TurnHubProtocol::encodeLifeAdjust(1,-3));
  TurnHubAtlas::handleHostedSigilEvent(1,TurnHubProtocol::PacketType::LifeAdjust,TurnHubProtocol::encodeLifeAdjust(2,-3));
  // A stale menu choice is dropped (Ben can't pass on Ana's turn).
  TurnHubAtlas::handleHostedSigilEvent(1,TurnHubProtocol::PacketType::SelectAction,
      TurnHubProtocol::encodeSelectAction(SigilAction::Pass,sigilMenuRevision(1)));
  inputs=hosted.inputsAfter(0);  // A new game id cleared the join.
  assert(inputs.size()==2);
  assert(inputs[0].player==1 && inputs[0].action=="{\"kind\":\"pass\",\"value\":0,\"slot\":0,\"source\":0}");
  assert(inputs[1].player==2 && inputs[1].action=="{\"kind\":\"life\",\"value\":-3,\"slot\":0,\"source\":0}");
  // Atlas changed nothing itself: the game is still the hub's.
  assert(hosted.game().players[0].life==38 && hosted.game().active && *hosted.game().active==0);
  assert(request("/api/hosted/release",ana)==200 && !TurnHubAtlas::hostedForSigil(0,testNow));
  TurnHub::fixtureRadio=false;
  ProfileFixture::profiles.erase(anaId.c_str());
}

// GET /api/v1/state and /info as an accessory (atlas-state-v1): the hub
// that holds the table, the battery and the links. The revision moves with
// the hub summary only.
static void atlasStateContract() {
  endHostedGame(); fixtureBattery=TurnHub::BatteryReading();
  assert(request("/api/v1/info","",{},HTTP_GET)==200 && server.body.find("\"protocolVersion\":\"1\"")!=std::string::npos &&
      server.body.find("\"sessionControls\":false")!=std::string::npos);
  saveClientFixture("info", server.body);
  assert(request("/api/v1/state","",{},HTTP_GET)==200 && server.body.find("\"hub\":null")!=std::string::npos &&
      server.body.find("\"battery\":null")!=std::string::npos && server.body.find("\"links\":{\"hotspot\":")!=std::string::npos);
  saveClientFixture("state-idle", server.body);
  const String idle=responseNumber("revision");
  assert(request("/api/v1/state","",{},HTTP_GET)==200 && responseNumber("revision")==idle);
  hostGame("LOBBY");
  assert(request("/api/v1/state","",{},HTTP_GET)==200 && responseNumber("revision")!=idle &&
      server.body.find("\"hub\":{\"name\":\"Fixture hub\",\"gameId\":\"fx\",\"state\":\"LOBBY\",\"players\":2}")!=std::string::npos);
  const String lobby=responseNumber("revision");
  // Life totals are the hub's business: a new publish of the same summary
  // keeps the revision; a state change moves it.
  hubPublish("LOBBY",0,2); assert(request("/api/v1/state","",{},HTTP_GET)==200 && responseNumber("revision")==lobby);
  fixtureBattery.present=true; fixtureBattery.percent=64; fixtureBattery.charging=true;
  hubPublish("RUNNING",1,3);
  assert(request("/api/v1/state","",{},HTTP_GET)==200 && responseNumber("revision")!=lobby &&
      server.body.find("\"state\":\"RUNNING\"")!=std::string::npos &&
      server.body.find("\"battery\":{\"percent\":64,\"low\":false,\"charging\":true}")!=std::string::npos);
  saveClientFixture("state-hosted", server.body);
  fixtureBattery=TurnHub::BatteryReading(); endHostedGame();
  assert(request("/api/v1/state","",{},HTTP_GET)==200 && server.body.find("\"hub\":null")!=std::string::npos);
}

int main() {
  utcBookkeepingClock();
  // Most scenarios inspect the interactive Game destination explicitly.
  resetTouchControls();
  // These scenarios exercise the presence code; tableCodeOptional() covers
  // the default (off).
  tableCodeRequired=true;
  sigilReceivePackets(); std::cout<<"PASS Sigil radio queue preserves legacy and game display packets\n";
  assert(configureIntentHandlers());
  intents.setObserver(observeIntent);
  registerWebCallbacks(); TurnHubWebApi::begin(server);
  dispatcherContract(); std::cout<<"PASS dispatcher contract\n";
  optionalStorage(); std::cout<<"PASS optional storage error policy\n";
  stackDiagnostics(); std::cout<<"PASS stack watermark semantics, warning rate limit, rollover and request tracing\n";
  batteryStability(); std::cout<<"PASS persistent battery stability, safety and source debounce\n";
  batteryGauge(); std::cout<<"PASS battery gauge curve, smoothing, low warning and /api/devices\n";
  chargingEstimate(); std::cout<<"PASS charging estimate: no jump on USB, counted climb, never down, full only after the limit\n";
  powerSource(); std::cout<<"PASS power source: USB pulled and plugged in, slow discharge, no cell, started on the cell\n";
  tenPlayerImportCredit(); std::cout<<"PASS ten-player imports, high winner bit, elimination credit and retry deduplication\n";
  helloCapabilityLayout(); std::cout<<"PASS Hello capability layout and firmware version fields\n";
  serialLogStream();
  otaServiceScenarios(); std::cout<<"PASS OTA service: authorization, variant checks, jobs, single-use downloads, fresh reboot confirmation\n";
  atlasStateContract(); std::cout<<"PASS Atlas state v1: hub summary, battery, links, revision on hub changes only\n";
  deviceNavigation(); std::cout<<"PASS device navigation, read-only table, live settings and Atlas roster projection\n";
  homeWifiCheckIns(); std::cout<<"PASS home Wi-Fi: release feed, check schedule, rejoin backoff, Settings toggle\n";
  manaTapOnAtlas(); std::cout<<"PASS Mana Tap on Atlas: touch play, pause on leaving or a pass, record, no table state\n";
  deliberatePairing(); std::cout<<"PASS deliberate pairing authorization, radio failure, timeout, rollover and gameplay exclusion\n";
  virtualProfileFlow(); std::cout<<"PASS profile registration/login, phone-only game, companion sessions, authorization and throttling\n";
  phoneHostedGame(); std::cout<<"PASS phone-hosted game: lease, revisions, Atlas screen controls queue inputs, input queue, expiry\n";
  hostedResume(); std::cout<<"PASS phone-hosted resume: a quiet or departed hub leaves its unfinished game for another phone\n";
  hostedSigils(); std::cout<<"PASS phone-hosted Sigils: join, bound seats drawn from the hub game, menus, keys queued not applied\n";
  profilePolicyFlow(); std::cout<<"PASS profile choices, physical authorization, companion privacy, expiry and claim revalidation\n";
  cardCacheOnAtlas(); std::cout<<"PASS Atlas card cache: planar catalog on the card, digital deck, phenomena, Planes screen, saved commanders\n";
  starStandaloneImport(); std::cout<<"PASS Star Commander standalone import: every listed winner credited, invalid winner sets refused\n";
  accountPermissionsAndModeration(); std::cout<<"PASS account setup, independent permissions, moderation, revocation and private counts\n";
  updateNotice(); std::cout<<"PASS update notice: app-reported versions, devices behind, Menu and Info text\n";
  atlasScreens(); std::cout<<"PASS Atlas screens: player chips, NO SD CARD, info, Wi-Fi QR code, turn clock" << std::endl;
  atlasSpeaker(); std::cout<<"PASS Atlas speaker: table-wide cues, phone-only table, Sigil mute independence, admin volume setting\n";
  standaloneImport(); std::cout<<"PASS standalone import: exact Atlas/profile scope, no name fallback, invalid mapping, guests, per-player receipts, duplicates, storage failure, draw\n";
  matchHistory(); std::cout<<"PASS match history: whole records kept once with Atlas profile IDs, CSV rows, torn lines, history route\n";
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
  powerSettingsChoices(); std::cout<<"PASS screen and sleep settings: Atlas screen and Admin HTTP" << std::endl;
  deviceManagement(); std::cout<<"PASS admin forget one/all Sigils, seated and in-game refusal, storage failure, pairing window setting\n";
  firstRunSetup(); std::cout<<"PASS first-run setup: boot stage, Welcome and Skip, account, table code, private Wi-Fi password, finish, all set, Pair a Sigil\n";
  artworkUploads(); std::cout<<"PASS bounded artwork upload, private review, approved public delivery and Atlas thumbnail\n";
  jewelColors(); std::cout<<"PASS personalization: Jewel color and preset avatars, validation, /api/avatars, /api/seats, TFT chips, SeatColor; custom stays private\n";
  serialLogCapture(); std::cout<<"PASS serial log capture, redaction, stream draining, ring overflow and self-describing log lines\n";
}
