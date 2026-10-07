#include "sigil_update_service.h"
#include "sigil_update_page.h"
#include "atlas_app.h"
#include "sigil_update_jobs.h"
#include "firmware_package_mbedtls.h"
#include "firmware_signing_key.h"
#include "web_api_internal.h"
#include "account_access.h"
#include "serial_log.h"
#include <esp_ota_ops.h>
#include <esp_system.h>

using TurnHub::serialLog;

namespace TurnHubAtlas {
namespace {
class IdleFlash final : public PackageFlash {
 public:
  const esp_partition_t *slot = nullptr;
  uint32_t capacity() const override { return slot ? slot->size : 0; }
  bool eraseSector(uint32_t at) override {
    return slot && esp_partition_erase_range(slot, at, PACKAGE_SECTOR_BYTES) == ESP_OK;
  }
  bool write(uint32_t at, const uint8_t *data, size_t n) override {
    return slot && esp_partition_write(slot, at, data, n) == ESP_OK;
  }
  bool read(uint32_t at, uint8_t *data, size_t n) override {
    return slot && esp_partition_read(slot, at, data, n) == ESP_OK;
  }
} flash;
TurnHubFirmwarePackage::MbedtlsPackageCrypto crypto;
SigilPackageStore store;
SigilUpdateJobs jobs;
char apSsid[TurnHubProtocol::UPDATE_SSID_BYTES] = {};
char apPassword[TurnHubProtocol::UPDATE_PASSWORD_BYTES] = {};
uint32_t initialSession = 0;
bool downloadUsed = false;
bool uploadOk = false;
bool uploadDenied = false;
bool uploadSeen = false;
constexpr uint32_t UPLOAD_STALL_MS = 30000;
uint32_t lastUploadChunkMs = 0;
bool betweenGames() { return allTablesBetweenGames(); }
using namespace TurnHubWebApi::internal;

void statusRoute() {
  if (!TurnHubWebApi::requirePermission(server, TurnHubAccounts::Admin)) return;
  String json = "{\"staged\":";
  json += store.staged() ? "true" : "false";
  json += ",\"product\":"; json += String(store.staged() ? store.header().product : 0);
  json += ",\"version\":\"";
  if (store.staged()) {
    json += String(store.header().version.major) + "." + String(store.header().version.minor) + "." + String(store.header().version.patch);
  }
  json += "\",\"busy\":"; json += sigilUpdatesBusy() ? "true" : "false";
  json += ",\"sigilId\":"; json += String(jobs.sigilId());
  json += ",\"stage\":\""; json += updateJobStageName(jobs.stage());
  json += "\",\"progress\":"; json += String(jobs.progress());
  json += ",\"message\":\""; json += jsonEscape(jobs.message()); json += "\"}";
  sendJson(server, 200, json);
}

void uploadChunk() {
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    // Multipart requests may contain only one package.
    if (uploadSeen) { store.abortUpload(); uploadDenied = true; uploadOk = false; return; }
    uploadSeen = true;
    uploadOk = false;
    // No response may be sent from here: the browser is still sending the
    // body. The POST handler answers once the upload is over.
    uploadDenied = !TurnHubWebApi::hasPermission(server, TurnHubAccounts::Admin) ||
        !TurnHubWebApi::verifiedAtTable(server) || !betweenGames() || sigilUpdatesBusy() || ota.inProgress();
    if (!uploadDenied) store.startUpload();
    lastUploadChunkMs = millis();
    serialLog.println(uploadDenied ? "ATLAS|SIGIL_OTA|UPLOAD|DENIED" : "ATLAS|SIGIL_OTA|UPLOAD|START");
  } else if (u.status == UPLOAD_FILE_WRITE && !uploadDenied) {
    store.writeUpload(u.buf, u.currentSize);
    lastUploadChunkMs = millis();
  } else if (u.status == UPLOAD_FILE_END && !uploadDenied) {
    uploadOk = store.finishUpload();
    serialLog.print("ATLAS|SIGIL_OTA|UPLOAD|");
    serialLog.println(uploadOk ? "STAGED" : TurnHubFirmwarePackage::errorName(store.error()));
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (!uploadDenied) store.abortUpload();
    uploadOk = false;
    uploadSeen = false;
    serialLog.println("ATLAS|SIGIL_OTA|UPLOAD|ABORTED");
  }
}

// An upload the web server never finished or aborted (the phone left
// mid-transfer) would keep sigilUpdatesBusy() true and block every game
// start. Chunks arrive inside handleClient(), so the loop only sees this
// silence when the upload is really orphaned.
void expireOrphanedUpload(uint32_t nowMs) {
  if (!store.uploading() || nowMs - lastUploadChunkMs < UPLOAD_STALL_MS) return;
  store.abortUpload();
  uploadOk = uploadDenied = uploadSeen = false;
  serialLog.println("ATLAS|SIGIL_OTA|UPLOAD|STALLED");
}

// Logs each job stage change and remembers when a job ended, so the Atlas
// screen can show the outcome for a while (sigilUpdateNotice).
UpdateJobStage loggedStage = UpdateJobStage::Idle;
uint32_t jobEndedMs = 0;
void noteJobStage(uint32_t nowMs) {
  if (jobs.stage() == loggedStage) return;
  loggedStage = jobs.stage();
  if (loggedStage == UpdateJobStage::Done || loggedStage == UpdateJobStage::Failed) jobEndedMs = nowMs;
  serialLog.printf("ATLAS|SIGIL_OTA|JOB|%u|%s|%s\n", static_cast<unsigned>(jobs.sigilId()),
      updateJobStageName(loggedStage), jobs.message());
}

void downloadRoute() {
  if (downloadUsed || !store.staged() || !jobs.downloadAllowed(server.arg("token").c_str())) {
    sendError(server, 403, "No active download"); return;
  }
  downloadUsed = true;
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(store.packageSize());
  server.send(200, "application/octet-stream", "");
  auto client = server.client();
  uint8_t buffer[1024];
  for (uint32_t at = 0; at < store.packageSize();) {
    size_t n = store.packageSize() - at;
    if (n > sizeof(buffer)) n = sizeof(buffer);
    if (!store.read(at, buffer, n) || client.write(buffer, n) != n) {
      client.stop(); jobs.cancel("Download interrupted; start a new update to retry"); return;
    }
    at += n;
    // Drain radio statuses during the synchronous HTTP transfer.
    processSigilEvents();
    delay(1);
  }
}
}  // namespace

