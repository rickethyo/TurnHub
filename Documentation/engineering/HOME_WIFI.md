# Home Wi-Fi and the Table Channel

Owner request, 2026-10-30: an optional way for Atlas to join a home network to
check for updates (and, later, sync), **off by default**, plus an advanced
setting for the radio channel so Atlas can stay on that network.

## What it does (*Planned* for hardware acceptance; host-tested)

- **Atlas keeps its own network.** The `TurnHub-Atlas` hotspot stays up the
  whole time (`WIFI_AP_STA`), so phones and tablets join it for games as
  before. The home network is an extra connection, never a replacement.
- **One radio, one channel.** The hotspot, ESP-NOW and the home network share
  the ESP32's radio and so one channel: the **table channel**
  (`TurnHubProtocol::DEFAULT_TABLE_CHANNEL` 6; the owner may choose 1-11, which
  every region allows).
- **Check-ins.** With home Wi-Fi on and a network saved, Atlas joins it
  `HOME_WIFI_FIRST_CHECK_MS` (2 min) after start-up and then every
  `HOME_WIFI_CHECK_INTERVAL_MS` (6 h; 30 min after a failure), or at once on
  **Check now** or when the setting is turned on. Only when no table has a
  match running and no firmware is being written. It gives up after
  `HOME_WIFI_JOIN_TIMEOUT_MS` (15 s), or if a match starts while joining.
- **The check.** Atlas fetches the same release feed the app reads
  (`turnhub-firmware.json`, [Firmware Updates](FIRMWARE_UPDATES.md)) and feeds
  the versions to `noteLatestFirmware`, exactly like an app's
  `POST /api/updates/latest`. Nothing installs by itself; the app or portal
  still installs signed packages. TLS is not certificate-checked: the feed is
  unsigned anyway and can only light the update notice, which the public POST
  can do too (CA checking is staged).
- **Leave or stay.** A router on another channel would pull the hotspot and
  ESP-NOW with it, so Atlas then leaves the home network and retunes to the
  table channel; a check-in can briefly drop a phone or Sigil in the lobby,
  and each reconnects by itself. If the home network is on the table channel,
  Atlas stays connected. It leaves (and goes back to check-ins) if the router
  drops it or changes channel.
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
| Atlas screen, Settings | **Home Wi-Fi: on/off** row; with no network saved it says to add one in the app |
| App, Atlas settings | **Home Wi-Fi** card: switch, network name and password, Check now, Forget; **Table channel (advanced)** with Save & restart |
| Portal, Device Settings | The same card |
| `GET /api/network` (Admin) | adds `channel` (in use), `savedChannel`, and `home` {`enabled`, `configured`, `ssid`, `result` (`none`, `checking`, `checked`, `noNetwork`, `feedFailed`), `lastCheckAgeMs`, `connected`, `channel` (the home network's, once joined)}; never a password |
| `POST /api/network/home` (Admin, table code if on) | `ssid` + `password` (blank keeps the saved one for the same network; none for an open network), `enabled=0/1`, or `forget=1` |
| `POST /api/network/home/check` (Admin) | Check now (409 while off) |
| `POST /api/network/channel` (Admin, table code if on) | `channel` 1-11; saves and restarts Atlas |

The network name and password are typed in the app or portal, not on the
Atlas screen. Both firmware sides log `ATLAS|HOME_WIFI|...` and
`SIGIL|CHANNEL|<n>`; the home password never reaches a log.

## Parity

The app already reads the release feed on the phone's own network; this gives
Atlas the same ability. The settings live in the app's Atlas settings and the
portal alike (Invariant 12). Sync over the home network is staged.
