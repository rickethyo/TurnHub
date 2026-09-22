# TurnHub Protocol and Pairing

Current pairing update (2026-09-22): physical buttons are owner-verified and manual
30-second pairing with persistent MAC associations is now implemented. The boot
pairing fallback and visual mock are superseded. Radio bench acceptance and
forget-device management remain pending. See [Manual Pairing](MANUAL_PAIRING.md).


This document separates three things that are easy to confuse during rapid prototyping:

1. The **logical protocol** TurnHub wants controllers to speak.
2. The **transport** used to carry those messages.
3. The **pairing/trust relationship** that decides which devices belong to an Atlas.

The logical protocol should remain as independent from the transport as practical.

---

## Generation 1 wired serial protocol

**Status:** Historical and verified from surviving Arduino firmware

The Raspberry Pi and wired modules exchanged human-readable serial messages.

### Module to host

Examples:

- `READY|1`
- `PASS|1`
- `ACTION|1|DOWN`
- `ACTION|1|UP`
- `ACTION|1|SHORT`
- `ACTION|1|LONG`
- `ACTION|1|WIN`

### Host to module

Examples:

- `BLUE|1|<0-255>`
- `RED|1|<0|1>`
- `GREEN|1|<0|1>`
- `OFF`

This protocol was simple, debuggable, and appropriate for a bench prototype, but it assumed wired modules and positional numeric IDs.

---

## ESP32 migration packet protocol

**Status:** Implemented experimental transport/protocol on the current migration branch

The migration introduced compact binary packets and a shared `protocol.h` used by Atlas and Sigil firmware.

Current/experimental packet concepts include:

- Hello.
- Acknowledgement.
- Pass.
- Action down/up/short/long/win.
- LED commands.
- Buzzer commands.
- Display/profile synchronization.
- Firmware/capability information.

The early transitional protocol used versioned packed packets and a maximum of eight Sigils.

### Important historical note

The current branch contains ESP-NOW implementation code, including broadcast discovery and peer registration. This represents a real development stage and should remain documented.

It is **not the final product decision**. Product direction has moved toward explicit pairing and away from a design where nearby devices can simply discover one another and become associated by proximity.

---

## Transport independence

A message such as `PASS_REQUEST` should mean the same thing whether it arrives through:

- Historical USB serial.
- A future BLE link.
- A local Wi-Fi transport.
- A browser HTTP/WebSocket API.
- A simulated controller in a test harness.

Transport-specific code should convert the incoming representation into a semantic event for Atlas.

This prevents a future transport change from requiring a game-engine rewrite.

---

## Proposed logical message families

**Status:** Working design, not frozen protocol numbers

### Device lifecycle

- `DEVICE_HELLO`
- `DEVICE_STATUS`
- `DEVICE_CAPABILITIES`
- `DEVICE_DISCONNECT`

### Pairing

- `PAIR_REQUEST`
- `PAIR_CHALLENGE`
- `PAIR_CONFIRM`
- `PAIR_ACCEPT`
- `PAIR_REJECT`
- `UNPAIR_REQUEST`

### Player/controller assignment

- `PLAYER_ASSIGN`
- `PLAYER_UNASSIGN`
- `PROFILE_SYNC`

### Game input

- `PASS_REQUEST`
- `ACTION_DOWN`
- `ACTION_UP`
- `ACTION_SHORT`
- `ACTION_LONG`
- `CLAIM_WIN`
- `CONCEDE`
- `NUDGE`

### Atlas state/output

- `STATE_SYNC`
- `TURN_CHANGED`
- `GAME_STATE_CHANGED`
- `PLAYER_STATE_CHANGED`
- `DISPLAY_STATE`
- `FEEDBACK_AUDIO`
- `FEEDBACK_LED`
- `FEEDBACK_HAPTIC`

### Firmware/update

- `FIRMWARE_INFO`
- `UPDATE_AVAILABLE`
- `OTA_BEGIN`
- `OTA_DATA` or transport-specific payload path
- `OTA_PROGRESS`
- `OTA_RESULT`

The exact message names, numeric IDs, packet encoding, and transport mapping remain to be finalized.

---

## Device identity

A production Sigil should have a stable device identity that is not merely "the third radio Atlas heard today."

Desired identity properties:

- Stable across reboot.
- Distinct from the temporary player/seat assignment.
- Available before joining a game.
- Able to report hardware revision.
- Able to report firmware/protocol version.
- Able to report capabilities.

A useful conceptual device record is:

```text
device_id
hardware_revision
firmware_version
protocol_version
capabilities
paired_atlas_id
friendly_name (optional)
```

Exact identity generation/storage is still to be chosen.

---

## Explicit pairing direction

**Status:** Planned

First-time association should require deliberate user action rather than passive proximity discovery.

Conceptual flow:

```text
Unpaired Sigil
    -> user presses/holds Pair
    -> Sigil enters limited pairing window

Atlas
    -> user enables Add/Pair Sigil mode
    -> sees pairing request
    -> validates/accepts device

Both devices
    -> store relationship
    -> exchange versions/capabilities
    -> transition to paired-ready state
```

Normal startup after pairing:

```text
Sigil boots
    -> loads paired Atlas identity
    -> discovers/reaches that Atlas using chosen transport
    -> validates relationship
    -> exchanges version/capability status
    -> receives state assignment
    -> ready
```

### Why this matters

Explicit pairing prevents a table in a game store from accidentally adopting a Sigil belonging to the next table simply because it is nearby.

It also gives us a natural place for:

- Device naming.
- Firmware compatibility checks.
- Hardware capability negotiation.
- Re-pair/reset workflows.
- Future security/authentication.

---

## Capabilities

Atlas should react to declared capabilities rather than scattering firmware-version checks across the codebase.

Potential capability flags:

- Display present.
- E-ink display profile.
- Buzzer.
- Haptic motor.
- RGB or discrete status LEDs.
- Battery reporting.
- Auxiliary action button.
- Pair button.
- Local storage.
- OTA support.

A future Sigil revision should be able to add a capability without forcing every older Sigil to emulate hardware it does not have.

---

## Synchronization model

Atlas is authoritative. A controller should be able to recover by asking for a state snapshot after reconnecting rather than reconstructing the game from old local assumptions.

Desired pattern:

```text
controller reconnects
    -> HELLO / identity
    -> Atlas validates pairing
    -> version/capability exchange
    -> STATE_SYNC
    -> normal event flow resumes
```

This is more robust than relying on every incremental event having arrived successfully during a disconnect.

---

## Reliability rules to define later

The production protocol should explicitly decide:

- Which messages require acknowledgement.
- Which actions need deduplication IDs.
- Retry timing.
- Reconnection behavior.
- Ordering guarantees.
- Maximum payload size.
- State snapshot format.
- Compatibility across protocol versions.
- Behavior when Atlas is newer than Sigil.
- Behavior when Sigil is newer than Atlas.
- Security/integrity requirements.
- How pairing data is cleared during factory reset.

These should be specified before calling the protocol production-stable.

Last reconstructed: 2026-09-19
