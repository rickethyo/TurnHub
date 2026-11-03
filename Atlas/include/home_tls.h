#pragma once

// HTTPS for phones on the home network (Ricky, 2026-11-03; HOME_WIFI.md,
// "Home Wi-Fi as primary", slice 2). Atlas makes its own ECDSA P-256 key and a
// self-signed certificate once (NVS `atlas-tls`, erased by factory reset) and
// serves HTTPS on HOME_TLS_PORT. There is no CA: the app pins the SHA-256 of
// the key (its SubjectPublicKeyInfo), learned over the hotspot or the Atlas
// screen's QR, and accepts that key on any address.
//
// The TLS side terminates in front of the existing HTTP routes: each HTTPS
// connection is piped to Atlas's own web server over loopback, so every route,
// session and permission check is the same code as on the hotspot. Arduino's
// WebServer closes the connection after each response, so TLS session
// resumption (tickets and a session cache) keeps later requests cheap.
//
// home_tls_server.cpp is firmware only (mbedTLS and sockets); the host tests
// stub these functions in test_globals.cpp.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace TurnHub {

constexpr uint16_t HOME_TLS_PORT = 443;
// A connection with nothing to send in either direction for this long closes.
constexpr uint32_t HOME_TLS_IDLE_MS = 10000;
// How long a client may take to finish the handshake.
constexpr uint32_t HOME_TLS_HANDSHAKE_MS = 8000;
// Pending connections the listener queues while one is being served.
constexpr int HOME_TLS_BACKLOG = 4;
// 64 hex digits and the terminator.
constexpr size_t HOME_TLS_PIN_HEX_SIZE = 65;

// Lower-case hex of a SHA-256 digest, as the app and mDNS show it.
inline void homeTlsPinHex(const uint8_t digest[32], char out[HOME_TLS_PIN_HEX_SIZE]) {
  for (size_t i = 0; i < 32; ++i) snprintf(out + i * 2, 3, "%02x", digest[i]);
  out[64] = '\0';
}

}  // namespace TurnHub

namespace TurnHubAtlas {

// Starts the HTTPS listener (once; later calls do nothing). Called when Atlas
// first joins the home network, so a table without home Wi-Fi spends no RAM
// on TLS. Making the key the first time takes a moment and happens in the
// server's own task.
void startHomeTls();
// True once the key is loaded and the listener is accepting.
bool homeTlsRunning();
// The pinned key's SHA-256 in lower-case hex, or "" before it exists.
const char *homeTlsPin();

}  // namespace TurnHubAtlas
