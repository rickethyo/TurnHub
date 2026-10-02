#include "ota_manager.h"
#include "sigil_update_service.h"
#include "firmware_package_mbedtls.h"
#include "firmware_signing_key.h"
#include "firmware_version.h"

#include <Update.h>
#include <esp_ota_ops.h>

#include "web_api.h"
#include "web_pages.h"
#include "portal_qr_asset.h"
#include "portal_pack.h"
#include "sd_card.h"
#if defined(TURNHUB_GZIP_PAGES)
#include "web_pages_gzip.h"
#endif
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHub {

namespace {
class AtlasFlashSink final : public TurnHubFirmwarePackage::Sink {
 public:
  bool header(const TurnHubFirmwarePackage::Header &h) override {
    TurnHubAtlas::invalidateSigilPackage();
    return Update.begin(h.imageSize, U_FLASH);
  }
  bool image(const uint8_t *data, size_t n) override {
    return Update.write(const_cast<uint8_t *>(data), n) == n;
  }
};
AtlasFlashSink atlasSink;
TurnHubFirmwarePackage::MbedtlsPackageCrypto atlasCrypto;
TurnHubFirmwarePackage::Reader atlasReader;

// Web portal pack uploads (portal_pack.h): checked like firmware, unpacked to
// the SD card instead of flash. The installer is made once the card is known.
TurnHubFirmwarePackage::Reader portalReader;
TurnHubPortal::Installer *portalInstaller = nullptr;

// Hashed portal assets (/assets/...) from the live pack on the card.
class PortalAssetHandler final : public RequestHandler {
 public:
  bool canHandle(HTTPMethod method, String uri) override {
    return method == HTTP_GET && uri.startsWith("/assets/");
  }
  bool handle(WebServer &server, HTTPMethod, String uri) override {
    // Asset names carry a content hash, so a cached copy never goes stale.
    if (!TurnHubAtlas::sdServePortalFile(server, uri.c_str() + 1, "public, max-age=31536000, immutable")) {
      server.send(404, "text/plain", "Not found");
    }
    return true;
  }
};
PortalAssetHandler portalAssetHandler;

String portalVersionText() {
  TurnHubFirmwarePackage::Version version{};
  if (!TurnHubAtlas::sdPortalVersion(version)) return String();
  char text[16];
  TurnHubPortal::formatVersion(version, text, sizeof(text));
  return String(text);
}


void printPartitionDiagnostic(
    const char *role,
    const esp_partition_t *partition) {
  serialLog.print("ATLAS|PARTITION|");
  serialLog.print(role);
  serialLog.print('|');

  if (partition == nullptr) {
    serialLog.println("NONE");
    return;
  }

  serialLog.print(partition->label);
  serialLog.print("|ADDRESS|0x");
  serialLog.print(static_cast<unsigned long>(partition->address), HEX);
  serialLog.print("|SIZE|");
  serialLog.println(static_cast<unsigned long>(partition->size));
}

void printBootPartitionDiagnostics() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);

  printPartitionDiagnostic("RUNNING", running);
  printPartitionDiagnostic("BOOT", boot);
  printPartitionDiagnostic("NEXT_OTA", next);

  if (running != nullptr && boot != nullptr) {
    serialLog.print("ATLAS|PARTITION|BOOT_MATCHES_RUNNING|");
    serialLog.println(running->address == boot->address ? "YES" : "NO");
  }
}

