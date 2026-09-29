#pragma once

// Sigil OTA (SIGIL_OTA.md, "Update sequence"): takes an update offer from
// Atlas, joins Atlas's Wi-Fi, downloads the staged .thfw package, checks its
// signature and hash while writing the image into the idle slot, and
// restarts into it. The new image stays pending until it has a secure
// session with Atlas again; otherwise the bootloader rolls back.
// Firmware-only (Wi-Fi, HTTP, flash); the offer decision is update_offer.h.

#include <stdint.h>

#include "firmware_package.h"
#include "protocol.h"
#include "update_offer.h"

namespace TurnHubSigil {

struct UpdaterHooks {
  // Sends a sealed SigilUpdateStatus (value: encodeUpdateStatus).
  void (*sendStatus)(int32_t value);
  // Shows progress on the screen and status light (percent -1: none).
  void (*showProgress)(const char *status, int8_t percent);
  // The update is over without installing: back to normal.
  void (*finished)();
};

class SigilUpdater {
 public:
  void begin(TurnHubFirmwarePackage::Product product, TurnHubFirmwarePackage::Version running,
      uint8_t wifiChannel, const UpdaterHooks &hooks);

  // An opened SigilUpdateOffer. Answers Refused or repeats the last status
  // at once; an accepted offer is started by run() from loop().
  void offer(const TurnHubProtocol::SigilUpdateOfferPacket &offer, uint8_t sigilId);
  bool pending() const { return pending_; }
  // Blocking: does the whole update. On success it restarts and never
  // returns; on failure it reports why and returns.
  void run();

  // Boot validation of a freshly updated image: call every loop. Once a
  // secure session is up it is marked valid; if that hasn't happened within
  // BOOT_CONFIRM_MS, it restarts and the bootloader rolls back.
  void confirmBoot(bool sessionReady, uint32_t nowMs);
  bool awaitingBootConfirm() const { return confirmPending_; }

  static constexpr uint32_t BOOT_CONFIRM_MS = 60000;

 private:
  void status(TurnHubProtocol::UpdateStage stage, uint8_t progress, uint8_t error);
  void fail(uint8_t error, const char *why);
  void leaveWifi();

  TurnHubFirmwarePackage::Product product_ = TurnHubFirmwarePackage::Product::SigilEink;
  TurnHubFirmwarePackage::Version running_ = {0, 0, 0};
  uint8_t channel_ = 1;
  UpdaterHooks hooks_ = {};
  UpdaterMemory memory_;
  TurnHubProtocol::SigilUpdateOfferPacket job_ = {};
  bool pending_ = false;
  int32_t lastStatus_ = 0;
  bool confirmChecked_ = false;
  bool confirmPending_ = false;
  uint32_t confirmStartMs_ = 0;
};

}  // namespace TurnHubSigil
