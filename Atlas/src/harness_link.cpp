// Link to a hardware test harness. See harness_link.h.
#include "harness_link.h"

#include "atlas_app.h"
#include "serial_log.h"

using TurnHub::serialLog;
using TurnHubProtocol::HarnessCommandKind;
using TurnHubProtocol::HarnessReportFields;

namespace TurnHubAtlas {

namespace {

bool reportValid = false;
uint8_t reportSigilId = INVALID_ID;
HarnessReportFields lastReport;
uint32_t reportAtMs = 0;

bool isHarness(uint8_t sigilId) {
  const TurnHub::SigilRecord *record = sigilBus.record(sigilId);
  return record != nullptr && record->helloInfoValid &&
      (record->capabilities & TurnHubProtocol::CAPABILITY_HARNESS) != 0;
}

bool sendCommand(int32_t value, uint32_t nowMs) {
  const uint8_t id = harnessSigilId(nowMs);
  return id != INVALID_ID &&
      sigilBus.send(id, TurnHubProtocol::PacketType::HarnessCommand, value);
}

}  // namespace

uint8_t harnessSigilId(uint32_t nowMs) {
  for (uint8_t id = 0; id < MAX_PHYSICAL_SIGILS; ++id) {
    if (isHarness(id) && sigilBus.isOnline(id, nowMs)) return id;
  }
  return INVALID_ID;
}

bool requestHarnessTest(TurnHubProtocol::HarnessTest test, uint32_t nowMs) {
  const bool sent = sendCommand(TurnHubProtocol::encodeHarnessCommand(HarnessCommandKind::Run, test), nowMs);
  serialLog.print("ATLAS|HARNESS|RUN|");
  serialLog.print(TurnHubProtocol::harnessTestName(static_cast<uint8_t>(test)));
  serialLog.println(sent ? "|SENT" : "|NO_HARNESS");
  return sent;
}

bool requestHarnessStop(uint32_t nowMs) {
  const bool sent = sendCommand(TurnHubProtocol::encodeHarnessCommand(HarnessCommandKind::Stop), nowMs);
  serialLog.println(sent ? "ATLAS|HARNESS|STOP|SENT" : "ATLAS|HARNESS|STOP|NO_HARNESS");
  return sent;
}

void noteHarnessReport(uint8_t sigilId, int32_t value, uint32_t nowMs) {
  if (!isHarness(sigilId)) return;
  const HarnessReportFields report = TurnHubProtocol::decodeHarnessReport(value);
  const bool changed = !reportValid || reportSigilId != sigilId ||
      report.state != lastReport.state || report.test != lastReport.test ||
      report.step != lastReport.step || report.failed != lastReport.failed;
  reportValid = true;
  reportSigilId = sigilId;
  lastReport = report;
  reportAtMs = nowMs;
  if (!changed) return;
  char line[96];
  snprintf(line, sizeof(line), "ATLAS|HARNESS|REPORT|%u|state=%u|test=%s|step=%s|passed=%u|failed=%u",
      static_cast<unsigned>(sigilId), static_cast<unsigned>(report.state),
      TurnHubProtocol::harnessTestName(report.test), TurnHubProtocol::harnessStepName(report.step),
      static_cast<unsigned>(report.passed), static_cast<unsigned>(report.failed));
  serialLog.println(line);
}

bool harnessReport(uint32_t nowMs, HarnessReportFields &report) {
  if (!reportValid || nowMs - reportAtMs >= HARNESS_REPORT_STALE_MS ||
      harnessSigilId(nowMs) != reportSigilId) {
    return false;
  }
  report = lastReport;
  return true;
}

void resetHarnessLink() {
  reportValid = false;
  reportSigilId = INVALID_ID;
  lastReport = HarnessReportFields();
  reportAtMs = 0;
}

}  // namespace TurnHubAtlas
