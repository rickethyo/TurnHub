#pragma once

#include <Arduino.h>
#include <WebServer.h>

namespace TurnHub {

class OtaManager {
 public:
  using AllowedCallback = bool (*)();

  OtaManager(WebServer &server, AllowedCallback allowedCallback);

  void begin();
  void update(uint32_t nowMs);

  bool inProgress() const;

 private:
  void handleUpload();
  void handleComplete();
  void resetAttempt();
  void fail(uint8_t errorCode);

  WebServer &server_;
  AllowedCallback allowedCallback_;

  bool inProgress_ = false;
  bool success_ = false;
  bool denied_ = false;
  uint8_t errorCode_ = 0;
  uint32_t bytesWritten_ = 0;
  uint32_t restartAtMs_ = 0;
};

}  // namespace TurnHub