const char UPDATE_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
  <meta name="theme-color" content="#110d09">
  <title>TurnHub Atlas Update</title>
  <script>try{const h=document.documentElement;h.dataset.theme=localStorage.getItem('turnhubTheme')||(matchMedia('(prefers-contrast: more)').matches?'contrast':'brass');if(localStorage.getItem('turnhubReduceMotion')==='1')h.dataset.motion='reduce'}catch(_){}</script>
  <link rel="stylesheet" href="/theme.css">
  <style>
    .status { font-family: ui-monospace, "SF Mono", Menlo, Consolas, monospace; font-size: .85rem; color: var(--muted); background: var(--inset); border: 1px solid var(--line); border-radius: var(--radius-sm); padding: 12px 14px; margin: 16px 0; }
    .drop { display: grid; gap: 10px; padding: 18px; border: 1px dashed var(--line-strong); border-radius: var(--radius-sm); background: var(--inset); }
    .drop label { margin: 0; }
    input[type=file] { min-height: 0; padding: 10px; }
    input[type=file]::file-selector-button { font: inherit; font-weight: 650; margin-right: 12px; border: 1px solid var(--line-strong); border-radius: 10px; padding: 8px 12px; background: var(--surface-2); color: var(--text); cursor: pointer; }
    #upload { width: 100%; margin-top: 16px; min-height: 50px; }
    progress { -webkit-appearance: none; appearance: none; width: 100%; height: 12px; margin-top: 18px; border: 1px solid var(--line-strong); border-radius: 99px; overflow: hidden; background: var(--inset); }
    progress::-webkit-progress-bar { background: var(--inset); }
    progress::-webkit-progress-value { background: var(--accent-grad); }
    progress::-moz-progress-bar { background: var(--accent); }
    #message { min-height: 48px; font-weight: 650; margin-top: 14px; }
    .ok { color: var(--good) !important; }
    .warn { color: var(--warn) !important; }
    .error { color: var(--bad) !important; }
  </style>
</head>
<body>
<div class="page narrow">
  <header class="page-head"><a class="brand" href="/portal"><span class="brand-mark" aria-hidden="true"></span><span><span class="brand-name">TurnHub</span><span class="brand-sub">Firmware</span></span></a><a class="btn small ghost" href="/portal">← Back to TurnHub</a></header>
  <main class="card">
    <h1>Atlas Firmware Update</h1>
    <p class="small">Upload a signed <code>.thfw</code> package: Atlas firmware, or the web portal pack (<code>portal-x.y.z.thfw</code>). A portal pack goes onto the microSD card and needs no restart.</p>
    <div class="notice" style="margin-top:14px">
      For safety, start updates only from Lobby or Game Over and <strong>verify at the table first (Device Settings in the portal: Verify at the table, then the code the Atlas screen shows), then upload within 10 minutes</strong>. Atlas will restart automatically after the image is written.
    </div>
    <div id="current" class="status">Reading current firmware...</div>
    <div class="drop"><label for="file">Firmware or portal package</label><input id="file" type="file" accept=".thfw,application/octet-stream"></div>
    <button id="upload" class="primary" disabled>Upload &amp; Verify</button>
    <progress id="progress" max="100" value="0" aria-label="Upload progress"></progress>
    <p id="message" role="status" aria-live="polite"></p>
  </main>
