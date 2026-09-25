// Atlas's microSD slot (VSPI; the TFT has HSPI to itself). See sd_card.h.
#include "sd_card.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "config.h"
#include "diagnostic_log.h"
#include "firmware_version.h"
#include "game_recovery.h"
#include "runtime_diagnostics.h"
#include "sd_blob_store.h"
#include "serial_log.h"

namespace TurnHubAtlas {
namespace {
using TurnHub::serialLog;
using TurnHubStorage::Status;

// Adapts the Arduino SD library to the store's file-system interface.
class ArduinoSdFileSystem final : public TurnHubStorage::FileSystem,
                                  public TurnHubStorage::DiagnosticFileSystem {
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
  bool fileSize(const char *path, size_t &size) override {
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) return false;
    size = file.size();
    file.close();
    return true;
  }
  bool appendFile(const char *path, const void *data, size_t size) override {
    File file = SD.open(path, FILE_APPEND);
    if (!file || file.isDirectory()) return false;
    const size_t written = file.write(static_cast<const uint8_t *>(data), size);
    file.flush();
    file.close();
    return written == size;
  }
};

SPIClass sdSpi(VSPI);
ArduinoSdFileSystem sdFileSystem;
TurnHubStorage::SdBlobStore sdStore;

enum class CardState : uint8_t { NotStarted, NoCard, Mounted, StoreError, SelfTestError };
enum class LogState : uint32_t { Off, Starting, Ready, IoError, TaskUnavailable };
CardState cardState = CardState::NotStarted;
std::atomic<LogState> logState{LogState::Off};
std::atomic<uint32_t> logLostBytes{0};
Status storeStatus = Status::Unavailable;
Status selfTestStatus = Status::Unavailable;
// Sample before starting the worker; HTTP diagnostics must not access SD.
const char *mountedType = "none";
uint32_t cardMB = 0, totalMB = 0, usedKB = 0;

const char *cardStateName(CardState state) {
  switch (state) {
    case CardState::NotStarted: return "not_started";
    case CardState::NoCard: return "no_card";
    case CardState::Mounted: return "mounted";
    case CardState::StoreError: return "store_error";
    case CardState::SelfTestError: return "self_test_error";
  }
  return "unknown";
}

const char *logStateName(LogState state) {
  switch (state) {
    case LogState::Off: return "off";
    case LogState::Starting: return "starting";
    case LogState::Ready: return "ready";
    case LogState::IoError: return "io_error";
    case LogState::TaskUnavailable: return "task_unavailable";
  }
  return "unknown";
}

// One lock for the card: the log worker and the application's record store
// (detailed statistics) never use the SD library at the same time.
SemaphoreHandle_t cardLock = nullptr;

class CardLock {
 public:
  CardLock() { if (cardLock) xSemaphoreTake(cardLock, portMAX_DELAY); }
  ~CardLock() { if (cardLock) xSemaphoreGive(cardLock); }
  CardLock(const CardLock &) = delete;
  CardLock &operator=(const CardLock &) = delete;
};

bool storeUsable() {
  return cardState == CardState::Mounted &&
      TurnHubStorage::sdStorageReady(storeStatus, selfTestStatus) &&
      logState.load() != LogState::IoError;
}

// The record store the application sees: every call takes the card lock and
// re-checks the card, so a store handed out at boot stops (Unavailable)
// after a later logging I/O error instead of touching a failed card.
class LockedSdStore final : public TurnHubStorage::BlobStore {
 public:
  Status read(const char *key, void *data, size_t capacity, size_t &size) override {
    size = 0;
    if (!storeUsable()) return Status::Unavailable;
    CardLock lock;
    return sdStore.read(key, data, capacity, size);
  }
  Status write(const char *key, const void *data, size_t size) override {
    if (!storeUsable()) return Status::Unavailable;
    CardLock lock;
    return sdStore.write(key, data, size);
  }
  Status remove(const char *key) override {
    if (!storeUsable()) return Status::Unavailable;
    CardLock lock;
    return sdStore.remove(key);
  }
};
LockedSdStore lockedStore;

void sdLogTask(void *) {
  TurnHubStorage::DiagnosticLog log;
  char marker[160];
  const int length = snprintf(marker, sizeof(marker),
      "\nATLAS|BOOT_RECORD|FW|%s|BOOT_ID|%08lX|RESET|%s\n",
      TurnHubFirmware::VERSION, static_cast<unsigned long>(esp_random()),
      TurnHub::resetReason());
  bool ok = false;
  {
    CardLock lock;
    ok = length > 0 && static_cast<size_t>(length) < sizeof(marker) &&
         log.begin(sdFileSystem) && log.append(marker, static_cast<size_t>(length));
  }
  if (ok) {
    logState.store(LogState::Ready);
    serialLog.println("ATLAS|SD|LOG|READY");
  }
  uint64_t cursor = 0;
  char buffer[2048];
  while (ok) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint64_t lost = 0;
    const size_t count = serialLog.readSince(cursor, buffer, sizeof(buffer), lost);
    if (!lost && !count) continue;
    CardLock lock;
    if (lost) {
      const uint32_t before = logLostBytes.load();
      logLostBytes.store(lost > UINT32_MAX - before ? UINT32_MAX : before + static_cast<uint32_t>(lost));
      const int n = snprintf(marker, sizeof(marker), "\nATLAS|SD|LOG|LOST_BYTES|%llu\n",
                             static_cast<unsigned long long>(lost));
      ok = n > 0 && static_cast<size_t>(n) < sizeof(marker) &&
           log.append(marker, static_cast<size_t>(n));
    }
    if (ok && count) ok = log.append(buffer, count);
  }
  logState.store(LogState::IoError);
  serialLog.println("ATLAS|SD|LOG|IO_ERROR");
  vTaskDelete(nullptr);
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
  mountedType = cardTypeName();
  cardMB = static_cast<uint32_t>(SD.cardSize() / (1024ULL * 1024ULL));
  totalMB = static_cast<uint32_t>(SD.totalBytes() / (1024ULL * 1024ULL));
  usedKB = static_cast<uint32_t>(SD.usedBytes() / 1024ULL);

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
  if (!TurnHubStorage::sdStorageReady(storeStatus, selfTestStatus)) {
    cardState = CardState::SelfTestError;
    sdStore.end();
    return;
  }
  // Without the card lock the worker would race the record store, so no lock
  // means no worker (the store stays usable from the application task alone).
  if (cardLock == nullptr) cardLock = xSemaphoreCreateMutex();
  if (cardLock == nullptr) {
    logState.store(LogState::TaskUnavailable);
    serialLog.println("ATLAS|SD|LOG|TASK_UNAVAILABLE");
    return;
  }
  logState.store(LogState::Starting);
  // SD writes are isolated from gameplay and radio callbacks. ESP32 stack size
  // is bytes; includes the 2 KiB drain buffer plus filesystem call headroom.
  if (xTaskCreate(sdLogTask, "sd-log", 6144, nullptr, 1, nullptr) != pdPASS) {
    logState.store(LogState::TaskUnavailable);
    serialLog.println("ATLAS|SD|LOG|TASK_UNAVAILABLE");
  }
}

String sdCardDiagnosticsJson() {
  String json = String("{\"state\":\"") + cardStateName(cardState) + "\"";
  if (cardState == CardState::Mounted || cardState == CardState::StoreError ||
      cardState == CardState::SelfTestError) {
    json += String(",\"type\":\"") + mountedType + "\"";
    json += ",\"cardMB\":" + String(cardMB);
    json += ",\"totalMB\":" + String(totalMB);
    json += ",\"usedKB\":" + String(usedKB) + ",\"usageSample\":\"boot\"";
    json += String(",\"store\":\"") + TurnHub::storageStatusName(storeStatus) + "\"";
  }
  if (cardState == CardState::Mounted || cardState == CardState::SelfTestError) {
    json += String(",\"selfTest\":\"") +
            TurnHub::storageStatusName(selfTestStatus) + "\"";
  }
  json += String(",\"logging\":{\"state\":\"") + logStateName(logState.load()) +
          "\",\"lostBytes\":" + String(logLostBytes.load()) + "}";
  json += "}";
  return json;
}

TurnHubStorage::BlobStore *sdBlobStore() {
  return storeUsable() ? &lockedStore : nullptr;
}

bool sdCardReady() { return storeUsable(); }

}  // namespace TurnHubAtlas
