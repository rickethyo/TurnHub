// Atlas's microSD slot: 4-bit SDMMC on the ES3C40P. See sd_card.h.
#include "sd_card.h"

#include <FS.h>
#include <SD_MMC.h>
#include <USB.h>
#include <USBMSC.h>
#include <ff.h>
#include <diskio_impl.h>
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
#include "sd_hotplug.h"
#include "serial_log.h"

namespace TurnHubAtlas {
namespace {
using TurnHub::serialLog;

fs::SDMMCFS &cardFs = SD_MMC;
using TurnHubStorage::Status;

// Adapts the Arduino SD library to the store's file-system interface.
class ArduinoSdFileSystem final : public TurnHubStorage::FileSystem,
                                  public TurnHubStorage::DiagnosticFileSystem {
 public:
  bool exists(const char *path) override { return cardFs.exists(path); }
  bool readFile(const char *path, void *data, size_t capacity,
                size_t &size) override {
    size = 0;
    File file = cardFs.open(path, FILE_READ);
    if (!file || file.isDirectory()) return false;
    const size_t length = file.size();
    bool ok = length <= capacity;
    if (ok) size = file.read(static_cast<uint8_t *>(data), length);
    file.close();
    return ok && size == length;
  }
  bool writeFile(const char *path, const void *data, size_t size) override {
    File file = cardFs.open(path, FILE_WRITE);
    if (!file) return false;
    const size_t written = file.write(static_cast<const uint8_t *>(data), size);
    file.flush();
    file.close();
    return written == size;
  }
  bool rename(const char *from, const char *to) override {
    return cardFs.rename(from, to);
  }
  bool remove(const char *path) override { return cardFs.remove(path); }
  bool mkdir(const char *path) override { return cardFs.mkdir(path); }
  bool fileSize(const char *path, size_t &size) override {
    File file = cardFs.open(path, FILE_READ);
    if (!file || file.isDirectory()) return false;
    size = file.size();
    file.close();
    return true;
  }
  bool appendFile(const char *path, const void *data, size_t size) override {
    File file = cardFs.open(path, FILE_APPEND);
    if (!file || file.isDirectory()) return false;
    const size_t written = file.write(static_cast<const uint8_t *>(data), size);
    file.flush();
    file.close();
    return written == size;
  }
};

ArduinoSdFileSystem sdFileSystem;
TurnHubStorage::SdBlobStore sdStore;

// UsbDrive: the card is lent to a computer over USB (shareSdCardOverUsb).
enum class CardState : uint8_t { NotStarted, NoCard, Mounted, StoreError, SelfTestError, UsbDrive };
enum class LogState : uint32_t { Off, Starting, Ready, IoError, TaskUnavailable };
// Written by the SD worker (and boot), read by the application and HTTP.
std::atomic<CardState> cardState{CardState::NotStarted};
std::atomic<LogState> logState{LogState::Off};
std::atomic<uint32_t> logLostBytes{0};
std::atomic<Status> storeStatus{Status::Unavailable};
std::atomic<Status> selfTestStatus{Status::Unavailable};
// Bumped on every mount and unmount; the application watches it
// (sdCardGeneration). mountCount is successful mounts, for diagnostics.
std::atomic<uint32_t> cardGeneration{0};
std::atomic<uint32_t> mountCount{0};
// Sampled at each mount; HTTP diagnostics must not access SD.
const char *mountedType = "none";
uint32_t cardMB = 0, totalMB = 0, usedKB = 0;

const char *cardStateName(CardState state) {
  switch (state) {
    case CardState::NotStarted: return "not_started";
    case CardState::NoCard: return "no_card";
    case CardState::Mounted: return "mounted";
    case CardState::StoreError: return "store_error";
    case CardState::SelfTestError: return "self_test_error";
    case CardState::UsbDrive: return "usb_drive";
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

// One lock for the card: the SD worker (logging, probing, remounting) and the
// application's record store (detailed statistics) never use the SD library
// at the same time.
SemaphoreHandle_t cardLock = nullptr;

class CardLock {
 public:
  CardLock() { if (cardLock) xSemaphoreTake(cardLock, portMAX_DELAY); }
  ~CardLock() { if (cardLock) xSemaphoreGive(cardLock); }
  CardLock(const CardLock &) = delete;
  CardLock &operator=(const CardLock &) = delete;
};

bool storeUsable() {
  return cardState.load() == CardState::Mounted &&
      TurnHubStorage::sdStorageReady(storeStatus.load(), selfTestStatus.load()) &&
      logState.load() != LogState::IoError;
}

// The record store the application sees. It re-checks the card inside the
// lock too, because the worker may have unmounted it (card pulled) between
// the first check and taking the lock.
class LockedSdStore final : public TurnHubStorage::BlobStore {
 public:
  Status read(const char *key, void *data, size_t capacity, size_t &size) override {
    size = 0;
    if (!storeUsable()) return Status::Unavailable;
    CardLock lock;
    if (!storeUsable()) return Status::Unavailable;
    return sdStore.read(key, data, capacity, size);
  }
  Status write(const char *key, const void *data, size_t size) override {
    if (!storeUsable()) return Status::Unavailable;
    CardLock lock;
    if (!storeUsable()) return Status::Unavailable;
    return sdStore.write(key, data, size);
  }
  Status remove(const char *key) override {
    if (!storeUsable()) return Status::Unavailable;
    CardLock lock;
    if (!storeUsable()) return Status::Unavailable;
    return sdStore.remove(key);
  }
};
LockedSdStore lockedStore;

const char *cardTypeName() {
  switch (cardFs.cardType()) {
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

// Mounts the card (never formatting it), checks the store and runs the
// self-test. Boot calls it directly; afterwards only the worker does, under
// the card lock. Repeated "no card" results are logged once.
SdMountResult mountCard() {
  // format_if_empty = false: an unreadable card is reported, never wiped.
  // At most 2 open files (was 4): each slot reserves a 4 KB sector cache
  // (CONFIG_FATFS_PER_FILE_CACHE), and every access runs under the card lock
  // with at most one file open (a wipe adds a directory, which takes no slot).
  const bool begun = cardFs.begin("/sd", false, false, AtlasConfig::SD_MMC_KHZ, 2);
  if (!begun ||
      cardFs.cardType() == CARD_NONE) {
    cardFs.end();
    if (cardState.exchange(CardState::NoCard) != CardState::NoCard) {
      serialLog.println("ATLAS|SD|NO_CARD");
    }
    return SdMountResult::NoCard;
  }
  mountedType = cardTypeName();
  cardMB = static_cast<uint32_t>(cardFs.cardSize() / (1024ULL * 1024ULL));
  totalMB = static_cast<uint32_t>(cardFs.totalBytes() / (1024ULL * 1024ULL));
  usedKB = static_cast<uint32_t>(cardFs.usedBytes() / 1024ULL);
  serialLog.print("ATLAS|SD|MOUNTED|");
  serialLog.print(mountedType);
  serialLog.print("|");
  serialLog.print(String(cardMB));
  serialLog.println("MB");

  const Status store = sdStore.begin(sdFileSystem, "/turnhub");
  storeStatus.store(store);
  if (store != Status::Ok) {
    cardState.store(CardState::StoreError);
    serialLog.print("ATLAS|SD|STORE|");
    serialLog.println(TurnHub::storageStatusName(store));
    cardFs.end();
    return SdMountResult::CardError;
  }
  const Status selfTest = runSelfTest();
  selfTestStatus.store(selfTest);
  serialLog.print("ATLAS|SD|SELF_TEST|");
  serialLog.println(TurnHub::storageStatusName(selfTest));
  if (!TurnHubStorage::sdStorageReady(store, selfTest)) {
    cardState.store(CardState::SelfTestError);
    sdStore.end();
    cardFs.end();
    return SdMountResult::CardError;
  }
  cardState.store(CardState::Mounted);
  mountCount.fetch_add(1);
  cardGeneration.fetch_add(1);
  return SdMountResult::Ok;
}

// The card was pulled or failed: drop the mount so a later begin() starts
// clean. Under the card lock.
void unmountCard() {
  cardState.store(CardState::NoCard);
  logState.store(LogState::Off);
  sdStore.end();
  cardFs.end();
  mountedType = "none";
  cardGeneration.fetch_add(1);
  serialLog.println("ATLAS|SD|REMOVED");
}

// Deletes everything under `path` (not `path` itself). One entry per pass,
// with no handle open while it is removed; an entry that will not go is
// skipped rather than retried forever. Depth-limited and under the card lock.
int removeTree(const String &path, uint8_t depth) {
  constexpr uint8_t MAX_DEPTH = 8;
  int removed = 0;
  uint16_t skip = 0;
  for (;;) {
    File dir = cardFs.open(path);
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      return removed;
    }
    File child = dir.openNextFile();
    for (uint16_t i = 0; child && i < skip; ++i) {
      child.close();
      child = dir.openNextFile();
    }
    if (!child) {
      dir.close();
      return removed;
    }
    const String childPath = child.path();
    const bool isDirectory = child.isDirectory();
    child.close();
    dir.close();
    bool gone = false;
    if (isDirectory) {
      if (depth < MAX_DEPTH) removed += removeTree(childPath, depth + 1);
      gone = cardFs.rmdir(childPath);
    } else {
      gone = cardFs.remove(childPath);
    }
    if (gone) {
      ++removed;
    } else {
      ++skip;
      serialLog.print("ATLAS|SD|WIPE|KEPT|");
      serialLog.println(childPath);
    }
  }
}

// A raw sector read reaches the card itself; a file check could be answered
// from the file system's cache after the card is gone.
bool cardAnswers() {
  static uint8_t sector[512];
  // SD_MMC has no raw read in this core; the card is the only FAT drive, so
  // FatFs drive 0 is the SDMMC driver's sector read.
  return disk_read(0, sector, 0, 1) == RES_OK;
}

// USB drive (owner request 2026-10-29): the card as a USB mass-storage disk
// next to the USB serial port, to copy logs and statistics off it or put
// files on it. Atlas and the computer never use the card at the same time:
// sharing drops Atlas's file system (no record store, no SD log, the header
// says USB DRIVE) and only then shows the computer a disk; ending it (a tap,
// the computer's Eject, or the cable pulled) hides the disk first and then
// remounts from scratch, so Atlas reads whatever the computer wrote. The SD
// worker makes both changes. Constructed at start-up so the USB descriptor
// has the disk before USB starts; with no card shared it reports no medium.
USBMSC usbDisk;
std::atomic<bool> usbShareWanted{false};
constexpr uint16_t SECTOR_BYTES = 512;

// Sector reads and writes for the computer, straight to the card (FatFs
// drive 0 is the SDMMC driver, as in cardAnswers). TinyUSB's task calls them;
// only while shared, and under the card lock.
int32_t usbDiskRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) {
  if (offset != 0 || size % SECTOR_BYTES != 0) return -1;
  CardLock lock;
  if (cardState.load() != CardState::UsbDrive) return -1;
  return disk_read(0, static_cast<BYTE *>(buffer), lba, size / SECTOR_BYTES) == RES_OK
             ? static_cast<int32_t>(size) : -1;
}
int32_t usbDiskWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) {
  if (offset != 0 || size % SECTOR_BYTES != 0) return -1;
  CardLock lock;
  if (cardState.load() != CardState::UsbDrive) return -1;
  return disk_write(0, buffer, lba, size / SECTOR_BYTES) == RES_OK
             ? static_cast<int32_t>(size) : -1;
}
// The computer's Eject (SCSI START STOP UNIT with LoEj) gives the card back.
bool usbDiskStartStop(uint8_t, bool start, bool loadEject) {
  if (loadEject && !start) usbShareWanted.store(false);
  return true;
}
// Cable pulled or computer asleep: the card comes back to Atlas.
void usbEvent(void *, esp_event_base_t, int32_t id, void *) {
  if (id == ARDUINO_USB_STOPPED_EVENT || id == ARDUINO_USB_SUSPEND_EVENT) usbShareWanted.store(false);
}

// Under the card lock, from the worker. Needs a working mount; logging and
// the record store stop first.
bool startUsbDrive() {
  if (cardState.load() != CardState::Mounted) return false;
  DWORD sectors = 0;
  if (disk_ioctl(0, GET_SECTOR_COUNT, &sectors) != RES_OK || sectors == 0) return false;
  sdStore.end();
  // Detach FatFs so nothing Atlas still holds can write stale tables over
  // the computer's changes; the files were all closed after each access.
  f_unmount("0:");
  cardState.store(CardState::UsbDrive);
  logState.store(LogState::Off);
  cardGeneration.fetch_add(1);
  usbDisk.begin(sectors, SECTOR_BYTES);
  usbDisk.mediaPresent(true);
  serialLog.print("ATLAS|SD|USB_DRIVE|ON|");
  serialLog.println(String(static_cast<unsigned long>(sectors)));
  return true;
}

// Under the card lock, from the worker: hide the disk, then a clean mount.
SdMountResult stopUsbDrive() {
  usbDisk.mediaPresent(false);
  cardFs.end();
  cardState.store(CardState::NoCard);
  serialLog.println("ATLAS|SD|USB_DRIVE|OFF");
  return mountCard();
}

// The SD worker: drains the diagnostic log to the card and handles hot-plug
// (SdHotplug). Low priority, never the gameplay loop.
void sdTask(void *) {
  TurnHubStorage::DiagnosticLog log;
  SdHotplug plug;
  plug.start(cardState.load() == CardState::Mounted ? SdMountResult::Ok
                                                    : SdMountResult::NoCard,
             millis());
  bool logOpen = false;
  bool logBlocked = false;  // This mount's card refused the log.
  bool firstOpen = true;
  const uint32_t bootId = esp_random();
  uint64_t cursor = 0;
  char buffer[1024];
  char marker[160];
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint32_t nowMs = millis();

    // USB drive: while shared, nothing else here touches the card.
    const bool shared = cardState.load() == CardState::UsbDrive;
    if (usbShareWanted.load() != shared) {
      if (shared) {
        SdMountResult result;
        {
          CardLock lock;
          result = stopUsbDrive();
        }
        plug.mountAttempted(result, millis());
        logOpen = false;
        logBlocked = false;
      } else {
        bool started;
        {
          CardLock lock;
          started = startUsbDrive();
        }
        if (started) {
          logOpen = false;
        } else {
          usbShareWanted.store(false);
          serialLog.println("ATLAS|SD|USB_DRIVE|NO_CARD");
        }
      }
      continue;
    }
    if (shared) continue;

    if (plug.mounted() && !logBlocked) {
      uint64_t lost = 0;
      serialLog.drainFramework();
      const size_t count = serialLog.readSince(cursor, buffer, sizeof(buffer), lost);
      if (!logOpen || lost || count) {
        CardLock lock;
        bool ok = true;
        if (!logOpen) {
          // A boot record the first time, a remount marker after a card swap.
          const int n = firstOpen
              ? snprintf(marker, sizeof(marker),
                    "\nATLAS|BOOT_RECORD|FW|%s|BOOT_ID|%08lX|RESET|%s\n",
                    TurnHubFirmware::VERSION, static_cast<unsigned long>(bootId),
                    TurnHub::resetReason())
              : snprintf(marker, sizeof(marker),
                    "\nATLAS|SD|LOG|REMOUNTED|BOOT_ID|%08lX|UPTIME_MS|%lu\n",
                    static_cast<unsigned long>(bootId), static_cast<unsigned long>(nowMs));
          ok = n > 0 && static_cast<size_t>(n) < sizeof(marker) &&
               log.begin(sdFileSystem) && log.append(marker, static_cast<size_t>(n));
          if (ok) {
            logOpen = true;
            firstOpen = false;
            logState.store(LogState::Ready);
            serialLog.println("ATLAS|SD|LOG|READY");
          }
        }
        // Drain the whole ring while the card is locked: the RAM ring is small
        // (serial_log.h), so a burst of lines must not wait for later passes.
        size_t chunk = count;
        for (;;) {
          if (ok && lost) {
            const uint32_t before = logLostBytes.load();
            logLostBytes.store(lost > UINT32_MAX - before ? UINT32_MAX : before + static_cast<uint32_t>(lost));
            const int n = snprintf(marker, sizeof(marker), "\nATLAS|SD|LOG|LOST_BYTES|%llu\n",
                                   static_cast<unsigned long long>(lost));
            ok = n > 0 && static_cast<size_t>(n) < sizeof(marker) &&
                 log.append(marker, static_cast<size_t>(n));
          }
          if (ok && chunk) ok = log.append(buffer, chunk);
          if (!ok || chunk < sizeof(buffer)) break;
          chunk = serialLog.readSince(cursor, buffer, sizeof(buffer), lost);
        }
        if (ok) {
          plug.accessed(true, nowMs);
        } else if (cardAnswers()) {
          // The card is there but won't take the log (full, read-only or an
          // unexpected log file): stop logging and the record store for this
          // mount, as before hot-plug, rather than remounting in a loop. The
          // probe still notices when it is pulled.
          logState.store(LogState::IoError);
          logBlocked = true;
          serialLog.println("ATLAS|SD|LOG|IO_ERROR");
          plug.accessed(true, nowMs);
        } else {
          plug.accessed(false, nowMs);  // Gone: unmount below.
        }
      }
    }

    nowMs = millis();
    switch (plug.next(nowMs)) {
      case SdStep::Idle:
        break;
      case SdStep::Probe: {
        bool ok;
        {
          CardLock lock;
          ok = cardAnswers();
        }
        plug.accessed(ok, nowMs);
        break;
      }
      case SdStep::Unmount: {
        CardLock lock;
        unmountCard();
        logOpen = false;
        logBlocked = false;
        plug.unmounted(millis());
        break;
      }
      case SdStep::Mount: {
        SdMountResult result;
        {
          CardLock lock;
          result = mountCard();
        }
        plug.mountAttempted(result, millis());
        break;
      }
    }
  }
}
}  // namespace

void beginSdCard() {
  cardFs.setPins(AtlasConfig::SD_CLK_PIN, AtlasConfig::SD_CMD_PIN, AtlasConfig::SD_D0_PIN,
                 AtlasConfig::SD_D1_PIN, AtlasConfig::SD_D2_PIN, AtlasConfig::SD_D3_PIN);
  usbDisk.vendorID("TurnHub");
  usbDisk.productID("Atlas microSD");
  usbDisk.productRevision("1.0");
  usbDisk.onRead(usbDiskRead);
  usbDisk.onWrite(usbDiskWrite);
  usbDisk.onStartStop(usbDiskStartStop);
  usbDisk.mediaPresent(false);
  USB.onEvent(usbEvent);
  // Boot mounts synchronously so profiles start with the card if it is there.
  mountCard();
  // Without the card lock the worker would race the record store, so no lock
  // means no worker: the boot mount (if any) stays, without logging or
  // hot-plug.
  if (cardLock == nullptr) cardLock = xSemaphoreCreateMutex();
  if (cardLock == nullptr) {
    logState.store(LogState::TaskUnavailable);
    serialLog.println("ATLAS|SD|LOG|TASK_UNAVAILABLE");
    return;
  }
  if (cardState.load() == CardState::Mounted) logState.store(LogState::Starting);
  // SD writes are isolated from gameplay and radio callbacks. ESP32 stack size
  // is bytes; includes the 1 KiB drain buffer plus filesystem call headroom
  // (the headroom the earlier 2 KiB buffer had in 6 KiB).
  // The worker runs with or without a card: it mounts one inserted later.
  if (xTaskCreate(sdTask, "sd-card", 5120, nullptr, 1, nullptr) != pdPASS) {
    logState.store(LogState::TaskUnavailable);
    serialLog.println("ATLAS|SD|LOG|TASK_UNAVAILABLE");
  }
}

String sdCardDiagnosticsJson() {
  const CardState state = cardState.load();
  String json = String("{\"state\":\"") + cardStateName(state) + "\"";
  if (state == CardState::Mounted || state == CardState::StoreError ||
      state == CardState::SelfTestError) {
    json += String(",\"type\":\"") + mountedType + "\"";
    json += ",\"cardMB\":" + String(cardMB);
    json += ",\"totalMB\":" + String(totalMB);
    json += ",\"usedKB\":" + String(usedKB) + ",\"usageSample\":\"mount\"";
    json += String(",\"store\":\"") + TurnHub::storageStatusName(storeStatus.load()) + "\"";
  }
  if (state == CardState::Mounted || state == CardState::SelfTestError) {
    json += String(",\"selfTest\":\"") +
            TurnHub::storageStatusName(selfTestStatus.load()) + "\"";
  }
  json += ",\"mounts\":" + String(mountCount.load());
  json += String(",\"logging\":{\"state\":\"") + logStateName(logState.load()) +
          "\",\"lostBytes\":" + String(logLostBytes.load()) + "}";
  json += "}";
  return json;
}

TurnHubStorage::BlobStore *sdBlobStore() {
  return storeUsable() ? &lockedStore : nullptr;
}

bool sdCardReady() { return storeUsable(); }

int wipeSdCard() {
  CardLock lock;
  if (cardState.load() != CardState::Mounted) {
    serialLog.println("ATLAS|SD|WIPE|NO_CARD");
    return -1;
  }
  const int removed = removeTree("/", 0);
  serialLog.print("ATLAS|SD|WIPE|REMOVED|");
  serialLog.println(String(removed));
  // Unmounted, so neither the log worker nor the luxury store writes again
  // before the restart.
  unmountCard();
  return removed;
}

uint32_t sdCardGeneration() { return cardGeneration.load(); }

bool shareSdCardOverUsb(bool share) {
  if (share && (cardLock == nullptr || cardState.load() != CardState::Mounted)) return false;
  usbShareWanted.store(share);
  return true;
}

bool sdCardUsbShareWanted() { return usbShareWanted.load(); }

bool sdCardOnUsb() { return cardState.load() == CardState::UsbDrive; }

}  // namespace TurnHubAtlas