</div>
<script>
  const file = document.getElementById('file');
  const button = document.getElementById('upload');
  const progress = document.getElementById('progress');
  const message = document.getElementById('message');
  const current = document.getElementById('current');

  let baseline = null;

  function setMessage(text, kind = '') {
    message.textContent = text;
    message.className = kind;
  }

  async function readStatus() {
    const response = await fetch('/api/status?ota=' + Date.now(), { cache: 'no-store' });
    if (!response.ok) throw new Error('status');
    return response.json();
  }

  async function loadBaseline() {
    try {
      baseline = await readStatus();
      current.textContent = 'Running: v' + baseline.firmware + ' · build ' + baseline.build;
    } catch (_) {
      current.textContent = 'Unable to read current firmware identity.';
    }
    try {
      const portal = await (await fetch('/api/portal', { cache: 'no-store' })).json();
      current.textContent += portal.installed ? ' · portal v' + portal.version
        : (portal.card ? ' · no portal pack installed' : ' · no microSD card');
    } catch (_) {}
  }

  // Byte 9 of a .thfw header is the product; 4 is the web portal pack.
  async function packageProduct(f) {
    const head = new Uint8Array(await f.slice(0, 16).arrayBuffer());
    const magic = String.fromCharCode(...head.slice(0, 8));
    return magic === 'THFWPKG1' ? head[9] : 0;
  }

  function uploadPortal(f) {
    setMessage('Uploading the portal pack... Atlas checks the signature and unpacks it onto the microSD card.');
    const form = new FormData();
    form.append('portal', f);
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/portal/install');
    xhr.upload.onprogress = event => {
      if (event.lengthComputable) progress.value = Math.round((event.loaded / event.total) * 95);
    };
    xhr.onload = () => {
      let data = {};
      try { data = JSON.parse(xhr.responseText); } catch (_) {}
      if (xhr.status >= 200 && xhr.status < 300) {
        progress.value = 100;
        setMessage('Portal v' + data.version + ' installed. Reload the portal to see it.', 'ok');
        loadBaseline();
      } else {
        setMessage(data.error || 'Portal install failed.', 'error');
      }
      button.disabled = false;
    };
    xhr.onerror = () => { setMessage('Connection lost during the upload; the previous portal is still installed.', 'error'); button.disabled = false; };
    xhr.timeout = 300000;
    xhr.ontimeout = () => { setMessage('The upload took too long and was stopped. The previous portal is still installed.', 'error'); button.disabled = false; };
    xhr.setRequestHeader('X-TurnHub-Token', localStorage.getItem('turnhubSessionToken') || '');
    xhr.send(form);
  }

  async function verifyRestart() {
    setMessage('Image written. Atlas is restarting; waiting for the new firmware to boot...', 'warn');

    const deadline = Date.now() + 25000;
    let sawDisconnect = false;

    while (Date.now() < deadline) {
      await new Promise(resolve => setTimeout(resolve, 1000));
      try {
        const status = await readStatus();
        const changed = !baseline ||
          status.firmware !== baseline.firmware ||
          status.build !== baseline.build;

        if (changed) {
          progress.value = 100;
          current.textContent = 'Running: v' + status.firmware + ' · build ' + status.build;
          setMessage('Update verified. Atlas rebooted into the newly uploaded firmware.', 'ok');
          button.disabled = false;
          return;
        }

        if (sawDisconnect) {
          current.textContent = 'Running: v' + status.firmware + ' · build ' + status.build;
        }
      } catch (_) {
        sawDisconnect = true;
      }
    }

    setMessage(
      'The firmware image was accepted, but the new build could not be verified after restart. Check the Atlas serial log and partition/boot configuration before treating this update as successful.',
      'error');
    button.disabled = false;
  }

  file.addEventListener('change', () => {
    button.disabled = !file.files.length;
    progress.value = 0;
    setMessage('');
  });

  button.addEventListener('click', async () => {
    if (!file.files.length) return;

    button.disabled = true;
    if (await packageProduct(file.files[0]) === 4) {
      uploadPortal(file.files[0]);
      return;
    }
    setMessage('Uploading... Atlas checks that you are still verified at the table when the upload begins.');

    const form = new FormData();
    form.append('firmware', file.files[0]);

    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/firmware');
    xhr.upload.onprogress = event => {
      if (event.lengthComputable) {
        progress.value = Math.round((event.loaded / event.total) * 95);
      }
    };
    xhr.onload = () => {
      let data = {};
      try { data = JSON.parse(xhr.responseText); } catch (_) {}
      if (xhr.status >= 200 && xhr.status < 300) {
        progress.value = 95;
        verifyRestart();
      } else {
        setMessage(data.error || 'Update failed.', 'error');
        button.disabled = false;
      }
    };
    xhr.onerror = () => {
      setMessage('Connection lost during upload before Atlas confirmed the image write.', 'error');
      button.disabled = false;
    };
    xhr.timeout = 180000;
    xhr.ontimeout = () => {
      setMessage('The upload took too long and was stopped. Check the Wi-Fi connection and try again.', 'error');
      button.disabled = false;
    };
    xhr.setRequestHeader('X-TurnHub-Token',localStorage.getItem('turnhubSessionToken')||'');
    xhr.send(form);
  });

  loadBaseline();
</script>
</body>
</html>
)HTML";

}  // namespace

OtaManager::OtaManager(WebServer &server, AllowedCallback allowedCallback)
    : server_(server), allowedCallback_(allowedCallback) {}

