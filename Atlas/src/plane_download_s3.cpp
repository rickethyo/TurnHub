// Direct Scryfall fallback for the hub's current/next planes. Network task only;
// loop() selects IDs and commits results through the existing SD write queue.
#include "plane_download.h"
#include "plane_image.h"
#include "atlas_app.h"
#include "card_cache.h"
#include "home_wifi.h"
#include "hosted_game.h"
#include "scryfall_roots.h"
#include "serial_log.h"
#include "sigil_update_service.h"
#include "touch_controls.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace TurnHubAtlas {
namespace {
constexpr size_t JSON_LIMIT = 16384;
constexpr char AGENT[] = "TurnHub/0.1 (https://github.com/rickethyo/TurnHub)";

class DownloadBuffer : public Stream {
 public:
  uint8_t *data = nullptr;
  size_t size = 0, capacity;
  uint32_t started = millis();
  explicit DownloadBuffer(size_t limit) : capacity(limit) {
    data = static_cast<uint8_t *>(heap_caps_malloc(limit + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  ~DownloadBuffer() override { free(data); }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *bytes, size_t count) override {
    if (!data || count > capacity - size || millis() - started > 20000) return 0;
    memcpy(data + size, bytes, count); size += count; data[size] = 0; return count;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

bool fetch(const char *url, const char *accept, DownloadBuffer &body) {
  if (!body.data) return false;
  WiFiClientSecure client;
  client.setCACert(SCRYFALL_ROOTS);
  client.setHandshakeTimeout(8);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(8000);
  // Both fixed API URL and validated image URL are HTTPS; never follow a redirect to another host.
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;
  http.addHeader("User-Agent", AGENT);
  http.addHeader("Accept", accept);
  http.addHeader("Accept-Encoding", "identity");
  const int code = http.GET();
  const int length = http.getSize();
  bool ok = code == 200 && (length < 0 || static_cast<size_t>(length) <= body.capacity);
  if (ok) ok = http.writeToStream(&body) >= 0 && body.size > 0;
  http.end();
  TurnHub::serialLog.printf("ATLAS|PLANES|DIRECT_HTTP|%d|bytes=%u|ok=%u\n", code, static_cast<unsigned>(body.size), ok);
  return ok;
}

PlaneDownloadSchedule schedule;
std::atomic<bool> done{false};
bool running = false;
char requested[37] = "";
bool needArt = true;
DownloadedPlane result;
uint8_t *jpeg = nullptr;
size_t jpegSize = 0;
bool parsed = false;

void downloadPlane() {
  DownloadBuffer metadata(JSON_LIMIT);
  const std::string url = std::string("https://api.scryfall.com/cards/") + requested;
  parsed = fetch(url.c_str(), "application/json", metadata) &&
      parseScryfallPlane(std::string(reinterpret_cast<char *>(metadata.data), metadata.size), requested, result);
  if (parsed && needArt && !result.artUrl.empty()) {
    // Scryfall asks for 50-100 ms spacing. Only one card is fetched at a time.
    vTaskDelay(pdMS_TO_TICKS(150));
    DownloadBuffer image(256 * 1024);
    if (fetch(result.artUrl.c_str(), "image/jpeg", image))
      normalizePlaneImage(image.data, image.size, jpeg, jpegSize);
  }
  TurnHub::serialLog.printf("ATLAS|PLANES|DIRECT|metadata=%u|art=%u|stack=%u\n", parsed,
      static_cast<unsigned>(jpegSize), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void downloadTask(void *) {
  downloadPlane(); // Its local buffers are destroyed before FreeRTOS deletes the task.
  done.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

void clearResult() {
  free(jpeg); jpeg = nullptr; jpegSize = 0; result = DownloadedPlane();
  parsed = false; done.store(false, std::memory_order_relaxed); running = false;
}
}  // namespace

void serviceHostedPlaneDownload(uint32_t nowMs) {
  static uint32_t checkedAt = 0;
  if (nowMs - checkedAt < 1000) return;
  checkedAt = nowMs;
  if (running && !done.load(std::memory_order_acquire)) return;
  const auto &table = TurnHubHosted::hostedTable();
  const auto ids = table.live(nowMs) ? hostedPlaneIds(table.game()) : std::vector<std::string>();
  if (running) {
    if (!done.load(std::memory_order_acquire)) return;
    if (!parsed || std::find(ids.begin(), ids.end(), requested) == ids.end() || !TurnHubCards::planeIndexReady()) {
      if (!parsed) showSyncStatus(nowMs, "Scryfall unavailable; will retry");
      clearResult(); return;
    }
    if (!TurnHubCards::rememberHostedPlane(result.id.c_str(), result.name.c_str(), result.phenomenon)) {
      showSyncStatus(nowMs, "Couldn't keep the plane catalog"); clearResult(); return;
    }
    bool art = false;
    const bool text = TurnHubCards::hasPlaneCard(requested, art);
    if (art || (text && !jpegSize)) { clearResult(); return; }
    const auto queued = TurnHubCards::savePlaneCard(requested, String(result.type.c_str()), String(result.artist.c_str()),
        String(result.text.c_str()), String(result.chaos.c_str()), jpeg, jpegSize);
    if (queued == TurnHubCards::CardQueue::Busy) return;
    showSyncStatus(nowMs, queued == TurnHubCards::CardQueue::Queued
        ? (jpegSize ? "Plane from Scryfall: saving" : "Plane text saved; art will retry") : "Couldn't save the Scryfall plane");
    clearResult(); return;
  }
  if (!homeWifiConnected() || !TurnHubCards::planeIndexReady() || sigilUpdatesBusy() || ota.inProgress()) {
    schedule.due("", nowMs); return;
  }
  std::string missing;
  bool hasArt = false;
  for (const auto &id : ids) {
    const bool text = TurnHubCards::hasPlaneCard(id.c_str(), hasArt);
    if (!text || !hasArt || !TurnHubCards::findPlane(id.c_str())) { missing = id; break; }
  }
  if (!schedule.due(missing, nowMs)) return;
  // TLS shares internal RAM with the home server; buffers above live in PSRAM.
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < 20000 ||
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 65000) return;
  snprintf(requested, sizeof(requested), "%s", missing.c_str());
  needArt = !hasArt; done.store(false, std::memory_order_relaxed);
  // Lowest task priority: the home TLS server, radio and idle watchdog keep
  // getting CPU while a progressive image is decoded and encoded.
  if (xTaskCreatePinnedToCore(downloadTask, "plane-fetch", 12288, nullptr, 0, nullptr, 0) == pdPASS) {
    running = true; schedule.started(nowMs);
    showSyncStatus(nowMs, "Getting a plane from Scryfall");
  } else schedule.started(nowMs);
}
}  // namespace TurnHubAtlas
