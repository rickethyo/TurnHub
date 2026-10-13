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