void OtaManager::begin() {
  printBootPartitionDiagnostics();

  server_.on("/portal-qr.js", HTTP_GET, [this]() {
    server_.sendHeader("Content-Encoding", "gzip");
    server_.send_P(200, "application/javascript", reinterpret_cast<const char *>(TurnHubWeb::QR_SCRIPT_GZIP), sizeof(TurnHubWeb::QR_SCRIPT_GZIP));
  });

  // Firmware builds send the gzip copies (tools/gzip_web_pages.py): about a
  // quarter of the bytes, so each phone's page load holds far fewer Wi-Fi
  // transmit buffers.
  server_.on("/theme.css", HTTP_GET, [this]() {
    server_.sendHeader("Cache-Control", "no-cache");
#if defined(TURNHUB_GZIP_PAGES)
    server_.sendHeader("Content-Encoding", "gzip");
    server_.send_P(200, "text/css", reinterpret_cast<const char *>(TurnHubWeb::THEME_CSS_GZIP), sizeof(TurnHubWeb::THEME_CSS_GZIP));
#else
    server_.send_P(200, "text/css", TurnHubWeb::THEME_CSS);
#endif
  });

  // The V1 portal comes from the pack on the microSD card when one is
  // installed; the built-in portal in flash is the fallback, and stays
  // reachable at /portal-classic (PORTAL_PACK.md).
  server_.on("/portal", HTTP_GET, [this]() {
    if (TurnHubAtlas::sdServePortalFile(server_, "index.html", "no-cache")) return;
    serveClassicPortal();
  });
  server_.on("/portal-classic", HTTP_GET, [this]() { serveClassicPortal(); });
  server_.addHandler(&portalAssetHandler);

  server_.on("/api/portal", HTTP_GET, [this]() {
    server_.sendHeader("Cache-Control", "no-store");
    const String version = portalVersionText();
    String json = "{\"card\":";
    json += TurnHubAtlas::sdCardReady() ? "true" : "false";
    json += ",\"installed\":";
    json += version.length() ? "true" : "false";
    json += ",\"version\":\"" + version + "\"}";
    server_.send(200, "application/json", json);
  });
  server_.on(
      "/api/portal/install",
      HTTP_POST,
      [this]() { handlePortalComplete(); },
      [this]() { handlePortalUpload(); });

  server_.on("/dev", HTTP_GET, [this]() {
    server_.sendHeader("Cache-Control", "no-store");
    TurnHubWebApi::serveRestrictedPage(server_,TurnHubWeb::DEV_HTML,TurnHubAccounts::Developer);
  });

  server_.on("/update", HTTP_GET, [this]() {
    server_.sendHeader("Cache-Control", "no-store");
    TurnHubWebApi::serveRestrictedPage(server_,UPDATE_HTML,TurnHubAccounts::Admin);
  });

  server_.on(
      "/api/firmware",
      HTTP_POST,
      [this]() { handleComplete(); },
      [this]() { handleUpload(); });

  TurnHubWebApi::begin(server_);
}

void OtaManager::serveClassicPortal() {
  server_.sendHeader("Cache-Control", "no-store");
#if defined(TURNHUB_GZIP_PAGES)
  server_.sendHeader("Content-Encoding", "gzip");
  server_.send_P(200, "text/html", reinterpret_cast<const char *>(TurnHubWeb::PORTAL_HTML_GZIP), sizeof(TurnHubWeb::PORTAL_HTML_GZIP));
#else
  server_.send_P(200, "text/html", TurnHubWeb::PORTAL_HTML);
#endif
}

void OtaManager::failPortal(const String &message) {
  portalInProgress_ = false;
  portalError_ = message;
  if (portalInstaller != nullptr) portalInstaller->abort();
  serialLog.print("ATLAS|PORTAL|ERROR|");
  serialLog.println(message);
}

String OtaManager::portalErrorText() const {
  using TurnHubFirmwarePackage::Error;
  const Error error = portalReader.error();
  // The installer's own error explains a sink refusal (Storage) or a failed
  // commit after a good package (None); otherwise it may be a stale one.
  if ((error == Error::Storage || error == Error::None) && portalInstaller != nullptr &&
      portalInstaller->error() != TurnHubPortal::ArchiveError::None) {
    if (portalInstaller->error() == TurnHubPortal::ArchiveError::Storage) {
      return "Atlas could not write the portal to the microSD card";
    }
    return String("The portal pack is damaged (") + TurnHubPortal::archiveErrorName(portalInstaller->error()) + ")";
  }
  if (error == Error::WrongProduct) return "That package is firmware, not a portal pack";
  if (error == Error::OlderVersion) {
    return "Atlas already has portal v" + portalVersionText() + "; install the same or a newer pack";
  }
  if (error == Error::None) return "Atlas could not install the portal pack";
  return TurnHubFirmwarePackage::errorMessage(error);
}

