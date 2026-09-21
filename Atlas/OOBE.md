# Atlas first-run setup and provisioning

This document defines the intended out-of-box experience (OOBE) for TurnHub Atlas. The goal is to make a new Atlas usable without source-code knowledge, serial access, or a permanent dedicated captive portal, while preserving TurnHub's local-first and offline-capable design.

## Design goals

- No cloud account is required.
- Atlas remains fully usable without internet access.
- Player identity, system roles, and seat assignment use one authentication/authorization model.
- A first-time owner can provision Atlas from a phone or computer.
- Atlas may optionally join the owner's normal Wi-Fi network.
- Atlas must always retain a recoverable local access path.
- ESP-NOW Sigil communication must continue to work regardless of the selected Wi-Fi mode.
- Factory reset and setup-mode entry must require a deliberate physical action.

## Important ESP32 radio constraint

Atlas uses one 2.4 GHz Wi-Fi radio for both normal Wi-Fi and ESP-NOW.

When Atlas is connected to an infrastructure Wi-Fi access point as a station, ESP-NOW must use that same Wi-Fi channel. In AP+STA coexistence, the station connection's home channel takes priority and the SoftAP follows it.

The current prototype assumes channel 6 on both Atlas and Sigils. Therefore, home-Wi-Fi support cannot simply connect Atlas to an arbitrary router while leaving Sigils fixed on channel 6.

The provisioning design must include dynamic Sigil channel discovery/rebinding before home-Wi-Fi mode becomes the default.

## Proposed first-boot flow

### 1. Detect unprovisioned Atlas

Atlas stores a small provisioning record in NVS. On boot, if no valid provisioning record exists, Atlas enters Setup Mode instead of the normal table runtime.

Suggested state:

```text
ProvisioningState
- schemaVersion
- provisioned
- atlasName
- preferredNetworkMode
- homeSsid
- encrypted/stored home credential
- setupCompletedAt / generation counter
- ownerProfileId
```

The owner profile is a normal durable TurnHub profile with elevated system permissions, not a separate login subsystem.

### 2. Start a temporary setup network

An unprovisioned Atlas advertises a temporary setup SSID such as:

```text
TurnHub-Setup-A1B2
```

The setup network is only a provisioning transport. It is not intended to remain the normal user experience.

For prototypes, setup can be reached by browsing directly to `192.168.4.1` rather than depending on operating-system captive-portal detection. Production hardware can later include a QR code containing the setup SSID and device-specific setup credential.

Setup Mode should time out or require the physical Atlas button to re-enter after provisioning.

### 3. Setup wizard

The browser wizard should walk through:

1. Name this Atlas.
2. Create the first owner/admin profile.
3. Set the profile PIN.
4. Choose network mode.
5. If joining home Wi-Fi, scan/select SSID and enter credentials.
6. Validate the connection before committing it as the preferred network.
7. Explain the recovery path before finishing.

The first owner profile should receive an Admin role/capability bundle. Developer access should be a separate role/capability assignment within the same identity system, not a separate authentication database.

## Network modes

Atlas should support at least two explicit modes rather than assuming one network topology.

### Standalone mode

Atlas hosts its own persistent `TurnHub-Atlas` network and serves the portal locally.

Benefits:
- always works offline
- predictable radio channel
- simplest ESP-NOW behavior
- useful at game stores, conventions, travel, and networks that block peer-to-peer clients

### Home/LAN mode

Atlas joins an existing 2.4 GHz Wi-Fi network and exposes the portal on that LAN.

Benefits:
- phones do not need to leave normal home Wi-Fi
- Atlas can be reached like another local appliance
- better home installation experience

Requirements before this mode is considered production-ready:
- dynamic ESP-NOW/Sigil channel coordination
- connection retry and failure detection
- local hostname discovery where practical, e.g. `turnhub.local`
- deterministic fallback when the configured network is unavailable

## Recommended runtime behavior

A provisioned Atlas should try its preferred mode first.

For Home/LAN mode:

1. Start Wi-Fi station connection using stored credentials.
2. Determine the infrastructure AP's actual channel.
3. Bring ESP-NOW onto that same channel.
4. Ensure Sigils can discover and migrate to the Atlas channel.
5. Start the normal portal and table services.

If home Wi-Fi cannot be reached within a bounded startup window, Atlas should fall back to a recoverable local network rather than becoming inaccessible.

Suggested fallback SSID:

```text
TurnHub-Recovery-A1B2
```

Recovery mode should clearly indicate that the configured home network was unavailable and offer Retry, Change network, and Standalone mode.

## Sigil channel discovery direction

The current hard-coded channel 6 behavior must be replaced before automatic home-network joining is enabled.

A reasonable future discovery sequence is:

1. Sigil boots with its last-known Atlas channel.
2. It attempts normal Hello traffic for a short bounded period.
3. If Atlas is not found, the Sigil scans supported 2.4 GHz channels using short discovery windows.
4. Atlas answers discovery/Hello on its current channel.
5. Sigil stores the successful channel as its new last-known channel.
6. Normal ESP-NOW traffic resumes.

This keeps normal reconnect fast while still allowing Atlas to follow a household router whose channel changes.

The exact scan timing must be designed so it does not make Sigils feel unresponsive or create excessive radio/power overhead.

## Identity and permissions in OOBE

The OOBE should create the first durable profile and make it the initial owner/admin principal.

Long-term browser authentication should look like:

```text
WebSession
- token
- profileId
- capabilities/role
- optional current seat assignment
- lastSeen
```

Profiles may exist without participating in the current game. A logged-in profile with no seat can spectate according to its permissions.

Suggested capability groups include:

```text
PROFILE_EDIT_SELF
PROFILE_VIEW_PUBLIC_STATS
PROFILE_VIEW_ALL_STATS
SEAT_ASSIGN_SELF
SEAT_ASSIGN_ANY
GAME_ADMIN
DEVICE_ADMIN
SYSTEM_CONFIG
FIRMWARE_UPDATE
DEV_DIAGNOSTICS
DEV_DATA_RESET
```

Roles such as Player, Game Master, Admin, and Developer should be bundles of these capabilities.

## Spectator behavior

A logged-in profile does not need to join the current game.

Unseated authenticated users may receive the read-only live table view, including information intentionally exposed as game state, such as:

- game elapsed time
- current player
- current turn elapsed time
- life totals
- Commander damage
- pause/game-over state
- other live table information allowed by policy

Historical profile statistics are separate from current game state. Each player should have a statistics visibility preference such as Private or Local Public.

## Recovery and reset

Atlas must never depend on a remembered router credential as its only access path.

Recommended recovery controls:

- Normal boot: use configured mode.
- Deliberate physical-button boot gesture: enter Recovery/Setup Mode.
- Factory-reset gesture: erase provisioning/network/profile configuration only after an additional confirmation step.
- Firmware recovery should remain independent from ordinary player authentication.

## Implementation sequence

1. Introduce a dedicated provisioning/network configuration service instead of keeping Wi-Fi preferences inside `main.cpp` and `web_api.cpp`.
2. Add an explicit provisioned/unprovisioned boot state.
3. Build the first-run setup wizard and owner-profile creation.
4. Refactor browser sessions from seat identity to profile identity and capability checks.
5. Add Standalone/Home network selection and connection validation.
6. Implement Sigil dynamic channel discovery.
7. Enable Home/LAN mode as a supported runtime mode only after ESP-NOW channel migration is verified on real hardware.
8. Add recovery-mode UX and factory-reset behavior.

This sequence intentionally keeps the current stable standalone ESP-NOW behavior intact while the OOBE is introduced incrementally.