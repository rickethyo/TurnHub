# TurnHub Android

This directory holds the native TurnHub Android client.

The Android app is a controller and presentation client. It is not a second TurnHub game engine, with one exception: the standalone tablet game below.

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

### The one exception: the standalone tablet game

With **no Atlas at the table**, **Play without Atlas** (on the Connect card)
runs a small game on the device itself: life, Commander damage, turns, pause,
concede and the winner (`standalone/StandaloneGame.kt`). It is a separate
game, never a copy of or a change to a game Atlas is running, so nothing is
merged back live. The rules above apply in full whenever Atlas is present.

- The table screen is tablet mode's (`ui/tablet/TabletTable.kt`); it sends
  the same seat actions through `data/TableControls`, which
  `StandaloneTable` applies locally instead of `AtlasTablet` sending them.
- Players are typed names or picks from the profiles of the last Atlas the
  app connected to (cached on every connect). Typed names that match a
  cached profile become that profile.
- Each finished game becomes a `GameRecord` kept on the device until an
  Atlas imports it once on a later connect, crediting games played and won
  to the matching profiles (by picked profile, else by name) and ignoring a
  record it has already taken. The import is the only way a standalone game
  reaches Atlas statistics.
- No turn timer, Two-Headed Giant, phones or Sigils in this mode.

```text
Compose UI
    -> ViewModel
        -> AtlasRepository
            -> TurnHub protocol adapter
                -> Atlas
```

State flows back in the opposite direction.

## Stack

- Kotlin, Jetpack Compose, Material3
- Unidirectional data flow with a ViewModel at screen boundaries
- Repository abstraction around Atlas communication
- Coroutines / Flow for asynchronous state

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
        UserManual.kt               parser for the bundled manual asset
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
        manual/                     ManualScreen (the "?" in the top bar)
        components/                 ConnectionStateBadge, TableSummaryCard,
                                    PlayerRow, PhysicalSigilRow
        theme/
    src/main/assets/manual.md       generated user manual (see below)
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

The app carries the user manual so it reads offline, before connecting to
any Atlas. `app/src/main/assets/manual.md` is generated from the newest
`Documentation/User Manual/TurnHub Manual V*.docx`; never edit it by hand.
After changing or adding a manual version, run
`python Android/tools/export_manual.py` (standard library only) and commit the
result. CI runs it with `--check` and fails when the asset is stale.

The application ID `com.turnhub.android` is frozen: the Google Play listing
was created with it on 2026-10-06 (Play App Signing), and Play never lets an
app change it.

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

Play builds: CI uploads a signed release bundle (`.aab`) from every run, ready
for the Internal testing track; see "Android release bundle" in
`Documentation/engineering/CONTINUOUS_INTEGRATION.md`.

## What the app does

The app talks to a physical Atlas over its HTTP API; production always uses
`HttpAtlasRepository`. It shows the live table, gives a signed-in player every
control the portal offers, runs first-run setup, and installs firmware
updates on Atlas and the Sigils.

