#include "home_tls.h"

// The HTTPS listener for the home network (home_tls.h). Firmware only: the
// host tests stub this file in test_globals.cpp.

#include "config.h"
#include "optional_preferences.h"
#include "serial_log.h"

#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#if defined(MBEDTLS_SSL_TICKET_C)
#include <mbedtls/ssl_ticket.h>
#endif
#if defined(MBEDTLS_SSL_CACHE_C)
#include <mbedtls/ssl_cache.h>
#endif

using TurnHub::serialLog;

namespace TurnHubAtlas {
namespace {

constexpr char TLS_NAMESPACE[] = "atlas-tls";
constexpr char TLS_KEY[] = "key";
constexpr char TLS_CERT[] = "crt";
constexpr size_t DER_MAX = 1024;

std::atomic<bool> started{false};
std::atomic<bool> running{false};
char pin[TurnHub::HOME_TLS_PIN_HEX_SIZE] = "";

mbedtls_entropy_context entropy;
mbedtls_ctr_drbg_context drbg;
mbedtls_pk_context key;
mbedtls_x509_crt cert;
mbedtls_ssl_config conf;
#if defined(MBEDTLS_SSL_TICKET_C)
mbedtls_ssl_ticket_context tickets;
#endif
#if defined(MBEDTLS_SSL_CACHE_C)
mbedtls_ssl_cache_context cache;
#endif

void logError(const char *step, int code) {
  serialLog.printf("ATLAS|HOME_TLS|ERROR|%s|-0x%04x\n", step, static_cast<unsigned>(-code));
}

bool loadBlob(TurnHub::OptionalPreferences &prefs, const char *name, uint8_t *out, size_t &length) {
  length = prefs.getBytesLength(name);
  if (length == 0 || length > DER_MAX) return false;
  return prefs.getBytes(name, out, length) == length;
}

// The stored key and certificate, if both parse and belong together.
bool loadIdentity() {
  static uint8_t keyDer[DER_MAX], certDer[DER_MAX];
  size_t keyLength = 0, certLength = 0;
  TurnHub::OptionalPreferences prefs;
  if (!prefs.begin(TLS_NAMESPACE, true)) return false;
  const bool found = loadBlob(prefs, TLS_KEY, keyDer, keyLength) && loadBlob(prefs, TLS_CERT, certDer, certLength);
  prefs.end();
  if (!found) return false;
  if (mbedtls_pk_parse_key(&key, keyDer, keyLength, nullptr, 0) != 0) return false;
  if (mbedtls_x509_crt_parse_der(&cert, certDer, certLength) != 0) return false;
  return mbedtls_pk_check_pair(&cert.pk, &key) == 0;
}

// A new P-256 key and a self-signed certificate for it, saved to NVS.
bool makeIdentity() {
  int rc = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
  if (rc == 0) rc = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), mbedtls_ctr_drbg_random, &drbg);
  if (rc != 0) { logError("KEYGEN", rc); return false; }

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  const String name = String("CN=atlas-") + mac + ",O=TurnHub";

  mbedtls_x509write_cert writer;
  mbedtls_x509write_crt_init(&writer);
  mbedtls_mpi serial;
  mbedtls_mpi_init(&serial);
  static uint8_t certBuf[DER_MAX], keyBuf[DER_MAX];
  int certLength = 0, keyLength = 0;
  mbedtls_x509write_crt_set_version(&writer, MBEDTLS_X509_CRT_VERSION_3);
  mbedtls_x509write_crt_set_md_alg(&writer, MBEDTLS_MD_SHA256);
  mbedtls_x509write_crt_set_subject_key(&writer, &key);
  mbedtls_x509write_crt_set_issuer_key(&writer, &key);
  rc = mbedtls_x509write_crt_set_subject_name(&writer, name.c_str());
  if (rc == 0) rc = mbedtls_x509write_crt_set_issuer_name(&writer, name.c_str());
  if (rc == 0) rc = mbedtls_mpi_fill_random(&serial, 16, mbedtls_ctr_drbg_random, &drbg);
  if (rc == 0) rc = mbedtls_x509write_crt_set_serial(&writer, &serial);
  // The app pins the key, not the dates, so the certificate never expires
  // in practice.
  if (rc == 0) rc = mbedtls_x509write_crt_set_validity(&writer, "20250101000000", "20991231235959");
  if (rc == 0) rc = mbedtls_x509write_crt_set_basic_constraints(&writer, 0, -1);
  if (rc == 0) {
    certLength = mbedtls_x509write_crt_der(&writer, certBuf, sizeof(certBuf), mbedtls_ctr_drbg_random, &drbg);
    if (certLength <= 0) rc = certLength ? certLength : -1;
  }
  mbedtls_mpi_free(&serial);
  mbedtls_x509write_crt_free(&writer);
  if (rc != 0) { logError("CERT", rc); return false; }
  keyLength = mbedtls_pk_write_key_der(&key, keyBuf, sizeof(keyBuf));
  if (keyLength <= 0) { logError("KEY_DER", keyLength); return false; }

