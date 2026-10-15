# Project-wide Codex / Claude collaboration

This is the permanent shared conversation and handoff document for **all of TurnHub**: Atlas, Sigils, Android, protocols, hardware, PCB and enclosure design, testing, releases, documentation, and product decisions. It is not limited to one feature or review.

Scope confirmed by project owner: 2026-10-08.

## Working agreement

- Persistent branch workflow adopted by the owner, 2026-10-09: Codex uses
  `codex/master`, Claude uses `claude/master` and `master` is the accepted
  baseline. Owner, 2026-10-13: `agent/experimental` is retired, and the current
  ESP32 boards are maintained on the one-way `legacy/gen1` branch (Gen 1 fixes
  only, never merged with master); master is the LVGL/S3 track.
  See [Agent workflow](Documentation/engineering/AGENT_WORKFLOW.md) for the
  single source of branch, PR, merge and synchronization rules.

- Refer to the project owner by role, without their personal name, in document text and author metadata (owner, 2026-10-08).

- Read this document at the start of project work and fetch current Git state before editing. Read CLAUDE.md and relevant engineering references as well.
- Both Codex and Claude may add topics, ask questions, respond, record disagreements, and hand work to the other. Neither is assumed to have approved the other's proposal.
- Give each discussion a stable ID and title. Include date, author, reviewed commit, status, affected areas, evidence, next action, and any blocker.
- Append clearly attributed replies. Preserve the other agent's statements; explain corrections rather than silently rewriting them. Update the topic index as status changes.
- Before implementation, record the intended scope and current owner to reduce conflicting work. An ownership entry is coordination, not a lock; check branches and current work before editing overlapping files.
- Documentation-only updates should not run CI (project owner, 2026-10-08). Use `[skip ci]` in documentation-only commit messages; do not skip CI for code or mixed changes.
- Record implementation commits and actual validation results at handoff. Distinguish source review, host tests, and device acceptance. Never claim tests or hardware checks that did not happen.
- This document does not grant permissions or supersede project owner's directions. Follow the repository's feature gate and architectural rules.
- Git is the shared transport. Updating this file does not automatically notify or run either agent. project owner can ask either agent to read and reply here.

## One home per fact

This file owns cross-agent discussion and coordination. Accepted unfinished engineering work belongs in Documentation/engineering/STAGED_CHANGES.md. Current behavior belongs in the relevant topic document; user-facing changes belong in the manual. Link those sources rather than maintaining competing backlogs here.

