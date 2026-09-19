#include "ota_manager.h"

#include <Update.h>
#include <esp_ota_ops.h>

#include "web_api.h"
#include "web_pages.h"

namespace TurnHub {

namespace {

void printPartitionDiagnostic(
    const char *role,
    const esp_partition_t *partition) {
  Serial.print("ATLAS|PARTITION|");
  Serial.print(role);
  Serial.print('|');

  if (partition == nullptr) {
    Serial.println("NONE");
    return;
  }

  Serial.print(partition->label);
  Serial.print("|ADDRESS|0x");
  Serial.print(static_cast<unsigned long>(partition->address), HEX);
  Serial.print("|SIZE|");
  Serial.println(static_cast<unsigned long>(partition->size));
}

void printBootPartitionDiagnostics() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);

  printPartitionDiagnostic("RUNNING", running);
  printPartitionDiagnostic("BOOT", boot);
  printPartitionDiagnostic("NEXT_OTA", next);

  if (running != nullptr && boot != nullptr) {
    Serial.print("ATLAS|PARTITION|BOOT_MATCHES_RUNNING|");
    Serial.println(running->address == boot->address ? "YES" : "NO");
  }
}

const char UPDATE_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="theme-color" content="#111318">
  <title>TurnHub Atlas Update</title>
  <style>
    :root { color-scheme: dark; }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      display: grid;
      place-items: center;
      padding: 18px;
      font-family: system-ui, sans-serif;
      background: #0b0d11;
      color: #f3f5f7;
    }
    main {
      width: min(620px, 100%);
      padding: 28px;
      border: 1px solid #2b3240;
      border-radius: 18px;
      background: #141820;
    }
    h1 { margin: 0 0 8px; }
    p { color: #aeb7c6; line-height: 1.5; }
    .notice {
      padding: 14px;
      border: 1px solid #384153;
      border-radius: 12px;
      background: #1a202b;
      margin: 18px 0;
    }
    .status {
      padding: 12px;
      border: 1px solid #2b3240;
      border-radius: 10px;
      margin: 12px 0 18px;
      font-family: ui-monospace, monospace;
      color: #cbd3df;
    }
    input[type=file] { width: 100%; margin: 12px 0; }
    button, a.button {
      display: inline-block;
      border: 0;
      border-radius: 10px;
      padding: 11px 16px;
      background: #e7ebf2;
      color: #111318;
      font-weight: 800;
      text-decoration: none;
      cursor: pointer;
    }
    button:disabled { opacity: .45; cursor: not-allowed; }
    progress { width: 100%; height: 16px; margin-top: 16px; }
    #message { min-height: 48px; font-weight: 700; }
    .ok { color: #62d58a !important; }
    .warn { color: #f2c66d !important; }
    .error { color: #ff7d7d !important; }
    .back { margin-top: 18px; display: inline-block; color: #9fc0ff; }
  </style>
</head>
<body>
  <main>
    <h1>Atlas Firmware Update</h1>
    <p>Upload the Atlas <code>firmware.bin</code> produced by PlatformIO.</p>
    <div class="notice">
      For safety, start updates only from Lobby or Game Over and <strong>hold the physical Atlas master button while clicking Upload</strong>. Atlas will restart automatically after the image is written.
    </div>
    <div id="current" class="status">Reading current firmware...</div>
    <input id="file" type="file" accept=".bin,application/octet-stream">
    <button id="upload" disabled>Upload &amp; Verify</button>
    <progress id="progress" max="100" value="0"></progress>
    <p id="message"></p>
    <a class="back" href="/portal">← Back to TurnHub</a>
  </main>
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
    setMessage('Uploading... keep holding the Atlas master button until the upload begins.');

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

  server_.on("/portal", HTTP_GET, [this]() {
    server_.sendHeader("Cache-Control", "no-store");
    server_.send_P(200, "text/html", TurnHubWeb::PORTAL_HTML);
  });

  server_.on("/dev", HTTP_GET, [this]() {
    server_.sendHeader("Cache-Control", "no-store");
    server_.send_P(200, "text/html", TurnHubWeb::DEV_HTML);
  });

  server_.on("/update", HTTP_GET, [this]() {
    server_.sendHeader("Cache-Control", "no-store");
    server_.send_P(200, "text/html", UPDATE_HTML);
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
  Serial.print("ATLAS|OTA|ERROR|");
  Serial.println(errorCode_);
}

void OtaManager::handleUpload() {
  HTTPUpload &upload = server_.upload();

  switch (upload.status) {
    case UPLOAD_FILE_START:
      resetAttempt();

      if (allowedCallback_ == nullptr || !allowedCallback_()) {
        denied_ = true;
        Serial.println("ATLAS|OTA|DENIED");
        return;
      }

      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        fail(static_cast<uint8_t>(Update.getError()));
        Update.printError(Serial);
        return;
      }

      inProgress_ = true;
      Serial.print("ATLAS|OTA|START|");
      Serial.println(upload.filename);
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
      Serial.print("ATLAS|OTA|IMAGE_WRITTEN|");
      Serial.println(bytesWritten_);
      break;

    case UPLOAD_FILE_ABORTED:
      if (inProgress_) {
        Update.abort();
      }
      inProgress_ = false;
      success_ = false;
      Serial.println("ATLAS|OTA|ABORTED");
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
        "{\"ok\":false,\"error\":\"Update not armed. Return to Lobby or Game Over and hold the Atlas master button while starting the upload.\"}");
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

  Serial.println("ATLAS|OTA|RESTART_FOR_VERIFICATION");
  delay(50);
  ESP.restart();
}

bool OtaManager::inProgress() const {
  return inProgress_;
}

}  // namespace TurnHub
