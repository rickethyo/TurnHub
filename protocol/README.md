# TurnHub Shared Protocol

**Status:** v0.1 working contract

This directory defines the logical contract between Atlas and every controller or client.

The protocol exists above transport. HTTP, WebSocket, BLE, a future local relay, physical Sigils, browser sessions, simulators, and the native Android app may encode or carry messages differently, but they must converge on the same semantic requests and authoritative Atlas state.

## Non-negotiable rule

Atlas is the sole owner of canonical table and game state.

Clients do not perform gameplay state transitions locally. They request an action by sending an Intent. Atlas authenticates the actor, validates the current state, applies the authoritative transition, increments the state revision when canonical state changes, and publishes the resulting state/event.

```text
Android ----\
Browser -----\
Sigil --------> Intent -> Atlas -> validate -> mutate canonical state -> publish state/event
Atlas UI ----/
Simulator ---/
```

A client may optimistically animate a button press, spinner, or pending indicator, but it must not treat a gameplay change as committed until Atlas accepts it and the authoritative state confirms it.

## Three separate layers

### 1. Internal Intent

The ESP32 application layer uses the heap-free C++ types in:

- `Atlas/include/intent.h`
- `Atlas/include/intent_dispatcher.h`
- `Atlas/src/intent_dispatcher.cpp`

These types are intentionally independent of HTTP, JSON, BLE, or Android.

### 2. Wire Intent Envelope

Networked clients use the versioned envelope described by `intent-v0.1.schema.json`.

Example:

```json
{
  "protocolVersion": "0.1",
  "requestId": "7d3f1d92-ff47-4b84-a013-11601901f934",
  "clientId": "android-7c94d2",
  "origin": "ANDROID_APP",
  "expectedRevision": 42,
  "type": "PASS",
  "actor": {
    "moduleId": 2,
    "slot": 1
  },
  "payload": {}
}
```

`requestId` exists for correlation and eventual deduplication. A retransmitted request with the same identity must not be interpreted as a second gameplay action once deduplication is implemented.

`expectedRevision` is optional. When supplied, it allows Atlas to reject a request made against stale state when that matters for the operation.

### 3. Authoritative State Snapshot

Clients recover from reconnects by requesting a snapshot described by `state-v0.1.schema.json`.

Snapshots carry a monotonically increasing Atlas `revision`. Incremental events are useful for responsiveness, but a client must always be able to discard local assumptions and rebuild from a current snapshot.

## Initial client API shape

The exact HTTP paths are not frozen, but the working v0.1 shape is:

```text
GET  /api/v1/state
POST /api/v1/intent
WS   /api/v1/events
```

Additional endpoints may exist for profiles, authentication, device management, firmware, diagnostics, and setup. Gameplay semantics should still enter Atlas through the Intent layer.

## Intent result

A network adapter translates the internal `IntentResult` into a transport response. The v0.1 result shape is conceptually:

```json
{
  "requestId": "7d3f1d92-ff47-4b84-a013-11601901f934",
  "status": "ACCEPTED",
  "message": "Pass queued",
  "revision": 42
}
```

Status vocabulary mirrors Atlas semantics:

- `ACCEPTED`
- `REJECTED`
- `UNSUPPORTED`
- `INVALID_ACTOR`
- `INVALID_STATE`
- `UNAUTHORIZED`
- `CONFLICT`

Transport adapters may map these onto HTTP status codes, BLE acknowledgements, tones, LEDs, or UI messages. The semantic result remains the same.

## Versioning rules

1. `protocolVersion` describes the logical client contract, not the firmware version.
2. Existing field meanings do not silently change within a released protocol version.
3. New optional fields may be added compatibly.
4. Breaking changes require a new protocol version.
5. Atlas should advertise supported protocol versions/capabilities during connection setup.
6. Clients must fail clearly when there is no compatible protocol version.

## State and event rules

- Atlas assigns the canonical state revision.
- Clients never increment the canonical revision themselves.
- A reconnect begins with a snapshot, not replaying guessed local history.
- Events may reference the revision they resulted from.
- If a client detects a revision gap it should request a new snapshot.
- Time-sensitive display data may be locally rendered between snapshots, but Atlas remains authoritative for the underlying game clock/state.

## Security direction

A table QR or discovery mechanism identifies/reaches an Atlas. It must not by itself grant control of a player seat.

Authentication/seat assignment is a separate step. Client-supplied player numbers are not trusted when Atlas can resolve identity from the authenticated session/device relationship.

## Current migration priority

Gameplay migration through Intents is implemented. Atlas `0.6.0-dev` adds profile
login and phone-only/mixed participation through the same application handlers.
See [implemented profile endpoints and ownership](../docs/engineering/PROFILE_LOGIN_AND_VIRTUAL_PLAY.md).
The generic v0.1 JSON envelope/routes above remain a working contract, not the
live HTTP ingress. Internal `controllerId` naming does not silently change the
draft JSON schema's `moduleId` field or the ESP-NOW wire packet.

Last established: 2026-09-19