Keep the topic index and active discussions here. Prune by the documentation hygiene rule in [Agent workflow](Documentation/engineering/AGENT_WORKFLOW.md#documentation-hygiene): when a topic closes or its outcome lands in an owning document, move its detailed exchange to `Documentation/engineering/history/` and keep at most a one-line outcome here. Do not archive unresolved questions just to shorten the file.

## Topic index

Open topics only. Closed topics (TH-004 to TH-011, TH-013 to TH-016) and the
session logs up to 2026-10-13 are in the [archive](Documentation/engineering/history/2026-10-13-collaboration-archive.md).

| ID | Topic | Areas | Status | Next action / owner |
|---|---|---|---|---|
| TH-023 | Atlas parity with the standalone app | Atlas, protocol, Android connected mode | Rounds 1 (#113) and 2 ready for review; host tests pass | Owner / flash, play and review |
| TH-022 | Native game-action sounds | Android | Ready for review; CI passes | Owner / PR #107 and device acceptance |
| TH-021 | Shared table authority design | Android, hosting design | UI integrated; common engine/authority staged | Owner / design implementation and device acceptance |
| TH-020 | Star Commander game mode | Atlas, protocol, Android, stats | Implemented on `claude/master`; host and JVM tests pass | Owner / interpretations below, playtest G14 |
| TH-019 | iOS foundation and development flow | iOS, contracts, CI | Active framework slice | Codex / scaffold and staged plan; owner / Apple device acceptance |
| TH-018 | Phone-owned profiles and lighter Atlas requests | Android, account import, documentation | Implemented; local validation passes | Owner / device acceptance and PR review |
| TH-017 | ESP32-S3 hardware and LVGL foundation | Hardware, BOM, LVGL | Master is now this track (2026-10-13) | Board drivers and snapshot/Intent adapters; see STAGED_CHANGES |
| TH-012 | Admin portal / app-play feature parity | Portal, Android, product scope | Direction agreed; source audit complete | Owner / review migration priorities |
| TH-001 | App-only player experience | Android, contracts | Local players/history/import implemented | Owner / physical acceptance |
| TH-002 | Shared tablet access and dedicated account type | Android, Atlas, identity, authorization | Agreed requirements; not implemented | Define capability allowlist and scope; unassigned |
| TH-003 | High-resolution player artwork and fuller tablet UI | Android, assets, profiles, storage | Implemented in PR #86 | Owner acceptance; LCD layouts planned |

## New topic / reply template

### TH-NNN: Topic title
- Date / author:
- Reviewed branch and commit:
- Status: proposed / active / blocked / ready for review / resolved
- Scope and affected areas:
- Findings and evidence:
- Proposal or question:
- Decision and rationale, if agreed:
- Implementation owner / branch / commits:
- Validation completed and remaining:
- Next action / intended responder:

Append replies under the same topic with date, author and reviewed SHA. Record a final outcome and links when resolved.

## TH-023: Atlas parity with the standalone app

- Date / author: 2026-10-14 / Claude. Baseline `master` 5a87a5b (#112).
- Scope: port the standalone app's #109-#112 rules to Atlas so connected games
  match. Round 1 (`claude/master`): official rules per mode with a House rules
  unlock (`OfficialRules`/`applyOfficialRules` in `game_profile.h`, enforced by
  `ConfigureGame`, Start and `GameEngine::start`), no Commander damage between
  Two-Headed Giant teammates, a random starter when nobody is chosen, and
  choosing the chosen player again clears it; the connected tablet lobby and
  the phone's Game setup get the House rules switch. Round 2 (stacked branch):
  Back to setup in the first three minutes, and games under a minute counted
  only when the table confirms.
- Host scenarios run with `randomStarterByDefault=false` so player 1 still
  starts; `officialRulesAndStarter()` checks the random start.
- Round 2 (`claude/atlas-standalone-parity-wmmxaj`, stacked on round 1):
  `BackToSetup` and `CountShortGame` Intents (`back-to-setup`, `count-game`),
  state `shortGameUncounted`; the bridge holds statistics for matches under
  `SHORT_GAME_MS` (host scenarios set `shortGameMinimumMs=0`).
- Validation: see the PRs; host tests only, no hardware or device pass yet.

## TH-022: Native game-action sounds

- Date / owner: 2026-10-14 / Codex on `codex/master`, baseline `25ce417`.
  Clean checkout, fetched and fast-forwarded accepted master; PR #106 is merged,
  with no open PR or overlapping audio scope. A new PR is needed for this slice.
- Scope: short foreground sounds for confirmed pass/start/pause/resume/game-over
  transitions, shared by local and connected play. Repeated polls, initial load,
  screen changes, reconnect and rejected actions must stay quiet.
- Feature gate: Android owns disposable cue detection and phone-only sound/volume
  preferences in `turnhub_ui`; existing engines still validate game actions.
  No Intent or protocol change. Native SoundPool plays original generated WAV
  assets; no third-party dependency. Standard labeled mute/volume controls in
  setup, table menu and My account; sounds supplement existing text/haptics and
  respect phone silent/vibrate mode and media volume. Lifecycle cleanup stops
  foreground cues. Device/audio/TalkBack acceptance is separate from JVM tests.
- Implementation: one foreground cue observer shared across local and connected
  screens, native SoundPool lifecycle cleanup, six original generated clips and
  saved sound/volume controls. Seven JVM regressions cover actual local actions,
  Atlas pending/committed passes, team/missed turns, replay suppression and epochs.
- Validation: [CI on `5549d06`](https://github.com/rickethyo/TurnHub/actions/runs/38492919879)
  passes JVM tests and release bundle compilation, plus relevant host/manual/
  design guards. Clip regeneration, mono PCM/duration/silent-edge/no-clipping,
  manual export, documentation links and whitespace checks pass locally.
  [PR #107](https://github.com/rickethyo/TurnHub/pull/107) is ready for review.
  No physical speaker, silent/media-volume, lifecycle or TalkBack acceptance
  is claimed. Next owner: review and device checks.

## TH-021: Unified table setup, tile backgrounds and authority design

UI/background changes are integrated through PR #106. The [authority design](Documentation/engineering/TABLE_AUTHORITY.md) and STAGED_CHANGES own future common-engine/takeover work; implementation evidence is in the [archive](Documentation/engineering/history/2026-10-13-collaboration-archive.md).

## TH-019: iOS foundation and development flow

- Date / owner: 2026-10-13 / Codex on `codex/master`; accepted baseline
  `9fa32bbd83824c2905b6be31524125b2e6239030`. Clean checkout, fetched master
  and agent refs, no open PRs or overlapping active iOS scope.
- Owner authorizes an iOS framework, with emphasis on architecture and the
  development flow. Scope: native SwiftUI iPhone/iPad shell, a local Swift
  package for non-UI code, metadata/endpoint contract tests, unsigned simulator
  CI and a milestone plan. No gameplay, live network controls or distribution
  in this slice. Android and firmware implementations remain separate.
- Feature gate: iOS owns shell navigation only; Atlas owns connected game
  state and validates existing form-based controls. The future standalone
  iOS domain owns its separate local matches/players/history. No game Intent
  or persistence is implemented here. SwiftUI renders the shell; the shared
  HTTP contract is unchanged. Apple frameworks/SwiftPM only, no third-party
  runtime library. System controls support VoiceOver and Dynamic Type;
  every forthcoming screen needs explicit accessibility acceptance.
- Validation: documentation hygiene and whitespace checks pass. Info.plist,
  scheme XML and workflow YAML parse; all 25 Xcode project objects and their
  file/package/scheme references validate. The canonical fixture path resolves.
  Linux workspace has neither Swift nor Xcode; Swift tests and simulator build
  run in the PR's macOS CI, with results recorded in its checks/handoff. Physical
  iPhone/iPad and VoiceOver acceptance remain open.
- Next action: finish the framework and plan, then start the standalone lobby
  slice after review. Signing, TestFlight and App Store submission remain owner
  decisions; no account credentials or release secrets required for foundation.

## TH-001: App-only player experience

Outcome: local players, history and import are implemented (Codex, 2026-10-08;
review, slices and validation in the [archive](Documentation/engineering/history/2026-10-13-collaboration-archive.md)). Remaining: physical
acceptance; Atlas-scoped profile identity is staged in STAGED_CHANGES.

## TH-002: Shared tablet access and dedicated account type

### 2026-10-08 / Codex / recording project owner's requirements

- Reviewed baseline: master `782e23b931542ddd479ba18a258bf4ad50f71316`.
- Status: agreed requirements, not implemented. No runtime validation performed.
- Scope: seated players must be able to re-enter tablet mode during a game;
  a distinct PIN-free Tablet-only account type must have no seat and no own
  statistics, with only tablet-operation settings access; shared tablet life
  changes must not require approval.
- Canonical requirements and acceptance checks: [Staged Changes, Table,
  controllers and accessibility](Documentation/engineering/STAGED_CHANGES.md#table-controllers-and-accessibility).
- Implementation owner: unassigned. Claude or Codex should record ownership
  and branch before starting.
- Next design task: map the exact tablet operation allowlist, including game
  setup, to the existing server authorization and Intent paths. Define how a
  seated player's shared-tablet session gains the same direct-life capability
  without allowing ordinary personal requests to bypass approval. Define who
  provisions/selects the PIN-free account and its table scope. These design
  details remain open; the requested account type and behavior are agreed.
- Feature gate before implementation: Atlas owns account type and connected
  game state; existing game Intents remain the mutation path; server validators
  enforce type/capability/table scope; account storage owns persisted type;
  clients render permitted controls; update account/session contracts and
  fixtures as needed; no new dependency assumed; provide accessible re-entry
  and clearly labeled shared-screen controls.
- Handoff evidence: documentation only. Update identity/tablet documentation
  and the user manual when behavior lands, and record actual authorization
  tests and device checks here.

## TH-003: High-resolution player artwork and fuller tablet UI

### 2026-10-08 / Codex / recording project owner's requirements

- Reviewed baseline: master `953ce303359ec32cf941404c26d6bcd896a9978a`.
- Status: planned, not implemented. Owner requests uploadable high-resolution
  avatars, tablet-mode card backgrounds, and to "de minimize" tablet mode.
- Canonical scope: [Staged Changes, Accounts, portal and app](Documentation/engineering/STAGED_CHANGES.md#accounts-portal-and-app).
- Working interpretation of the last phrase: visually richer player cards with
  more useful information exposed, rather than the current minimal presentation.
  Exact layout and information density remain open for a visual design review.
- Preserve the existing public-avatar Admin approval requirement. Do not mistake
  the new PIN-free tablet-only account for a player profile or grant it general
  profile/asset administration implicitly.
- Next action / owner: unassigned. Propose the upload, crop, storage, derivative
  and cache pipeline plus a tablet mockup. Account for standalone local players
  and the existing rule that SD is optional for play.
- Feature gate to complete before implementation: Atlas owns connected profile
  asset metadata and authorized uploads; Android owns standalone local assets;
  define upload/replace/remove/approve operations and their server validators,
  persistence and cleanup ownership, rendering clients, asset API/contract,
  dependency/legal impact, and contrast/TalkBack/large-text behavior.
- Validation: documentation only; no code, image pipeline or UI changes tested.

## TH-012: Admin portal / app-play feature parity

- Date / author: 2026-10-09 / Codex; branch `codex/master`; baseline `5e067f7`
  plus staged TH-011 UI fixes. Scope: source audit/documentation only; preserve
  those staged changes. Reused this session's fetched refs/open-PR evidence.
- Owner confirmed the direction: web portal for Admin administration and app
  for play, including shared tablet play. Supersedes TH-011's provisional scope.
- Status: source audit complete; [parity report](Documentation/engineering/history/2026-10-09-portal-app-parity.md)
  staged locally. No runtime change, commit, push or new PR for this audit.
- Main gameplay gaps: game/table selection, arbitrary new custom timer input
  in the reachable setup UI, and phone-side attachment of an empty paired
  Sigil. Invite generation and configurable native event feedback also differ.
  An old custom timer component exists but is not mounted by the production UI.
- Admin-only differences: hardware test buttons and manual signed-package
  upload/targeted Sigil updates can remain web-admin capabilities. Core play,
  tablet panels, account preferences, statistics/export, moderation, settings,
  release updates and diagnostic monitoring have active app implementations.
- Validation: inspected portal/full and flash fallback UI, active app screens,
  callback wiring, data paths and Atlas handlers; documented relative source
  links and ran whitespace/link checks. No new build/device test claimed.
- Next owner: project owner reviews the migration order. Implementation should
  close the identified gameplay gaps and verify device flows before removal;
  preserve the app's HTTP API and initial setup/cardless update recovery.

## TH-017: ESP32-S3 hardware and LVGL foundation (numbered TH-015 on its branch)

- Date / owner: 2026-10-11 / Codex. Baseline `01bfe9c3be2b51b7de38d49a9cbdea4d43933db1`, latest fetched `origin/codex/master`; no open PRs or overlapping unintegrated scope found.
- Branch: `codex/esp32-s3-lvgl`, explicitly requested dedicated scope. User direction overrides normal persistent-branch/master-merge startup; neither master nor codex/master is modified. Existing checkout was clean; all history preserved.
- Scope: complete hardware/BOM planning first, then an isolated desktop LVGL UI foundation. Legacy Atlas/DevKit Sigils, PlatformIO, radio, OTA, engine, profile/statistics and CAD assets are preserved.
- Hardware research: [board/electrical/mechanical record](Documentation/engineering/hardware/ESP32_S3_REFRESH.md), [prototype BOM](Documentation/engineering/hardware/PROTOTYPE_BOM.md), primary-source digests. Documented schematic/page conflicts and unsupported $1.80 price; no physical acceptance claimed.
- Status: completed first desktop milestone. Hardware/BOM `76a447c`, architecture/feature gate `addf22d`, working simulator `2857474`. Validation/previews/handoff follow in the final milestone on this same branch.
- Implemented: [LVGL simulator](LVGL/README.md), both profiles, common Inter/Cinzel and theme palettes, existing 64px presets, disposable presentation state/action sink, SDL pointer/five-way input, life requests, player/theme menus, mock pairing and deterministic timed/paused/game-over fixtures. No production hardware or transport bindings.
- Validation: Linux Debug + source-pinned Release builds; 2/2 CTest suites on both; both X11 event-input suites; 2/2 ASan/UBSan suites with leak detection disabled; 14 SDL/Xvfb captures; font/token/avatar digests, 31 adapter guards and whitespace passed. [Previews and evidence](Documentation/engineering/history/2026-10-11-lvgl-previews/README.md). Windows and all physical checks remain untested.
- Handoff: use `LVGL/README.md` to build/launch; hardware holds and remaining migration work are owned by STAGED_CHANGES and linked topic docs. Next development: supplier/sample qualification, power/connector verification, then separate board drivers and production snapshot/Intent adapters. No merge, release or hardware flash. Push this feature branch only.
- Git completion follows AGENT_WORKFLOW step 4: one reviewable PR to master is required by repository conventions, permitted by the user's exception; no integration/release.

## 2026-10-13 — Claude: Gen 1 split onto `legacy/gen1`; master becomes the LVGL/S3 track

- Owner direction (2026-10-13): keep the current ESP32 Atlas/Sigil firmware on
  an isolated branch where Gen 1 bugs can be fixed in place, and keep master,
  `codex/master` and `claude/master` free of it. `agent/experimental` is
  unused and retired (owner deletes the ref). Codex and Claude keep separate
  branches.
- `legacy/gen1` was created from master `70d378b`; its one setup commit points
  CI at itself, drops the agent-branch workflow and adds README/AGENTS banners.
  It never merges with master and is not in the maintenance allowlist.
- Master cleanup PR (branch `claude/project-thread-17ttlk`): removes
  `TestHarness/`, `KiCad/`, `3D/`, the e-ink/OLED/spare/bench/Wokwi Sigil
  builds and drivers, the Atlas harness link and Tests screen, the harness
  packets and capability bit, the flash/board-setup scripts and `release.yml`.
  Sigil is one `sigil` env with a headless `SigilDisplay`. CI drops the
  sigil-oled/sigil-spare jobs and Sigil previews and adds an `LVGL simulator`
  job.
- Owner follow-up, same PR: master is board-less. The Atlas E32R28T layer
  (TFT, touch calibration, Brass art and fonts, speaker, battery ADC, screen
  previews) is replaced by `atlas_board_none.cpp` behind `atlas_board.h`; the
  Sigil's pins moved to `Sigil/include/sigil_board.h`; the spare pairing path
  (`CAPABILITY_SPARE` 0x40, `PairRequestSpare` 52) is gone, which also removes
  the only pairing that skipped the code check. Left for the S3 step because
  the Android update flow reads them: the OTA product names (`sigilEink`,
  `sigilOled`), `CAPABILITY_DISPLAY_OLED` and `TURNHUB_OTA_RETIRED`.
- Codex: rebase or merge any open `Sigil/` display or TestHarness work onto
  `legacy/gen1` instead of master.
- Same PR, owner request 2026-10-13: documentation hygiene rule in
  AGENT_WORKFLOW.md ("Documentation hygiene"), `tools/scripts/check_docs.py`
  in CI (dead repo links, unmarked retired paths), and this file pruned to open
  topics; the full previous text is in
  `Documentation/engineering/history/2026-10-13-collaboration-archive.md`.

## TH-018: Phone-owned profiles, background Atlas discovery and lighter polling

- Date / owner: 2026-10-13 / Codex on `codex/master`; baseline `946ef2f`.
- Scope: immediate local launch, quiet discovery, standalone accounts and
  personalization, full retained match/statistics data, explicit Atlas account
  creation/linking and automatic imports for verified identities; reduce redundant
  presentation requests. No overlapping open PR found; clean startup, fetched
  refs, fast-forwarded agent branch to accepted master.
- Feature gate: Android owns standalone identities/history/preferences; Atlas
  owns connected identities, validation, game Intents and import credit. Preserve
  UUID local identity and Atlas-scoped verified links, never infer ownership from
  names or an unverified 32-bit ID collision. Atlas accepts an unused requested
  profile ID at registration without granting existing-account access. Existing
  accounts require login before linking; match delivery freezes mappings and IDs.
  No new dependency. Accessible text controls, bounded image crops, optional
  Atlas entry; no Wi-Fi/permission dialogs from automatic discovery.
- Status: implemented. Always-local cold entry (including process recreation),
  prompt-free current-network probe, optional local credentials, personalized
  accounts, full lifetime/last-game statistics and retained counter snapshots.
  Verified account links enable automatic match delivery; duplicate public IDs
  cannot overwrite Atlas accounts. Imports preserve active login; if unsigned,
  the just-verified account signs in for delivery. No Gen 1 changes or flashing.
- Validation: Android debug build, unit tests and lint pass (zero lint errors;
  existing warnings remain). Atlas board-less 0.7.15 firmware builds, all four
  Windows GCC host suites, 20 client-contract cases and 31 adapter guards pass.
  Metadata/lifecycle cadence and identity/security/storage failure regressions
  cover the new behavior; 254 Android tests pass. Documentation
  hygiene and Manual V0.19 asset check pass. Manual render attempt failed because
  LibreOffice is unavailable; source/export integrity checked, no visual QA claimed.
  Original integrated V0.18 retained in the manual archive.
- Continuation: owning behavior in `Android/README.md`, `protocol/http-v1.md`
  and `DIAGNOSTICS.md`; remaining device/performance acceptance in STAGED_CHANGES.
  Request-count reduction is validated; Atlas CPU/heap/SD/radio improvement is not
  yet measured. Existing bounded import receipts/partial-credit limitations remain.
  MSVC host runner hit pre-existing GNU-attribute incompatibility; supported GCC
  runner passed instead. Keep `codex/master` after integration with a merge commit.

## 2026-10-13 — Claude: standalone setup shows the table

- Owner feedback: the lobby's player list was hard to match to where the
  panels land; a small version of the table with drag-and-drop seats was
  suggested.
- The lobby now shows `SeatMap`, a miniature of the full-screen table using
  the same `placements()` and orientation rule: each player's panel in its
  seat, turned the way it will face, in its seat color and numbered in turn
  order. Drag one panel onto another to swap them, or tap two panels; screen
  readers get a "Swap with" action per seat. The list's turn-order arrows are
  gone; the list keeps adding and removing. `StandaloneGame.swapPlayers`
  (lobby only) backs it, with a unit test.
- Validation: `testDebugUnitTest` passes locally. Needs an owner device pass.

## 2026-10-13 — Claude: ask to turn on Wi-Fi before joining Atlas

- Owner bug: with the phone's Wi-Fi off, connecting to Atlas just failed.
- Connect, Set up a new table and Try again now check `AtlasWifiLink.wifiEnabled()`
  first. When Wi-Fi is off the app asks "Turn on Wi-Fi to connect?". Turn on Wi-Fi
  opens Android's Wi-Fi panel (apps can't switch it on themselves from Android 10)
  and the same connection carries on once Wi-Fi is on, waiting up to 60 s;
  otherwise it shows `AtlasFailure.WifiOff`. Not now closes the question. The
  quiet launch probe still never prompts.
- Validation: `testDebugUnitTest` passes locally with two new `HomeViewModelTest`
  cases. Needs an owner device pass.

## 2026-10-13 — Claude: full-screen table parity with and without Atlas; short panels

- Owner asked for parity between the full-screen table with and without Atlas.
  Both run the same `TabletTable`; the setup screens differed. Both lobbies are
  now "Full-screen table" and share `TableSetup.kt`: the seat map, turn timer
  presets, the zero-life elimination switch and the big start button. With Atlas,
  the turn timer goes through `/api/tablet/settings` (`turnTimerMs`, already
  parsed by `parseGameSettings`). A seat-map swap is sent as Atlas's one-place
  `move-later`/`move-earlier` steps. The list's turn-order arrows are gone in both.
  Atlas-only extras stay: Sigil tags, accounts, Two-Headed Giant.
- Owner screenshot: with five players on a phone, Hold to pass and Menu covered
  the life total. Short panels (under 260 dp) now size the number to the room
  between the name and a slim bottom bar, the Menu pill keeps only its icon, and
  Hold to pass stays on one line. The change pill floats over the number instead
  of taking a row.
- Validation: `testDebugUnitTest` passes locally. Needs an owner device pass
  (with Atlas, the seat map's swap is untested on hardware).

## 2026-10-13 — Claude: game mode picker; Two-Headed Giant without Atlas

- Owner: Two-Headed Giant is a game modification, not an Atlas feature; game
  modes belong in a picker so more can be added.
- Both full-screen setups now show **Game mode** (Standard, Two-Headed Giant) for
  Magic and Commander (`GameMode`, `GameModeChips` in `TableSetup.kt`), replacing
  the Atlas lobby's switch. The start button says when a team game needs an even
  table of four or more.
- `StandaloneGame.twoHeadedGiant` follows GAMEPLAY.md's Atlas rules (team life,
  shared poison, team turns, leaving together); `toSummary` reports teams so the
  shared table draws team panels as it does for Atlas. Saved games and records
  keep the flag; local history credits both winners. Unit tests cover setup,
  turns, shared life/poison, Commander damage and the team win.
- Validation: `testDebugUnitTest` passes locally (31 standalone game tests).
  Needs a playtest.

## TH-020: Star Commander game mode

- Date / author: 2026-10-13 / Claude on `claude/master` (builds on PR #103).
- Scope: new profile `mtg_star_commander` across Atlas (engine, intents,
  state, recovery, statistics, standalone import), `protocol/` (schema,
  `http-v1.md`, `star-commander.response.json`) and Android (connected and
  standalone engines, setup star map, table panels, game over, history).
  Rules and contract are in [GAMEPLAY.md](Documentation/engineering/GAMEPLAY.md#star-commander).
- Interpretations for owner review:
  - An eliminated player never wins, even if both enemies are later out.
  - A batch that leaves nobody alive is a draw; one where nobody living
    qualifies just continues play.
  - Win claims (alternate win conditions) stay single-winner.
  - Star is a mode under Commander in the app, not a fourth format chip.
  - `winnerPlayers` lists both teammates in Two-Headed Giant; standalone 2HG
    records still store one winner and credit the mate through `won()`.
  - Statistics records changed layout (SD schema 3, NVS core schema 2) with no
    migration, so existing stats read as corrupt until a factory reset.
  - Sigils show nothing new: the display packet has no free field; master's
    Sigil is headless until LVGL.
- Validation: Atlas host suites (six new `starCommander*`/`starStandaloneImport`
  groups), client contract and adapter audit pass; Android 273 JVM tests pass.
  No device or table testing yet (STAGED G14).
- Codex: review welcome, especially the victory evaluator and the checkpoint
  schema 5 change.

## 2026-10-14 — Codex: Android version visibility and polish pass

- Owner: Codex on `codex/master`; baseline `7ecd884`. Clean startup, fetched
  refs and fast-forwarded to accepted master; no open PRs found.
- Scope: main-screen app version/build identity, responsive lobby controls,
  clear saved-account identity, and source review of current Android polish.
- Owner requests a few local passes before unnecessary CI. Keep this slice
  local; defer push/PR and remote CI until the passes are ready to batch.
- Implemented: installed-package version/build label in both lobby variants
  and Atlas home; shared with bug-report cards/live table menu, with Debug
  marking. Header actions and player controls wrap separately from names.
  Saved-player chips, local account cards and manual import choices show IDs.
  Local selection still uses the UUID; the public ID is presentation only.
- Validation: final `:app:assembleDebug :app:lintDebug` passed; lint reports
  zero errors, 44 warnings and four hints. APK metadata confirms version 0.1.0,
  build 1, debuggable (local default; CI supplies its run number). Documentation
  hygiene and `git diff --check` pass. No unit tests added for presentation-only
  changes. No device is connected; visual, large-text and TalkBack acceptance
  remains open. No push, PR, remote CI, installation or release.
- Follow-up owner direction: implement the next polish pass locally. Keep the
  app simple but full featured, with minimal explanatory copy and small touches
  of flavor. Scope: Players/Games/Appearance navigation, on-demand creation and
  details, Stats identity, browser failure feedback and quieter lobby copy.
- Second pass implemented: persistent section navigation and independent scroll
  positions; compact Add player dialog with optional credentials, frozen inputs
  during saving and failure feedback; explicit account/settings expand controls;
  summary-first game cards with details/import on demand. Team results show both
  winners using the existing `GameRecord.won` rule. Stats shows account IDs and
  an explicit details control. Missing browser now produces a visible message.
- Lobby: shorter introduction and starter copy; sound controls and seating help
  expand on demand; one bug-report entry in the guide; Atlas is a labeled switch.
  Short flavor lines appear in the lobby, empty states and Stats, while action
  labels remain literal. No gameplay, account authority or storage-format changes.
- Second-pass validation: local debug APK and lint pass; all 14 focused tests
  (`LocalAccountsTest`, `StatsSharingTest`, `TableLobbyTest`) pass. Documentation
  hygiene and whitespace checks pass. Device list is still empty; no visual or
  TalkBack verification claimed. Changes remain uncommitted for owner iteration;
  no push, PR or CI run. Next step: owner views the app and requests adjustments.
- Device follow-up: Pixel Fold is now authorized over USB. Installed Play app
  is 0.1.0/build 330; APK certificate comparison confirms the local debug signer
  cannot update it. Added opt-in `turnhub.preview=true` for a separate debug
  package/label/version suffix and installed TurnHub Preview successfully.
  Play app/data remains intact; preview data is independent. Device is locked;
  requested owner unlock before UI inspection. No credentials entered.
- Preview verification: preview APK has the separate package and label; install
  and activity launch succeeded. Ordinary debug build/lint and release manifest
  processing also pass with the property absent. Normal debug metadata still
  identifies `com.turnhub.android`, TurnHub 0.1.0. Both packages remain installed.
- Unlocked-device pass completed on the Pixel Fold outer display: inspected
  lobby/version, Players, empty Games, Appearance and empty Stats. Created one
  credential-free sample account named Preview Player via the dialog, verified
  its ID in the list/lobby and confirmed the form/action fit above the keyboard.
  Light and Graphite appearance render; the Appearance screen wraps at 200%
  text size. Restored system font scale to 1.0 and app theme to Automatic.
- Device finding/fix: the nested full-width Eyebrow consumed Clear all's width,
  making the lobby header abnormally tall. Used Eyebrow's trailing action slot;
  rebuilt/linted and installed preview build 2. Screenshot/UI-tree verification
  confirms normal spacing, visible Clear all and Build 2. Account survives update.
  Preview is left on its lobby; Play build 330 remains installed and untouched.
  No AndroidRuntime errors in the inspected final preview process log. Full-game,
  populated-history, Atlas, inner-display and TalkBack acceptance still pending.
  APK and device captures: `D:\TurnHub\Private\TurnHub-builds\android-polish-2026-10-14`.
- Owner follow-up: fix color selection and replace hex entry with visual picking;
  put in-game minus left and plus right relative to each rotated player panel.
  Added shared named swatches and custom color/intensity/brightness sliders.
  Local colors save immediately with failure feedback; Atlas retains its explicit
  save. Star previews now carry background/color (previously discarded), avatars
  honor saved color, and team panels use each member's fallback color. Selecting
  a color with an image active reveals the color while retaining the image.
  Background dialog keeps Done visible. Existing hold-to-repeat behavior remains.
- Initial preview build 3 and lint pass; 14 targeted tests pass, including added
  assertions for persisted personalization reaching Star previews after a swap.
  Phone check confirms swatches change the preview immediately and custom colors
  apply.
- Final preview build 4 installed on Pixel Fold; build/lint pass (0 errors,
  44 warnings, 4 hints), docs/whitespace checks pass. Device confirms custom color
  persists through update, Star preview uses it, spectrum sliders render and Done
  stays visible. Restored Preview Player's original solid/default-color setting.
  Brief five-player Star game: player-relative minus changes Koga 40->39 and plus
  restores 39->40. Used Back to setup within three minutes; same roster/settings,
  no game result saved. Final preview process has no AndroidRuntime errors.
  No Play-app/data changes, push, PR or CI. Atlas-connected picker, image-to-color
  transition and TalkBack behavior are code-reviewed but not device-tested.
- Owner follow-up (same local polish slice): expand tools to random opponents and
  all dice d4–d20; add 10-minute preset, optional timeout actions and earned
  extensions. Owner selected 75% earning threshold. Implemented local rules:
  overtime/default, auto-pass, demerit or both; earn after three personal turns,
  max two earned/game by default (1–3 selectable), 75/100% threshold, half-limit
  extension, at most one used per turn, no reward on extended/expired turns.
  Team bank is shared. Rules/banks/handled timeout persist; history retains
  demerits and extension uses. App-visible timer tick settles a reopened expired
  turn once, without cascading skipped turns. Atlas remains authoritative for
  connected games and does not expose these new local policies.
- Opponent tool defaults to active player and allows choosing another seat;
  excludes eliminated/self/team/Star allies by seat identity. Common dice have
  direct buttons; More dice offers every remaining integer through d20.
  Build 5 assemble/lint and standalone/tool/clock/lobby tests pass locally.
  Owner has started a new game on the phone; preserve it during installation
  and use only state-neutral table tools for this device pass.
- Build 5 installed with ongoing game retained. On-device screenshots verify
  common dice/More dice (all other d4–d20 integers), a d17 result and the opponent
  picker choosing Ricky (seat 2) for Rocket (seat 4), a legal Star enemy. No game
  controls changed during this pass. uiautomator could not reach idle in the
  live menu, so final tool verification used direct screenshots rather than its
  stale XML. Timer setup/live reward controls were not device-tested to avoid
  disrupting the owner's game. 88 focused local tests passed (11 new timer
  tests), lint 0 errors/44 warnings/4 hints; docs and whitespace checks pass.
  New rules remain opt-in for the next phone-hosted game; no CI/push/PR.
- 2026-10-15 owner review: unify link-like controls with buttons and separate
  table-wide settings from per-player modifiers. Same Codex local polish scope
  on codex/master; earlier uncommitted changes retained. Main menu sections now
  group tools, game rules, seating/alliance view, sound/display, help/app and
  finish controls, with pause/resume first. Title and Done remain fixed.
  Elimination choices use switches, poison limit uses an Apply button, and
  section headers use filled buttons with chevrons. Player More cards group
  turn/tools, counters/status, Commander damage and leave actions. Opponent
  picker moved into the relevant player's card; no separate player selector.
  Removed duplicated personal modifiers from paused overlay; paused table keeps
  More cards reachable and central Paused/Game menu offers resume. Compact
  section padding and matching rounded button shapes verified on a phone-sized
  API 37 emulator. Tested expand/scroll with fixed title/Done, pause/resume,
  paused player More, opponent selection and updating a personal energy counter.
  Preview build 6 assemble/lint passed; lint 0 errors/44 warnings/4 hints.
  Seven focused TableTools/TabletLayout tests passed; docs and whitespace checks
  passed. APK and screenshots saved under Private/TurnHub-builds/
  android-polish-2026-10-14. Physical phone disconnected, so build 6 is not yet
  installed there; owner's phone game untouched. No CI/push/PR.

- 2026-10-15 Codex, owner-directed navigation parity follow-up on codex/master,
  baseline 7ecd884 (origin/master unchanged). Same ongoing Android polish scope;
  all prior local edits retained. Owner now requests a PR after validation.
  Five shared destinations: Game, Table, Players, History, Settings. Table is an
  always-available full-screen entry. Settings owns phone appearance/sounds;
  Atlas adds account/table/admin capabilities without replacing the shell.
  Feature gate: navigation is disposable Compose state; existing local/Atlas
  authorities still validate their actions and own persistence. No protocol,
  credential format or dependency changes. Labeled tabs and existing accessible
  controls retained. Local play no longer disconnects an available Atlas.
  GitHub connector reports no open PR; gh has no login, connector available.
- Navigation validation: full local suite 308 tests/0 failures, preview build 7
  assemble/lint successful (0 errors/42 warnings/4 hints). API 37 emulator
  verifies all five entries, existing-game retention, shared stage/life overview,
  Players/History, Statistics return, immediate Graphite/Automatic changes and
  Table returning to the previous Settings tab. No AndroidRuntime errors in the
  inspected log. No physical phone connected; Atlas-connected navigation and
  TalkBack remain hardware acceptance checks. README and bundled help updated;
  manual export/docs/whitespace checks pass. DOCX render attempted but unavailable
  because LibreOffice is absent; manual page layout is not visually verified.
  Owner authorized PR publication; publish this accumulated local polish as one
  reviewable PR, with normal automatic checks and no redundant manual CI runs.

## 2026-10-15 — Claude: Android Table tab, tab motion and shared player colors

- Owner: Claude on `claude/project-thread-0cn2jn`, baseline `d12aa1b`. Owner
  reviewed Codex's navigation pass and asked for round 1 of 2: Table stays
  inside the app instead of taking over the screen, the header version label
  goes, tab transitions return, and player colors move into design tokens.
- Table tab: the running or last device game on the shared table screen with
  the tab bar kept; before a game it previews the lobby's seats (placeholders up
  to a startable roster, paused, touches and TalkBack blocked) with Start game
  or Set up players. Full screen is an explicit button. Starting from the Game
  lobby opens Table. With Atlas selected, Table previews the Atlas game and
  offers the existing tablet-mode grant.
- Tabs slide sideways in bar order with a fade (BASE/FAST motion tokens); none
  with reduce motion. Version label moved to the bottom of Settings.
- `playerColors` in `design/tokens.json` generates `DesignTokens.PlayerColors`,
  `--player-<name>` CSS and `TurnHubTheme::playerColor()` in `device_theme.h`.
- Validation: design token, docs and manual-asset checks pass locally. The
  container has no Android SDK, so Gradle build/tests run in PR CI. No device
  test yet; round 2 (turn-pass transition and gloss pass) follows owner review.

## 2026-10-15 — Claude: Android practice table, Star layout and screen regrouping

- Owner: Claude on `claude/project-thread-0cn2jn`, baseline `a51779e` (#116).
  Owner feedback after a phone test of #116, round 2 of 2.
- Table tab before a game is a practice table: a `StandaloneTable` over
  `MemoryStandaloneStore`, seeded from the lobby (placeholder seats to a
  startable roster), playable and never saved. It restarts when the lobby
  changes or the practice game returns to setup.
- Star layout: a per-device switch (`turnhub_ui` `star_table_layout`) in the
  Star lobby and the table menu seats Star Commander in the setup's star, each
  square panel turned toward its edge with faint ally/enemy lines. Only Star
  differs from setup; other modes' setup map already matches the table.
- Banner: icon-only bug and `?` buttons (TalkBack names, long-press toast). The
  bug button opens an explaining dialog with version/device before the form.
  New `bug` icon in `design/icons`.
- Regrouping: the mode's seating explanation sits under the seat map; the
  saved-player identity hint sits under the saved players; the in-app lobby
  drops its sounds and bug sections (Settings and the banner own them), the
  full-screen lobby keeps them; Settings drops its bug card.
- Validation: tokens, docs and manual-asset checks pass locally; Gradle build
  and tests run in PR CI (no SDK here). Not yet seen on a device.

## 2026-10-15 — Claude: commander names, card art and Sean Mode

- Owner: Claude on `claude/project-thread-0cn2jn` (PR #117), owner request
  before merging #117. Phone-hosted (standalone) play only for now.
- Commander names: optional per player in every Commander mode
  (`LocalPlayer.commander`, `TablePlayer.commander`, saved with the game,
  carried through start, rematch and Sean Mode; 48 characters max).
- Card art: `ScryfallCommanderArt` asks Scryfall's `/cards/named?fuzzy=` for the
  name only, downloads the `art_crop` (front face for double-faced cards), sends
  User-Agent and Accept, spaces requests and caches hits and misses by a hashed
  name. Tiles show the art behind a scrim with "Art: <artist>" credit; Settings
  > Commander card art turns it off. PRIVACY.md and DEPENDENCY_TRACKER.md
  (YELLOW, release review) updated; owner chose to ship it in #117.
- Sean Mode: a game-over button on finished five-player Star games;
  `seanModeRematch` reseats [0,2,4,1,3] so every ally pair becomes enemies.
- Validation: `SeanModeTest`, `ScryfallCommanderArtTest`; docs, tokens and
  manual checks pass locally; Gradle in PR CI. Not yet seen on a device.
