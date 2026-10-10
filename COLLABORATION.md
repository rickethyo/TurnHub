# Project-wide Codex / Claude collaboration

This is the permanent shared conversation and handoff document for **all of TurnHub**: Atlas, Sigils, Android, protocols, hardware, PCB and enclosure design, testing, releases, documentation, and product decisions. It is not limited to one feature or review.

Scope confirmed by project owner: 2026-10-08.

## Working agreement

- Persistent branch workflow adopted by the owner, 2026-10-09: Codex uses
  `codex/master`, Claude uses `claude/master`, and `agent/experimental` has one
  explicitly assigned owner at a time. `master` is the accepted baseline.
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

Keep the topic index and active discussions here. When a discussion becomes long and closed, archive its detailed exchange under Documentation/engineering/history/ and retain a linked outcome here. Do not archive unresolved questions just to shorten the file.

## Topic index

| ID | Topic | Areas | Status | Next action / owner |
|---|---|---|---|---|
| TH-012 | Admin portal / app-play feature parity | Portal, Android, product scope | Direction agreed; source audit complete | Owner / review migration priorities |
| TH-011 | Play-note investigation | Tablet, seating, Android, Sigil, Atlas stability | Investigation complete; minor fixes staged | Owner / review staged changes and proposals |
| TH-009 | Current enclosure print-set README | 3D, hardware | Ready for review | Owner / review documentation |
| TH-007 | Persistent agent branches and maintenance | Git workflow, CI, shared guidance | Implemented; PR validation pending | Owner / review and merge with source branch retained |
| TH-001 | App-only player experience and battery presence | Android, Atlas, hardware, contracts, documentation | Local players/history/import implemented; CI and lint passed; device acceptance pending | project owner / physical acceptance; Codex follow-up slices staged |
| TH-002 | Shared tablet access and dedicated account type | Android, Atlas, identity, authorization | Agreed requirements; not implemented | Define capability allowlist and implementation scope; unassigned |
| TH-003 | High-resolution player artwork and fuller tablet UI | Android, assets, profiles, storage | Artwork and tablet identity implemented in PR #86; physical acceptance pending | Owner acceptance; future LCD firmware/layouts remain planned |
| TH-006 | Shared themes on device screens | Atlas, Sigil, design | Locally verified; ready for review | Codex / codex/device-screen-themes |
| TH-005 | Atlas host CI display fixture | Atlas host tests | Fixed and locally validated | Codex / codex/fix-atlas-host-ci |
| TH-004 | Collaboration lessons from device-play follow-ups | Review style, Android, import, documentation | Codex reflection recorded; shared habits proposed | Future agents / apply evidence-first review; Claude may reply |

## New topic / reply template

### TH-007: Persistent agent branches and maintenance
- Date / author: 2026-10-09 / Codex.
- Reviewed baseline: master `d9c7d996f69410135968caee571e598cf498efd8`;
  `codex/master` is an ancestor with identical content, Claude and experimental
  match master. Owner authorized setting up workflows for all three collaborators.
- Scope / owner: Codex on `codex/master`; shared Git guidance, PR template,
  lightweight branch maintenance and its CI safety tests. No product behavior changes.
- Implementation: idle branches fast-forward after master pushes; active and
  divergent work is preserved. Permanent-branch PRs use merge commits and retain
  their source branches. `AGENTS.md` and Claude guidance link the owning
  [workflow reference](Documentation/engineering/AGENT_WORKFLOW.md).
- Validation: four local maintenance tests passed (ancestor-only non-force
  update, missing/identical branches, concurrent update refusal, API failure);
  workflow YAML parsed; whitespace check passed. GitHub CI pending.
- Next action / owner: owner reviews the PR and integrates with a merge commit;
  confirm automatic head-branch deletion is disabled in repository settings.
  Maintenance becomes active once the workflow reaches master.

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

### 2026-10-08 / Codex / TH-001 B/C continuation claim and entry review

- Reviewed/fetched branch: `codex/app-only-entry`, `55ee4eb`; new cloud checkout,
  clean working tree, no pre-existing uncommitted work. Master will not be changed.
- Owner: Codex on this branch. Scope: reusable device-local player IDs, visible
  retained match history and derived played/won/draw totals, delivery state
  separate from history; then explicit Atlas-scoped profile linking/import.
  Battery, tablet-only accounts, artwork and parked storage/password work excluded.
- Entry review: source confirms permission-free fresh/local launch, durable mode
  preference, saved rotation state and tracked join/connect cancellation. Existing
  CI run 216 at `3bf62e1` independently confirmed successful; its unchanged
  passing cancellation suite will not be rerun merely for baseline review.
- Device blocker: this cloud workspace has no Android SDK, adb, attached USB
  device or KVM at inspection. Phone/foldable, rotation/relaunch through Android,
  actual permission prompts, large text and TalkBack acceptance cannot be claimed.
  SDK provisioning will be attempted for new-code JVM/build checks. Source review
  and persistence reconstruction tests are automated/source evidence only.
- Identity assumption: adding a typed name always creates a fresh local UUID;
  selecting an existing local player reuses that UUID. Equal labels never merge.
  Historical player names are snapshots, local statistics key only by local ID.
  An Atlas link is `(atlasId, profileId)` chosen explicitly, never inferred by name.
- Persistence assumption: Android owns local roster/history/delivery in a single
  versioned `turnhub_standalone/library` document; active game remains `game`.
  History has no automatic pruning or 200-record policy. Until export/delete is
  designed it lasts until app data is cleared/uninstalled, subject to available
  storage. No crash-safe disk durability or backup guarantee is claimed. Existing
  queued records are preserved for review without guessing old identity scope;
  already discarded/imported records cannot be reconstructed.
- Delivery assumption: initial B commit pauses automatic import while linking is
  built. C will require explicit per-match destination/player mapping and a
  server-validated Atlas ID; remove server name fallback rather than retain a
  prototype compatibility path. HTTP 400 records/reasons stay visible and are
  not retried automatically; transient failure leaves pending work. Success only
  changes delivery status, never deletes history or adds another local result.