void OtaManager::handlePortalUpload() {
  HTTPUpload &upload = server_.upload();
  switch (upload.status) {
    case UPLOAD_FILE_START: {
      portalInProgress_ = false;
      portalSuccess_ = false;
      portalDenied_ = false;
      portalError_ = "";
      if (!TurnHubWebApi::hasPermission(server_, TurnHubAccounts::Admin) || !TurnHubWebApi::verifiedAtTable(server_) ||
          allowedCallback_ == nullptr || !allowedCallback_() || inProgress()) {
        portalDenied_ = true;
        serialLog.println("ATLAS|PORTAL|DENIED");
        return;
      }
      TurnHubPortal::Files *files = TurnHubAtlas::sdPortalFiles();
      if (files == nullptr) {
        portalError_ = "Atlas needs its microSD card for the portal. Insert the card that came with Atlas and try again";
        return;
      }
      if (portalInstaller == nullptr) portalInstaller = new TurnHubPortal::Installer(*files);
      TurnHubFirmwarePackage::Version running{0, 0, 0};
      TurnHubAtlas::sdPortalVersion(running);
      portalReader.begin(atlasCrypto, {TurnHubFirmwarePackage::PUBLIC_KEY, TurnHubFirmwarePackage::KEY_ID,
          TurnHubFirmwarePackage::productBit(TurnHubFirmwarePackage::Product::Portal), running,
          TurnHubPortal::MAX_PACK_BYTES}, *portalInstaller);
      portalInProgress_ = true;
      portalBytes_ = 0;
      serialLog.print("ATLAS|PORTAL|START|");
      serialLog.println(upload.filename);
      break;
    }
    case UPLOAD_FILE_WRITE:
      if (!portalInProgress_) return;
      if (!portalReader.write(upload.buf, upload.currentSize)) {
        failPortal(portalErrorText());
        return;
      }
      portalBytes_ += upload.currentSize;
      break;
    case UPLOAD_FILE_END:
      if (!portalInProgress_) return;
      portalInProgress_ = false;
      if (!portalReader.finish() || !portalInstaller->commit()) {
        failPortal(portalErrorText());
        return;
      }
      portalSuccess_ = true;
      TurnHubAtlas::sdPortalChanged();
      serialLog.print("ATLAS|PORTAL|INSTALLED|");
      serialLog.print(portalVersionText());
      serialLog.print("|FILES|");
      serialLog.println(portalInstaller->filesWritten());
      break;
    case UPLOAD_FILE_ABORTED:
      if (portalInProgress_ && portalInstaller != nullptr) portalInstaller->abort();
      portalInProgress_ = false;
      serialLog.println("ATLAS|PORTAL|ABORTED");
      break;
    default:
      break;
  }
}

void OtaManager::handlePortalComplete() {
  server_.sendHeader("Cache-Control", "no-store");
  if (portalDenied_) {
    server_.send(403, "application/json",
        "{\"ok\":false,\"error\":\"Portal install not armed. Return to Lobby or Game Over and verify at the table in the portal (the code the Atlas screen shows), then start the upload within 10 minutes.\"}");
    return;
  }
  if (!portalSuccess_) {
    // The messages above are fixed text plus version digits: no quotes to escape.
    const String error = portalError_.length() ? portalError_ : String("The upload ended before the portal pack arrived");
    server_.send(500, "application/json", "{\"ok\":false,\"error\":\"" + error + "\"}");
    return;
  }
  server_.send(200, "application/json",
      "{\"ok\":true,\"version\":\"" + portalVersionText() + "\",\"files\":" +
      String(portalInstaller->filesWritten()) + ",\"bytes\":" + String(portalBytes_) + "}");
}

void OtaManager::resetAttempt() {
  inProgress_ = false;
  success_ = false;
  denied_ = false;
  errorCode_ = 0;
  bytesWritten_ = 0;
}

void OtaManager::fail(uint8_t errorCode) {
  inProgress_ = false;
  success_ = false;
  errorCode_ = errorCode;
  serialLog.print("ATLAS|OTA|ERROR|");
  serialLog.println(errorCode_);
}