  // Both writers fill their buffer from the end.
  const uint8_t *certDer = certBuf + sizeof(certBuf) - certLength;
  const uint8_t *keyDer = keyBuf + sizeof(keyBuf) - keyLength;
  rc = mbedtls_x509_crt_parse_der(&cert, certDer, certLength);
  if (rc != 0) { logError("CERT_PARSE", rc); return false; }

  TurnHub::OptionalPreferences prefs;
  bool saved = prefs.begin(TLS_NAMESPACE, false) &&
      prefs.putBytes(TLS_KEY, keyDer, keyLength) == static_cast<size_t>(keyLength) &&
      prefs.putBytes(TLS_CERT, certDer, certLength) == static_cast<size_t>(certLength);
  prefs.end();
  // Unsaved, the key still serves this boot, but the app's pin would break
  // at the next one; say so in the log.
  if (!saved) serialLog.println("ATLAS|HOME_TLS|ERROR|SAVE");
  serialLog.println("ATLAS|HOME_TLS|KEY_CREATED");
  return true;
}

bool computePin() {
  static uint8_t spki[256];
  const int length = mbedtls_pk_write_pubkey_der(&key, spki, sizeof(spki));
  if (length <= 0) return false;
  uint8_t digest[32];
  if (mbedtls_sha256_ret(spki + sizeof(spki) - length, length, digest, 0) != 0) return false;
  TurnHub::homeTlsPinHex(digest, pin);
  return true;
}

bool setUp() {
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&drbg);
  mbedtls_pk_init(&key);
  mbedtls_x509_crt_init(&cert);
  mbedtls_ssl_config_init(&conf);
  static const char personal[] = "turnhub-atlas-tls";
  int rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
      reinterpret_cast<const unsigned char *>(personal), sizeof(personal) - 1);
  if (rc != 0) { logError("SEED", rc); return false; }
  if (!loadIdentity()) {
    mbedtls_pk_free(&key);
    mbedtls_pk_init(&key);
    mbedtls_x509_crt_free(&cert);
    mbedtls_x509_crt_init(&cert);
    if (!makeIdentity()) return false;
  }
  if (!computePin()) { serialLog.println("ATLAS|HOME_TLS|ERROR|PIN"); return false; }

  rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
      MBEDTLS_SSL_PRESET_DEFAULT);
  if (rc != 0) { logError("CONFIG", rc); return false; }
  mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
  mbedtls_ssl_conf_min_version(&conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
  rc = mbedtls_ssl_conf_own_cert(&conf, &cert, &key);
  if (rc != 0) { logError("OWN_CERT", rc); return false; }
#if defined(MBEDTLS_SSL_TICKET_C) && defined(MBEDTLS_SSL_SESSION_TICKETS)
  mbedtls_ssl_ticket_init(&tickets);
  if (mbedtls_ssl_ticket_setup(&tickets, mbedtls_ctr_drbg_random, &drbg, MBEDTLS_CIPHER_AES_128_GCM, 86400) == 0) {
    mbedtls_ssl_conf_session_tickets_cb(&conf, mbedtls_ssl_ticket_write, mbedtls_ssl_ticket_parse, &tickets);
  }
#endif
#if defined(MBEDTLS_SSL_CACHE_C)
  mbedtls_ssl_cache_init(&cache);
  mbedtls_ssl_cache_set_max_entries(&cache, 8);
  mbedtls_ssl_conf_session_cache(&conf, &cache, mbedtls_ssl_cache_get, mbedtls_ssl_cache_set);
#endif
  return true;
}

int sendCallback(void *context, const unsigned char *data, size_t length) {
  const int fd = *static_cast<int *>(context);
  const int n = send(fd, data, length, 0);
  if (n >= 0) return n;
  return errno == EAGAIN || errno == EWOULDBLOCK ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
}

int recvCallback(void *context, unsigned char *data, size_t length) {
  const int fd = *static_cast<int *>(context);
  const int n = recv(fd, data, length, 0);
  if (n > 0) return n;
  if (n == 0) return MBEDTLS_ERR_NET_CONN_RESET;
  return errno == EAGAIN || errno == EWOULDBLOCK ? MBEDTLS_ERR_SSL_TIMEOUT : MBEDTLS_ERR_NET_RECV_FAILED;
}

void setTimeout(int fd, uint32_t ms) {
  timeval tv{};
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

bool sslWriteAll(mbedtls_ssl_context &ssl, const uint8_t *data, size_t length) {
  while (length > 0) {
    const int n = mbedtls_ssl_write(&ssl, data, length);
    if (n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_WANT_READ) continue;
    if (n <= 0) return false;
    data += n;
    length -= static_cast<size_t>(n);
  }
  return true;
}

bool sendAll(int fd, const uint8_t *data, size_t length) {
  while (length > 0) {
    const int n = send(fd, data, length, 0);
    if (n <= 0) return false;
    data += n;
    length -= static_cast<size_t>(n);
  }
  return true;
}

// Opens a plain connection to Atlas's own web server.
int connectBackend() {
  const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) return -1;
  setTimeout(fd, TurnHub::HOME_TLS_IDLE_MS);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(AtlasConfig::HTTP_PORT);
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, reinterpret_cast<sockaddr *>(&to), sizeof(to)) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

