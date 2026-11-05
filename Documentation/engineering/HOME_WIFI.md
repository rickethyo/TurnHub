# Home Wi-Fi and the Table Channel

Atlas uses home Wi-Fi for phones and tablets. Its hotspot is available only
during [Atlas setup](FIRST_RUN_SETUP.md), with a join QR on Welcome.
Completed devices never start a fallback hotspot.

## What it does (*Planned* for hardware acceptance; host-tested)

- **Off until a network is saved.** Saving a home network (app or portal)
  turns home Wi-Fi on; the Atlas screen's row and the app's switch turn it off
  and on again.
- **Joins and stays.** Atlas joins `HOME_WIFI_START_JOIN_MS` (10 s) after
  start-up, or at once when a network is saved or the setting turned on, and
  stays connected. A failed join retries after `HOME_WIFI_REJOIN_MIN_MS`
  (30 s), doubling to `HOME_WIFI_REJOIN_MAX_MS` (5 min); a dropped connection
  retries after 10 s (`HomeWifiRejoin`). Each attempt gives up after
  `HOME_WIFI_JOIN_TIMEOUT_MS` (15 s).
- **Setup-only hotspot.** Only Welcome starts TurnHub-Atlas. Finishing setup
  requires connected home Wi-Fi and a ready HTTPS key. Finished and Complete
  use station mode even when the home network is missing or unreachable.
  Factory reset is the only way back to hotspot provisioning.
- **One radio, one channel: the router's.** The hotspot, ESP-NOW and the home
  network share the ESP32's radio. While Atlas is on the home network they all
  run on the router's channel; the saved **table channel**
  (`TurnHubProtocol::DEFAULT_TABLE_CHANNEL` 6, or 1-11) applies only until
  Atlas joins. A router that moves channel moves the table too, also mid-match
  (owner decision 2026-11-03): Sigils search for it, and phones that drop
  carry on through offline play. Atlas's ESP-NOW peers use channel 0 (the
  radio's current one) so they stay valid.
- **No channel hopping during a match.** A join from the lobby may scan every
  channel (the last known one first). During a match Atlas first scans only
  its current channel (`HomeWifiJoinScan::CurrentChannelOnly`) and joins only
  if the router is there, so the Sigils' channel never changes because of a
  search. A failed join puts the radio back on the channel it was on; turning
  home Wi-Fi off or forgetting the network returns it to the table channel.
- **The update check.** While connected, Atlas fetches the same release feed
  the app reads (`turnhub-firmware.json`, [Firmware Updates](FIRMWARE_UPDATES.md))
  `HOME_WIFI_FIRST_CHECK_MS` (2 min) after start-up, then every
  `HOME_WIFI_CHECK_INTERVAL_MS` (6 h; 30 min after a failure) or on **Check
  now**, with no match running and no firmware being written. It feeds the
  versions to `noteLatestFirmware`, exactly like an app's
  `POST /api/updates/latest`. Nothing installs by itself. TLS is not
  certificate-checked: the feed is unsigned anyway and can only light the
  update notice, which the public POST can do too (CA checking is staged).