1. Open the app. It asks for "Nearby devices" (local network, Android 17),
   then rejoins the saved table by itself (`HomeViewModel.onAppStarted`):
   Android remembers its approval, so this is silent when the table is
   there. *Verified* on the Pixel Fold, 2026-09-30: a cold start opens the
   live lobby with no taps.
   - The saved table doesn't answer: "Couldn't reach your table" with Try
     again and Set up a new table (a factory-reset Atlas).
   - A phone that never joined a table leads with Set up a new table, which
     joins with the printed default; first-run setup then opens.
   - No Wi-Fi scan. Spotting tables in range was tried first
     (NEARBY_WIFI_DEVICES, `neverForLocation`), but on this Android scan
     results need location permission (`WifiService`: "has no location
     permission", zero results), and the owner chose not to ask for location
     (2026-09-30).
   Connect still works as before. The address defaults to
   `http://192.168.4.1` and is under Advanced. For that address the app
   first joins Atlas's Wi-Fi itself (see "Targeted Wi-Fi" below); other
   addresses must already be reachable.
   Anything that restarts Atlas from the app (setup's Finish and updates,
   Settings > Wi-Fi password, factory reset) saves the password Atlas comes
   back with and rejoins by itself.
2. `GET /api/v1/info` must be a TurnHub Atlas with API `1`, protocol `0.1`
   and state snapshots; anything else fails with a clear message.
3. `GET /api/v1/state` is mapped into the Home screen; only then is the app
   CONNECTED. State is then polled about once per second.
4. Every poll replaces the view (clocks change at the same revision). A new
   `atlasId`, new `bootId` or a lower `revision` discards the view and
   re-runs info + state. Three failed polls in a row put the app offline
   (2026-10-07, *Implemented*): it stays on its current screen, tablet mode
   included, with the last snapshot shown read-only under an "Offline:
   reconnecting" banner and its age, rejoins Atlas's Wi-Fi if Android dropped
   it, and polls every 2 s until Atlas answers; the epoch rules above then
   apply, so a restarted Atlas is rebuilt from a fresh handshake and its
   sessions reset. Disconnect stops polling and clears state.
   During a game, tablet mode keeps working offline for life and Commander
   damage only: each change is kept per participant with when it was tapped
   (`AtlasTablet.queueOffline`), shown on the panels, and sent once Atlas
   answers again through the usual tablet routes with `queuedMs`, where
   Atlas's rules decide it. Changes from before an Atlas restart, from an
   earlier game, or for a player no longer in it are dropped, as is any Atlas
   refuses, with one note; a clean replay is silent. Passing, pausing,
   conceding, win claims, answers, seating and settings are refused offline,
   since they depend on Atlas's live clock and turn. A match Atlas recovered after a reboot is simply shown
   as `PAUSED`.

V1 redesign, phases 5 and 6 (2026-10-02, *Implemented*, built by CI; not yet
checked on a phone):

- Themes come from the shared design tokens (`design/tokens.json`, generated
  `ui/theme/DesignTokens.kt`): Automatic (the default: Graphite on a dark
  phone, Daylight on a light one), Graphite, Daylight, Brass and High
  contrast. `Palette.kt` builds every component role from them; a saved
  `midnight` or `parchment` choice opens as Graphite or Daylight. Only Brass
  draws ornament (sheens, rivets, the brass dial); the modern themes draw flat
  cards, tinted pills and buttons, and a turn ring.
- Inter (all text) and Cinzel (Brass headings) are bundled in `res/font`,
  copied from `design/fonts` by `design/build_tokens.py`; clocks and life
  totals use tabular figures.
- New launcher icon (the portal's gold gear, with an Android 13 themed-icon
  layer), an Android 12 splash on the theme's ground, edge-to-edge drawing with
  system bar icons that follow the theme, and a bottom bar using the shared
  icons: Game, Players, Me, Settings (Admins and Game Masters) and Developer.
- Game screen: the turn hero is a native ring that drains or fills with the
  clock and breathes while a game runs; the active player's avatar sits under
  it. Life tiles count to a new total on a spring, bounce slightly and show
  the change for a few seconds. Haptics: a firm tap when the turn reaches this
  phone's player, ticks on the life buttons and tab changes, confirm/reject on
  Pass. Reduce motion turns the springs, halo and transitions off.

V1 redesign, phases 7 and 8 (2026-10-02, *Implemented*, built by CI; not yet
checked on a phone):

- App lock and automatic sign-in (`data/ProfileVault.kt`, Android 11+): the
  sign-in sheet's "Sign in automatically" keeps that profile's PIN or password
  AES-GCM encrypted under an Android Keystore key that opens for 30 seconds
  after the phone's fingerprint, face or screen lock. It is saved only after
  Atlas accepts the secret. On each new Atlas boot the app asks for the
  phone's lock and signs in; cancelling or signing out by hand is respected
  until the next boot, and a rejected secret reopens the sheet. Me > This
  phone turns it off. Excluded from backup and device transfer.
- Sign-in is a bottom sheet with a grouped account list (`GroupedList`).
- The seat controls rise in as the join flow moves through its stages.
- Live turn notification (`data/TurnNotifier.kt`, `domain/LiveTurn.kt`): while
  the app is in the background and its player sits in a game, the shade and
  lock screen show whose turn it is with a running turn clock (or the turn
  timer counting down). It alerts only when the turn comes to you and asks to
  be a promoted live update on Android 16+. It lasts as long as the app holds
  the Atlas connection.
- Haptics use the vibrator's full-strength primitives (`ui/components/Haptics.kt`).

Portal parity (2026-09-26):

- The app now mirrors the Atlas portal's look and its player features. The
  portal themes (since V1: Automatic, Graphite, Daylight, Brass, High contrast;
  `ui/theme/Palette.kt`) are chosen in My Account and saved on the phone only; the
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
  every request.
- **Settings** (Admin or Game Master accounts) and **Dev** (Developer
  accounts) tabs appear from `/api/session/me`'s `permissions`:
  verify at the table (the six-digit code from the Atlas screen; a request
  Atlas answers `403 presenceRequired` asks for the code and retries once),
  Wi-Fi password, paired Sigils (rename, forget, factory reset), pairing
  window, speaker volume, Return to lobby, Atlas factory reset, account
  permissions and archiving, Game Master moderation, first-Admin setup, and
  the Developer activity feed, raw status/devices/seats/diagnostics JSON and
  a shareable serial log (`AtlasAdminConsole`). Firmware updates run from
  first-run setup and the **Update now** card ([Sigil OTA](../Documentation/engineering/FIRMWARE_UPDATES.md)).
- **First-run setup** (2026-09-30, FIRST_RUN_SETUP.md): after Connect the
  app reads `GET /api/setup`; while Atlas is new it shows the setup steps in
  place of the table (`AtlasSetupAssistant`, `ui/setup/SetupScreen.kt`):
  account (create or sign in), table code (makes the account the Admin),
  Sigil pairing, one update prompt for Atlas and every paired Sigil, and the
  table's own Wi-Fi password. Firmware comes from the public GitHub release
  feed (`GitHubFirmwareReleases`) over the phone's own internet, checked for
  size and SHA-256; Atlas checks each package's signature. After an Atlas
  restart the app rejoins with the saved password and signs in again with
  the PIN typed during setup (kept in memory only).
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
  remembers it. Disconnect or a failed handshake gives the network back; a
  lost connection keeps the request and rejoins with the saved password while
  the app is offline.
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
- `/api/v1/state` carries each player's `displayName`; players without one
  show as `Player N`. Paired Sigils come from the Admin device list, not from
  the state snapshot.