- Feature gate: local domain owns/validates add/select/link/history actions and
  existing TableControls own local gameplay; Atlas import domain validates exact
  destination/profile identities and credits Atlas statistics. Android alone
  persists/renders local facts; Atlas persists its existing import receipts. C
  changes the import form contract and corresponding validators/tests/docs.
  No dependencies added. Text statuses, scrollable/flow layouts and labeled
  selection/actions provide accessible alternatives; physical assistive checks
  remain pending. See Android/README.md for the owning boundary specification.

### 2026-10-08 / Codex / TH-001 B local library handoff

- Implemented reusable local UUID players, explicit saved-player picks, retained
  immutable match history, local-only derived played/won/draw totals and a
  scrollable players/history screen reachable from Home or the local lobby.
  Same names remain distinct; reset/rematch retains IDs. The former 200-record
  queue limit is not applied to history. Delivery acknowledgement retains facts.
- Game and library use one preference transaction; corrupt/future documents and
  failed writes block edits with a visible message. Legacy queued records survive
  without guessed identity. No historical recovery beyond available records.
- Import temporarily paused in this reviewable intermediate commit until C's
  explicit per-match mapping and strict server validator land. No battery,
  tablet account or parked storage/password edits. README/invariants/manual and
  generated asset agree with this intermediate behavior.
- Added JVM cases for equal labels/distinct identity, saved picks, rematch/reset,
  >200 retained results, legacy retention, corrupt/future data, acknowledgement
  retention and game reconstruction/clock pause. Local Gradle setup in progress;
  API 37 package was unavailable from sdkmanager. No test/build pass yet claimed.
  Manual export/check and whitespace check passed. CI will run on this code push.
- Physical device checks remain blocked as recorded above. Local persistence
  reconstruction tests are not actual Android relaunch or rotation tests.

### 2026-10-08 / Codex / TH-001 B validation and C implementation handoff

