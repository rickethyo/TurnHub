# Portal versus Android feature audit

Date: 2026-10-09. Author/owner: Codex. Branch: `codex/master`.
Baseline: `5e067f7`, plus the staged tablet-overlay/signup fixes from TH-011.

The owner confirmed the planned direction: **web portal for administration,
Android app for play**, including shared tablet play. This audit identifies
what must move, what already exists, and what can remain in the admin portal.
No gameplay pages, APIs or app features were removed or implemented here.

Evidence is **Reconstructed from source**, not device acceptance. The audit
examined full portal pages and the flash basic fallback, Android UI entry
points/callback wiring, repositories/transports, protocol models and Atlas
handlers. Finding an endpoint or an unused UI component does not establish a
reachable app feature. No new builds/tests were needed for this documentation
slice; previous TH-011 results do not establish Android parity on devices.

## Confirmed differences

| Feature | Current portal | Reachable Android app | Recommended disposition |
|---|---|---|---|
| Select/follow Game 1 or Game 2 | Main portal lists both games with state/player counts; signed-in switching uses `/api/session/game`, signed-out viewing uses `?game=N` | No game selector, session-game call or equivalent game-scoped polling path | Move before retiring web play; include personal and shared-tablet selection |
| Enter a custom turn duration | Game setup has Custom seconds, 15–3600, alongside presets | Current GameTab setup offers presets plus an already-configured custom value; cannot enter a new arbitrary duration | Move into active game setup before retirement |
| Attach an unseated paired Sigil from the phone | Paired-device cards offer Attach seat A to my profile in the lobby, even when the device has no table seat | PlayersTab offers Use this seat only for physical players already in the roster; Settings device cards offer administration, not attachment | Add a normal-player paired-device picker and attach action; reuse existing physical confirmation |
| Player invite QR generation | Invite players displays portal and sign-in QR codes | App displays instructions/portal address and refers to Atlas's QR; it scans Admin presence codes but does not generate player invitations | Replace with app-oriented invite/install/connect flow; retire old portal-play links |
| Configurable local sound and vibration feedback | Browser sound on/off, vibration on/off, volume Low/Medium/High, test feedback; tones for turn, decision, pause/resume and game over | Themes/reduced motion, action/turn/nudge haptics and Android turn notification exist; no matching in-app event-sound, volume or vibration preferences, or win-decision notification alert | Define native feedback defaults/settings; prioritize a clear decision-needed alert |
| Device hardware tests | Developer page selects Atlas/Sigil and starts screen, buzzer and Sigil light tests | Developer UI has activity/raw JSON/log sharing, but no hardware-test action or `/api/device/test` client call | Keep in web administration/diagnostics; optional later app convenience |
| Upload a local signed firmware/portal package | `/update` accepts a selected `.thfw` for Atlas or portal; Sigil update page accepts a local package and lets the admin choose one compatible device | Update assistant downloads release-feed packages and uploads them; no local file picker/manual signed-package installation UI | Keep in admin web as recovery/development path; optional later app support |
| Choose one Sigil to update manually | Web Sigil page explicitly selects a target | App's release-update flow constructs a batch of outdated devices; no equivalent arbitrary local-package target picker | Keep in web administration unless per-device app maintenance is requested |

The first three are the clearest gameplay capability gaps. Invites need a
cutover redesign because their current destination becomes administration.
Feedback is partial parity, not a complete absence of turn/vibration support.
Hardware tests and local update uploads need not delay gameplay retirement if
they remain usable in the admin portal.

### Source evidence for the gaps

- Game selection: [portal index](../../../Atlas/web/src/index.html),
  `renderGameSwitch`, `forGame` and the `/api/session/game` handler;
  [Atlas session handler](../../../Atlas/src/web_session.cpp),
  `handleSessionGame`. No corresponding app request/UI was found. Current
  `TableSummary`/state polling does not supply a selectable list of games.
