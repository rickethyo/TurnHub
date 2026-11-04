# TurnHub Shared Protocol

**Status:** v1 working contract (phone-hosted games since 2026-11-04)

This directory defines the logical contract between Atlas and every controller or client.

The protocol exists above transport. HTTP, WebSocket, BLE, a future local relay, physical Sigils, browser sessions, simulators, and the native Android app may encode or carry messages differently, but they must converge on the same semantic requests and one authoritative state.

## Non-negotiable rule

The hub that runs the shared rules core owns each game (owner, 2026-11-04):
a phone or tablet, never Atlas. Atlas owns its hardware and what it keeps at
rest: profiles, statistics, match history, settings, Sigil pairings and the
table lease ([hosted-v1.md](hosted-v1.md)).

Clients do not perform gameplay transitions themselves. Guest phones, Sigils
and the Atlas screen send inputs to the hub; the hub validates and applies
them with the core and publishes the resulting state, which Atlas and guests
render.

```text
Guest phone ---\
Sigil ----------> input -> hub (shared core) -> validate -> mutate -> publish state
Atlas screen --/
```

A client may optimistically animate a button press, spinner, or pending
indicator, but it must not treat a gameplay change as committed until the hub
accepts it and its state confirms it. Atlas's own Intents
(`Atlas/include/intent.h`) carry device and account work only.

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

### 3. State snapshots

Atlas's own status is `GET /api/v1/state` (`atlas-state-v1.schema.json`): who
holds the table, battery and links, with a `revision` scoped to `atlasId` and
`bootId`. A hosted game's state is the hub's ([hosted-v1.md](hosted-v1.md)).
See [http-v1.md](http-v1.md) for revision and reconnect rules.

## Initial client API shape

The implemented HTTP surface is documented in [http-v1.md](http-v1.md):

```text
GET  /api/v1/state
GET  /api/v1/info
POST /api/hosted/claim | state | inputs | release   (hosted-v1.md)
```

Other endpoints cover profiles, authentication, device management, firmware,
diagnostics and setup.

`POST /api/v1/intent` and `WS /api/v1/events` remain unimplemented proposals.
The JSON envelope above is a draft.

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

## Shared rule scenarios

[`scenarios/`](scenarios/README.md) holds game rules written as data: setup
and actions in, expected state out, with all randomness scripted. Every engine
that implements a rule runs the same files. Planechase is the first.

## Versioning rules

1. `protocolVersion` describes the logical client contract, not the firmware version.
2. Existing field meanings do not silently change within a released protocol version.
3. New optional fields may be added compatibly.
4. Breaking changes require a new protocol version.
5. Atlas should advertise supported protocol versions/capabilities during connection setup.
6. Clients must fail clearly when there is no compatible protocol version.

## State and event rules

- The state's owner (the hub for a game, Atlas for its own status) assigns its revision.
- Clients never increment another owner's revision themselves.
- A reconnect begins with a snapshot, not replaying guessed local history.
- Events may reference the revision they resulted from.
- If a client detects a revision gap it should request a new snapshot.
- Time-sensitive display data may be locally rendered between snapshots, but the hub remains authoritative for the underlying game clock/state.

## Security direction

A table QR or discovery mechanism identifies/reaches an Atlas. It must not by itself grant control of a player seat.

Authentication/seat assignment is a separate step. Client-supplied player numbers are not trusted when the hub or Atlas can resolve identity from the authenticated session/device relationship.

## Current state

Games run on the hub's shared core; Atlas relays its Sigils' and screen's
inputs to the hub ([hosted-v1.md](hosted-v1.md)). Atlas's own device and
account actions go through its Intents. The generic v0.1 JSON envelope remains
a draft. Internal `controllerId` naming does not silently change the draft
JSON schema's `moduleId` field or the ESP-NOW wire packet.
