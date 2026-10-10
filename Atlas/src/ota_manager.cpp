#include "ota_manager.h"
#include "sigil_update_service.h"
#include "firmware_package_mbedtls.h"
#include "firmware_signing_key.h"
#include "firmware_version.h"

#include <Update.h>
#include <esp_ota_ops.h>

#include "web_api.h"
#include "web_pages.h"

#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHub {

namespace {
class AtlasFlashSink final : public TurnHubFirmwarePackage::Sink {
 public:
  bool header(const TurnHubFirmwarePackage::Header &h) override {
    TurnHubAtlas::invalidateSigilPackage();
    return Update.begin(h.imageSize, U_FLASH);
  }
  bool image(const uint8_t *data, size_t n) override {
    return Update.write(const_cast<uint8_t *>(data), n) == n;
  }
};
AtlasFlashSink atlasSink;
TurnHubFirmwarePackage::MbedtlsPackageCrypto atlasCrypto;
TurnHubFirmwarePackage::Reader atlasReader;

class PortalAssetHandler final : public RequestHandler {
 public:
  bool canHandle(HTTPMethod method, String uri) override {
    return method == HTTP_GET && strncmp(uri.c_str(), "/assets/", 8) == 0;
  }
  bool handle(WebServer &server, HTTPMethod, String uri) override {
    if (!TurnHubWeb::serveFile(server, uri.c_str() + 1)) server.send(404, "text/plain", "Not found");
    return true;
  }
};
PortalAssetHandler portalAssetHandler;

void printPartitionDiagnostic(
    const char *role,
    const esp_partition_t *partition) {
  serialLog.print("ATLAS|PARTITION|");
  serialLog.print(role);
  serialLog.print('|');

  if (partition == nullptr) {
    serialLog.println("NONE");
    return;
  }

  serialLog.print(partition->label);
  serialLog.print("|ADDRESS|0x");
  serialLog.print(static_cast<unsigned long>(partition->address), HEX);
  serialLog.print("|SIZE|");
  serialLog.println(static_cast<unsigned long>(partition->size));
}

void printBootPartitionDiagnostics() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);

  printPartitionDiagnostic("RUNNING", running);
  printPartitionDiagnostic("BOOT", boot);
  printPartitionDiagnostic("NEXT_OTA", next);

  if (running != nullptr && boot != nullptr) {
    serialLog.print("ATLAS|PARTITION|BOOT_MATCHES_RUNNING|");
    serialLog.println(running->address == boot->address ? "YES" : "NO");
  }
}

}  // namespace

OtaManager::OtaManager(WebServer &server, AllowedCallback allowedCallback)
    : server_(server), allowedCallback_(allowedCallback) {}

void OtaManager::begin() {
  printBootPartitionDiagnostics();

  server_.on("/portal", HTTP_GET, [this]() {
    TurnHubWebApi::servePortalPage(server_, "index.html");
  });
  server_.addHandler(&portalAssetHandler);
  server_.on("/dev", HTTP_GET, [this]() {
    TurnHubWebApi::serveRestrictedPage(server_, TurnHubAccounts::Developer, "dev.html");
  });
  server_.on("/update", HTTP_GET, [this]() {
    TurnHubWebApi::serveRestrictedPage(server_, TurnHubAccounts::Admin, "update.html");
  });

  server_.on(
      "/api/firmware",
      HTTP_POST,
      [this]() { handleComplete(); },
      [this]() { handleUpload(); });

  TurnHubWebApi::begin(server_);
}

void OtaManager::resetAttempt() {
  inProgress_ = false;
  success_ = false;
  denied_ = false;
  errorCode_ = 0;
  bytesWritten_ = 0;
}

void OtaManager::fail(uint8_t errorCode) {
  inProgress_ = false;
  success_ = false;
  errorCode_ = errorCode;
  serialLog.print("ATLAS|OTA|ERROR|");
  serialLog.println(errorCode_);
}