- Custom timer: [GameTab](../../../Android/app/src/main/java/com/turnhub/android/ui/home/GameTab.kt),
  `SetupCard` builds a dropdown from presets and the current value.
  [PlayerPanelCard](../../../Android/app/src/main/java/com/turnhub/android/ui/home/PlayerPanelCard.kt)
  contains `TurnTimerSetting` with custom input, but the production UI has no
  call to `PlayerPanelCard`; its view-model setter is not wired from MainActivity.
  The data-layer game-settings action already accepts a timer value.
- Empty Sigil attachment: portal `renderDevices` supplies the action;
  [PlayersTab](../../../Android/app/src/main/java/com/turnhub/android/ui/home/PlayersTab.kt)
  derives claim buttons from `summary.players` only. `SettingsTab::DeviceRow`
  has Rename/Forget/Factory reset, with no attach action.
  [AtlasPlayerSession](../../../Android/app/src/main/java/com/turnhub/android/data/AtlasPlayerSession.kt)
  already implements `claimSeat(module, slot, ...)`, so much of the request
  path exists. Pairing and selecting the profile directly on the Sigil remain
  alternatives, but do not replace the portal's phone-side workflow.
- Invites: portal `renderInviteCodes` generates `/portal` and `/login` URLs.
  PlayersTab has explanatory text only; the app's `QrScanner` is a decoder.
  AndroidManifest currently declares only launcher navigation, not an app-join
  deep-link handler. An app invitation needs a deliberate payload/navigation
  design, not a copied web-login QR.
- Feedback: portal `saveBrowserPrefs`, `playFeedback` and
  `processBrowserFeedback`; app `AccountTab::AppearanceCard`, `Haptics.kt`,
  `GameTab::StageCard`, `LiveTurn` and `TurnNotifier`. The notification models
  turns/paused state, not a win-confirmation-needed alert. System notification
  channel controls are distinct from the portal's foreground event settings.
- Hardware tests: [web Developer page](../../../Atlas/web/src/dev.html),
  `runHardwareTest`; [app DevTab](../../../Android/app/src/main/java/com/turnhub/android/ui/home/DevTab.kt)
  and `AtlasAdminConsole` offer monitoring and logs only.
- Local updates: web `update.html` and `sigil-update.html` file inputs;
  app `SetupScreen` and `AtlasSetupAssistant` implement release discovery,
  download, Atlas/portal upload and Sigil batches, with no file-selection path.

## Features already represented in the active app

“Present” below means reachable UI and request wiring were found in source;
it does not claim successful physical-device testing.

| Area | App coverage found | Evidence / important limit |
|---|---|---|
| Account registration/sign-in/logout | Present, including PIN/password and first-secret confirmation | SignInDialog, HomeViewModel, AtlasPlayerSession; Fold scrolling mitigation is staged |
| Phone participation and controls | Join/leave, starter selection, start/cancel countdown, pass/cancel pass, pause/resume, claim/confirm/deny win, concede, rematch/reset, nudge | GameTab, ControlAction, MainActivity/HomeViewModel wiring and session transport |
| Life totals and edits | Own preset/custom edits, other-player requests, accept/reject and outgoing status | GameTab life/request controls; same authoritative Atlas operations |
| Commander damage | Source and commander 1/2 selection, signed corrections, life coupling, displayed totals | GameTab CommanderCard and counter requests |
| Game setup | Profile, starting life, Two-Headed Giant, timer presets/current custom value | GameTab SetupCard; arbitrary new timer input is the gap above |
| Roster and game presentation | Names, avatars, life, active turn/timers, passing, elimination, winner, team grouping and Commander totals | GameTab/PlayersTab and TableSummary |
| Existing physical seat linking | Use this seat with Link phone approval, A/B slots, existing-profile sign-in | PlayersTab, SeatActions, AtlasPlayerSession; empty paired-device selection is missing |
| Shared tablet access/seating | Enable with role/presence grant, add new/saved players, PIN prompts, unseat, earlier/later ordering, profile/life/team setup | TabletScreen, AtlasTablet |
| Shared tablet play | Oriented player/team panels, life, Commander damage, pass/cancel, pause/resume, win claims, confirmations, concede, rematch/reset | TabletTable; claim overlay fix staged; no multi-game selector |
| Personal profile/privacy | Rename, set/remove secret subject to rules, physical-without-PIN policy, stats privacy | AccountTab/AtlasPlayerSession |
| Player personalization | Preset avatar and custom Sigil seat color/standard colors | AccountTab, personalization transport |
| Sigil accessibility | Sound, LED style, hold thresholds, life-approval window | AccessibilityDialog and session transport |
| App appearance | Automatic/Graphite/Daylight/Brass/Contrast and reduced motion | AccountTab/Theme; local feedback preferences are separate missing controls |
| Profile statistics/export | Lifetime, turn/time records, last game, private moderation history and JSON sharing | StatsSheet/ProfileStatistics, exportStats, MainActivity shareStats |
| Game Master tools | Force pass, connection reset, removal, nudge mute subject to sub-permissions | PeopleSheet, AtlasAdminConsole, MainActivity callbacks |
| Account administration | Roles, initial Admin protection, archive/restore | PeopleSheet, AdminConsole |
| Atlas/device administration | Presence code and QR scan, table-code switch, network password, paired device names/forget/reset, pairing decisions, speaker/pair window, table reset/factory reset | SettingsTab/AdminConsole |
| Setup and release updates | Initial-Admin onboarding, Wi-Fi setup, release-feed Atlas/Sigil/portal updates, restart/reconnect | SetupScreen/AtlasSetupAssistant; local file upload differs |
| Diagnostics | Activity feed, status/devices/seats/runtime panels, serial-log sharing | DevTab/refreshDeveloper; hardware tests differ |

