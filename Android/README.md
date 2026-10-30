# TurnHub Android

This directory holds the native TurnHub Android client.

The full-screen table lobby, the bottom of Settings and the banner's bug button
show the installed app version and build number (for example,
`TurnHub 0.1.0 · Build 123`). Debug packages also show `Debug`. The same label
is available in the live table menu, so a saved game need not be ended to
identify the app.

The banner holds two small unlabeled buttons beside the connection pill: a bug
(Report a bug: a dialog says what it does and which version to mention, then
opens the form) and `?` (User manual). TalkBack reads their names, and a long
press shows them.

The app uses the same **Game, Table, Players, History and More** navigation
with or without Atlas, and tabs slide sideways as you move between them (no
motion with reduce motion on). Table shows the game's table inside the app: the
running or last game, or before a game a practice table built from the lobby's
seats (placeholder seats fill a short roster; two when nobody is seated). It
plays like the real table but lives only in memory, so nothing on it is saved.
Full screen takes a started game over the whole screen. Star Commander games can
use the setup's star as the table layout (a per-device switch in the lobby and
in the table menu's Sound & display), with each panel facing its player. In any
Commander mode a player can optionally name their commander in the lobby (Scryfall
suggests card names as they type, and they can pick which printing's art); the
name shows under theirs, on their commander-tax counter and in others' Commander
damage, and its Scryfall art crop (credited to the artist,
cached in `commander_art/`, off via Settings > Commander card art) fills their
tile. In every game the same picker chooses a tile card from any of Magic,
Pokémon (TCGdex), Lorcana (Lorcast, AVIF so Android 12+) or Yu-Gi-Oh!
(YGOPRODeck), typed or **Random card** (`TileCardGame`, `data/TileCards.kt`;
saved as a prefixed id in `commanderArt`). Only a Magic card typed in a
Commander mode is named as the commander; every other tile card is art only.
When the device game is a Magic format, its setup has a Planechase
switch and a Planes tab runs Planechase. Choose all planes, a release collection
(including Anthology), or browse cards and save a named custom deck. Search,
collection and selected-only filters, checkboxes, and Previous/Next previews
help build the pool. Deck choices freeze before play, persist across reopening
and rematches, and work offline after downloading; uncached preview art needs
internet. Up to 20 named decks are kept locally. Planechase has:
setup (a shuffled digital deck, downloaded once from Scryfall into
`files/planes/` with art cached in `plane_art/`, or the players' own cards),
then the face-up plane with artist credit, the planar die with each roll's
cost, phenomena and counters. During live play, Planar die beside Pass animates
one roll: blank keeps Table open, while chaos or planeswalk opens Planes and
highlights the effect. Local starts and rematches show a cancellable three-second
countdown; clocks start after it finishes. Back to setup returns to Game,
including from the full-screen table. The engine is `standalone/Planechase.kt`, and
`PlanechaseScenarioTest` runs the shared scenarios in
`protocol/scenarios/planechase/`. A finished phone-hosted Star game also offers Sean Mode, a rematch that
reseats the five players so allies become enemies. Game is one tab
(`GameTab`) for either game source: Atlas's game when connected, unless the
device already has one under way (`gameSource`), with a switch while setting
up. A device game shows its local setup, then the same stage/life overview
and game settings as connected play, without the per-seat Atlas controls. Players keeps local
profiles available and adds Table and Me sections when connected. History keeps
local results and Atlas import controls. More opens About TurnHub, Settings
and Store as separate pages; the store is not open yet. Support TurnHub offers
optional PayPal and Cash App contributions beneath a labeled tabletop hardware
static concept image,
through matching full-width buttons,
separate from app features and purchases. Settings contains
appearance, reduced motion, sound and Atlas connection; authorized administration and developer
controls appear there when available. Navigation and scroll positions survive
visits to the full-screen table and statistics.

The main app shell, Play Together and full-screen table views have soft warm
background glows, a breathing gold gear with outward rings, and a secondary
gear outline behind opaque cards. The glows breathe
at different slow rates; gameplay uses a quieter version. Reduced motion keeps
the decoration still, high contrast uses a plain background, and ambient motion
stops while the app is backgrounded. Embedded pages share the shell's backdrop.

Add player opens a compact dialog; player settings and individual game details
expand on request. Account IDs distinguish equal names. Controls use direct
labels, with brief flavor text in introductions and empty states.

The Android app is a controller and presentation client. It is not a second TurnHub game engine, with one exception: the standalone tablet game below.

Product direction (owner, 2026-10-13): the app is a full, general-purpose game app on its own; a physical Atlas unlocks extras (Sigils, a phone for every player, shared profiles and table-wide statistics, the Atlas screen and speaker, administration). TCG games (Magic variants, Yu-Gi-Oh!, Pokémon and Lorcana) support a phone host and same-network player clients; Death May Die also supports Elder One displays and investigator clients ([shared devices](../Documentation/engineering/SHARED_DEVICES.md)). TCG players switch between their own controls and a read-only full table without reconnecting. Both modes use the same navigation and shared table presentation. The standalone game is therefore a supported engine for games with no Atlas, but it still never merges into, copies or changes a game Atlas runs.

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

### Game-action sounds

Local and Atlas-connected play share short cues for confirmed start, pass/turn
change, pause, resume, victory and draw. Initial load, repeated polls, cancelled
or rejected actions, rotation, returning to the app and reconnect catch-up stay
quiet. Cues stop when the app leaves the foreground; the existing background
turn notification remains separate.

**Game sounds** and **Sound volume** are phone-only preferences available in
both table setup screens, the live table menu and Settings. Default: on at
50%. Phone silent/vibrate mode and media volume still apply. Sound supplements
the visible state and existing haptics, and never changes a game. Original WAV
assets come from `tools/generate_game_sounds.py` (stdlib; `--check` validates
them); native SoundPool adds no third-party runtime dependency.

### Bug reports

**Report a bug** opens the TurnHub Google Form in the browser with editable
defaults for the installed app version/build, reporting phone/tablet type,
manufacturer/model, Android version and display size. The report includes the
screen and game format/state when known, and the last known firmware version
for an Atlas screen. Device play stays device play even if an Atlas connection
exists in the background. Settings leaves play mode for the reporter to choose.

The single-page form adds optional expected-result and frequency prompts. Its
version/device fields are optional so old app versions and direct links still
work. The user can change the issue device to Physical Atlas, review all defaults,
attach a file and explicitly submit. Google Forms collects the signed-in email;
there is no automatic submission, new permission or developer credential in the
app. Question IDs in `BugReportLink.kt` must stay aligned with the owned form.

### The one exception: the standalone tablet game

Fresh launches open the shared Game tab; rotation restores the open screen.
Saved local games remain available through Table, and starting a game opens
it there. With Atlas discovery enabled,
a once-per-launch probe checks the current network when a saved table and
local-network permission already exist. It never joins Wi-Fi, prompts for
permission, requests biometric sign-in or takes over navigation. Connect, retry
and setup in Settings > Atlas connection can join Wi-Fi and request permission.
Turning discovery off disconnects Atlas. Opening a local table keeps an existing
Atlas connection available; the two games remain independent.

Players and History are available without Atlas. Accounts have independent UUID
history keys and random eight-hex public IDs compatible with Atlas. Optional
PINs/passwords use the same input rules as Atlas; only PBKDF2-HMAC-SHA256 verifiers
(120,000 iterations, random 16-byte salts) are saved locally. They protect account
settings after restart; shared-table play and local results remain available.
Setting or changing one keeps the account's settings open until the app
restarts, and it can be removed again. An account can be deleted from this
phone (not while it's in a game in progress); its finished games stay in
History under the name played with. Local accounts have no permissions: on a
phone every player is equal, and Admin is only for an Atlas's administration
(web portal) functions (owner decision 2026-10-15). Reduced motion applies to
a table when one of its seated players has it on.
Local name, color, preset/custom avatar, reduced motion and phone appearance work
offline. Image decode/crop/compression and thumbnail generation run on Android.
Player and tile colors use named swatches and a visual custom picker. Local color
choices save immediately and appear in the lobby, including Star Commander.
Choosing a tile color while using an image switches to a solid background and
keeps the image available. In-game life controls use minus on the player's left
and plus on their right; holding either side applies larger steps.
The local library retains detailed lifetime/last-game statistics and final
counter/Commander details even if the phone never connects. Statistics can be
shared as text. Version-1 UUIDs and retained records upgrade without name guessing.

Atlas 0.7.15 accepts an unused requested public ID at registration; collisions
return 409 without modifying any account. Account import creates that identity,
then copies available personalization through ordinary authenticated APIs and
image review. Linking to an existing account requires its PIN/password, retains
Atlas personalization and saves an `(atlasId, profileId)` link. Existing local
history and UUIDs remain unchanged. **Bring players to this table**
(`ui/tablet/BringPlayersCard.kt`) lists every local account's state with the
connected Atlas; PIN-less accounts upload together on the signed-in session
(Atlas 0.7.18+), accounts with a PIN one at a time with it. Each link then
holds a link key (`AtlasProfileLink.linkKey`), and whenever the Atlas
accounts load the app syncs every keyed account through
`/api/profiles/sync`: the copy with the later `lastActivityMs` wins name,
color, preset avatar and reduced motion as a whole (an Atlas copy applied here
keeps Atlas's time); the card shows each account's Atlas totals. PINs and
statistics never sync this way. Verifying an exact-ID signed-in account can
establish its own link; an unverified public-ID match never does.

Once signed in, complete verified mappings automatically queue finished matches,
and retry pending work after reconnect. On Atlas 0.7.16+ one verified link is
enough: unlinked players go as guests, and a match sent with guests is sent
again (crediting only the new player) when another of its players is linked. Match IDs and mappings remain frozen
while acknowledgement is uncertain. Imported or rejected records are never
queued again automatically; partial credit needs review. Missing/archived profiles
or duplicate targets leave games for manual mapping. This does not enlarge
Atlas's 128-receipt deduplication window or make its statistics transaction atomic.

With **no Atlas at the table**, the **full-screen table**
runs the game on the device itself: life, Commander damage, counters,
Monarch/Initiative, turns, pause, concede, the winner and the turn timer
(`standalone/StandaloneGame.kt`). Timer presets are Off/1/2/3/5/10 min, with a
warning at 10 seconds and a Long turn indicator after five minutes when off.
Overtime is the default. **Timer rules** can instead auto-pass, add one demerit
per expired turn, or do both. Optional extensions start empty: passing before
75% (or 100%) of the original limit, from your fourth personal turn onward,
earns one. The default cap is two earned per game (configurable 1–3), including
ones already spent. One extension can be spent before expiry per turn, adding
half the limit; extended turns cannot earn another. Teams share their bank and
demerits. Pausing freezes the clock. Timeout actions run while the app is visible;
on reopening, an expired turn is settled once and an auto-passed next turn gets
a fresh clock. Banks, demerits and timer settings survive app restart. Demerits
and extensions used remain in local game details and shared summaries; they
are not imported into Atlas statistics. Atlas-connected games keep Atlas's rules.

The central **Game menu** groups table tools, game rules, seating/alliance views,
sound/display, help and finishing the game. Pause/resume stays at the top; the
title and Done button stay visible while sections scroll. Tools offer d4 through
d20 (common dice directly, others under **More dice**), a coin and a random player.
Elimination rules use on/off switches and an explicit button to apply a new limit.

Each player's **More** card holds turn actions/extensions, counters/status,
Commander damage and concede/eliminate controls. **Pick an opponent** uses that
card's player, excluding self, eliminated players, teammates and Star Commander
allies. Player cards remain available while paused; the central hub displays
**Paused**, and the Game menu provides Resume.

History > Statistics shows per-player statistics derived from
local records (went first, eliminations, concessions, turn and game times). It is a separate
game, never a copy of or a change to a game Atlas is running, so nothing is
merged back live. The rules above apply in full whenever Atlas is present.

- The table screen is tablet mode's (`ui/tablet/TabletTable.kt`); it sends
  the same seat actions through `data/TableControls`, which
  `StandaloneTable` applies locally instead of `AtlasTablet` sending them.
- Adding a typed name creates a reusable player with a local UUID. Selecting a
  saved player reuses that identity. Same-name players stay distinct and short ID
  labels distinguish them. Reset/rematch preserves IDs; records snapshot names.
- The lobby seat preview stays visible with zero or one player, reserving two
  seats until the second player joins. Drag a player onto another or tap two
  seats to swap them. **Clear all** empties the local selection in one saved
  update, preserving accounts, history and game settings.
- Local accounts store an independent tile background: default, solid, gradient
  or a cropped image (up to 512 × 512 pixels and 48 KiB). The lobby's Tile color & photo
  button and account editor configure it. It follows the local UUID through
  swaps, clearing and new games; avatars and match records remain separate.
  Connected tables use this phone's background only through a verified link
  to that Atlas/profile. Images are decorative and dimmed behind controls.
- `TableLobby` and `TableLobbyActions` share setup rendering/input behavior;
  `TabletTable` and `TableControls` share live rendering/controls. Authority
  adapters own validation and persistence. Complete common-engine hosting,
  automatic phone takeover and reconnect reconciliation remain a
  [design](../Documentation/engineering/TABLE_AUTHORITY.md).
- **History** is available in both modes. It shows results and detailed statistics derived
  only from retained local matches by local ID, without combining Atlas totals.
- The versioned local library owns roster, history and independent delivery
  metadata. Active game/library writes share one preference transaction. Every
  completed match is retained once by its match UUID; there is no automatic
  200-record pruning. Imported/rejected results stay in history. Current game
  and identities survive reconstruction; actual Android rotation/relaunch still
  needs device acceptance. Corrupt/future data fails closed, preserves the
  stored bytes, and explains the problem rather than overwriting it.
- Account links enable automatic import as described above. Unlinked or historical
  records still support manual per-match mapping, with no name-based choices.
- History separately shows needs-linking, pending, imported and needs-attention
  (rejected) states. HTTP 400 retains the record and reason without automatic
  retry; explicit review/relink can queue it again. Transient/malformed responses
  keep pending work and block newer deliveries until acknowledged. A signed-in
  user can send only pending work for the current Atlas. Atlas 0.7.2+ requires
  exact destination/profile IDs and rejects missing, duplicate, deleted,
  archived or unreadable identities before taking a receipt; no name fallback.
  Acknowledged partial credit is displayed and never automatically retried.
- Existing queue records are retained as legacy history without guessed local
  or Atlas identity and can be explicitly mapped per match. Earlier imported/
  dropped records cannot be reconstructed. Atlas's 128-receipt dedupe window and
  possible partial credit after storage failure/power loss remain unchanged;
  this is not crash-safe exactly-once statistics across arbitrary retries.
- No multi-phone play or Sigils in standalone mode.

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
`Documentation/User Manual/TurnHub App Manual V*.docx` followed by the newest
`TurnHub Hardware Manual V*.docx`; never edit it by hand.
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
`HttpAtlasRepository`. It shows the live table, gives a signed-in player personal and shared-tablet game controls, runs first-run setup, and installs firmware
updates on Atlas and the Sigils.

1. Open the app to the shared Game tab. Atlas discovery stays in the background.
   Settings > Atlas connection provides Connect, retry and setup actions.
   Android 17 requests Nearby devices permission only after an explicit Atlas
   action. A saved table that cannot be reached shows retry/setup controls there;
   it does not replace local play. The app does not scan for nearby Wi-Fi networks.
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

Native player experience (original parity pass: 2026-09-26):

- The app provides the player experience and shares TurnHub design tokens. The
  themes (since V1: Automatic, Graphite, Daylight, Brass, High contrast;
  `ui/theme/Palette.kt`) are chosen in My Account and saved on the phone only; the
  device's raised-contrast setting still forces High contrast. A Reduce motion
  switch stops the turning gear and dial sweeps.
- Native tabs: **Game** (the brass turn dial, my seat with every
  session controls: join, I go first, start, cancel
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
  while connected to Atlas, hosting a shared game or seated as a guest,
  `AtlasLinkHoldService` (a `connectedDevice` foreground service with a
  notification, a Wi-Fi lock and a partial wake lock) keeps the connection for
  five minutes (`HOLD_MS`) and stops as soon as the app returns. A host keeps
  answering its guests during the hold.

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


### Local library feature boundary (TH-001 B/C implemented, device acceptance pending)

Android's standalone domain owns reusable local-player UUIDs, completed match
snapshots and their derived local statistics. Explicit add/select actions are
validated locally; names are labels and never identity keys. The local library
is a versioned JSON document in `turnhub_standalone/library`, separate from the
active game's `game` key. Match UUIDs remain stable across import. History is
retained without automatic pruning; delivery metadata references history by
match UUID and acknowledgement/rejection cannot delete a result. Clearing app
data/uninstall removes it; export and storage-error recovery remain future work.
Existing unscoped records are retained without inferred Atlas or local identity.

Optional delivery requires a per-match explicit `(atlasId, profileId)` mapping
for every player, validated locally and by Atlas's import domain. Connected
state remains Atlas-owned. No new dependency or gameplay Intent is needed;
the import form contract/validator changes together. Android renders text
statuses and labeled player picks in scrollable layouts; local controls use
existing TableControls. TalkBack, text-scale and phone/foldable acceptance must
be recorded separately from JVM tests and builds.

Local schema 1 uses JSON object fields `schema`, `players` (localId/name),
`history` (immutable GameRecord objects) and `deliveries` (recordId/status/reason/
atlasId/profileIds in immutable player order). IDs are UUID strings; dates and
clocks are Unix/integer milliseconds. JSON null marks unknown legacy identity.
Unknown schema, malformed records or duplicate local/match IDs fail closed.
Android platform backup may include these preferences under existing app backup
rules, but no TurnHub export/restore guarantee is offered. Every local game edit
currently serializes the library and commits preferences synchronously; very
large histories need performance/storage work before a release guarantee.

### Platform and control presentation boundaries

The profile vault checks Android 11 at its key-generation API boundary as well
as exposing availability; its authentication policy and stored format are
unchanged. Haptics use typed click/thud primitives, check each required motor
capability, and retain the existing predefined-click/pulse fallbacks. The
connected-player control transition renders its target table/player snapshot,
keyed by control stage so ordinary polls do not start another stage animation.
Outgoing-stage controls are disabled while the next stage is shown. These
presentation paths still send existing Atlas actions and require physical
app-lock, haptics, motion and assistive checks before device acceptance.

### App-first play cutover (2026-10-10)

Atlas 0.7.7 (with the embedded admin portal) retires browser gameplay and browser tablet/statistics.
Android polls state and seats with the current session token, or `?game=N` while
signed out. It parses Atlas’s existing `game`/`games` fields and offers the venue
selector in personal and shared-tablet views. Atlas validates switching; personal
players cannot leave an active game, and tablet switches leave their players in
place. Game changes rebuild name/avatar presentation and reset staged controls;
responses from changed polling contexts never publish. Tablet queued edits are
scoped by game, boot and participant. `expectedGame` protects raw table actions
from a session switch racing a queued request. No new gameplay owner or dependency.

The reachable Game setup now accepts custom 15–3600-second timers, saved explicitly.
Normal signed-in players can refresh paired Sigils and request attachment of an
empty seat A in the lobby, confirmed with Link phone. Atlas still rejects occupied
or unavailable seats. The Players invitation QR installs the app; joining Atlas
Wi-Fi and choosing Connect remain explicit steps. There is no password in the QR.

Native vibration preferences and a background win-decision notification remain
planned. Foreground game-action sounds, existing native haptics, turn notifications and
text decision controls remain; device acceptance is tracked in STAGED_CHANGES.

Atlas firmware includes the complete admin portal and its maintenance/Developer
assets. The update assistant lists only Atlas and Sigils, never queries portal
installation or card status, and ignores retired `portal` release entries.
