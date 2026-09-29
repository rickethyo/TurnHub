#include "sigil_update_jobs.h"

#include <string.h>

namespace TurnHubAtlas {

using TurnHubFirmwarePackage::Error;
using TurnHubFirmwarePackage::Header;
using TurnHubProtocol::UpdateError;
using TurnHubProtocol::UpdateStage;

// --- Package store ------------------------------------------------------------

bool SigilPackageStore::header(const Header &header) {
  return writeFlash(0, reinterpret_cast<const uint8_t *>(&header), sizeof(header));
}

bool SigilPackageStore::image(const uint8_t *data, size_t length) {
  return writeFlash(static_cast<uint32_t>(sizeof(Header)) + reader_.imageWritten(), data, length);
}

void SigilPackageStore::begin(PackageFlash &flash, TurnHubFirmwarePackage::PackageCrypto &crypto,
    const uint8_t *publicKey, const uint8_t *keyId) {
  flash_ = &flash;
  crypto_ = &crypto;
  publicKey_ = publicKey;
  keyId_ = keyId;
  staged_ = false;
  uploading_ = false;
}

TurnHubFirmwarePackage::Policy SigilPackageStore::policy() const {
  // Any Sigil's package, any version (each Sigil applies its own version
  // rule), and it must fit both a Sigil slot and this flash.
  uint32_t maxImage = SIGIL_SLOT_BYTES;
  const uint32_t room = flash_->capacity() - static_cast<uint32_t>(sizeof(Header));
  if (room < maxImage) maxImage = room;
  return TurnHubFirmwarePackage::Policy{publicKey_, keyId_, TurnHubFirmwarePackage::ANY_SIGIL,
      TurnHubFirmwarePackage::Version{0, 0, 0}, maxImage};
}

bool SigilPackageStore::loadStaged() {
  staged_ = false;
  if (flash_ == nullptr || flash_->capacity() <= sizeof(Header)) return false;
  Header header;
  if (!flash_->read(0, reinterpret_cast<uint8_t *>(&header), sizeof(header))) return false;
  if (TurnHubFirmwarePackage::checkHeader(*crypto_, policy(), header) != Error::None) {
    return false;
  }
  header_ = header;
  packageSize_ = static_cast<uint32_t>(sizeof(Header)) + header.imageSize;
  staged_ = true;
  return true;
}

bool SigilPackageStore::writeFlash(uint32_t offset, const uint8_t *data, size_t length) {
  if (flash_ == nullptr || offset + length > flash_->capacity()) return false;
  while (erasedUpTo_ < offset + length) {
    if (!flash_->eraseSector(erasedUpTo_)) return false;
    erasedUpTo_ += PACKAGE_SECTOR_BYTES;
  }
  return flash_->write(offset, data, length);
}

void SigilPackageStore::startUpload() {
  staged_ = false;
  uploading_ = flash_ != nullptr && crypto_ != nullptr;
  erasedUpTo_ = 0;
  packageSize_ = 0;
  error_ = Error::None;
  if (uploading_) reader_.begin(*crypto_, policy(), *this);
}

bool SigilPackageStore::writeUpload(const uint8_t *data, size_t length) {
  if (!uploading_) return false;
  if (!reader_.write(data, length)) {
    error_ = reader_.error();
    uploading_ = false;
    return false;
  }
  return true;
}

bool SigilPackageStore::finishUpload() {
  if (!uploading_) return false;
  uploading_ = false;
  if (!reader_.finish()) {
    error_ = reader_.error();
    return false;
  }
  header_ = reader_.header();
  packageSize_ = reader_.packageSize();
  staged_ = true;
  return true;
}

void SigilPackageStore::abortUpload() {
  uploading_ = false;
  staged_ = false;
}

void SigilPackageStore::invalidate() {
  uploading_ = false;
  staged_ = false;
}

bool SigilPackageStore::read(uint32_t offset, uint8_t *data, size_t length) {
  if (!staged_ || offset + length > packageSize_) return false;
  return flash_->read(offset, data, length);
}

// --- Jobs ---------------------------------------------------------------------

const char *updateJobStageName(UpdateJobStage stage) {
  switch (stage) {
    case UpdateJobStage::Idle: return "idle";
    case UpdateJobStage::Offering: return "offering";
    case UpdateJobStage::Accepted: return "accepted";
    case UpdateJobStage::Downloading: return "downloading";
    case UpdateJobStage::Installed: return "installed";
    case UpdateJobStage::Done: return "done";
    case UpdateJobStage::Failed: return "failed";
  }
  return "unknown";
}

const char *updateErrorText(uint8_t error) {
  switch (error) {
    case static_cast<uint8_t>(Error::BadMagic):
    case static_cast<uint8_t>(Error::BadFormat): return "The package is damaged";
    case static_cast<uint8_t>(Error::UnknownKey):
    case static_cast<uint8_t>(Error::BadSignature): return "The package is not signed by TurnHub";
    case static_cast<uint8_t>(Error::WrongProduct): return "The package is for the other kind of Sigil";
    case static_cast<uint8_t>(Error::OlderVersion): return "The Sigil already runs newer firmware";
    case static_cast<uint8_t>(Error::TooLarge): return "The package is too large";
    case static_cast<uint8_t>(Error::Truncated):
    case static_cast<uint8_t>(Error::TooLong):
    case static_cast<uint8_t>(Error::HashMismatch): return "The download was damaged";
    case static_cast<uint8_t>(Error::Storage): return "The Sigil could not write its flash";
    case static_cast<uint8_t>(Error::Crypto): return "The Sigil could not check the package";
    case static_cast<uint8_t>(UpdateError::WifiJoin): return "The Sigil could not join Atlas's Wi-Fi";
    case static_cast<uint8_t>(UpdateError::Download): return "The download failed";
    case static_cast<uint8_t>(UpdateError::Timeout): return "The download stalled";
    case static_cast<uint8_t>(UpdateError::FlashBegin):
    case static_cast<uint8_t>(UpdateError::FlashFinish): return "The Sigil could not install the image";
    case static_cast<uint8_t>(UpdateError::Busy): return "The Sigil is already updating";
    default: return "The update failed";
  }
}

bool SigilUpdateJobs::start(uint8_t sigilId, const Header &target,
    const uint8_t token[TurnHubProtocol::UPDATE_TOKEN_BYTES], uint32_t nowMs) {
  if (busy()) return false;
  stage_ = UpdateJobStage::Offering;
  sigilId_ = sigilId;
  product_ = target.product;
  target_ = target.version;
  memcpy(token_, token, sizeof(token_));
  progress_ = 0;
  error_ = 0;
  message_ = "Waiting for the Sigil";
  startedMs_ = nowMs;
  lastHeardMs_ = nowMs;
  offerSent_ = false;
  return true;
}

bool SigilUpdateJobs::busy() const {
  return stage_ == UpdateJobStage::Offering || stage_ == UpdateJobStage::Accepted ||
      stage_ == UpdateJobStage::Downloading || stage_ == UpdateJobStage::Installed;
}

void SigilUpdateJobs::fail(uint8_t error, const char *message) {
  stage_ = UpdateJobStage::Failed;
  error_ = error;
  message_ = message;
}

void SigilUpdateJobs::cancel(const char *why) {
  if (busy()) fail(0, why);
}

void SigilUpdateJobs::onStatus(uint8_t sigilId, int32_t value, uint32_t nowMs) {
  TurnHubProtocol::UpdateStatusFields status;
  if (!busy() || sigilId != sigilId_ || !TurnHubProtocol::decodeUpdateStatus(value, status) ||
      status.tokenTag != token_[0]) {
    return;  // Not this job's (a late answer to an older one).
  }
  lastHeardMs_ = nowMs;
  switch (status.stage) {
    case UpdateStage::Accepted:
      if (stage_ == UpdateJobStage::Offering) {
        stage_ = UpdateJobStage::Accepted;
        message_ = "Joining Atlas's Wi-Fi";
      }
      break;
    case UpdateStage::Downloading:
      if (stage_ == UpdateJobStage::Offering || stage_ == UpdateJobStage::Accepted ||
          stage_ == UpdateJobStage::Downloading) {
        stage_ = UpdateJobStage::Downloading;
        progress_ = status.progress;
        message_ = "Downloading";
      }
      break;
    case UpdateStage::Installed:
      if (stage_ != UpdateJobStage::Installed) {
        stage_ = UpdateJobStage::Installed;
        progress_ = 100;
        installedMs_ = nowMs;
        message_ = "Installed; the Sigil is restarting";
      }
      break;
    case UpdateStage::Failed:
    case UpdateStage::Refused:
      fail(status.error, updateErrorText(status.error));
      break;
  }
}

bool SigilUpdateJobs::tick(uint32_t nowMs, bool haveVersion,
    TurnHubFirmwarePackage::Version reported) {
  switch (stage_) {
    case UpdateJobStage::Offering:
      if (nowMs - startedMs_ > OFFER_TIMEOUT_MS) {
        fail(0, "The Sigil did not answer; it may run firmware without the updater");
        return false;
      }
      if (!offerSent_ || nowMs - lastOfferMs_ >= OFFER_INTERVAL_MS) {
        offerSent_ = true;
        lastOfferMs_ = nowMs;
        return true;
      }
      return false;
    case UpdateJobStage::Accepted:
    case UpdateJobStage::Downloading:
      if (nowMs - lastHeardMs_ > SILENCE_TIMEOUT_MS) fail(0, "The Sigil stopped answering");
      return false;
    case UpdateJobStage::Installed:
      if (haveVersion && nowMs - installedMs_ > REBOOT_GRACE_MS) {
        if (TurnHubFirmwarePackage::compareVersions(reported, target_) == 0) {
          stage_ = UpdateJobStage::Done;
          message_ = "Updated";
        } else {
          fail(0, "The Sigil went back to its previous firmware");
        }
        return false;
      }
      if (nowMs - installedMs_ > REBOOT_TIMEOUT_MS) {
        fail(0, "The Sigil did not come back after installing");
      }
      return false;
    default:
      return false;
  }
}

bool SigilUpdateJobs::downloadAllowed(const char *tokenHex) const {
  if (tokenHex == nullptr || (stage_ != UpdateJobStage::Offering &&
      stage_ != UpdateJobStage::Accepted && stage_ != UpdateJobStage::Downloading)) {
    return false;
  }
  static const char HEX_DIGITS[] = "0123456789abcdef";
  if (strlen(tokenHex) != 2 * TurnHubProtocol::UPDATE_TOKEN_BYTES) return false;
  uint8_t difference = 0;
  for (size_t i = 0; i < TurnHubProtocol::UPDATE_TOKEN_BYTES; ++i) {
    difference |= static_cast<uint8_t>(tokenHex[2 * i] ^ HEX_DIGITS[token_[i] >> 4]);
    difference |= static_cast<uint8_t>(tokenHex[2 * i + 1] ^ HEX_DIGITS[token_[i] & 0x0F]);
  }
  return difference == 0;
}

}  // namespace TurnHubAtlas
