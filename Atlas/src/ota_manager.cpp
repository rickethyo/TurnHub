#include "ota_manager.h"

#include <Update.h>
#include <esp_ota_ops.h>

#include "web_api.h"
#include "web_pages.h"
#include "portal_qr_asset.h"
#include "serial_log.h"

using TurnHub::serialLog;

namespace TurnHub {

namespace {

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
    <p class="small">Upload the Atlas <code>firmware.bin</code> produced by PlatformIO.</p>
    <div class="notice" style="margin-top:14px">
      For safety, start updates only from Lobby or Game Over and <strong>verify at the table first (Device Settings in the portal: Verify at the table, then the code the Atlas screen shows), then upload within 10 minutes</strong>. Atlas will restart automatically after the image is written.
    </div>
    <div id="current" class="status">Reading current firmware...</div>
    <div class="drop"><label for="file">Firmware image</label><input id="file" type="file" accept=".bin,application/octet-stream"></div>
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

  button.addEventListener('click', () => {
    if (!file.files.length) return;

    button.disabled = true;
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

  server_.on("/theme.css", HTTP_GET, [this]() {
    server_.sendHeader("Cache-Control", "no-cache");
    server_.send_P(200, "text/css", TurnHubWeb::THEME_CSS);
  });

  server_.on("/portal", HTTP_GET, [this]() {
    server_.sendHeader("Cache-Control", "no-store");
    server_.send_P(200, "text/html", TurnHubWeb::PORTAL_HTML);
  });

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

      if (!TurnHubWebApi::requirePermission(server_,TurnHubAccounts::Admin) || !TurnHubWebApi::verifiedAtTable(server_) ||
          allowedCallback_ == nullptr || !allowedCallback_()) {
        denied_ = true;
        serialLog.println("ATLAS|OTA|DENIED");
        return;
      }

      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        fail(static_cast<uint8_t>(Update.getError()));
        Update.printError(Serial);
        return;
      }

      inProgress_ = true;
      serialLog.print("ATLAS|OTA|START|");
      serialLog.println(upload.filename);
      break;

    case UPLOAD_FILE_WRITE:
      if (!inProgress_ || denied_) {
        return;
      }

      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        fail(static_cast<uint8_t>(Update.getError()));
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

      if (!Update.end(true)) {
        fail(static_cast<uint8_t>(Update.getError()));
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
    char response[96];
    snprintf(
        response,
        sizeof(response),
        "{\"ok\":false,\"error\":\"Firmware update failed\",\"code\":%u}",
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
  return inProgress_;
}

}  // namespace TurnHub
