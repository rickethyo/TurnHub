# Project-wide Codex / Claude collaboration

This is the permanent shared conversation and handoff document for **all of TurnHub**: Atlas, Sigils, Android, protocols, hardware, PCB and enclosure design, testing, releases, documentation, and product decisions. It is not limited to one feature or review.

Scope confirmed by project owner: 2026-10-08.

## Working agreement

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

Keep the topic index and active discussions here. When a discussion becomes long and closed, archive its detailed exchange under Documentation/engineering/history/ and retain a linked outcome here. Do not archive unresolved questions just to shorten the file.

## Topic index

| ID | Topic | Areas | Status | Next action / owner |
|---|---|---|---|---|
| TH-001 | App-only player experience and battery presence | Android, Atlas, hardware, contracts, documentation | Device-entry slice implemented; CI passed; device acceptance pending | Codex / codex/app-only-entry; local Codex device acceptance next |
| TH-002 | Shared tablet access and dedicated account type | Android, Atlas, identity, authorization | Agreed requirements; not implemented | Define capability allowlist and implementation scope; unassigned |
| TH-003 | High-resolution player artwork and fuller tablet UI | Android, portal, assets, profiles, storage | Agreed direction; visual details open | Design image pipeline and tablet layout; unassigned |

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

## TH-001: App-only player experience and battery presence

Review baseline: master `cb200a63b0bc48ac452e2339890a9b55fd9574ec`.
Status: source review complete; proposals below are not implemented or hardware-verified.

## project owner's request

Review the new master commits, make the app more friendly to players who do not own an Atlas, and address detection of an Atlas with no battery installed.

## Codex review, 2026-10-08

### What landed

Verified by source and Git history, not device testing:
- Standalone play already exists. `068326d` makes its entry available while Atlas discovery is in progress.
- `a1789cf` adds Atlas import of finished standalone games.
- `293f4d9` updates the manual for standalone/tablet play and two games.
- `38b1c80`, `0953109`, `bcf9d76`, `6c52561`, and `56a1ca8` add and refine the battery estimate and its displays.
- `1ce21e4` and `9b6cb3e` introduce battery-related screen behavior and sleep.
- `0246201` separates Atlas and Sigil release tags.

### App: the missing piece is independence, not another game engine

The app has a real standalone engine and persistence already. Extend that boundary.

Source evidence:
- `Android/app/src/main/java/com/turnhub/android/ui/home/HomeScreen.kt`: ConnectCard centers Atlas connection; standalone is a secondary button. Footer says TurnHub runs locally on Atlas.
- `.../ui/tablet/StandaloneScreen.kt`: explanation promises later Atlas statistics; empty profile guidance says connect once; unlinked players say "Matched by name on Atlas"; records are described as waiting for Atlas.
- `.../standalone/StandaloneTable.kt`: persists game and pending records, caches Atlas profiles, caps records at 200, removes records after successful import or HTTP 400.
- `.../MainActivity.kt`: standalone routing is a rememberSaveable Boolean, not a durable preferred launch mode.
- `.../standalone/StandaloneTable.kt`: KnownProfile holds profileId and name without an Atlas identity. Any future cross-device linking needs explicit identity scope.

These choices make sense for an Atlas owner's temporary offline session, but leave permanent app-only users feeling unfinished.

### Proposed sequence

**A. First-class entry and language**
- Landing choices: **Play on this device** and **Connect to Atlas**, with a short description of each.
- Device-play description: "Track life, Commander damage, turns, and results on this phone or tablet."
- Remember preferred mode. Offer **Resume game** when a local match exists.
- Keep mode switching easy; a connection completing in the background must not replace a local match.
- Keep local play free of Atlas setup, account, Wi-Fi and nearby-device permission requirements.
- Stop or suspend unneeded discovery when entering local play; verify existing connection lifecycle before changing it.
- Replace Atlas-dependent local-player and history copy. Explain optional import only in the connection/import flow.
- Use “this device” rather than assuming a tablet; verify narrow-phone and foldable layouts.

**B. Local players and history**
- Reusable local players with stable local IDs, independent of Atlas profiles.
- A visible local match history and basic statistics useful even if Atlas is never connected.
- Separate permanent history from the outbound import queue. Import acknowledgement must not erase local history.
- Retain a rejected record and its reason for review rather than silently dropping it on HTTP 400.
- Make retention explicit. The present 200-record rolling queue must not silently become the product's permanent-history policy.
- Later, offer export/backup after the local record model is settled.

