#pragma once

#include <Arduino.h>
#include <WebServer.h>

namespace TurnHub {

// Serves the portal pages (the SD card's portal pack when installed, else the
// built-in one), the Atlas firmware and portal pack upload endpoints (Admin
// permission, verified at the table, and only while allowedCallback reports a
// safe table state), and registers the web API routes. update() restarts
// Atlas after a successful firmware upload; a portal pack needs no restart.
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
  void handlePortalUpload();
  void handlePortalComplete();
  void failPortal(const String &message);
  String portalErrorText() const;

  WebServer &server_;
  AllowedCallback allowedCallback_;

  bool inProgress_ = false;
  bool success_ = false;
  bool denied_ = false;
  uint8_t errorCode_ = 0;
  uint32_t bytesWritten_ = 0;
  uint32_t restartAtMs_ = 0;

  bool portalInProgress_ = false;
  bool portalSuccess_ = false;
  bool portalDenied_ = false;
  String portalError_;
  uint32_t portalBytes_ = 0;
};

}  // namespace TurnHub