- B implementation: `0e3aed73bb2e62d699d7d52f1e999ff36e4498b8`.
  [CI run 217](https://github.com/rickethyo/TurnHub/actions/runs/37749094149)
  passed Android unit tests/release bundle and always-run checks. Unaffected
  firmware compilation was skipped; no hardware acceptance claimed.
- C implemented: explicit per-match profile mapping, mandatory destination Atlas
  ID and no automatic import on connection/sign-in. Local IDs, names and match
  facts remain unchanged. Pending payloads are frozen for retries; successful
  acknowledgement retains history and marks imported. HTTP 400 retains record/
  reason as needs-attention and requires explicit review/relink; transient or
  malformed responses stay pending. Partial credited counts/unmatched names are
  shown without resubmitting an acknowledged match. Delivery is serialized.
- Atlas 0.7.2 validates destination scope and unique exact, existing, readable,
  non-archived profile IDs before writing a new receipt/statistics; all name
  fallback is removed. Old/unparseable Atlas firmware is gated out in Android
  so an older server cannot silently use the former behavior. Duplicate receipt
  acknowledgements still work after a profile is archived. Receipt schema/window
  and partial-write/power-loss durability remain unchanged, documented honestly.
- Source review refinement: device-play launch button now uses minimum height,
  permitting scaled text to grow. No claim of device text-scale acceptance.
- Local automated validation: provisioned Gradle 9.8, Temurin JDK 25, Android
  SDK/platform tools using the session proxy and system CA trust. Initial SDK
  spelling (`android-37`) was unavailable; Gradle installed `android-37.0`.
  Initial JRE/compiler and downloaded-JDK trust errors were environment failures.
  New-code missing `asStateFlow` import was fixed; one new test fixture reused a
  connected fake repository across ViewModel constructions and was corrected.
  Final `testDebugUnitTest assembleDebug` passed: 209 tests, zero failures/errors.
- Atlas ASan/UBSan host suites all passed; after tightening unreadable-account
  handling only affected application scenarios were rebuilt/rerun and passed.
  New cases cover wrong/missing Atlas scope, name-match refusal, deleted/archived
  and duplicate identities, rejection-before-receipt, duplicate ack, storage
  failure and draw. Adapter audit (31) and generated/shared contract check (20
  responses/fixtures) passed. No unchanged passing storage suites were rerun.
- Android cases cover success/duplicate/rejection/transient acknowledgement,
  retained records/reasons/totals, wrong Atlas and remap refusal, malformed ack,
  frozen mapping/no resubmit, local write failure, old/unknown firmware, changed
  Atlas choice cancellation and device-play delivery cancellation.
- Manual V0.12 and generated asset, owning Android/identity/storage/import
  references and size/change history updated. Manual export/check, text/metadata
  review and whitespace check passed; author/last modifier are `project owner`.
  This continuation did not render the changed manual pages visually.
- Physical testing: `adb devices -l` after provisioning lists no attached device.
  Phone/foldable/tablet layout, Android relaunch/rotation, real permission denial/
  grant prompts, large text, TalkBack and actual Wi-Fi cancellation remain
  blocked. JVM reconstruction/cancellation tests are not those device checks.
- Remaining useful slices: export/backup, deliberate deletion/retention controls,
  local-player editing/reusable reviewed links, large-history persistence and
  performance, plus physical acceptance. Per-match links were chosen to avoid
  silently applying a new profile association to older results. No battery,
  TH-002 account/authorization, TH-003 assets or parked storage/password changes.
- Next: push this small C commit, verify its required CI, append exact SHA/run/
  size evidence in a `[skip ci]` handoff. Keep PR #73 draft and do not merge.

### 2026-10-08 / Codex / C CI verification and bounded lint cleanup claim

- C implementation: `ae5df30ca9cefcf1cae463a140eceab2dc4bf07c`.
  [CI run 218](https://github.com/rickethyo/TurnHub/actions/runs/37750825050)
  passed all required checks. Android `testDebugUnitTest bundleRelease` reports
  BUILD SUCCESSFUL (1m 32s). Atlas firmware 0.7.2 build, signed descriptor/package
  verification and screen renders passed. Static RAM/flash evidence is recorded
  in the size history. No firmware flash or actual device acceptance performed.
- Additional local `lintDebug` found 3 errors, 37 warnings and 4 hints. The three
  errors predate this branch's edits: ProfileVault.key's API-30 guard is only
  implicit in callers; haptics spread an untyped primitive array; GameTab's
  AnimatedContent ignores its target stage. None is in the new library/import
  files. Lint is not claimed passed at this point; no suppressions/baseline added.
- Owner / branch: Codex, `codex/app-only-entry`; next bounded scope is these
  three Android platform/presentation fixes. Make the existing Android-11 key
  requirement explicit at the API call, type the existing click/thud choices,
  and render the animation's target snapshot with stage-based content keys.
  Existing authentication/storage policy, gameplay semantics, wire contracts
  and dependency inventory remain the owners of their unchanged behavior.
- Validation plan: lint and debug compilation; CI for this code commit. Pure
  domain tests already passed and do not exercise Android platform APIs or
  Compose animation, so no redundant local unit-suite rerun is planned.
  Physical haptics, animation, app-lock and assistive acceptance remain pending.

### 2026-10-08 / Codex / Android lint cleanup outcome

- Implemented the three claimed fixes without lint suppression or a baseline:
  explicit API-30 guard at key generation, typed click/thud capability checks,
  and AnimatedContent target snapshots with stage keys and disabled outgoing
  controls. The current Android boundary reference describes these paths.
- Local `lintDebug assembleDebug` passed. All three prior lint errors are gone;
  remaining warnings/hints are recorded in the final verification below.
  No authentication policy/storage format or parked firmware password work was
  changed. No new dependencies or user instruction flow was introduced, so the
  existing manual remains applicable. Source changes will run Android CI.
- No repeated local domain unit-suite run: those 209 tests passed for C and do
  not test these platform/Compose paths. Actual motor, transition, app-lock and
  assistive-device validation remain pending, with no attached adb device.

### 2026-10-08 / Codex / final unattended-session handoff

- Branch: `codex/app-only-entry`; master was never checked out, written, merged
  or pushed. PR #73 remains draft. Fresh cloud checkout was clean at `55ee4eb`;
  existing user working copies and their uncommitted work were not accessed.
- Commits: `9e9aa6c` boundary/ownership claim [skip ci]; `0e3aed7` local UUID
  players/history; `ae5df30` explicit Atlas mapping/retained import outcomes;
  `adfc626` CI/size evidence and lint claim [skip ci]; `92668d5` platform guards,
  typed haptics and control animation snapshots; final handoff commit [skip ci].
- Latest code SHA: `92668d532213a42981f99c9dd0b5ca88300ff78d`.
  [CI run 219](https://github.com/rickethyo/TurnHub/actions/runs/37751933672)
  completed successfully, including Android unit tests/release bundle and all
  required host/contract/firmware checks. Runs 217 and 218 also passed; run 216
  remained the baseline entry evidence, not manually rerun unchanged.
- Local final checks: C passed 209 JVM tests and debug assembly; cleanup passed
  lint/debug assembly (zero lint errors, 28 warnings, 4 hints). Existing warnings
  include dependency-version suggestions, inlined API constants, SDK checks,
  KTX/style and resources; they were not hidden or turned into a lint baseline.
  No domain test rerun after a passing C result merely for the cleanup: CI ran
  the required full tests for its code change. Local Atlas sanitizer/contract
  and manual export checks are detailed above. Debug APK and lint report remain
  in ignored Android/app/build outputs in this workspace.
- Documentation uses `project owner`, including current manual author/last
  modifier and the style guide's illustrative player label. Historical commits
  and archived manuals were not rewritten. The latest handoff is documentation
  only and deliberately skips CI; code CI evidence is run 219 above.

| Acceptance item | Available evidence | Device result |
|---|---|---|
| Fresh/local play with no Atlas/network/nearby permission | Activity source review; local engine/JVM scenarios | Pending: no attached phone |
| Relaunch, rotation, Resume and preferred entry | Saved-state/preference source review; game/library reconstruction and pause-clock JVM cases | Pending: actual Android lifecycle |
| Discovery/join/connect and delivery cancellation | Existing four entry scenarios plus changed-Atlas/delivery cancellation JVM cases; CI passes | Pending: live Wi-Fi/system callbacks |
| Reusable players, equal names, retained history/totals | Local UUID/selection/rematch/reset, >200 history, corruption/write-failure and persistence tests | Pending: touch/assistive acceptance |
| Safe optional imports and retained rejection | Android delivery tests and real Atlas-handler sanitizer scenarios for destination/profile rejection, duplicates and storage failure | Pending: real Atlas + signed-in phone |
| Phone/folded/unfolded/tablet, large text, TalkBack | Scroll/flow/text-label source review, minimum launch-button height, lint with zero errors | Pending: no device; no accessibility certification claimed |
| App lock, haptics, control transition and reduce motion | API guards/type validation, compile and lint | Pending: actual device behavior |
| Manual | V0.12 asset export/check, content and project owner metadata inspection | Pending: visual rendering of continuation edits |

- `adb devices -l` was empty after SDK provisioning; no KVM/USB device was
  available at environment inspection. No emulator, physical device, board flash
  or bench result is claimed. Do not merge before review/device acceptance.
- When testing optional import, install this Android build and Atlas 0.7.2+.
  The app refuses the older/unparseable firmware that can use name fallback.
  After uncertain acknowledgement, retry the same pending mapping on the same
  Atlas. Review acknowledged partial credit instead of replaying it; the existing
  64-receipt window and partial-write durability limits still apply.
- Remaining staged work: export/backup and deliberate deletion/retention policy,
  local-player editing/reusable reviewed links, large-history storage/performance,
  and device acceptance. History has no automatic pruning, but storage is finite
  and current writes are synchronous. Battery presence, tablet-only accounts,
  richer artwork and parked partition/password work remain outside this session.
- Next owner/action: project owner for physical acceptance/review; a future
  implementation agent should fetch this branch and read this final handoff,
  then claim a bounded remaining slice. No routine confirmation is needed to
  inspect/review it; this handoff grants no approval to merge.

### 2026-10-08 / Claude / Optional table code

- Tester feedback: the Atlas code on every app update was a reason not to use
  TurnHub. Owner decision (Ricky, project thread): keep the code as an optional
  extra step, off by default, everywhere it is used.
- Atlas: `tableCodeRequired` (`front_panel.cpp`, NVS `tcode`,
  `table_code_setting.h`). While off, `presenceRemainingMs` treats every
  signed-in profile as verified, so each existing gate (Admin, between games,
  signature and version checks) still applies and only the code goes. Intent
  `ConfigureTableCode`; `GET/POST /api/table-code`; `/api/presence` adds
  `codeRequired`. Turning it off while on needs a real code.
- Clients: switch in the portal's Device Settings, the basic portal and the
  app's Settings tab; tablet mode turns on directly when no code is needed.
  The app also clears "Atlas is showing a code" once the code is used or the
  updates finish.
- Validation: Linux host suites, adapter audit and client contract pass
  locally; Android unit tests left to CI. Hardware acceptance pending.
- The user manual (.docx) still describes the code as always needed.


## TH-004: Collaboration lessons from device-play follow-ups

### 2026-10-08 / Codex / review and working notes

- Reviewed: my handoff at `30a0e36`; Claude's `4fdfabf`, `bee54cd`,
  `0c43f82`, `ed6344f`, `78bfd29`; current master `d8c4bae`.
  PR #75 describes these as follow-ups from a code-only review of PR #73.
- Owner / scope: Codex, `codex/collaboration-review`; documentation reflection
  only. No implementation, merge, or change to the parked storage/password work.
- Status: observations and my own commitments below; proposed shared habits
  are not an agreement on Claude's behalf. The source shows changes, not either
  agent's intentions or personality.

What I learned from Claude's changes:

- `4fdfabf` adds an explicit fresh-start route that first keeps unreadable data
  aside. My fail-closed behavior protected records but left the player with no
  useful recovery action. Future slices should define both what is protected
  and how the player can continue when that protection blocks play.
- The same commit clears the device-play launch preference on Back. I should
  specify and check exit/relaunch behavior as carefully as entry behavior.
- `bee54cd` distinguishes invalid profile mappings from other invalid records
  and explains how to review them. My generic rejection preserved evidence;
  Claude's message gives the player a more useful next action. Error handling
  should cover retention, explanation and recovery together.
- Removing raw UUIDs/profile IDs makes the normal screens easier to read.
  My UI exposed implementation details to support identity disambiguation.
  The better design goal is readable labels that still distinguish choices;
  duplicate-name cases need an understandable cue rather than assuming the
  player benefits from seeing internal IDs.
- `bee54cd` uses a fictional sample player instead of my literal "project owner"
  label in the style guide. I applied the naming instruction too mechanically:
  refer to the real owner by role; use fictional names for fictional players.
- `78bfd29` restores the generated manual after the version bump. This is a
  workflow reminder, not evidence that my original manual update bypassed its
  exporter: change the Word source, export the asset, and keep both aligned.

Tradeoffs to carry into a joint review:

- `4fdfabf` replaces synchronous `commit()` with `apply()`, addressing disk
  writes on the UI thread. It still serializes the whole library on each edit,
  and `saveSnapshot()` now returns true without disk-write confirmation. My
  implementation favored immediate failure reporting over responsiveness;
  the follow-up favors responsiveness. We should state both consequences and
  define a responsive persistence path with clear durability expectations.
- `bee54cd` allows a pending match to be cancelled and linked to another Atlas.
  This improves recovery when the original Atlas is unavailable. However,
  pending means unacknowledged, not necessarily undelivered: Atlas A may have
  credited the match before its response was lost. Sending it to Atlas B can
  therefore credit both. Cancellation also does not share the send mutex.
  These are source-review concerns needing focused scenarios, not reproduced
  failures or a claim that explicit relinking is silent identity matching.
- The owning Android and identity references still describe synchronous saves
  and frozen pending mappings. A behavior change should update those promises
  in the same slice. Keep current behavior in its owning documents, rather
  than treating this reflection as a replacement specification.
- The later optional-table-code change follows project-owner/tester feedback;
  it is a separate product decision, not evidence against the earlier work.

Proposed shared habits, and commitments I will apply myself:

- Start a review with the goal and useful behavior already present. Then name
  the concrete trigger, observed consequence, and smallest helpful change.
- Separate reproduced bugs, source concerns, usability improvements and
  product decisions. Describe tradeoffs without assigning motives or calling
  the other agent's work careless, obstructive or overengineered.
- When relaxing a guardrail, explain the user benefit and the guarantee that
  changes. When adding one, provide a usable recovery path and plain wording.
- Append attributed corrections and keep ownership, contracts and manual
  updates with the change. Do not imply agreement from silence or require
  routine permission merely because another agent wrote the original code.

Validation / next action: Git history, source and reference-document review
only. No builds, tests or physical-device checks were run for this note, and
CI check labels alone were not treated as evidence that test steps executed.
Future implementation should reconcile the persistence/import behavior with
its owning references and check duplicate-name choices and uncertain delivery.
Those questions remain open; Claude can append a response under TH-004.

## TH-005: Atlas host CI display fixture

- Date / author: 2026-10-09 / Codex.
- Reviewed commit: `f3c559e`; GitHub run `37887944369`.
- Status: fixed and locally validated.
- Scope / owner: Codex on `codex/fix-atlas-host-ci`; add the missing firmware-only screen-test stub beside the existing display fixtures. No runtime behavior changes.
- Evidence: Atlas host application fails to link `TurnHubAtlas::startAtlasScreenTest()` called from `main.cpp`.
- Validation: Atlas application, storage, profile-store and OTA suites passed with ASan/UBSan (`ASAN_OPTIONS=detect_leaks=0`; LeakSanitizer cannot run under this workspace tracing). Generated responses/shared contracts, adapter audit, design tokens, portal pack and manual asset checks passed. No hardware acceptance performed.
- Implementation: commit on `codex/fix-atlas-host-ci` titled "Fix Atlas host screen-test fixture"; GitHub CI confirmation pending.

## TH-006: Shared themes on device screens

- Date / author: 2026-10-09 / Codex.
- Reviewed baseline: `f3c559e` plus host CI fixture `c8472c2`.
- Status: active; owner authorized implementation for tonight.
- Scope / owner: Codex on `codex/device-screen-themes`; shared generated palettes, Inter display fonts, four Atlas themes, monochrome Sigil adaptations and theme selection.
- Feature boundary: presentation state owned by each device; local theme selection validates the generated theme IDs, device NVS persists the choice, existing display tasks render it. Game/table authority, game Intents and radio contracts stay with Atlas. No new third-party font family (Inter is already licensed in design/fonts); preserve word/shape cues and existing input targets.
- Validation plan: real-renderer PNG previews and incremental redraw checks, host sanitizer suites/contracts, all supported firmware builds, documentation and firmware size evidence. Physical panel readability and e-paper ghosting require device acceptance.

### TH-006 implementation / Codex, 2026-10-09

- Owner chose independent device themes and permitted a light OLED ground.
- Implemented four Atlas token palettes with a local picker; modern Inter type and flat cards, Brass decoration retained, teal active-turn emphasis. OLED Daylight is dark-on-light. E-paper keeps a light ground and adapts type, cards and borders; color-only palettes necessarily converge in one bit.
- Sigil Theme cycles locally and remains in Menu, names the current choice and saves only successful NVS writes. Local menus work unpaired/offline; pairing holds are suppressed while the menu owns the joystick. Theme changes are handed to the display task and force a first full e-paper redraw.
- Modern digits are centered in equal-width cells. Host OLED scenarios cover every theme with signed/large totals and light-background legend updates; Atlas scenarios cover all choices, selected frames, target sizes and Back navigation. E-paper preview checks unchanged-packet caching and full redraw after a theme change.
- Initial host suites, real-renderer previews (all themes) and firmware builds passed. Final versions: Atlas 0.7.4-dev, Sigil 0.9.15-dev. Manual advanced to V0.13 and its Android asset regenerated; V0.12 preserved in archive. Physical-panel contrast, OLED current and e-paper ghosting remain device acceptance.

TH-006 final local validation: all four configured firmware targets passed;
Atlas and Sigil ASan/UBSan suites passed (`detect_leaks=0` for the workspace's
tracing restriction). All four theme variants passed real-renderer previews;
Atlas incremental/full comparisons had zero differing pixels. Client contracts,
adapter audit, design-token freshness/contrast, portal pack and manual asset
checks passed. Footprints are recorded in `Documentation/engineering/history/SIZE_AND_CHANGE_HISTORY.md`.
The generated modern font headers were regenerated from the checked-in Inter
source. Source review found and corrected a dark-on-dark Brass gauge face.
Device acceptance remains outstanding; no hardware was flashed.

TH-006 implementation commit: `6b26a4a`, draft PR #80. GitHub run
`37890517304` exposed a 3,933-byte Atlas font reload leak in the real-renderer
preview under LeakSanitizer. LovyanGFX's VLWfont loader replaces allocated
glyph tables without freeing them. Theme reload now unloads the previous font
before repointing its data wrapper and releases failed loads. The preview
regression repeatedly cycles every theme and checks that returning to the
original theme restores identical pixels. GitHub LeakSanitizer validation is
required because local workspace tracing prevents leak detection.

GitHub run `37890951092` validates code commit `f108a34`: all four firmware
jobs passed, including Atlas previews under LeakSanitizer and repeated theme
switches. The font reload leak is resolved. Atlas rebuilt locally at
1,545,293 B flash and 105,276 B static RAM; history records the corrected size.
Android build/unit tests and host tests/contracts also passed in the same run.
TH-006 implementation is complete in draft PR #80; physical device acceptance
remains open. The prerequisite host fixture PR #79 is separately green.

### 2026-10-09 / Codex / E-ink theme identities

- Owner: Codex; branch `codex/master`; baseline `8a2002f`.
- Scope: distinct one-bit icon families and frames for existing Sigil themes;
  calm LED theme defaults with explicit seat colors taking precedence.
- Presentation remains Sigil-owned, validated by existing theme IDs, persisted
  in existing display NVS. No Intent, wire contract or dependency changes.
  Words, life totals, action colors and accessibility patterns remain authoritative.
- Implemented four glyph/frame families, existing font families retained; calm
  defaults follow theme on load/cycle, with independent seat overrides.
- Validation: both firmware builds passed; Sigil Linux host suites passed with
  ASan/UBSan (`detect_leaks=0`: LeakSanitizer cannot run under this workspace's
  ptrace). All four themes rendered for e-paper/OLED; turn comparison inspected.
  Generated tokens and manual export checks passed. Firmware 0.9.16; sizes
  recorded in SIZE_AND_CHANGE_HISTORY. No new dependencies or wire changes.
- Next owner: project owner for review and physical e-paper/LED acceptance.
  Merge with a merge commit and retain `codex/master`.

### 2026-10-09 / Codex / Inversion and header emblems

- Owner: Codex; continuing PR #82 on `codex/master`, baseline `8d54fda`.
- User requested saved inversion on e-ink and possibly all three hardware
  displays, plus a header icon in every theme. Scope: e-ink/OLED/Atlas saved
  independent inversion setting and shared monochrome header marks. Retain
  four themes; no new library entries until these eight looks are reviewed.
- Device-local presentation and existing display NVS; no gameplay Intent,
  transport change or third-party dependency. Words remain the accessible
  meaning; LED preferences and defaults are independent of screen inversion.
- Implemented device-local Invert controls for all three screens. Swaps all
  monochrome pixels; complements Atlas RGB palette, preserving black-on-white
  QR codes for scanning. Shared header marks added to all modern themes;
  Brass keeps its gear. Existing themes and LED preferences stay independent.
- Validation: Atlas and Sigil Linux host suites passed (ASan/UBSan with leak
  detection disabled under ptrace); Atlas touch toggle/selected state and
  Sigil menu paging tested. All normal/inverted themes rendered on all three
  screens; e-paper/OLED gameplay frames assert exact pixel complements;
  e-paper full refresh/cache behavior and Atlas incremental/full redraw and
  repeated theme/inversion restoration passed. Comparison previews inspected.
- Latest firmware builds passed: Atlas 0.7.5, Sigil e-paper/OLED 0.9.17.
  Adapter audit, HTTP/client contract, design token, manual export and whitespace
  checks passed. Sizes recorded; no new dependency or wire contract.
- User confirmed the continuation belongs in PR #82; no additional PR.
  Next owner: review and physical acceptance in STAGED_CHANGES.md. Merge with
  a merge commit and retain `codex/master`; no integration performed here.
- User also requested Python 3.12 pip caching in this same PR. Both CI Python
  setup steps now cache against `tools/ci-requirements.txt`; that file pins the
  existing PlatformIO/cryptography versions and drives their install commands.
  Requirement changes trigger firmware and packaging checks; the redundant
  signing-step cryptography install is removed. No version upgrades.
- CI cache validation: workflow YAML parsed; both Python setup/cache blocks
  match the requested Python 3.12 settings and reference the checked-in pins.
  Packaging/signing tool's five local tests passed; whitespace check passed.
  Actual cache restore/save behavior awaits GitHub Actions.

## TH-008: E-ink joystick cardinal direction correction

- Date / author: 2026-10-09 / Codex.
- Baseline: `673013a`; owner: Codex; branch: `codex/master` (owner requested upload to the persistent branch).
- Scope: reverse both e-ink joystick axes in `Sigil/src/main.cpp`, per owner report that Up/Down and Left/Right are still reversed. OLED and Wokwi keep their existing orientation.
- Remote fetch and open-PR inspection attempted; environment proxy unavailable. Working tree was clean; branch starts at the locally available `origin/master`.
- Validation: compiled the actual `stickConfig()` with the production joystick tracker for e-ink, OLED and Wokwi using GCC C++14 with warnings as errors. All four corrected e-ink cardinal directions passed; OLED and Wokwi configuration checks passed. `git diff --check` passed. PlatformIO is unavailable, so no firmware build or device test was run.
- Status: implemented; physical acceptance requires flashing the updated firmware. Network-permitted retry fetched current origin/master and confirmed no open PRs. Owner authorized publishing on codex/master.

### TH-008 follow-up: both installed joysticks reversed

- Date / author: 2026-10-09 / Codex; baseline `08fcdf5`; owner Codex on `codex/master`.
- Owner confirms both Sigils have Up/Down and Left/Right reversed after flashing, and reports rotating the stick 180 degrees for case installation. Boot logs identify both physical display variants and successful joystick calibration.
- Scope: toggle both axis inversions relative to each current physical build; retain the axis swap and simulated orientation. Add the active orientation to the joystick boot log so reflashing can be verified.
- Validation: GCC C++14 with warnings as errors compiled the actual previous/current firmware configuration and production tracker for e-ink, OLED and Wokwi. All four raw cardinal inputs produce the opposite direction on both physical profiles, return-to-center works, and Wokwi directions remain unchanged. Whitespace check passed. PlatformIO/physical validation remains pending.

## TH-009: Current enclosure print-set README

- Date / author: 2026-10-09 / Codex.
- Baseline: `a3c39db` (owner-uploaded 3D print files). Scope / owner: Codex on `codex/print-files-readme`; document `3D/` inventory, measured mesh extents, fit workflow and installed joystick orientation. Separate worktree because joystick PR #84 is active.
- Status: ready for review; original STL files remain unchanged.
- Validation: parsed all 12 binary STL files; triangle counts match their byte lengths and all vertex coordinates are finite. Measured extents and inspected a rendered mesh overview. README local links resolve and all 12 uploaded models are listed; whitespace check passed. No physical fit or slicer validation claimed. Documentation-only; no CI run.

- Consolidation / owner verification (2026-10-09): owner confirms both installed joysticks now work correctly. At owner request, enclosure README PR #85 is combined into joystick PR #84 on `codex/master`; PR #85 is superseded. Physical enclosure fit remains unverified.

## TH-010: Mandatory resource efficiency

- Date / author: 2026-10-09 / Codex; baseline `2cd5a56`; owner Codex on `codex/master`.
- Owner requests a firm rule minimizing third-party resource time, agent execution and PR count.
- Implemented in AGENT_WORKFLOW.md with entry points in AGENTS.md and CLAUDE.md: batch calls/pushes and related work, reuse applicable evidence, avoid redundant polling/validation, use one agent by default, and justify separate PRs. Required checks and task completion remain mandatory.
- Included in active PR #84 as requested; documentation-only validation is link/reference review and whitespace checks.

## TH-011: Play-note investigation

- Date / author: 2026-10-09 / Codex; branch `codex/master`; baseline `5e067f7`.
- Scope: trace all ten owner play notes, implement only small clear fixes,
  and stage the findings and changes for review. Larger features and hardware
  choices remain proposals. No publishing or integration requested.
- Startup: clean working tree, fetched master and codex/master; both match
  the baseline; no open PRs. Previous active scopes are integrated.
- Status: investigation complete; minor fixes and report staged locally for review.
- Findings: both tablet pause overlays blocked win confirmation; now they leave
  the responding panel reachable. Signup sheet lacked scrolling; scrolling
  added as a Fold mitigation needing device acceptance. Seat B rejects
  already-seated profiles by design and needs seat-level reassignment; other
  feature/hardware proposals are recorded in the report. Front LEDs are now a
  required planned feature, superseding the former case-dependent decision.
- Evidence and next work: [full investigation](Documentation/engineering/history/2026-10-09-play-note-investigation.md).
  Atlas ASan/UBSan host suites, HTTP contracts, adapter audit, real Chromium
  tablet regression, portal pack and whitespace checks passed. Android build
  could not start with the unavailable Gradle/SDK setup; no physical acceptance.
  No firmware code, wire contract, dependency, commit, push or PR created.
  Next owner: project owner reviews the staged fixes and proposed larger work.
- Owner follow-up: considering a router-style Admin portal and retiring web
  gameplay to focus on Android. Recorded the proposed cutover and tradeoffs in
  the report/STAGED_CHANGES; preserve HTTP game APIs used by the app. Shared
  web tablet inclusion is being clarified; no retirement code implemented.

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

## TH-013: App-first play cutover

- Date / owner: 2026-10-10 / Codex; branch `codex/master`; baseline `6fa2627`.
- Owner requested beginning portal play retirement and bridging app parity.
  Latest TH-011/012 handoff explicitly identified by owner and fast-forwarded.
- Scope: game-scoped native polling/selection, reachable custom timer input,
  paired-Sigil attachment, and administration-only web cutover. Preserve Atlas
  HTTP gameplay/tablet APIs, initial Admin bootstrap, cardless recovery, signed
  updates and existing Developer authorization. No new dependencies.
- Feature boundary: Atlas owns/validates game and controller actions through
  existing Intents. Android owns only selected viewing context and presentation
  caches; context changes discard cross-game caches/offline edits. Portal renders
  administration; hiding gameplay UI does not change HTTP authorization.
- Implemented: native venue selector and authenticated/game-scoped polling;
  context changes discard presentation caches and offline edits. Explicit custom
  timers, paired empty-Sigil attachment and app-install QR close reachable app
  gaps. Atlas `expectedGame` rejects stale-screen actions before dispatch.
- Web cutover: full/flash administration pages, preserved Admin bootstrap and
  signed maintenance; retired browser tablet/statistics pages. Firmware
  0.7.6-dev requires portal pack >=2.0.0, so installed legacy packs fall back to
  cardless administration until replaced. Deploy firmware and pack together.
- Validation: 215 Android unit tests and debug APK passed; Atlas ASan/UBSan host,
  storage/profile/OTA suites passed (`detect_leaks=0` for runner restrictions).
  HTTP contracts (10 projections / 10 fixtures), adapter audit (31 adapters),
  design tokens (4 themes / 65 icons), manual export and whitespace checks passed.
  Real Chromium smokes passed full/flash Admin access, explicit save, bootstrap,
  no gameplay requests and 390px layout. Portal pack build passed (17 files).
  Atlas PlatformIO build passed: 105,388 B RAM, 1,545,377 B flash. Sigil unchanged.
- Manual advanced to V0.14; V0.13 archived. Native foreground sound/vibration
  preferences and background win-decision notification remain planned. Physical
  two-game switching, claim attachment, tablet decisions, accessibility and
  cutover/update acceptance remain owner checks; no hardware acceptance claimed.
- Delivery: one draft review PR from `codex/master` to `master`, following
  AGENT_WORKFLOW; owner integration and physical cutover acceptance remain pending.

### TH-013 follow-up: embed administration in Atlas firmware

- Date / owner: 2026-10-10 / Codex; branch `codex/master`; baseline `9e57a04`.
- Owner authorized folding the admin portal into Atlas firmware and retiring
  separate portal packs. Continue draft PR #86 as the same app-first slice.
- Scope: deterministic compressed assets generated from `Atlas/web/src` at
  firmware build time, flash serving for every admin/Developer/update page,
  removal of SD installer and independent pack signing/release/app updates.
- Feature gate: Atlas remains the state/authorization owner; existing admin
  APIs validate actions. Assets are disposable firmware presentation with no
  new persisted state, gameplay Intent, radio contract or dependency. Retain
  explicit saves, accessible controls, first-Admin bootstrap and Developer gates.
- Validation and firmware size will be recorded after implementation. Physical
  acceptance and owner integration remain pending.
- Implemented: one deterministic compressed PROGMEM bundle from the web sources
  in every Atlas build; flash serving for administration, login, Developer and
  local Atlas/Sigil upload pages. No portal reads or uploads use the card.
  Removed pack installer/archive/product 4, independent pack version/signing/
  release assets and Android card/portal update planning. Retired duplicated
  fallbacks and unused browser QR library; retained fonts/licenses in firmware.
- Validation: Atlas PlatformIO build passed (0.7.7-dev): 105,164 B RAM,
  1,728,329 B flash; actual image 1,735,040 B leaves 231,040 B in the OTA slot.
  Complete-image packaging/verification passed with a disposable test key;
  generated bundle matches firmware, reproduces exactly and all files decode.
  Atlas ASan/UBSan application/storage/profile-store suites and separately
  rebuilt OTA scenarios passed; real OTA page registration/gates are now linked
  into the application host suite. Sigil ASan/UBSan suites passed, including
  refusal of retired product 4. Leak detection disabled for runner restrictions.
  215 Android unit tests (24 suites, zero failures/errors) and debug APK passed.
  Chromium administration smokes passed explicit saves, unsaved choices,
  ordinary/Admin access, bootstrap and 390px layout with/without a card;
  maintenance smoke passed local upload/error handling, Sigil target selection,
  Developer hardware controls and authenticated requests with no portal APIs.
  HTTP contracts, adapter audit, tokens, manual export, workflow YAML and
  whitespace checks passed. Manual V0.14 updated within this pending PR.
- Delivery / next owner: extend draft PR #86 on `codex/master`; owner reviews
  and performs physical firmware update, cardless admin/Developer maintenance
  and bootstrap acceptance. No physical flashing, release signing/publication
  or integration performed. Existing app-first physical acceptance remains in
  STAGED_CHANGES; merge with a merge commit and retain the source branch.


## TH-014: Accessibility-only portal appearance after thread handoff

- Date / owner: 2026-10-10 / Codex; branch `codex/master`; baseline `4da302a`.
  Owner identified another thread's completed firmware embedding. Fetched its
  explicit remote ref (this clone's normal fetch covers only master), reviewed
  the handoff, preserved the overlapping local work in a named stash, then
  fast-forwarded. Keep that completed implementation and extend draft PR #86.
- Owner follow-up: remove all decorative portal themes. Scope: portal-only
  System/Dark/Light/High contrast appearance, explicit Save, safe migration of
  old Brass choices to System; remove decorative styling and bundled fonts.
  Shared Android/device themes and full device administration remain unchanged.
- Validation: real Chromium card-present/cardless administration and maintenance
  smokes pass accessibility options, preview/Save, old Brass migration, saved
  High contrast on maintenance pages, firmware uploads and Developer controls.
  Added CI smoke verifies all 13 actual flash-serving C++ gzip/binary responses,
  cache/type headers and unknown paths. Asset generation, manual export,
  whitespace and Android debug APK pass. Reused the unchanged handoff's 215
  Android unit tests, Atlas/HTTP/adapter/Sigil/signing verification.
- Atlas PlatformIO build passes: RAM 105,164 B; flash 1,583,385 B (80.5%).
  Embedded web bytes: 103,155 B. Full image/slot figures recorded in size history.
  No partition changes, physical flashing, release publication or integration.
  Owner still performs cardless device administration, OTA and native play
  acceptance. Continue draft PR #86; preserve the local pre-handoff stash.

## TH-014: Post-cutover leftover audit

- Date / owner: 2026-10-10 / Codex; branch `codex/master`; baseline `d8e621f`.
  Owner asked about remaining unnecessary material. Reviewed portal assets and
  Android references; fetched both remote branches and confirmed master is
  already incorporated. Scope: audit plus small portal cleanup in draft PR #86.
- Corrected a malformed comment introduced during the prior Brass CSS removal,
  removed an unreferenced `showTab` helper, and updated the manifest description
  to reflect device administration. No remaining optional features removed.
- Candidates: retired gameplay CSS; full 65-symbol shared sprite bundled into
  each page despite only a handful of icons used; Android `PlayerPanelCard` and
  its callback type have no callers. Home-screen PNG icons occupy 51,622 stored
  bytes but still provide installation icons, so their removal is a product
  choice. Shared gameplay APIs, storage and retired-package rejection tests
  still have active purposes.
- Validation: asset check, all 13 actual C++ asset responses, Chromium card and
  cardless administration and maintenance smokes pass. Atlas firmware build
  passes: RAM 105,164 B; flash 1,583,305 B (80.5%); stored portal 103,078 B.
  No physical flashing, release or integration. Owner reviews draft PR #86.

### TH-003 / 2026-10-10 / Codex / Player icon implementation

- Owner: Codex, continuing draft PR #86 on `codex/master`; baseline `c0ca713`.
- Scope: scalable default artwork, Android crop/upload/replace/remove and Admin
  review, Atlas-owned bounded SD artwork and public approved-image delivery.
  Current e-paper/OLED Sigil development is frozen by owner; their firmware,
  bitmaps and radio contract stay at the accepted baseline. Future ESP32-S3
  2.8/4-inch layouts remain planned, with provisional 64/128-pixel previews.
- Feature gate: Atlas profiles own artwork/approval; authenticated profile
  upload/remove requests and Admin approve/reject requests change it. Atlas
  validates identity, ordered chunks, JPEG dimensions/length and approval.
  Checksummed SD blobs own image chunks and metadata, using the existing card
  lock; originals never enter NVS or game snapshots. Android renders vectors
  and approved JPEGs, and crops/compresses before transfer. HTTP adds artwork
  routes/metadata; no gameplay Intent or radio change. No production dependency.
  Names remain visible, crop sliders provide an accessible alternative to
  dragging, preview and errors are labeled, and missing media falls back safely.
- Limits: input 20 MiB/32 megapixels; cropped square JPEG up to 512 pixels and
  48 KiB; 768-byte SD chunks keep Atlas scratch buffers bounded. Pending
  replacement does not displace approved art until Admin approval.
- Implementation: default vectors now generate 64-pixel Atlas masks; the native
  app imports, orients, crops and previews bounded JPEGs before ordered upload.
  Approved masters remain on SD; current Atlas uses a 16-pixel RGB332 projection.
  Admin review, replacement/removal and revision-scoped delivery are implemented.
- Validation: Android debug APK and 218 unit tests passed, including ordered
  upload failure and tablet identity handoff. Atlas application, real SD storage,
  profile-store and OTA host suites passed with ASan/UBSan (LeakSanitizer disabled
  because of workspace tracing). All four Atlas themes and both inversion modes
  passed incremental/full rendering comparisons, including visible uploaded
  thumbnail, revision replacement and missing-art fallback. Atlas 0.7.8-dev
  PlatformIO build passed: 109,516 B RAM, 1,646,429 B flash; complete image
  1,653,152 B leaves 312,928 B in the unchanged OTA slot.
- Generated masks/tokens, adapter audit (31 rules), client contracts (10 actual
  plus 10 fixture responses), portal pack and actual flash asset responses
  (13), manual export, workflow YAML and whitespace checks passed. Manual V0.15
  replaces V0.14, which is archived. No Sigil/shared implementation changes.
- Physical acceptance remains: SD removal/replacement during upload, on-device
  crop/approval and Atlas readability, tablet activation/exit and simultaneous
  personal-phone login. Future LCD layouts/firmware and persisted tablet account
  administration are still planned. No hardware flashing, integration or release.
- Handoff: continue existing draft PR #86; owner reviews and accepts hardware
  behavior before integration. Implementation is in the commit containing this
  entry (baseline `c0ca713`).

TH-003 scope follow-up / owner, 2026-10-10: move Open tablet mode to Game;
on activation sign out this device's personal account and automatically use a
separate shared tablet account. Codex implements a token rotation to a RAM-only,
profile-free tablet credential, with an explicit route allowlist, preserving
other devices' sessions/player seats and leaving the app signed out on exit.
This covers the requested dedicated runtime identity; persisted tablet account
management/invitations from TH-002 remain separate planned work.
