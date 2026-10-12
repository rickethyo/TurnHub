#include <cassert>
#include <iostream>
#include <new>
// Exercise the actual ADC/NVS/RTC orchestration, not a copied restore flow.
// RTC attributes/reset reason and ADC conversions are the hardware boundary.
#include "../../src/atlas_battery.cpp"

uint32_t testNow = 0, fixtureAdcPinMv = 1900, testRandom = 0;
int fixtureResetReason = ESP_RST_POWERON, loggedErrors = 0;
SerialStub Serial;
std::map<std::string,std::vector<uint8_t>> fixtureFlash;
std::vector<uint8_t> fixturePending;
unsigned fixtureCommits = 0;
bool fixtureFailCommit = false;

static void reboot(int reason, uint16_t mv) {
  using namespace TurnHubAtlas;
  // Simulate process RAM initialization but preserve flash and RTC separately.
  gauge.reset(); power = TurnHub::PowerSourceTracker();
  publishedReading = TurnHub::BatteryReading();
  checkpointStore.~NvsBlobStore(); new (&checkpointStore) TurnHubStorage::NvsBlobStore();
  checkpointPolicy = TurnHub::BatteryCheckpointPolicy();
  bootCheckpoint = TurnHub::BatteryCheckpoint();
  restoreStatus = TurnHub::BatteryRestore::Missing;
  haveBootCheckpoint = bootWasRtc = false; startupSamples = 0;
  gaugeSum = gaugeCount = lastSampleMs = lastDiagnosticMs = loggedCorrections = 0;
  sampled = loggedPresent = loggedCharging = loggedCritical = false; loggedPercent = 0;
  fixtureResetReason = reason; fixtureAdcPinMv = mv/2; testNow = 0;
  TurnHub::serialLog.clear();
  beginAtlasBattery();
}
static void seconds(unsigned count) {
  for (unsigned i=0;i<count*4;++i) {
    testNow += 250; TurnHubAtlas::serviceAtlasBattery(testNow);
  }
}
static void store(uint8_t percent, uint16_t mv, bool usb = false) {
  TurnHub::BatteryReading r; r.present = true; r.percent = percent; r.millivolts = mv;
  const auto c = TurnHub::makeBatteryCheckpoint(r,usb);
  fixtureFlash["gauge"] = std::vector<uint8_t>(c.bytes,c.bytes+sizeof(c));
}
int main() {
  using namespace TurnHubAtlas;
  using TurnHub::BatteryRestore;
  store(35,3800);
  reboot(ESP_RST_SW,3800);
  seconds(3); assert(!atlasBattery().present && !atlasBattery().criticalLow);
  seconds(2);
  assert(atlasBattery().present && atlasBattery().percent==35 && restoreStatus==BatteryRestore::RestoredNvs);
  // Same voltage cannot prove USB; charging flag derives from fresh detection.
  assert(atlasBattery().charging);
  seconds(29); assert(atlasBattery().percent==35);
  seconds(1); assert(atlasBattery().percent==36);
  seconds(24); assert(fixtureCommits==0);
  seconds(60); assert(fixtureCommits==1); // at most one write per minute

  // New RTC evidence wins on deep sleep; cold/software reboot ignores it.
  TurnHub::BatteryReading newer; newer.present=true; newer.percent=45; newer.millivolts=3830;
  rtcCheckpoint=TurnHub::makeBatteryCheckpoint(newer,false);
  store(35,3830);
  reboot(ESP_RST_DEEPSLEEP,3830); seconds(5);
  assert(restoreStatus==BatteryRestore::RestoredRtc && atlasBattery().percent==45);
  seconds(56); assert(fixtureFlash["gauge"][5]>=45); // RTC progress eventually reaches flash.
  store(35,3830);
  reboot(ESP_RST_SW,3830); seconds(5);
  assert(restoreStatus==BatteryRestore::RestoredNvs && atlasBattery().percent==35);

  store(90,4100);
  reboot(ESP_RST_SW,3500); seconds(1);
  assert(atlasBattery().present && atlasBattery().criticalLow && atlasBattery().percent<10);
  seconds(4); assert(restoreStatus==BatteryRestore::Contradicted);
  // Corrupt and wrong-sized records cannot restore charge.
  store(35,3800); fixtureFlash["gauge"][5]^=1;
  reboot(ESP_RST_SW,3800); seconds(5); assert(restoreStatus==BatteryRestore::Invalid);
  fixtureFlash["gauge"].resize(3);
  reboot(ESP_RST_SW,3800); seconds(5); assert(restoreStatus==BatteryRestore::Invalid);
  // One startup sample at a charger-like voltage cannot invalidate history.
  store(35,3800); reboot(ESP_RST_SW,3800); seconds(4);
  fixtureAdcPinMv=2050; seconds(1);
  assert(restoreStatus==BatteryRestore::RestoredNvs && atlasBattery().percent==35);
  fixtureFlash.clear();
  reboot(ESP_RST_SW,3800); seconds(5); assert(restoreStatus==BatteryRestore::Missing);

  // Failed real adapter commits retry only after the minimum interval.
  fixtureFailCommit=true; const auto before=fixtureCommits;
  seconds(56); assert(fixtureCommits==before+1 && fixtureFlash.empty());
  seconds(59); assert(fixtureCommits==before+1);
  seconds(1); assert(fixtureCommits==before+2);
  fixtureFailCommit=false; seconds(60); assert(fixtureCommits==before+3 && !fixtureFlash.empty());
  assert(TurnHub::serialLog.snapshot().find("CHECKPOINT_SAVED")!=std::string::npos);

  // A high cosmetic estimate disproved by critical voltage must invalidate
  // older RTC history before charger elevation on the subsequent USB wake.
  gauge.restorePercent(95); fixtureAdcPinMv=1750; seconds(10);
  assert(atlasBattery().criticalLow && atlasBattery().percent>50);
  assert(TurnHub::validBatteryCheckpoint(rtcCheckpoint) && !(rtcCheckpoint.bytes[8]&1));
  reboot(ESP_RST_DEEPSLEEP,4100); seconds(5);
  assert(restoreStatus==BatteryRestore::Contradicted && atlasBattery().percent<80);
  // Confirmed absence becomes an RTC tombstone; replacement is not restored.
  fixtureAdcPinMv=0; seconds(4); assert(!atlasBattery().present);
  assert(TurnHub::validBatteryCheckpoint(rtcCheckpoint) && !(rtcCheckpoint.bytes[8]&1));
  reboot(ESP_RST_DEEPSLEEP,4100); seconds(5);
  assert(restoreStatus==BatteryRestore::Contradicted && atlasBattery().percent!=35);
  std::cout << "PASS real battery ADC adapter: NVS restart, RTC wake, corrupt/stale history, absence and failed-write throttling\n";
}
