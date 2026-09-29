#include "sigil_updater.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>

#include "firmware_package_mbedtls.h"
#include "firmware_signing_key.h"

// Keeps a freshly updated image pending until SigilUpdater::confirmBoot()
// marks it valid, instead of the core doing so at startup. A USB-flashed
// image is never pending, so this changes nothing for it.
extern "C" bool verifyRollbackLater() { return true; }

namespace TurnHubSigil {

namespace {

using TurnHubProtocol::UpdateError;
using TurnHubProtocol::UpdateStage;

constexpr uint32_t WIFI_JOIN_MS = 20000;
constexpr uint32_t STALL_MS = 15000;
constexpr const char *PACKAGE_URL = "http://192.168.4.1/api/sigil-package?token=";

// Writes the checked image into the idle app slot. Update keeps the first
// bytes back until end(), so the slot can't boot until the hash has passed.
class FlashSink final : public TurnHubFirmwarePackage::Sink {
 public:
  bool header(const TurnHubFirmwarePackage::Header &header) override {
    return Update.begin(header.imageSize, U_FLASH);
  }
  bool image(const uint8_t *data, size_t length) override {
    return Update.write(const_cast<uint8_t *>(data), length) == length;
  }
};

}  // namespace

void SigilUpdater::begin(TurnHubFirmwarePackage::Product product,
    TurnHubFirmwarePackage::Version running, uint8_t wifiChannel, const UpdaterHooks &hooks) {
  product_ = product;
  running_ = running;
  channel_ = wifiChannel;
  hooks_ = hooks;
}

void SigilUpdater::status(UpdateStage stage, uint8_t progress, uint8_t error) {
  lastStatus_ = TurnHubProtocol::encodeUpdateStatus(stage, progress, error, job_.token[0]);
  if (hooks_.sendStatus != nullptr) hooks_.sendStatus(lastStatus_);
}

void SigilUpdater::offer(const TurnHubProtocol::SigilUpdateOfferPacket &offer, uint8_t sigilId) {
  const OfferCheck check = checkUpdateOffer(offer, sigilId, product_, running_, memory_);
  switch (check.decision) {
    case OfferDecision::Ignore:
      return;
    case OfferDecision::Repeat:
      if (lastStatus_ != 0 && hooks_.sendStatus != nullptr) hooks_.sendStatus(lastStatus_);
      return;
    case OfferDecision::Refuse:
      Serial.printf("SIGIL|OTA|REFUSED|%u\n", static_cast<unsigned>(check.error));
      if (hooks_.sendStatus != nullptr) {
        hooks_.sendStatus(TurnHubProtocol::encodeUpdateStatus(
            UpdateStage::Refused, 0, check.error, offer.token[0]));
      }
      return;
    case OfferDecision::Accept:
      break;
  }
  job_ = offer;
  memory_.hasJob = true;
  memory_.running = true;
  memcpy(memory_.token, offer.token, sizeof(memory_.token));
  pending_ = true;
  Serial.printf("SIGIL|OTA|ACCEPTED|%u.%u.%u|%lu\n", static_cast<unsigned>(offer.major),
      static_cast<unsigned>(offer.minor), static_cast<unsigned>(offer.patch),
      static_cast<unsigned long>(offer.packageSize));
  status(UpdateStage::Accepted, 0, 0);
}

void SigilUpdater::leaveWifi() {
  WiFi.disconnect(false, true);
  // Back on Atlas's channel for ESP-NOW, whatever the join did.
  esp_wifi_set_channel(channel_, WIFI_SECOND_CHAN_NONE);
  memset(job_.password, 0, sizeof(job_.password));
}

void SigilUpdater::fail(uint8_t error, const char *why) {
  if (Update.isRunning()) Update.abort();
  leaveWifi();
  memory_.running = false;
  Serial.printf("SIGIL|OTA|FAILED|%u|%s\n", static_cast<unsigned>(error), why);
  status(UpdateStage::Failed, 0, error);
  if (hooks_.showProgress != nullptr) hooks_.showProgress("Update failed", -1);
  delay(2000);
  if (hooks_.finished != nullptr) hooks_.finished();
}

void SigilUpdater::run() {
  if (!pending_) return;
  pending_ = false;
  if (hooks_.showProgress != nullptr) hooks_.showProgress("Joining Atlas", -1);

  // Station mode is already on for ESP-NOW; joining Atlas's AP keeps its
  // channel. Nothing is saved: the credentials live only in this job.
  WiFi.persistent(false);
  WiFi.begin(job_.ssid, job_.password, channel_);
  const uint32_t joinStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - joinStart > WIFI_JOIN_MS) {
      fail(static_cast<uint8_t>(UpdateError::WifiJoin), "wifi");
      return;
    }
    delay(50);
  }
  Serial.printf("SIGIL|OTA|WIFI|%s\n", WiFi.localIP().toString().c_str());

  char url[96];
  char tokenHex[2 * TurnHubProtocol::UPDATE_TOKEN_BYTES + 1];
  updateTokenHex(job_.token, tokenHex);
  snprintf(url, sizeof(url), "%s%s", PACKAGE_URL, tokenHex);
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(STALL_MS);
  if (!http.begin(client, url)) {
    fail(static_cast<uint8_t>(UpdateError::Download), "begin");
    return;
  }
  const int code = http.GET();
  const int size = http.getSize();
  if (code != 200 || size != static_cast<int>(job_.packageSize)) {
    Serial.printf("SIGIL|OTA|HTTP|%d|%d\n", code, size);
    http.end();
    fail(static_cast<uint8_t>(UpdateError::Download), "http");
    return;
  }

  const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
  if (slot == nullptr) {
    http.end();
    fail(static_cast<uint8_t>(UpdateError::FlashBegin), "slot");
    return;
  }
  TurnHubFirmwarePackage::MbedtlsPackageCrypto crypto;
  FlashSink sink;
  TurnHubFirmwarePackage::Reader reader;
  const TurnHubFirmwarePackage::Policy policy{TurnHubFirmwarePackage::PUBLIC_KEY,
      TurnHubFirmwarePackage::KEY_ID, TurnHubFirmwarePackage::productBit(product_), running_,
      slot->size};
  reader.begin(crypto, policy, sink);

  WiFiClient *stream = http.getStreamPtr();
  static uint8_t buffer[1024];
  uint32_t received = 0;
  uint32_t lastDataMs = millis();
  uint8_t nextReport = 10;
  if (hooks_.showProgress != nullptr) hooks_.showProgress("Downloading", 0);
  while (received < job_.packageSize) {
    const size_t available = stream->available();
    if (available == 0) {
      if (!http.connected() || millis() - lastDataMs > STALL_MS) break;
      delay(2);
      continue;
    }
    size_t want = available < sizeof(buffer) ? available : sizeof(buffer);
    if (want > job_.packageSize - received) want = job_.packageSize - received;
    const int n = stream->readBytes(buffer, want);
    if (n <= 0) continue;
    if (!reader.write(buffer, static_cast<size_t>(n))) break;
    received += static_cast<uint32_t>(n);
    lastDataMs = millis();
    const uint8_t percent = static_cast<uint8_t>(
        static_cast<uint64_t>(received) * 100 / job_.packageSize);
    if (percent >= nextReport && percent < 100) {
      status(UpdateStage::Downloading, percent, 0);
      if (hooks_.showProgress != nullptr) hooks_.showProgress("Downloading", percent);
      nextReport = static_cast<uint8_t>(percent / 10 * 10 + 10);
    }
  }
  http.end();

  if (reader.error() != TurnHubFirmwarePackage::Error::None) {
    fail(static_cast<uint8_t>(reader.error()), TurnHubFirmwarePackage::errorName(reader.error()));
    return;
  }
  if (received < job_.packageSize) {
    fail(static_cast<uint8_t>(UpdateError::Timeout), "stalled");
    return;
  }
  if (!reader.finish()) {
    fail(static_cast<uint8_t>(reader.error()), TurnHubFirmwarePackage::errorName(reader.error()));
    return;
  }
  // Signature and hash passed: only now may the new slot become bootable.
  if (!Update.end()) {
    Serial.printf("SIGIL|OTA|UPDATE_END|%u\n", static_cast<unsigned>(Update.getError()));
    fail(static_cast<uint8_t>(UpdateError::FlashFinish), "end");
    return;
  }
  Serial.printf("SIGIL|OTA|INSTALLED|%u.%u.%u\n", static_cast<unsigned>(job_.major),
      static_cast<unsigned>(job_.minor), static_cast<unsigned>(job_.patch));
  if (hooks_.showProgress != nullptr) hooks_.showProgress("Restarting", 100);
  for (uint8_t i = 0; i < 3; ++i) {
    status(UpdateStage::Installed, 100, 0);
    delay(150);
  }
  leaveWifi();
  delay(300);
  ESP.restart();
}

void SigilUpdater::confirmBoot(bool sessionReady, uint32_t nowMs) {
  if (!confirmChecked_) {
    confirmChecked_ = true;
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
      confirmPending_ = true;
      confirmStartMs_ = nowMs;
      Serial.println("SIGIL|OTA|BOOT|PENDING_VERIFY");
    }
  }
  if (!confirmPending_) return;
  if (sessionReady) {
    esp_ota_mark_app_valid_cancel_rollback();
    confirmPending_ = false;
    Serial.println("SIGIL|OTA|BOOT|VALID");
    return;
  }
  if (nowMs - confirmStartMs_ > BOOT_CONFIRM_MS) {
    Serial.println("SIGIL|OTA|BOOT|NO_SESSION|ROLLBACK");
    delay(50);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

}  // namespace TurnHubSigil