// One HTTPS connection: the handshake, then bytes both ways until either side
// closes or goes quiet.
void serve(int client) {
  static uint8_t buffer[2048];
  mbedtls_ssl_context ssl;
  mbedtls_ssl_init(&ssl);
  int backend = -1;
  int rc = mbedtls_ssl_setup(&ssl, &conf);
  if (rc != 0) { logError("SETUP", rc); goto done; }
  mbedtls_ssl_set_bio(&ssl, &client, sendCallback, recvCallback, nullptr);
  setTimeout(client, TurnHub::HOME_TLS_HANDSHAKE_MS);
  {
    const uint32_t begun = millis();
    while ((rc = mbedtls_ssl_handshake(&ssl)) != 0) {
      if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) break;
      if (millis() - begun > TurnHub::HOME_TLS_HANDSHAKE_MS) break;
    }
    if (rc != 0) {
      // A phone that doesn't trust the key yet hangs up here; not an Atlas fault.
      serialLog.printf("ATLAS|HOME_TLS|HANDSHAKE|-0x%04x\n", static_cast<unsigned>(-rc));
      goto done;
    }
  }
  setTimeout(client, TurnHub::HOME_TLS_IDLE_MS);
  backend = connectBackend();
  if (backend < 0) { serialLog.println("ATLAS|HOME_TLS|ERROR|BACKEND"); goto done; }
  for (;;) {
    if (mbedtls_ssl_get_bytes_avail(&ssl) == 0) {
      fd_set readable;
      FD_ZERO(&readable);
      FD_SET(client, &readable);
      FD_SET(backend, &readable);
      timeval tv{};
      tv.tv_sec = TurnHub::HOME_TLS_IDLE_MS / 1000;
      const int ready = select((client > backend ? client : backend) + 1, &readable, nullptr, nullptr, &tv);
      if (ready <= 0) break;
      if (FD_ISSET(backend, &readable)) {
        const int n = recv(backend, buffer, sizeof(buffer), 0);
        if (n <= 0 || !sslWriteAll(ssl, buffer, static_cast<size_t>(n))) break;
        continue;
      }
      if (!FD_ISSET(client, &readable)) continue;
    }
    const int n = mbedtls_ssl_read(&ssl, buffer, sizeof(buffer));
    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
    if (n <= 0 || !sendAll(backend, buffer, static_cast<size_t>(n))) break;
  }
  mbedtls_ssl_close_notify(&ssl);
done:
  if (backend >= 0) close(backend);
  mbedtls_ssl_free(&ssl);
  close(client);
}

void serverTask(void *) {
  if (!setUp()) {
    serialLog.println("ATLAS|HOME_TLS|OFF");
    started = false;
    vTaskDelete(nullptr);
    return;
  }
  const int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in at{};
  at.sin_family = AF_INET;
  at.sin_port = htons(TurnHub::HOME_TLS_PORT);
  at.sin_addr.s_addr = htonl(INADDR_ANY);
  const int yes = 1;
  if (listener >= 0) setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  if (listener < 0 || bind(listener, reinterpret_cast<sockaddr *>(&at), sizeof(at)) != 0 ||
      listen(listener, TurnHub::HOME_TLS_BACKLOG) != 0) {
    serialLog.println("ATLAS|HOME_TLS|ERROR|LISTEN");
    if (listener >= 0) close(listener);
    started = false;
    vTaskDelete(nullptr);
    return;
  }
  running = true;
  serialLog.printf("ATLAS|HOME_TLS|READY|port=%u|pin=%s\n", static_cast<unsigned>(TurnHub::HOME_TLS_PORT), pin);
  for (;;) {
    sockaddr_in from{};
    socklen_t fromLength = sizeof(from);
    const int client = accept(listener, reinterpret_cast<sockaddr *>(&from), &fromLength);
    if (client < 0) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    serve(client);
  }
}

}  // namespace

void startHomeTls() {
  bool expected = false;
  if (!started.compare_exchange_strong(expected, true)) return;
  // Core 0, beside the Wi-Fi stack, so a handshake's key maths never holds up
  // loop() and the Sigils on core 1.
  if (xTaskCreatePinnedToCore(serverTask, "home-tls", 12288, nullptr, 1, nullptr, 0) != pdPASS) {
    serialLog.println("ATLAS|HOME_TLS|ERROR|TASK");
    started = false;
  }
}

bool homeTlsRunning() { return running; }
const char *homeTlsPin() { return running ? pin : ""; }

}  // namespace TurnHubAtlas