**C. Optional Atlas linking**
- Scope linked profile identity to atlasId plus profileId.
- Preserve the local player and local match ID when linking to an Atlas profile.
- Require disambiguation for same-name players; names are labels, not identity.
- Show pending/imported/needs-attention states separately from match results.
- Keep import retry idempotence, and avoid counting a local match twice in any combined statistics.
- Multi-phone hosting without Atlas is a separate future feature. Current standalone play is one shared device.

### Feature gate for A/B/C

| Gate | Proposed boundary |
|---|---|
| State owner | StandaloneTable/local domain owns local games; Atlas still owns Atlas games and imported Atlas statistics |
| Intent | Existing TableControls for game actions; explicit local actions for player editing, mode preference and history/linking |
| Validator | Local domain validates local changes; Atlas validates imports; linking validates destination and player mapping |
| Persistence | Android owns local players, history and preference; import queue stores delivery status separately |
| Rendering | Android local screens render local state; connected screens render Atlas state |
| Contract | A can be UI-only; B needs a local schema plan; C must review import contract and identity scope before changing it |
| Dependencies | Prefer existing stack; any added dependency requires legal inventory review |
| Accessibility | Text mode labels, TalkBack actions/status, large-text and narrow-width checks; no color-only sync status |

The explicit standalone exception in ARCHITECTURAL_INVARIANTS.md permits this separate local game. Update identity/persistence documentation deliberately as local profiles become a product feature.

### Atlas: battery absence is currently ambiguous

Verified from `Atlas/include/battery_gauge.h`:
- BatteryReading.present means a cell is connected **or the charger holds the line up**.
- A reading below 2500 mV resets the gauge to absent; a higher voltage seeds present.
- PowerSourceTracker infers USB/cell operation from voltage steps and drift.
- USB charging percentage is estimated with fixed capacity/current assumptions; it is not measured charge or confirmed charger status.

`Documentation/engineering/HARDWARE.md`, Atlas battery, already states that USB with no cell can look like a full battery and that this board has no charge-status or USB-sense pin available to the implementation.

Therefore a new voltage threshold cannot be presented as reliable automatic battery detection. A full battery and a charger-held empty connector can overlap. This is especially relevant now that power estimates influence screen/sleep behavior.

**Recommended design, pending hardware investigation**
1. Separate battery presence (present / absent / unknown), power source (USB / battery / unknown), and charge state. Do not overload a percentage or a single Boolean.
2. For confirmed absence, show **No battery installed** in device info, suppress percentage/charging, and avoid battery-specific low-charge actions.
3. For ambiguous readings, show **Battery status unavailable** instead of claiming absence or inventing a percentage. Only claim USB power if independently known.
4. Inspect the actual board schematic and accessible test points. Determine whether a suitable detection circuit or charger diagnostic can distinguish an absent cell from full, empty and protected cells. A charge-status signal alone is not assumed sufficient.
5. Bench-capture raw ADC readings with USB and no cell, USB plus full/partial/low cells, and transitions. Use those traces to evaluate any heuristic; label it experimental if states overlap.
6. If the current board cannot support reliable detection, offer an explicit installation setting as a temporary user override, clearly distinguished from automatic detection. Investigate dedicated presence sensing for a board revision.
7. Do not disable the supply or charger to probe presence without first verifying the power path and hardware support.

**Feature gate:** the hardware battery module owns evidence and confidence; service sampling updates that state, no gameplay Intent is needed for a measurement. Any manual override needs the existing authorized settings path and a persistence owner. Atlas, portal and Android render one defined battery contract. Schema/DTO/fixtures/renderers must change together if unknown is exposed. No new dependency is currently proposed. Text labels must accompany icons.

### Acceptance checks

App:
- Fresh install with no Atlas, no network and no nearby-device permission can start and finish a local match.
- Relaunch/rotation preserves the match; preferred local launch and explicit Resume work without discovery blocking them.
- Repeat players and completed games are usable without an Atlas.
- Import success or rejection preserves local history; duplicate import cannot double-count.
- Same-name players and two different Atlases cannot silently cross-link identities.
- TalkBack, large text, narrow phone, folded/unfolded device and tablet remain usable.

Battery:
- USB-only cold boot with connector empty shows no invented charge or charging animation.
- Full installed cell remains distinguishable from confirmed absence, or is honestly unknown.
- Low/protected cell is not confidently labeled absent solely from low voltage.
- Presence/source noise does not trigger false sleep or screen transitions.
- All clients agree on absent/unknown/present semantics.
- Host tests exercise classification and serialization; physical tests establish actual detection. Passing host tests alone is insufficient.

### Review limits

No builds or runtime tests were run for this documentation-only review. No board schematic or fresh measurements were verified in this session. Attachment copies were unavailable at their supplied paths; findings above come from current repository source and engineering documentation.

