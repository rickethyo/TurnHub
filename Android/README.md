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
      protocol/                     wire DTOs + parser, 1:1 with /protocol schemas
        AtlasConnectionState.kt
        AtlasInfo.kt                /api/v1/info
        StateSnapshot.kt            /api/v1/state (+ PendingDecisions)
        Player.kt                   players[] (+ CommanderDamage, LifeRequest)
        GameProfile.kt
        TableState.kt
        AtlasWireParser.kt          strict org.json parsing, fails closed
      domain/
        TableSummary.kt             UI aggregate (+ TablePlayer, ControllerHandle,
                                    PhysicalSigilAtTable)
        TableSummaryMapper.kt       info + state -> TableSummary
      data/
        AtlasRepository.kt          the only seam the UI talks through
        HttpAtlasRepository.kt      connect handshake, polling, reconnect rules
        AtlasTransport.kt           getInfo()/getState() seam (+ factory)
        HttpAtlasTransport.kt       HttpURLConnection implementation
        WifiPreferringConnectionOpener.kt
        AtlasEndpoint.kt            user-editable http://host[:port]
        AtlasCompatibility.kt       API "1" / protocol "0.1" / stateSnapshot
        AtlasFailure.kt             user-facing failure types
      ui/
        home/                       HomeScreen, HomeUiState, HomeViewModel
        components/                 ConnectionStateBadge, TableSummaryCard,
                                    PlayerRow, PhysicalSigilRow
        theme/
    src/main/res/xml/network_security_config.xml
    src/test/java/com/turnhub/android/
      testing/Fixtures.kt           loads ../protocol/examples/*.json
      protocol/  domain/  data/  ui/home/
```

`protocol/` holds Kotlin wire models that mirror the JSON shapes in `/protocol`
(`state-v0.1.schema.json`, `info-v1.schema.json`) -- not the ESP32 C++ types,
and not a claim that the wire format is frozen. Unsigned 32-bit Atlas values
(revision, participant IDs, turn counts, `*Ms` clocks) are `Long`. `domain/`
holds UI-oriented models that combine more than one wire response
(`TableSummary` merges `/api/v1/info` identity fields with an `/api/v1/state`
snapshot) and so aren't a 1:1 mirror of any single schema.
`data/AtlasRepository` is the only seam the UI talks through; networking stays
below it. As lobby/game/player/settings/scanner screens are added, `ui/`
should grow one subpackage per screen alongside `ui/home`, following the same
pattern.

The exact Java/Kotlin package/application ID (`com.turnhub.android`) is a
development placeholder, not a frozen choice -- see `app/build.gradle.kts`.
That identifier becomes externally important once the application is
published, so it should be chosen deliberately rather than inherited
accidentally from this bootstrap.

## First vertical slice

The first useful app build should do only enough to prove the architecture:

1. Launch natively. *(done)*
2. Accept an Atlas endpoint manually or from a test QR payload. *(manual entry done)*
3. Fetch `/api/v1/state`. *(done)*
4. Render Atlas/table state. *(done, polled)*
5. Bind to one player seat/session.
6. Send semantic `PASS`, currently mapped by the adapter to `/api/control/pass`.
7. Observe the resulting authoritative revision/state.
8. Disconnect/reconnect and rebuild from a fresh snapshot. *(done)*

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

The current suite is plain JVM tests (no Robolectric/instrumentation). They
parse the real `protocol/examples/` fixtures, drive `HttpAtlasRepository`
through a scripted fake `AtlasTransport` in virtual time, and exercise
`HttpAtlasTransport` against the JDK's local HTTP server. `org.json` is a
test-only dependency because the unit-test `android.jar` only has stubs.

Build and test from `Android/` (Android Studio's bundled JDK works):

```text
./gradlew assembleDebug
./gradlew testDebugUnitTest
./gradlew testDebugUnitTest --tests "com.turnhub.android.data.HttpAtlasRepositoryTest"
```

## Current milestone

**Live read-only Atlas (first real integration):** the installed app talks to
a physical Atlas over its existing HTTP API. `MockAtlasRepository` is gone;
production always uses `HttpAtlasRepository`.

1. Open the app and tap Connect. The address defaults to `http://192.168.4.1`
   and stays editable. For that address the app first joins Atlas's Wi-Fi
   itself (see "Targeted Wi-Fi" below); other addresses must already be
   reachable.
2. `GET /api/v1/info` must be a TurnHub Atlas with API `1`, protocol `0.1`
   and state snapshots; anything else fails with a clear message.
3. `GET /api/v1/state` is mapped into the Home screen; only then is the app
   CONNECTED. State is then polled about once per second.
4. Every poll replaces the view (clocks change at the same revision). A new
   `atlasId`, new `bootId` or a lower `revision` discards the view and
   re-runs info + state. Three failed polls in a row drop the live view to
   DISCONNECTED; Connect again loads a fresh snapshot. Disconnect stops polling
   and clears state. A match Atlas recovered after a reboot is simply shown
   as `PAUSED`.

Portal parity (2026-09-26, *Needs verification* on hardware):

- The app now mirrors the Atlas portal's look and its player features. The
  four portal themes (Brass, Midnight, Parchment, High contrast; `ui/theme/
  Palette.kt`) are chosen in My Account and saved on the phone only; the
  device's raised-contrast setting still forces High contrast. A Reduce motion
  switch stops the turning gear and dial sweeps.
- Tabs as in the portal: **Game** (the brass turn dial, my seat with every
  session control the portal offers: join, I go first, start, cancel
  countdown, pass/cancel pass, pause/resume, claim, confirm or deny a win,
  concede, leave, rematch, reset; life tiles for every player with my -5/-1/
  +1/+5 and custom changes, tap-to-request changes to other players, the
  incoming life-request banner with its 15 s countdown, Commander damage, game
  setup and table facts), **Players** (roster and seated Sigils) and **My
  Account** (name, PIN, avatar, Sigil light color, Sigil accessibility,
  appearance, connection).
- All of it goes through the same routes as the portal (`/api/control/*`,
  `/api/control/life*`, `/api/control/commander`, `/api/game/settings`,
  `/api/session/profile`, `/api/session/personalization`); Atlas validates
  every request. Admin device settings (Wi-Fi password, pairing window,
  speaker, account permissions, factory reset) remain portal-only.
- **Quick app switch:** Android releases an app's `WifiNetworkSpecifier`
  network once the app leaves the foreground. When the app leaves the screen
  while connected, `AtlasLinkHoldService` (a `connectedDevice` foreground
  service with a notification) keeps the app eligible for two minutes
  (`HOLD_MS`) and stops as soon as the app returns.

Accessibility (2026-09-24):

- A signed-in player's **Sigil accessibility** button opens
  `AccessibilityDialog`: Sigil sound, light style and Action hold times, read
  from and saved to Atlas through `/api/session/accessibility` (the same
  endpoint and validation as the portal). Atlas stores them with the profile.
- When Android 14+'s contrast setting is raised, `TurnHubTheme` switches to fixed
  high-contrast colors instead of wallpaper colors, and follows changes live.

Targeted Wi-Fi (no trip to Android settings):

- `TargetedAtlasWifiLink` asks Android for `TurnHub-Atlas` with a
  `WifiNetworkSpecifier` (no internet capability). The network is used only
  by TurnHub: the phone keeps its normal Wi-Fi/mobile connection. The Pixel
  Fold even stayed on home Wi-Fi at the same time. The link is also the
  `HttpConnectionOpener`, so Atlas requests go over that network.
- Password order: the saved password for the SSID, else the shipped default
  `TurnHub-Setup` (protocol/http-v1.md), else the "Atlas Wi-Fi" prompt. The
  prompt allows editing the SSID and has an "I've joined this Wi-Fi already"
  option for the manual path. Only credentials that actually joined are saved
  (`PreferencesWifiCredentialStore`, app-private, excluded from backup and
  device transfer).
- Android shows its approval dialog at most once per access point, then
  remembers it. Disconnect, a lost connection or a failed handshake gives the
  network back. A joined network is dropped when Atlas reboots, so the view
  drops to DISCONNECTED and Connect rejoins.
- Needs Android 10+ (API 29). On older phones the prompt points to the manual path.
- Verified on a Pixel Fold (Android 17): the default fails against an Atlas
  with an owner-set password, the prompt appears, Join connects, Disconnect
  releases, and the next Connect reuses the saved password with no prompt.

Networking notes:

- Android 17 enforces local network protection for apps targeting API 37:
  without the `ACCESS_LOCAL_NETWORK` runtime permission ("Nearby devices"),
  every TCP connection to Atlas silently times out. `MainActivity` requests it
  when the user taps Connect (Android 17+ only); if denied, Home explains why
  and offers "Open app settings". Verified on a Pixel Fold running Android 17.

- Cleartext HTTP is allowed only for `192.168.4.1`
  (`res/xml/network_security_config.xml`). Other addresses are refused by
  Android until that file is deliberately extended (e.g. for Home/LAN mode).
- For the manual path (phone joined to Atlas in settings), Android often
  keeps mobile data as the default route because Atlas has no internet.
  `WifiPreferringConnectionOpener` sends Atlas requests over the current Wi-Fi
  network (needs `ACCESS_NETWORK_STATE`).
- `/api/v1/state` carries no player names today, so players show as
  `Player N`. The "Physical Sigils at this table" list only covers handles
  0-7 seated in `players[]`; Atlas publishes no paired-device inventory, so
  none is shown or invented.

Next milestone, prepared by `HttpAtlasTransport.request()` (form bodies and
`X-TurnHub-Token` already supported): profiles -> login -> join ->
`/api/session/me` -> authenticated `PASS` via the form-based
`/api/control/pass` -> fetch state again. Never auto-replay a timed-out
control. `/api/v1/intent`, events, BLE and QR discovery remain out of
scope. OOBE setup networks and QR-provided credentials can reuse
`AtlasWifiLink` as-is.

Last established: 2026-09-24
