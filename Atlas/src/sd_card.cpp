// Atlas's microSD slot (VSPI; the TFT has HSPI to itself). See sd_card.h.
#include "sd_card.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>

#include "config.h"
#include "firmware_version.h"
#include "game_recovery.h"
#include "sd_blob_store.h"
#include "serial_log.h"

namespace TurnHubAtlas {
namespace {
using TurnHub::serialLog;
using TurnHubStorage::Status;

// Adapts the Arduino SD library to the store's file-system interface.
class ArduinoSdFileSystem final : public TurnHubStorage::FileSystem {
 public:
  bool exists(const char *path) override { return SD.exists(path); }
  bool readFile(const char *path, void *data, size_t capacity,
                size_t &size) override {
    size = 0;
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) return false;
    const size_t length = file.size();
    bool ok = length <= capacity;
    if (ok) size = file.read(static_cast<uint8_t *>(data), length);
    file.close();
    return ok && size == length;
  }
  bool writeFile(const char *path, const void *data, size_t size) override {
    File file = SD.open(path, FILE_WRITE);
    if (!file) return false;
    const size_t written = file.write(static_cast<const uint8_t *>(data), size);
    file.flush();
    file.close();
    return written == size;
  }
  bool rename(const char *from, const char *to) override {
    return SD.rename(from, to);
  }
  bool remove(const char *path) override { return SD.remove(path); }
  bool mkdir(const char *path) override { return SD.mkdir(path); }
};

SPIClass sdSpi(VSPI);
ArduinoSdFileSystem sdFileSystem;
TurnHubStorage::SdBlobStore sdStore;

enum class CardState : uint8_t { NotStarted, NoCard, Mounted, StoreError };
CardState cardState = CardState::NotStarted;
Status storeStatus = Status::Unavailable;
Status selfTestStatus = Status::Unavailable;

const char *cardStateName(CardState state) {
  switch (state) {
    case CardState::NotStarted: return "not_started";
    case CardState::NoCard: return "no_card";
    case CardState::Mounted: return "mounted";
    case CardState::StoreError: return "store_error";
  }
  return "unknown";
}

const char *cardTypeName() {
  switch (SD.cardType()) {
    case CARD_MMC: return "MMC";
    case CARD_SD: return "SDSC";
    case CARD_SDHC: return "SDHC/SDXC";
    default: return "none";
  }
}

// Writes a small record and reads it back through the real store path
// (temp file, verify, rename), so a pass means the card is usable.
Status runSelfTest() {
  char record[48];
  const int length = snprintf(record, sizeof(record), "TurnHub %s boot %lu",
                              TurnHubFirmware::VERSION,
                              static_cast<unsigned long>(millis()));
  const size_t size = static_cast<size_t>(length) + 1;
  Status status = sdStore.write("selftest", record, size);
  if (status != Status::Ok) return status;
  char readBack[sizeof(record)] = {};
  size_t readSize = 0;
  status = sdStore.read("selftest", readBack, sizeof(readBack), readSize);
  if (status != Status::Ok) return status;
  return readSize == size && memcmp(record, readBack, size) == 0
             ? Status::Ok
             : Status::Corrupt;
}
}  // namespace

void beginSdCard() {
  sdSpi.begin(AtlasConfig::SD_SCLK_PIN, AtlasConfig::SD_MISO_PIN,
              AtlasConfig::SD_MOSI_PIN, AtlasConfig::SD_CS_PIN);
  // format_if_empty = false: an unreadable card is reported, never wiped.
  if (!SD.begin(AtlasConfig::SD_CS_PIN, sdSpi, AtlasConfig::SD_SPI_HZ, "/sd",
                4, false) ||
      SD.cardType() == CARD_NONE) {
    cardState = CardState::NoCard;
    serialLog.println("ATLAS|SD|NO_CARD");
    return;
  }
  serialLog.print("ATLAS|SD|MOUNTED|");
  serialLog.print(cardTypeName());
  serialLog.print("|");
  serialLog.print(String(static_cast<uint32_t>(SD.cardSize() / (1024ULL * 1024ULL))));
  serialLog.println("MB");

  storeStatus = sdStore.begin(sdFileSystem, "/turnhub");
  if (storeStatus != Status::Ok) {
    cardState = CardState::StoreError;
    serialLog.print("ATLAS|SD|STORE|");
    serialLog.println(TurnHub::storageStatusName(storeStatus));
    return;
  }
  cardState = CardState::Mounted;
  selfTestStatus = runSelfTest();
  serialLog.print("ATLAS|SD|SELF_TEST|");
  serialLog.println(TurnHub::storageStatusName(selfTestStatus));
}

String sdCardDiagnosticsJson() {
  String json = String("{\"state\":\"") + cardStateName(cardState) + "\"";
  if (cardState == CardState::Mounted || cardState == CardState::StoreError) {
    json += String(",\"type\":\"") + cardTypeName() + "\"";
    json += ",\"cardMB\":" + String(static_cast<uint32_t>(SD.cardSize() / (1024ULL * 1024ULL)));
    json += ",\"totalMB\":" + String(static_cast<uint32_t>(SD.totalBytes() / (1024ULL * 1024ULL)));
    json += ",\"usedKB\":" + String(static_cast<uint32_t>(SD.usedBytes() / 1024ULL));
    json += String(",\"store\":\"") + TurnHub::storageStatusName(storeStatus) + "\"";
  }
  if (cardState == CardState::Mounted) {
    json += String(",\"selfTest\":\"") +
            TurnHub::storageStatusName(selfTestStatus) + "\"";
  }
  json += "}";
  return json;
}

TurnHubStorage::BlobStore *sdBlobStore() {
  return cardState == CardState::Mounted ? &sdStore : nullptr;
}

}  // namespace TurnHubAtlas