## Claude response requested

Please append your response here:
1. Which of A, B and C should be the first implementation slice?
2. Can current Atlas wiring provide reliable battery-presence evidence? Cite schematic signals and measurements, not only thresholds.
3. Do you agree with separating local history from the import queue and scoping profile links to Atlas identity?
4. Record any implementation SHA and exact tests/device checks completed.

## Decision and implementation log

| Date | Author | Item | Status | Evidence |
|---|---|---|---|---|
| 2026-10-08 | Codex | Source review and proposed slices | Ready for discussion | master cb200a6; paths above |
| Pending | Claude | Review and reply | Awaiting response | None yet |


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

### 2026-10-08 / Codex / TH-001 A implementation claim

- Baseline: master `d83e314eeaea914780add28906b84dd1df1cf694`.
- Owner / branch: Codex, `codex/app-only-entry`; active.
- Scope: prominent device-play entry and Resume, device-first wording, durable local launch preference, permission-free fresh/local launch, and cancellation of pending Atlas joins when entering local play. No history schema, battery, tablet account or artwork changes.
- Feature gate: Android owns navigation and stores the preference in `turnhub_ui`; explicit entry/exit/connect callbacks change it, with no gameplay Intent or wire-contract change. StandaloneTable retains local game validation/persistence and Atlas retains connected games. Existing Android screens render both; no added dependency. Labels and full-width actions remain accessible; phone/foldable/large-text device checks remain required.
- Validation plan: focused HomeViewModel cancellation scenarios and Android CI. This environment has no Android SDK or Gradle/Kotlin installation; do not report a local Android build as passed.

### 2026-10-08 / Codex / TH-001 A implementation handoff

- Branch: `codex/app-only-entry`; implementation commit recorded below once pushed.
- Implemented: prominent Play on this device / Resume game entry; device-first copy; persisted local launch choice; fresh/local launches do not ask for nearby-device permission; explicit Atlas Connect/search/setup does. Entering local play cancels tracked discovery/join/connect jobs and disconnects polling. Returning to Atlas waits for disconnect completion. Existing StandaloneTable game persistence and import queue are unchanged.
- Added four HomeViewModel regression scenarios: saved-table discovery cancellation, manual Wi-Fi join cancellation without password fallback, pending repository connect cancellation, and reconnect after disconnect completes.
- Manual V0.12 and generated Android manual asset updated; bounded import queue and lack of permanent history now explained. Personal name removed from current text documents that contained it; current manual author/last-modifier set to TurnHub. Existing Git history was not rewritten.
- Local checks: manual export `--check` passed; 28-page manual rendered and visually reviewed, with changed pages 5 and 19 inspected at full size. Android SDK, Kotlin and Gradle are unavailable here, so no local build/test pass is claimed. Draft PR CI is the build/test gate.
- Local Codex continuation: fetch this branch, read CLAUDE.md and this handoff; run `cd Android && ./gradlew testDebugUnitTest assembleDebug` if CI has not already passed. Fix actual failures before extending the feature. Device acceptance: fresh install with no Atlas/network and Nearby devices denied; local start/end; relaunch/rotation/resume; Back to Home and explicit Atlas Connect/search/setup; switch to local while a join is pending; narrow/folded/unfolded/tablet layouts, large text and TalkBack. Do not merge before review/device acceptance.
- Remaining outside this slice: permanent local players/history, rejected-import retention and Atlas-scoped identity (B/C); battery presence investigation; TH-002 tablet accounts/re-entry; TH-003 artwork and richer tablet UI.

### 2026-10-08 / Codex / verified CI outcome

- Implementation commit: `3bf62e14a6953f41039aa522a59424e4b854f3ec`; draft [PR #73](https://github.com/rickethyo/TurnHub/pull/73).
- [TurnHub CI run 216](https://github.com/rickethyo/TurnHub/actions/runs/37747422886) completed successfully. Android ran `testDebugUnitTest bundleRelease`, including the four new cancellation scenarios; job log reports `BUILD SUCCESSFUL in 1m 55s`. The always-run adapter, token, portal-pack and manual checks passed. Firmware jobs skipped compilation because firmware source was unaffected; no firmware/device pass is claimed.
- Status: ready for source review and local Codex continuation. Physical phone/foldable, large-text and TalkBack checks remain pending. No merge or hardware validation performed. The continuation should use the existing CI evidence and rerun affected checks only after code changes or for a concrete unresolved concern.
- Documentation cleanup also replaces the personal name in the Wokwi example. Future documentation uses the project-owner role; Git history and archived binary manual versions have not been rewritten.
