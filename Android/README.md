# TurnHub Android

This directory holds the native TurnHub Android client.

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

## Current source layout

The Gradle/Kotlin project was bootstrapped as a single `:app` module (Kotlin +
Jetpack Compose, Material3, no other UI framework). Current layout:

```text
Android/
  app/
    src/main/java/com/turnhub/android/
      MainActivity.kt
      protocol/
        AtlasConnectionState.kt
        AtlasInfo.kt
        GameProfile.kt
        Player.kt
        Sigil.kt
        TableState.kt
        TableSummary.kt
      data/
        AtlasRepository.kt        (interface -- the future networking seam)
        MockAtlasRepository.kt    (in-memory implementation, no transport)
      ui/
        home/
          HomeScreen.kt
          HomeUiState.kt
          HomeViewModel.kt
        components/
          ConnectionStateBadge.kt
          PlayerRow.kt
          SigilListItem.kt
          TableSummaryCard.kt
        theme/
          Color.kt
          Theme.kt
          Type.kt
    src/test/java/com/turnhub/android/data/
      MockAtlasRepositoryTest.kt
```

`protocol/` holds Kotlin data models that mirror the JSON shapes in `/protocol`
(`state-v0.1.schema.json`, `info-v1.schema.json`) and the concepts in
`Documentation/engineering/` -- not the ESP32 C++ types, and not a claim that
the wire format is frozen. `data/AtlasRepository` is the only seam the UI talks
through; today `MockAtlasRepository` is the only implementation. As lobby/game/
player/settings/scanner screens are added, `ui/` should grow one subpackage per
screen alongside `ui/home`, following the same pattern.

The exact Java/Kotlin package/application ID (`com.turnhub.android`) is a
development placeholder, not a frozen choice -- see `app/build.gradle.kts`.
That identifier becomes externally important once the application is
published, so it should be chosen deliberately rather than inherited
accidentally from this bootstrap.

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

`app/src/test/.../data/MockAtlasRepositoryTest.kt` is the first of these: a
plain JVM test (no Robolectric/instrumentation) exercising the mock repository
through the same `AtlasRepository` interface a real implementation will
satisfy. It is a starting seam, not coverage of the points above.

## Current milestone

**Bootstrap 1 (UI shell, mocked data):** the Gradle/Compose project now exists
and builds a single Home screen: Atlas connection state (Disconnected/
Connecting/Connected), a mocked table summary, a mocked paired-Sigil list, and
a connect/disconnect action -- all backed by `MockAtlasRepository`. No HTTP,
Bluetooth, discovery, or device control is implemented; `AtlasRepository` is
the seam a real implementation will fill in later.

Foundation 1 remains the current Atlas baseline this app will eventually talk
to: Atlas exposes `/api/v1/info` and `/api/v1/state` with boot-scoped gameplay
revisions, and authenticated session controls that return revision metadata.
See the [implemented HTTP contract](../protocol/http-v1.md) and
[machine-readable examples](../protocol/examples/). The next Android slice
should still map `PASS` to the existing form-based `/api/control/pass`
endpoint and use the existing login/join/session endpoints once real
networking begins; the generic JSON Intent envelope and event stream are not
live yet. No Wi-Fi automation, discovery, or second game engine has been
introduced.

Last established: 2026-09-23