void OtaManager::handleUpload() {
  HTTPUpload &upload = server_.upload();

  switch (upload.status) {
    case UPLOAD_FILE_START:
      resetAttempt();

      if (!TurnHubWebApi::hasPermission(server_,TurnHubAccounts::Admin) || !TurnHubWebApi::verifiedAtTable(server_) ||
          allowedCallback_ == nullptr || !allowedCallback_() || TurnHubAtlas::sigilUpdatesBusy() || restartAtMs_ != 0) {
        denied_ = true;
        serialLog.println("ATLAS|OTA|DENIED");
        return;
      }

      {
        const auto *slot = esp_ota_get_next_update_partition(nullptr);
        if (!slot) { fail(static_cast<uint8_t>(TurnHubFirmwarePackage::Error::Storage)); return; }
        atlasReader.begin(atlasCrypto, {TurnHubFirmwarePackage::PUBLIC_KEY,
            TurnHubFirmwarePackage::KEY_ID,
            TurnHubFirmwarePackage::productBit(TurnHubFirmwarePackage::Product::Atlas),
            {TurnHubFirmware::MAJOR, TurnHubFirmware::MINOR, TurnHubFirmware::PATCH}, slot->size}, atlasSink);
      }

      inProgress_ = true;
      serialLog.print("ATLAS|OTA|START|");
      serialLog.println(upload.filename);
      break;

    case UPLOAD_FILE_WRITE:
      if (!inProgress_ || denied_) {
        return;
      }

      if (!atlasReader.write(upload.buf, upload.currentSize)) {
        fail(static_cast<uint8_t>(atlasReader.error()));
        Update.printError(Serial);
        Update.abort();
        return;
      }

      bytesWritten_ += upload.currentSize;
      break;

    case UPLOAD_FILE_END:
      if (!inProgress_ || denied_) {
        return;
      }

      if (!atlasReader.finish() || !Update.end()) {
        fail(static_cast<uint8_t>(atlasReader.error() == TurnHubFirmwarePackage::Error::None
            ? TurnHubFirmwarePackage::Error::Storage : atlasReader.error()));
        Update.abort();
        Update.printError(Serial);
        return;
      }

      inProgress_ = false;
      success_ = true;
      serialLog.print("ATLAS|OTA|IMAGE_WRITTEN|");
      serialLog.println(bytesWritten_);
      break;

    case UPLOAD_FILE_ABORTED:
      if (inProgress_) {
        Update.abort();
      }
      inProgress_ = false;
      success_ = false;
      serialLog.println("ATLAS|OTA|ABORTED");
      break;

    default:
      break;
  }
}

void OtaManager::handleComplete() {
  server_.sendHeader("Cache-Control", "no-store");

  if (denied_) {
    server_.send(
        403,
        "application/json",
        "{\"ok\":false,\"error\":\"Update not armed. Return to Lobby or Game Over and verify at the table in the portal (the code the Atlas screen shows), then start the upload within 10 minutes.\"}");
    return;
  }

  if (!success_) {
    char response[192];
    snprintf(
        response,
        sizeof(response),
        "{\"ok\":false,\"error\":\"Firmware update failed: %s\",\"code\":%u}",
        TurnHubFirmwarePackage::errorMessage(static_cast<TurnHubFirmwarePackage::Error>(errorCode_)),
        static_cast<unsigned>(errorCode_));
    server_.send(500, "application/json", response);
    return;
  }

  char response[160];
  snprintf(
      response,
      sizeof(response),
      "{\"ok\":true,\"message\":\"Firmware image written; restart and boot verification pending.\",\"bytes\":%lu}",
      static_cast<unsigned long>(bytesWritten_));
  server_.send(200, "application/json", response);

  restartAtMs_ = millis() + 1200;
}

void OtaManager::update(uint32_t nowMs) {
  if (restartAtMs_ == 0) {
    return;
  }

  if (static_cast<int32_t>(nowMs - restartAtMs_) < 0) {
    return;
  }

  serialLog.println("ATLAS|OTA|RESTART_FOR_VERIFICATION");
  delay(50);
  ESP.restart();
}

bool OtaManager::inProgress() const {
  return inProgress_ || restartAtMs_ != 0;
}

}  // namespace TurnHub