- **HTTPS** (`home_tls.h`, `home_tls_server.cpp`; slice 2). On first joining
  the home network Atlas starts an HTTPS listener on port 443
  (`HOME_TLS_PORT`). The first time, it makes an ECDSA P-256 key and a
  self-signed certificate (CN `atlas-<mac>`, valid to 2099) and keeps both in
  NVS `atlas-tls` (factory reset erases them; a new key means phones pin
  again). There is no CA: the app is to pin the SHA-256 of the key's
  SubjectPublicKeyInfo (`/api/network` `home.tls.pin`, the serial line
  `ATLAS|HOME_TLS|READY|port=443|pin=...`, and mDNS `fp`) and accept that key
  on any address. TLS 1.2 (mbedTLS 2.28), with session tickets and a session
  cache (both in Arduino-ESP32 2.0.17's mbedTLS). That build's mbedTLS allocates from internal RAM by default (about 35 KB per connection with its 16 KB record buffers), which left the release-feed check about 28 KB and failed its handshake, so `startHomeTls()` points mbedTLS at PSRAM first, internal RAM only as a fallback, and logs `HOME_TLS|PSRAM` and `HOME_TLS|HEAP` with the internal free and largest block (*Needs verification* on the board). Each decrypted connection is piped over
  loopback to Atlas's own web server on port 80, so routes, sessions and
  permissions are the same code as on the hotspot; that server closes after
  each response, so resumption keeps repeat requests cheap. One connection is
  served at a time (four queue), in a task on core 0, apart from loop() and
  the Sigils. A connection quiet for `HOME_TLS_IDLE_MS` (10 s) closes; a
  handshake gets `HOME_TLS_HANDSHAKE_MS` (8 s). *Needs verification* on the
  board: handshake time, heap, and Sigil latency during handshakes.
- **Found by name.** On joining, Atlas advertises mDNS `atlas-<mac>.local`
  with service `_turnhub-atlas._tcp` on port 443 (TXT `id=THA-<mac>`,
  `api=1`, `tls=1`, and `fp=<pin>` once the key exists).
- **The app on the home network** (`AtlasHomeLink`, `PinnedTls`; slice 3).
  `/api/v1/info` carries `home` {`address`, `tlsPort`, `tlsPin`} while Atlas
  is connected with its key ready. The app remembers it (with the `atlasId`)
  only when it read it over the hotspot (`http://192.168.4.1`), the network it
  already trusts; never from the home network itself. On start-up, Search
  again and Connect it first tries `https://<saved address>`, trusting only a
  server key whose SPKI SHA-256 is the pin (a trust manager and a hostname
  check by key, since the address changes) and only an `atlasId` that matches.
  If that address doesn't answer, it looks for `_turnhub-atlas._tcp` by mDNS
  (Android NSD, 4 s) with a matching `fp`, saves the new address and tries it.
  A saved home Atlas never falls back to setup Wi-Fi, even if old hotspot
  credentials are saved. A mismatched key fails the handshake; the app never
  re-pins from the home network. Setup Wi-Fi is entered explicitly for a new Atlas.
  Android's cleartext rule stays limited to 192.168.4.1.
- **First-time home pairing.** On the current home Wi-Fi, **Find Atlas on
  Wi-Fi**, Connect, Search again and new-table setup also discover an Atlas
  when no trusted key is saved. NSD requires a valid ID, TLS flag and full key
  fingerprint; pinned HTTPS must answer with that same ID. Discovery alone
  saves no trust. The app asks the user to compare its 12-hex-digit code with
  **App link** in the physical Atlas Settings before saving the full key and
  connecting. Confirmation rechecks the pinned endpoint. Cancel saves nothing
  and does not join a hotspot. An existing saved key is never replaced through
  discovery. Background discovery only reconnects previously trusted tables.
- **Sigils find Atlas on any channel** (`Sigil/include/channel_search.h`).
  A Sigil that has lost Atlas (`LINK_TIMEOUT_MS`), or is pairing with no
  answer, steps through channels 1-11, `CHANNEL_DWELL_MS` (300 ms) on each,
  sending a SecureHello (or its pair request) on arrival. The first frame from
  Atlas ends the search; the Sigil saves that channel (NVS `radio/channel`)
  and starts there next time, and its OTA updater joins the hotspot there.
  A full sweep takes about 3.3 s. ESP-NOW peers use channel 0 (the radio's
  current one) on the Sigil.

## Settings and API

NVS namespace `atlas-net` (with the hotspot password), erased by factory reset:
`home-on` ("1"/"0"), `home-ssid`, `home-pass`, and `chan` (absent = 6).

| Where | What |
|---|---|
| Atlas screen, Settings | **Home Wi-Fi: off / on / connected** row (a tap turns it off or on); with no network saved it says to add one in the app |
| App, Atlas settings | **Home Wi-Fi** card: switch, status with Atlas's home address, network name and password, Check now, Forget; **Table channel (advanced)** with Save & restart |
| Portal, Device Settings | The same card |
| `GET /api/network` (Admin) | adds `channel` (in use), `savedChannel`, and `home` {`enabled`, `configured`, `ssid`, `result` (`none`, `checking`, `checked`, `noNetwork`, `feedFailed`), `lastCheckAgeMs`, `connected`, `address` (Atlas's home IP while connected, else null), `channel` (the home network's, once joined), `tls` {`running`, `port`, `pin` (hex SHA-256 of the key, null before it exists)}}; never a password |
| `POST /api/network/home` (Admin, table code if on) | `ssid` + `password` (blank keeps the saved one for the same network; none for an open network; saving turns home Wi-Fi on), `enabled=0/1`, or `forget=1` |
| `POST /api/network/home/check` (Admin) | Check now (409 while off); joins first if Atlas is not connected |
| `POST /api/network/channel` (Admin, table code if on) | `channel` 1-11; saves and restarts Atlas |

The network name and password are typed in the app or portal, not on the
Atlas screen. Both firmware sides log `ATLAS|HOME_WIFI|...` and
`SIGIL|CHANNEL|<n>`; the home password never reaches a log.

## Parity

The app already reads the release feed on the phone's own network; this gives
Atlas the same ability. The settings live in the app's Atlas settings and the
portal alike (Invariant 12). Sync over the home network is staged.
