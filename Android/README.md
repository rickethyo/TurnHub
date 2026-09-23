# TurnHub Android

This directory is reserved for the native TurnHub Android client.

The Android app is a controller and presentation client. It is not a second TurnHub game engine.

## Hard architecture rule

The app may:

- Discover or connect to an Atlas.
- Authenticate a player/session.
- Request a current state snapshot.
- Render authoritative Atlas state.
- Send semantic Intents.
- Receive state/events.
- Cache user preferences and non-authoritative profile conveniences locally.
- Recover from connection loss by resynchronizing with Atlas.

The app must not:

- Advance turns locally as authoritative state.
- Decide whether a concession, win claim, pause, or life change is legal.
- Become authoritative for life totals, active player, timers, winner, or elimination state.
- Reimplement gameplay rules that already belong to Atlas.
- Depend directly on ESP32 C++ implementation details.

```text
Compose UI
    -> ViewModel
        -> AtlasRepository
            -> TurnHub protocol adapter
                -> Atlas
```

State flows back in the opposite direction.

## Planned stack

- Kotlin
- Jetpack Compose
- Unidirectional data flow
- ViewModel at screen boundaries
- Repository abstraction around Atlas communication
- Coroutines / Flow for asynchronous state
- DataStore later for local preferences
- QR scanning / Android connection bootstrap later

The current Android project should be created from the contemporary official Compose template rather than hand-locking old Gradle/plugin versions into the repository. As of this foundation pass, protocol and architecture are being established first so Android Studio project generation can happen without inventing a separate backend contract.

## Proposed source layout

```text
Android/
  app/
    src/main/java/.../
      protocol/
        IntentEnvelope
        IntentResult
        StateSnapshot
        TurnHubEvent
      data/
        AtlasRepository
        AtlasConnection
      ui/
        lobby/
        game/
        player/
        settings/
      scanner/
        AtlasJoinParser
```

The exact Java/Kotlin package/application ID is intentionally not frozen yet. That identifier becomes externally important once the application is published, so it should be chosen deliberately rather than inherited accidentally from a prototype.

## First vertical slice

The first useful app build should do only enough to prove the architecture:

1. Launch natively.
2. Accept an Atlas endpoint manually or from a test QR payload.
3. Fetch `/api/v1/state`.
4. Render Atlas/table state.
5. Bind to one player seat/session.
6. Send semantic `PASS`, currently mapped by the adapter to `/api/control/pass`.
7. Observe the resulting authoritative revision/state.
8. Disconnect/reconnect and rebuild from a fresh snapshot.

If this works without duplicating game logic in Android, the architecture is doing its job.

## Development dependency order

Android work may proceed in parallel with firmware, but these interfaces need to stabilize in roughly this order:

```text
Intent vocabulary
    -> Atlas dispatcher enforcement
        -> v0.1 wire adapter
            -> state snapshot + revision
                -> Android repository
                    -> Android UI
```

The app UI does not need to wait for every TurnHub feature. It does need a stable way to say "I want X" and receive "this is now the authoritative state."

## Testing direction

Android tests should eventually include:

- Protocol serialization tests against files in `/protocol`.
- Repository tests using a fake Atlas transport.
- ViewModel tests that verify UI state follows repository state.
- Reconnect/state-revision tests.
- Duplicate `requestId` behavior once Atlas implements deduplication.
- Stale `expectedRevision` conflict handling.

Game-rule tests stay with Atlas/domain code. Android tests verify client behavior, not whether TurnHub rules are correct.

## Current milestone

**Foundation 1:** Atlas exposes `/api/v1/info` and `/api/v1/state` with boot-scoped
gameplay revisions. Authenticated session controls use the shared dispatcher,
return revision metadata and accept optional concurrency checks. See the
[implemented HTTP contract](../protocol/http-v1.md) and
[machine-readable examples](../protocol/examples/).

The first Android adapter should map `PASS` to the existing form-based
`/api/control/pass` endpoint and use the existing login/join/session endpoints.
The generic JSON Intent envelope and event stream are not live. Poll snapshots,
rebuild on reconnect, and never automatically retry ambiguous PASS requests.
No Android project, application ID, Wi-Fi automation or second game engine has
been introduced.

Last established: 2026-09-22