bool sigilUpdatesBusy() { return store.uploading() || jobs.busy(); }

bool sigilUpdateNotice(char *out, size_t size, uint32_t nowMs) {
  constexpr uint32_t OUTCOME_SHOWN_MS = 60000;
  if (size == 0) return false;
  out[0] = '\0';
  noteJobStage(nowMs);
  const UpdateJobStage stage = jobs.stage();
  const unsigned sigil = static_cast<unsigned>(jobs.sigilId()) + 1;
  if (jobs.busy()) {
    if (stage == UpdateJobStage::Downloading) {
      snprintf(out, size, "Sigil %u update: %u%%", sigil, static_cast<unsigned>(jobs.progress()));
    } else {
      snprintf(out, size, "Sigil %u update: %s", sigil, jobs.message());
    }
    return true;
  }
  if (store.uploading()) {
    snprintf(out, size, "Receiving Sigil firmware");
    return true;
  }
  if ((stage == UpdateJobStage::Failed || stage == UpdateJobStage::Done) &&
      nowMs - jobEndedMs < OUTCOME_SHOWN_MS) {
    snprintf(out, size, stage == UpdateJobStage::Done ? "Sigil %u updated" : "Sigil %u update failed: %s",
        sigil, jobs.message());
    return true;
  }
  return false;
}
void invalidateSigilPackage() { store.invalidate(); }
void noteSigilUpdateStatus(uint8_t id, int32_t value, uint32_t nowMs) {
  const auto before = jobs.stage();
  jobs.onStatus(id, value, nowMs);
  if (before != UpdateJobStage::Installed && jobs.stage() == UpdateJobStage::Installed) {
    const auto *r = sigilBus.record(id);
    if (r) initialSession = r->sessionGeneration;
  }
}

TurnHub::IntentResult handleUpdateSigilIntent(const TurnHub::Intent &intent, void *) {
  TurnHubAccounts::Account admin;
  if (intent.actor.origin != IntentOrigin::Browser ||
      !TurnHubAccounts::load(String(intent.payload.moderatorId), admin) || admin.archived ||
      !(admin.permissions & TurnHubAccounts::Admin) ||
      !presenceConfirmedFor(String(intent.payload.moderatorId), millis())) {
    return IntentResult::reject(IntentStatus::Unauthorized, "Admin verified at the table required");
  }
  if (!betweenGames() || factoryResetScheduled() || sleepScheduled() || ota.inProgress() || sigilUpdatesBusy())
    return IntentResult::reject(IntentStatus::Conflict, "Update between games with no other update running");
  const int32_t id = intent.payload.value;
  const auto *r = id >= 0 && id < MAX_PHYSICAL_SIGILS ? sigilBus.record(id) : nullptr;
  if (!r || !r->helloInfoValid || !r->session.ready() || !sigilBus.isOnline(id, millis()) ||
      (r->capabilities & TurnHubProtocol::CAPABILITY_HARNESS))
    return IntentResult::reject(IntentStatus::InvalidActor, "Choose an online paired Sigil, not a harness");
  const auto product = (r->capabilities & TurnHubProtocol::CAPABILITY_DISPLAY_OLED) ?
      TurnHubFirmwarePackage::Product::SigilOled : TurnHubFirmwarePackage::Product::SigilEink;
  if (!store.staged() || store.header().product != static_cast<uint8_t>(product))
    return IntentResult::reject(IntentStatus::Rejected, "Upload a package for this Sigil's display first");
  if (!TurnHubFirmwarePackage::versionAllowed({r->firmwareMajor, r->firmwareMinor, r->firmwarePatch}, store.header().version))
    return IntentResult::reject(IntentStatus::Rejected, "The Sigil already runs newer firmware");
  uint8_t token[TurnHubProtocol::UPDATE_TOKEN_BYTES];
  esp_fill_random(token, sizeof(token));
  initialSession = r->sessionGeneration;
  downloadUsed = false;
  jobs.start(id, store.header(), token, millis());
  return IntentResult::accept("Update started");
}

