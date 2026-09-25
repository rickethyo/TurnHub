# TurnHub Protocol and Pairing

Current pairing update (2026-09-22): physical buttons are owner-verified and manual
15-second pairing with persistent MAC associations is now implemented. The boot
pairing fallback and visual mock are superseded. Forgetting (Sigil 10-second Pair
hold; admin Forget on Atlas with an `Unpair = 12` packet) and an admin-adjustable
Atlas window were added 2026-09-24. Radio bench acceptance remains pending. See
[Manual Pairing](MANUAL_PAIRING.md).


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

The migration introduced compact binary packets and a shared `protocol.h` used by Atlas and Sigil firmware. Its single source is `shared/include/protocol.h`; both PlatformIO projects and the Atlas host tests add `shared/include` to their include path.

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

### Sigil-rendered status light: LedState (2026-09-25)

`LedState = 25` (Atlas -> Sigil) carries the light's *meaning* instead of
channel levels: the cue (`LedCue`), overlay bits (`LedOverlay`), player number,
focused seat (A/B) and whether the Sigil is shared, the seated players' LED
style (Standard, Reduced motion, Monochrome-safe), and the time since the cue's
anchor (turn or countdown start) in 16 ms units so anchored patterns stay in
phase. `encodeLedState`/`decodeLedState` in `protocol.h` define the bit layout;
the cue and overlay enums moved there from Atlas's `led_cues.h`, which now
aliases them.

A Sigil advertising `CAPABILITY_LED_STATE` (0x20) gets one LedState per change,
plus a resend whenever its Hello arrives (about every 2 s), so a lost packet or
quiet reboot heals itself. Atlas still decides every cue (`selectSigilLedState`);
the Sigil only draws it (`Sigil/src/sigil_led.cpp`): full color on an RGB LED
(PWM on all three pins) and, on the E-ink Sigil, spatially on the NeoPixel Jewel
(player number as lit pixels, a shared seat as its ring half, the top overlay in
the center). Sigils without the bit keep the `SetBlue`/`SetRed`/`SetGreen`
stream, and a new Sigil still follows those packets from an older Atlas.
Pairing blink and the Pass acknowledgement flash stay Sigil-local. Protocol
version stays 1. *Needs verification* on hardware.

### Unpair (2026-09-24)

`Unpair = 12` (Atlas -> Sigil, `value` 0) tells a Sigil that Atlas forgot it. The
Sigil honours it only from its saved Atlas MAC with its own Sigil ID, then erases
its pairing. It is best effort and unacknowledged. Older Sigils ignore it.
`FORGET_PAIRING_HOLD_MS` (10 s) is the Sigil's Pair hold that forgets locally.
`PAIRING_WINDOW_MS` stays 15 s: it is the Sigil's window and Atlas's default.
Protocol version stays 1.

### Hold timing (2026-09-24)

Sigils from firmware 0.5.4 advertise `CAPABILITY_INPUT_TIMING = 0x08` in Hello.
Atlas sends those peers `InputTiming = 24` in the existing seven-byte packet:
the long-press threshold in bits 0-15 and the win-hold threshold in bits 16-31
of `value`, both in milliseconds. `protocol.h` holds the defaults (2,000 and
5,000 ms), the limits (1,000-4,000 and 3,000-10,000 ms, 250 ms steps, win hold
at least 1,000 ms longer) and `validInputTiming()`, which both sides use. The
Sigil ignores invalid values and keeps them in RAM only, so a rebooted Sigil
uses the defaults until Atlas resends (every 10 s, or on change). Older Sigils
ignore the unknown type and keep their fixed thresholds. Protocol version stays
1. The thresholds only change when a gesture is recognized; the resulting
packets (`ActionLong`, `ActionWin`) and their Intents mean the same thing.
Atlas picks the values from the seated players' accessibility preferences
(ACCESSIBILITY.md).

### Running-game Sigil display (2026-09-22)

Sigils advertise `CAPABILITY_GAME_DISPLAY = 0x04` in Hello. Atlas sends those
peers `GameDisplay = 32` while running; existing seven-byte packet meanings,
protocol version 1, `DisplayState = 30`, and seat-name chunks (`31`) are unchanged.
Older peers retain their existing display. Lobby, pairing, ready, pause, and
game-over screens continue using the existing display/profile path.

The new packed ESP-NOW datagram is 110 bytes, with little-endian integers:

| Offset | Field |
| --- | --- |
| 0�2 | Version, type (32), target Sigil ID |
| 3�6 | Existing encoded DisplayState |
| 7�9 | Commander boolean, visible source count (0�3), omitted source count |
| 10�26 | Focused participant: signed int32 life, 13-byte terminated name |
| 27�43 | Secondary participant, same layout (zero when absent) |
| 44�109 | Three sources: player number, 13-byte terminated name, two int32 damage values |

Atlas's existing `LedRenderer::syncDisplay` resolves the focused and secondary
players through `GameEngine::playersForController` and the existing focus rules.
Life comes directly from `lifeTotal(playerNumber)`; names come from the profile
ID captured in the game participant. Guest names fall back to `Player N`.
Name reads reuse the display cache until existing Hello/lifecycle invalidation,
so profile edits appear on the next Hello resynchronization. No life or damage
is stored in profiles or calculated by Sigil.

Commander rows contain `commanderDamage(focusedRecipient, source, 1/2)`.
Sources with no damage and the recipient itself are omitted. The first three
nonzero sources in player-number order are sent, including browser participants
and eliminated sources with recorded damage. Further sources produce a `+N`
indicator. Names are clipped to nine characters in rows. One commander appears
as `Jaime 6`, two as `Jaime 6/3`; damage only from slot 2 appears as `Jaime 3 (C2)`.
`CMD none received` distinguishes an empty Commander display from normal mode.

The 250�122 running screen emphasizes the name and life total, retains Sigil/host
and turn status, and removes P1/P2 badges. Large signed totals shrink to fit.
Shared Sigils emphasize the existing focused participant (active local seat,
otherwise the first local seat), with a smaller secondary name/life summary;
Commander rows belong to the focused participant only.

Snapshots use the existing serialized radio TX queue and paired MAC destination.
Sigil validates exact size, version/type, target, bounds and terminated names,
and accepts only its saved Atlas MAC. A critical section protects the rendering
snapshot across radio/display tasks. Each datagram is complete, so loss cannot
combine life from one update with damage or identity from another. No new
pairing or trust mechanism is introduced.

Atlas compares display snapshots before enqueueing. Existing Hello invalidation
resends current values for reconnect/loss recovery; queue admission failure is
retried by the next state comparison. Sigil compares both received and rendered
snapshots, so identical recovery packets do not refresh the panel. Profile-name
packets cannot force a running-game redraw. Updates received while the panel is
busy wake the display task to render the latest snapshot. There is no periodic
e-ink refresh timer. Delivery still depends on existing ESP-NOW/Hello recovery;
there is no new application acknowledgement protocol.

Host scenarios cover wire size/round-trip/validation, named recipients and
sources, received-vs-dealt semantics, partner damage, shared focus, negative life,
source overflow, unchanged snapshots, resynchronization and legacy peers.
Physical acceptance remains required for readability/ghosting, radio loss and
reconnect behavior, and updates arriving during a panel refresh.

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
