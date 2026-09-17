#include "ota_manager.h"

#include <Update.h>

namespace TurnHub {

OtaManager::OtaManager(WebServer &server, AllowedCallback allowedCallback)
    : server_(server), allowedCallback_(allowedCallback) {}

void OtaManager::begin() {
  server_.on(
      "/api/firmware",
      HTTP_POST,
      [this]() { handleComplete(); },
      [this]() { handleUpload(); });
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
  Serial.print("ATLAS|OTA|ERROR|");
  Serial.println(errorCode_);
}

void OtaManager::handleUpload() {
  HTTPUpload &upload = server_.upload();

  switch (upload.status) {
    case UPLOAD_FILE_START:
      resetAttempt();

      if (allowedCallback_ == nullptr || !allowedCallback_()) {
        denied_ = true;
        Serial.println("ATLAS|OTA|DENIED");
        return;
      }

      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        fail(static_cast<uint8_t>(Update.getError()));
        Update.printError(Serial);
        return;
      }

      inProgress_ = true;
      Serial.print("ATLAS|OTA|START|");
      Serial.println(upload.filename);
      break;

    case UPLOAD_FILE_WRITE:
      if (!inProgress_ || denied_) {
        return;
      }

      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        fail(static_cast<uint8_t>(Update.getError()));
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

      if (!Update.end(true)) {
        fail(static_cast<uint8_t>(Update.getError()));
        Update.printError(Serial);
        return;
      }

      inProgress_ = false;
      success_ = true;
      Serial.print("ATLAS|OTA|SUCCESS|");
      Serial.println(bytesWritten_);
      break;

    case UPLOAD_FILE_ABORTED:
      if (inProgress_) {
        Update.abort();
      }
      inProgress_ = false;
      success_ = false;
      Serial.println("ATLAS|OTA|ABORTED");
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
        "{\"ok\":false,\"error\":\"Hold the Atlas master button and update only from Lobby or Game Over.\"}");
    return;
  }

  if (!success_) {
    char response[96];
    snprintf(
        response,
        sizeof(response),
        "{\"ok\":false,\"error\":\"Firmware update failed\",\"code\":%u}",
        static_cast<unsigned>(errorCode_));
    server_.send(500, "application/json", response);
    return;
  }

  server_.send(
      200,
      "application/json",
      "{\"ok\":true,\"message\":\"Firmware installed. Atlas is restarting.\"}");

  restartAtMs_ = millis() + 1200;
}

void OtaManager::update(uint32_t nowMs) {
  if (restartAtMs_ == 0) {
    return;
  }

  if (static_cast<int32_t>(nowMs - restartAtMs_) < 0) {
    return;
  }

  Serial.println("ATLAS|OTA|RESTART");
  delay(50);
  ESP.restart();
}

bool OtaManager::inProgress() const {
  return inProgress_;
}

}  // namespace TurnHub
