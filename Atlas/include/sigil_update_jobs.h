#pragma once

// Sigil OTA on Atlas (SIGIL_OTA.md): the one staged Sigil package and the
// one update job at a time. Pure logic over a flash interface and the
// package crypto, host-tested; the idle-slot flash, HTTP routes and radio
// live in sigil_update_service.cpp and web_admin_api.cpp.

#include <stddef.h>
#include <stdint.h>

#include "firmware_package.h"
#include "protocol.h"

namespace TurnHubAtlas {

// Where the staged package lives: Atlas's idle app slot on the device, RAM
// in the host tests. Offsets are from the start of the package.
class PackageFlash {
 public:
  virtual ~PackageFlash() = default;
  virtual uint32_t capacity() const = 0;
  virtual bool eraseSector(uint32_t offset) = 0;  // SECTOR_BYTES-aligned.
  virtual bool write(uint32_t offset, const uint8_t *data, size_t length) = 0;
  virtual bool read(uint32_t offset, uint8_t *data, size_t length) = 0;
};

constexpr uint32_t PACKAGE_SECTOR_BYTES = 4096;
// A Sigil app slot (default partition table: 1.25 MB).
constexpr uint32_t SIGIL_SLOT_BYTES = 0x140000;

// One Sigil package, checked (signature, product, size, image hash) while it
// is written, before it can be offered to any Sigil.
class SigilPackageStore : private TurnHubFirmwarePackage::Sink {
 public:
  void begin(PackageFlash &flash, TurnHubFirmwarePackage::PackageCrypto &crypto,
      const uint8_t *publicKey, const uint8_t *keyId);

  // After a restart: the header still in flash is re-checked (signature,
  // product); the image hash is checked again by the Sigil itself.
  bool loadStaged();

  void startUpload();
  bool writeUpload(const uint8_t *data, size_t length);
  // True once the whole package arrived and every check passed.
  bool finishUpload();
  void abortUpload();
  // Atlas's own update is about to overwrite the slot.
  void invalidate();

  bool uploading() const { return uploading_; }
  bool staged() const { return staged_; }
  TurnHubFirmwarePackage::Error error() const { return error_; }
  const TurnHubFirmwarePackage::Header &header() const { return header_; }
  uint32_t packageSize() const { return staged_ ? packageSize_ : 0; }
  bool read(uint32_t offset, uint8_t *data, size_t length);

 private:
  // Sink: where the reader puts checked bytes.
  bool header(const TurnHubFirmwarePackage::Header &header) override;
  bool image(const uint8_t *data, size_t length) override;
  bool writeFlash(uint32_t offset, const uint8_t *data, size_t length);
  TurnHubFirmwarePackage::Policy policy() const;

  PackageFlash *flash_ = nullptr;
  TurnHubFirmwarePackage::PackageCrypto *crypto_ = nullptr;
  const uint8_t *publicKey_ = nullptr;
  const uint8_t *keyId_ = nullptr;
  TurnHubFirmwarePackage::Reader reader_;
  TurnHubFirmwarePackage::Header header_ = {};
  uint32_t erasedUpTo_ = 0;
  uint32_t packageSize_ = 0;
  bool uploading_ = false;
  bool staged_ = false;
  TurnHubFirmwarePackage::Error error_ = TurnHubFirmwarePackage::Error::None;
};

enum class UpdateJobStage : uint8_t {
  Idle = 0,
  Offering,     // Offer sent, waiting for the Sigil to answer.
  Accepted,     // Joining Atlas's Wi-Fi.
  Downloading,  // progress = percent.
  Installed,    // Restarting into the new image; waiting for its Hello.
  Done,         // Its Hello shows the new version.
  Failed,       // error / message say why.
};

const char *updateJobStageName(UpdateJobStage stage);

// Answers to the Sigil's failure codes, in words for the portal and app.
const char *updateErrorText(uint8_t error);

class SigilUpdateJobs {
 public:
  static constexpr uint32_t OFFER_INTERVAL_MS = 1000;
  static constexpr uint32_t OFFER_TIMEOUT_MS = 15000;
  static constexpr uint32_t SILENCE_TIMEOUT_MS = 45000;
  static constexpr uint32_t REBOOT_TIMEOUT_MS = 120000;
  // Hellos this soon after Installed may still come from the old image.
  static constexpr uint32_t REBOOT_GRACE_MS = 3000;

  // A new job (any finished one is replaced). token: fresh random bytes.
  bool start(uint8_t sigilId, const TurnHubFirmwarePackage::Header &target,
      const uint8_t token[TurnHubProtocol::UPDATE_TOKEN_BYTES], uint32_t nowMs);
  bool busy() const;
  // The Sigil's SigilUpdateStatus.
  void onStatus(uint8_t sigilId, int32_t value, uint32_t nowMs);
  // Called every loop with that Sigil's reported firmware (from its Hello).
  // Returns true when an offer should go out now.
  bool tick(uint32_t nowMs, bool haveVersion, TurnHubFirmwarePackage::Version reported);
  // While the Sigil may download: the route checks the token.
  bool downloadAllowed(const char *tokenHex) const;
  void cancel(const char *why);

  UpdateJobStage stage() const { return stage_; }
  uint8_t sigilId() const { return sigilId_; }
  uint8_t progress() const { return progress_; }
  uint8_t error() const { return error_; }
  const char *message() const { return message_; }
  TurnHubFirmwarePackage::Version target() const { return target_; }
  uint8_t product() const { return product_; }
  const uint8_t *token() const { return token_; }

 private:
  void fail(uint8_t error, const char *message);

  UpdateJobStage stage_ = UpdateJobStage::Idle;
  uint8_t sigilId_ = 0;
  uint8_t product_ = 0;
  TurnHubFirmwarePackage::Version target_ = {0, 0, 0};
  uint8_t token_[TurnHubProtocol::UPDATE_TOKEN_BYTES] = {};
  uint8_t progress_ = 0;
  uint8_t error_ = 0;
  const char *message_ = "";
  uint32_t startedMs_ = 0;
  uint32_t lastOfferMs_ = 0;
  bool offerSent_ = false;
  uint32_t lastHeardMs_ = 0;
  uint32_t installedMs_ = 0;
};

}  // namespace TurnHubAtlas