void serviceSigilUpdates(uint32_t nowMs) {
  expireOrphanedUpload(nowMs);
  noteJobStage(nowMs);
  if (!jobs.busy()) return;
  const auto *r = sigilBus.record(jobs.sigilId());
  if (!r) { jobs.cancel("The Sigil is no longer paired"); return; }
  // Cached Hello metadata cannot confirm a reboot. Require a new session.
  const bool fresh = r->helloInfoValid && r->sessionGeneration != initialSession &&
      r->confirmedSessionGeneration == r->sessionGeneration &&
      sigilBus.isOnline(jobs.sigilId(), nowMs);
  if (!jobs.tick(nowMs, fresh, {r->firmwareMajor, r->firmwareMinor, r->firmwarePatch})) return;
  TurnHubProtocol::SigilUpdateOfferPacket offer{};
  offer.version = TurnHubProtocol::VERSION;
  offer.type = PacketType::SigilUpdateOffer;
  offer.sigilId = jobs.sigilId(); offer.product = store.header().product;
  offer.major = store.header().version.major; offer.minor = store.header().version.minor;
  offer.patch = store.header().version.patch; offer.packageSize = store.packageSize();
  memcpy(offer.token, jobs.token(), sizeof(offer.token));
  memcpy(offer.ssid, apSsid, sizeof(offer.ssid));
  memcpy(offer.password, apPassword, sizeof(offer.password));
  sigilBus.sendUpdateOffer(offer);
  TurnHubSecureLink::wipe(&offer, sizeof(offer));
}

void beginSigilUpdates(const char *ssid, const char *password) {
  memset(apSsid, 0, sizeof(apSsid));
  TurnHubSecureLink::wipe(apPassword, sizeof(apPassword));
  if (ssid) strncpy(apSsid, ssid, sizeof(apSsid) - 1);
  if (password) strncpy(apPassword, password, sizeof(apPassword) - 1);
  flash.slot = esp_ota_get_next_update_partition(nullptr);
  store.begin(flash, crypto, TurnHubFirmwarePackage::PUBLIC_KEY, TurnHubFirmwarePackage::KEY_ID);
  store.loadStaged();
  server.on("/sigil-update", HTTP_GET, []() {
    TurnHubWebApi::serveRestrictedPage(server, SIGIL_UPDATE_HTML, TurnHubAccounts::Admin, "sigil-update.html");
  });
  server.on("/api/sigil-firmware", HTTP_GET, statusRoute);
  server.on("/api/sigil-firmware", HTTP_POST, []() {
    if (uploadDenied) sendError(server, 403, "Verify at the table and upload between games with no update running");
    else if (!uploadOk) sendError(server, 400, TurnHubFirmwarePackage::errorMessage(store.error()));
    else sendOkMessage(server, "Signed Sigil package ready");
    uploadSeen = uploadOk = uploadDenied = false;
  }, uploadChunk);
  server.on("/api/sigil-package", HTTP_GET, downloadRoute);
  server.on("/api/sigil-update", HTTP_POST, []() {
    if (!TurnHubWebApi::requirePermission(server, TurnHubAccounts::Admin) ||
        !TurnHubWebApi::verifiedAtTable(server)) return;
    const String module = server.arg("module");
    if (!module.length()) { sendError(server, 400, "Choose a Sigil"); return; }
    for (size_t i = 0; i < module.length(); ++i) if (module[i] < '0' || module[i] > '9') {
      sendError(server, 400, "Invalid Sigil ID"); return;
    }
    String message;
    if (!deviceHandler || !deviceHandler(sessionForRequest(server)->profileId,
        IntentType::UpdateSigil, module.toInt(), message)) sendError(server, 409, message);
    else sendOkMessage(server, message);
  });
}
}  // namespace TurnHubAtlas