The app also adds standalone local play, saved local players/history and
optional finished-match import, automatic network joining/sign-in and turn
notifications. These are not portal capabilities that need migrating.

## Similar workflows that do not require exact copies

- Portal seat-based PIN login has an app equivalent: select the seat owner's
  profile and sign in; existing participation follows that profile. A dedicated
  seat-specific PIN dialog is convenience, not a missing authorization path.
- Portal statistics downloads a JSON file; the app shares the same export
  response through Android. If a guaranteed local Save file action is desired,
  add it deliberately; basic export capability is already present.
- Portal offers additional ±10 life shortcuts in some formats; the app's custom
  delta supports the same value, so this is a speed-of-entry difference.
- Portal paired-device cards expose more metadata (age/capability details).
  The app has an admin device list/raw diagnostics but not an equivalent
  normal-player device browser. Add the needed player-facing metadata with the
  empty-Sigil attachment flow instead of exposing admin controls to players.
- Both shared tablet screens omit the main portal's custom timer editor; the
  app's personal Game setup must be complete before that portal editor goes.

## Shared gaps, not features lost from the portal

Seat B attachment to an already-seated tablet profile, controller release
after elimination, poison/tax/general counters and gifted extra turns are not
complete in either client. They stay on the play-note backlog, with Android
as the target play UI. Likewise tablet-only account semantics, richer tablet
artwork and re-entry require the separately staged work; do not count them as
existing web capabilities merely because designs or endpoints are mentioned.

## Cutover plan

1. Finish app game selection and scope all polling/actions to the selected
   table. Respect Atlas's profile-in-one-game rules; handle tablet sessions
   separately. Isolate state/name/queued-action caches when changing tables.
2. Add custom timer entry to the active setup card and paired-Sigil attachment
   to the normal Players UI. Keep Atlas validation/physical confirmation.
3. Redesign player invitations and notifications/feedback for app play. Verify
   the staged Fold and win-claim fixes, teams, shared seats and two games.
4. Replace both SD and flash portal gameplay with Admin administration while
   retaining setup/bootstrap, diagnostics, hardware tests and manual signed
   updates/recovery. Specify Developer versus Admin visibility explicitly;
   current diagnostic API permissions must not silently broaden.
5. Retire `/tablet`, `/stats` and personal gameplay/profile pages after their
   app paths are accepted. Keep administrator sign-in and every HTTP API the
   app needs, including `/api/tablet/*`. Replace old invitations/QR links and
   manual copy. Check installed/cached portal packs during cutover.

Acceptance should verify each surviving web-admin feature plus all migrated
app flows on devices. This audit documents the source comparison; removal and
implementation are separate reviewable work.