void OtaManager::handleUpload() {
  HTTPUpload &upload = server_.upload();

  switch (upload.status) {
    case UPLOAD_FILE_START:
      resetAttempt();

      if (!TurnHubWebApi::hasPermission(server_,TurnHubAccounts::Admin) || !TurnHubWebApi::verifiedAtTable(server_) ||
          allowedCallback_ == nullptr || !allowedCallback_() || TurnHubAtlas::sigilUpdatesBusy() || restartAtMs_ != 0) {
        denied_ = true;
        serialLog.println("ATLAS|OTA|DENIED");
        return;
      }

      {
        const auto *slot = esp_ota_get_next_update_partition(nullptr);
        if (!slot) { fail(static_cast<uint8_t>(TurnHubFirmwarePackage::Error::Storage)); return; }
        atlasReader.begin(atlasCrypto, {TurnHubFirmwarePackage::PUBLIC_KEY,
            TurnHubFirmwarePackage::KEY_ID,
            TurnHubFirmwarePackage::productBit(TurnHubFirmwarePackage::Product::Atlas),
            {TurnHubFirmware::MAJOR, TurnHubFirmware::MINOR, TurnHubFirmware::PATCH}, slot->size}, atlasSink);
      }

      inProgress_ = true;
      serialLog.print("ATLAS|OTA|START|");
      serialLog.println(upload.filename);
      break;

    case UPLOAD_FILE_WRITE:
      if (!inProgress_ || denied_) {
        return;
      }

      if (!atlasReader.write(upload.buf, upload.currentSize)) {
        fail(static_cast<uint8_t>(atlasReader.error()));
        Update.printError(Serial);
        Update.abort();
        return;
      }

      bytesWritten_ += upload.currentSize;
      break;

    case UPLOAD_FILE_END:
      if (!inProgress_ || denied_) {
        return;
      }

      if (!atlasReader.finish() || !Update.end()) {
        fail(static_cast<uint8_t>(atlasReader.error() == TurnHubFirmwarePackage::Error::None
            ? TurnHubFirmwarePackage::Error::Storage : atlasReader.error()));
        Update.abort();
        Update.printError(Serial);
        return;
      }

      inProgress_ = false;
      success_ = true;
      serialLog.print("ATLAS|OTA|IMAGE_WRITTEN|");
      serialLog.println(bytesWritten_);
      break;

    case UPLOAD_FILE_ABORTED:
      if (inProgress_) {
        Update.abort();
      }
      inProgress_ = false;
      success_ = false;
      serialLog.println("ATLAS|OTA|ABORTED");
      break;

    default:
      break;
  }
}

void OtaManager::handleComplete() {
  server_.sendHeader("Cache-Control", "no-store");

  if (denied_) {
    server_.send(
        403,
        "application/json",
        "{\"ok\":false,\"error\":\"Update not armed. Sign in as an Admin and return to Lobby or Game Over; if the table code is on, verify at the table first (the code the Atlas screen shows) and start the upload within 10 minutes.\"}");
    return;
  }

  if (!success_) {
    char response[192];
    snprintf(
        response,
        sizeof(response),
        "{\"ok\":false,\"error\":\"Firmware update failed: %s\",\"code\":%u}",
        TurnHubFirmwarePackage::errorMessage(static_cast<TurnHubFirmwarePackage::Error>(errorCode_)),
        static_cast<unsigned>(errorCode_));
    server_.send(500, "application/json", response);
    return;
  }

  char response[160];
  snprintf(
      response,
      sizeof(response),
      "{\"ok\":true,\"message\":\"Firmware image written; restart and boot verification pending.\",\"bytes\":%lu}",
      static_cast<unsigned long>(bytesWritten_));
  server_.send(200, "application/json", response);

  restartAtMs_ = millis() + 1200;
}

void OtaManager::update(uint32_t nowMs) {
  if (restartAtMs_ == 0) {
    return;
  }

  if (static_cast<int32_t>(nowMs - restartAtMs_) < 0) {
    return;
  }

  serialLog.println("ATLAS|OTA|RESTART_FOR_VERIFICATION");
  delay(50);
  ESP.restart();
}

bool OtaManager::inProgress() const {
  return inProgress_ || restartAtMs_ != 0;
}

}  // namespace TurnHub
