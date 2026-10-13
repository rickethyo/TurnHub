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
| TH-016 | Independent LVGL branch synchronization | Git workflow, maintenance tests | Verified and guarded | Owner / select upstream integrations later |
| TH-015 | Atlas persistent, stable battery indication | Atlas battery, power, safety, persistence, tests | Ready for review; local checks pass | Owner / review and physical acceptance |
| TH-017 | ESP32-S3 hardware and LVGL foundation | Hardware, BOM, LVGL | Simulator validated; ready for review | Owner review; future agents use LVGL/README.md |
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

### 2026-10-10 / Codex / Zero-life elimination and concessions
- Owner: Codex on `codex/master`, baseline `c111da0`; no open PR or overlapping active implementation found.
- Owner request: automatic elimination at zero or negative life, on by default,
  editable during play and applied immediately on enable; separate Concede and
  Eliminate controls and recorded reasons.
- Feature gate: Atlas GameEngine owns connected settings/outcomes; ConfigureGame
  validates seated-player changes and allows only this setting during play.
  A separate seat elimination Intent shares domain removal with concession.
  NVS settings and match checkpoints persist the setting and exit reason;
  Android standalone owns its separate equivalent and local history. HTTP state,
  settings and standalone import expose the additions; no radio change or new
  dependency. Controls have text labels and deliberate confirmation for removal.
- Implementation: automatic zero/negative-life elimination defaults on, with
  live settings in personal Game, shared tablet menus (including pause) and
  standalone play. Immediate batch resolution prevents a false winner when
  all remaining players have nonpositive life. Separate confirmed controls,
  state/recovery reasons, local history/import and profile statistics distinguish
  concession from elimination. Existing Sigil “I’m out” records a concession;
  no Sigil firmware or radio contract change. Manual V0.16 supersedes V0.15.
- Validation: all Atlas application/storage/profile-store/OTA host suites passed
  with ASan/UBSan at -O0 (LeakSanitizer disabled for workspace tracing); 20
  actual/fixture contracts, 31 adapter guards, generated tokens and manual export
  passed. Android 222 unit tests passed and the final debug APK (including
  the scrollable table menu) built. Atlas 0.7.9-dev firmware built: 109,580 B RAM,
  1,649,473 B flash. Physical tablet/phone and hardware acceptance pending.
- Handoff: new PR from `codex/master` because PR #86 is already integrated;
  owner reviews/integrates. No hardware flashing, release or merge performed.

### 2026-10-10 — Codex: Magic monarch (active follow-up to PR #87)

- Owner: Codex; branch `codex/master`; baseline `4f65854`. Add a crown and
  “I’m the monarch” control for Magic/Commander, with exactly one holder.
- Feature gate: GameEngine owns the connected singleton player number;
  ClaimMonarch Intent validates a living seated player in an active Magic game.
  Atlas checkpoints persist it; clients render it. Standalone owns its separate
  equivalent. Additive HTTP/state fields only, no radio changes or dependencies.
  Crown has a text/accessibility label and claims transfer immediately. Clear
  when the holder leaves or a new game begins; preserve through pause/recovery.
- Implementation: added per-game monarch state, seated ClaimMonarch intent and
  personal/tablet HTTP controls; nullable state projection and schema 3 recovery.
  Native personal/table tiles show the existing crown icon with a text label;
  Magic/Commander controls transfer it immediately, including while paused.
  Standalone games persist the same behavior. Exiting clears the crown, including
  team removal. Updated Manual V0.16 within the same unintegrated feature PR.
- Validation: 226 Android unit tests passed and final debug APK built; Atlas
  application/storage/profile-store/OTA host suites passed with ASan/UBSan at
  -O0 (LeakSanitizer disabled for workspace tracing). New scenarios cover
  authenticated personal and tablet claims, transfer, pause, recovery, invalid
  formats/states, exits and teams. All 20 state contracts, 31 adapter guards,
  generated tokens and manual export passed. Atlas 0.7.10-dev built: 109,588 B
  RAM, 1,650,065 B flash. Physical phone/tablet and hardware acceptance pending.
- Handoff: included in active PR #87 for owner review/integration; no merge,
  release or hardware flashing performed.

### 2026-10-10 — Codex: Magic counters and statuses (PR #87 follow-up)

- Owner Codex, branch `codex/master`, baseline `a332e61`. Add poison, optional
  automatic poison/Commander-damage loss, commander casts/tax, initiative,
  city’s blessing, energy and experience to connected and standalone games.
- Feature gate: Atlas GameEngine owns connected counters and the initiative
  singleton. Authenticated ChangeCounter and ConfigureGame intents validate
  living seats, Magic format and live rule changes. NVS checkpoints/settings
  persist them; Android renders state. Standalone owns its separate equivalent.
  Additive HTTP/state contracts only; no radio change or dependency. Text labels
  identify status and counter controls, corrections are explicit, automatic
  elimination remains independently switchable while playing. Standard defaults
  are 10 poison (15 shared for 2HG) and 21 per commander, never pooled.
- Implementation: Magic/Commander poison, energy, experience, initiative and
  city’s blessing; Commander command-zone casts with separate derived tax.
  Poison is shared in 2HG. Independent live zero-life, poison and 21-per-commander
  rules default on; poison limit defaults 10/15 on team-mode selection and is
  editable. The common rule finalizer checks loss conditions as one batch.
  Authenticated personal/tablet counter routes share ChangeCounter; state,
  settings schema 3 and checkpoint schema 4 preserve all additions. Native
  personal/table drawers and paused controls expose them; standalone mirrors
  the behavior and recovery. Updated Manual V0.16 in this same feature PR.
- Validation: Atlas application/storage/profile-store/OTA suites passed with
  ASan/UBSan at -O0 (LeakSanitizer disabled for workspace tracing). New scenarios
  cover scoped personal/tablet actions, shared poison, independent damage totals,
  live immediate rule changes, casts, statuses, bounds and recovery. All 20
  state contracts, 31 adapter guards, generated tokens and manual export passed.
  Final Android 231 tests and debug APK passed, including tablet expected-game
  scope. Atlas 0.7.11-dev built: 111,884 B RAM, 1,655,053 B flash.
- Handoff: extend PR #87 for owner review/integration. Physical phone/tablet and
  hardware acceptance pending. Per-counter profile statistics/custom counters
  remain future work in STAGED_CHANGES; this slice tracks current match values.
  No Sigil firmware/radio change, new dependency, merge, flash or release.

### 2026-10-11 — Codex: SD artwork preview and approval investigation

- Owner Codex on `codex/master`, baseline `32c2fe1` after fast-forwarding
  accepted master. User reports uploaded artwork could not be approved.
- Scope: artwork-specific HTTP deadlines and visible preview retry; suppress
  nonexistent backup cleanup/rollback calls without changing commit semantics.
  Preserve size limits, authenticated review and canonical ownership. No new
  dependency, radio change or storage schema. Hardware cause remains unconfirmed.
- Open-PR lookup currently unavailable: GitHub CLI returned HTTP 401; Git fetch
  succeeded. No overlapping unintegrated scope found in shared records.
- Implementation: 30-second artwork read/upload/review deadlines; polling stays
  short. Failed Admin previews explain disabled approval and offer Retry preview.
  First SD writes skip missing backup removal, and failed first commits skip
  nonexistent rollback. Existing replacement/recovery semantics remain intact.
- Validation: all 233 Android tests and debug APK passed; real HTTP regression
  tests cover delayed preview/upload/review and rejection above 48 KiB. Storage
  host scenarios passed with ASan/UBSan at -O0 (LeakSanitizer disabled), including
  first-write failure and successful commit despite backup-cleanup failure.
  Atlas 0.7.12-dev built: 111,884 B RAM / 1,655,089 B flash; whitespace clean.
- Evidence: observed upload contained 44 chunks (about 33 KiB), below the 48 KiB
  limit after client crop/compression. SD-backed image GET validates every chunk
  before headers; the original 3-second timeout can leave approval disabled.
  This is a plausible hardware cause, not a confirmed reproduction. GitHub REST
  lookup recovered and showed no open PR; use a new PR for this integrated-baseline
  follow-up. No merge, release or flash; owner retests on hardware.

### 2026-10-11 — Codex: optional upload approval (PR #88 follow-up)

- Owner Codex on `codex/master`, baseline `0af9ba6`; master already merged,
  PR #88 remains open. Add device-wide upload approval, on by default, selected
  during OOBE and editable by Admins afterward. Off publishes validated new
  submissions immediately; existing pending images retain explicit review.
- Feature gate: Atlas owns device policy; ConfigureUploadApproval Intent validates
  Admin actor and boolean. NVS `imgreview` stores it; missing/bad records default
  on. Android and portal setup/settings render the option; personalization reports
  it. Existing upload validation and SD publication own image persistence. No
  new dependency/radio contract. Text-labeled switch explains local visibility.
- Implementation: NVS-backed Admin ConfigureUploadApproval; policy is reported in
  setup/personalization. Android and portal final startup forms select On/Off;
  Device Settings can change it later. Off publishes valid new submissions to
  the local approved asset and selects it immediately. Existing pending assets
  stay private and retain review controls. Upload text/feedback follows the
  outcome. Archived integrated Manual V0.16; new V0.17 and app asset document it.
- Validation: all 233 Android tests and final APK passed; Off survives setup's
  presence verification retry. Portal with-card/cardless browser tests passed,
  exercising persisted Admin toggle and startup Off form. Atlas application,
  storage, profile-store and OTA suites passed with ASan/UBSan at -O0 (workspace
  tracing requires LeakSanitizer off). Tests cover default-on/private submission,
  unauthorized/invalid policy edits, storage failure, immediate publication,
  retaining older pending assets, re-enabling review and invalid images while
  Off. NVS round-trips/corrupt records tested. All 20 state contracts, 31 adapter
  guards, token check, manual export and whitespace passed. Atlas 0.7.13-dev
  built: 111,892 B RAM / 1,657,585 B flash. Physical acceptance pending.
- Handoff: add to existing PR #88 as explicitly requested. No integration,
  hardware flashing or release. Setup preference/Wi-Fi/stage writes are separate;
  if a later setup step fails, the chosen policy may remain saved and is reported
  on the next setup read. Full image validation and size limits still apply.

## TH-015: Atlas persistent, stable battery indication

- Date / owner: 2026-10-12 / Codex on `codex/master`; baseline
  `01bfe9c3be2b51b7de38d49a9cbdea4d43933db1` (master and agent branch match).
- Status: implemented; ready for review. Clean startup; fetched master and both persistent agent refs.
  Open PR #89 uses `codex/esp32-s3-lvgl`; no overlapping battery scope found.
- Scope: pure gauge/source/checkpoint policy, firmware NVS/RTC adapter,
  voltage-based safety through the existing System Sleep Intent, host regressions
  and engineering documentation. Preserve game recovery and client JSON shape.
- Feature gate: Atlas battery module owns historical estimator evidence (cannot
  reconstruct it from one ADC sample); sampling validates evidence without a new
  game Intent. Existing System Sleep handler owns sleep/recovery authorization.
  Firmware owns `battery/gauge` NVS and RTC copies; pure helpers own validation
  and throttling. Existing screen/HTTP/Android clients render percent/low/charging.
  No radio/API schema or dependency changes. Low warning remains text plus icon.
- Assumptions: source/charging remain voltage-only inferences; charger-held empty
  connectors cannot be identified reliably. No wall clock survives cold restart,
  so checkpoint age cannot be certified; voltage plausibility and sustained
  reconciliation must reject/correct stale evidence. Keep the generic curve;
  its middle slope reaches 7 points per 20 mV and needs physical calibration.
- Implementation commit: `7d6f7bc77b57a18a094696fa592735377b078b8b`.
  Delivery also aligns firmware descriptor/string at 0.7.14-dev and updates the
  engineering record and Manual V0.18 with the Android offline export.
- Decisions: retain the uncalibrated curve; median plus existing 16-sample EMA,
  separate four-sample voltage safety, 1%/30 s cosmetic discharge with sustained
  5/20-point acceleration, two-minute evidence before upward reconciliation,
  debounced 0.75/1.5 s source steps and corroborated drift. Source changes retain
  percentage; charging/full timers never restore. Five live samples validate
  boot history before publication, with critical protection active meanwhile.
  NVS writes require at least 60 s plus meaningful movement; RTC retains newer
  evidence on actual deep-sleep resets. Contradicted estimates invalidate old
  storage history; flash policy compares against NVS even after RTC restoration.
- Validation: all five Atlas host suites passed with GCC C++14, -O1, ASan/UBSan
  (`ASAN_OPTIONS=detect_leaks=0` for workspace tracing). The real battery adapter
  suite was rerun after the final invalidation change and passed. Scenarios cover
  all twelve requested categories, including ADC/load pulses, sustained discharge,
  USB cycles/taper, valid/corrupt/stale/short records, absence/replacement, simulated
  RTC/deep sleep, high displayed percent during critical voltage, wraparound,
  minimum intervals and failed commits, and spurious source pulses. All 20 client
  contract cases, 31 adapter guards, generated tokens, manual export/XML integrity
  and diff whitespace checks passed. Windows source lists updated, not executed.
  Atlas 0.7.14-dev PlatformIO build passed: 112,060 B RAM / 1,660,629 B flash.
  Existing application format-truncation warnings remain outside battery scope.
- Review: fetched again before delivery; master/codex remote baselines unchanged,
  no unrelated working-tree changes. No Android behavior/API/radio changes beyond
  the manual asset; no Sigil work, flashing, merge, signing or release performed.
  Mixed firmware commits run normal PR CI; no CI-skip marker is used.
- Outstanding / next owner: physical acceptance and measured curve/divider/charger
  calibration, source debounce, regulator protection margin, RTC persistence and
  USB sleep wake. Voltage-only detection cannot reliably resolve every charger,
  battery presence and power-source combination. Procedures are in
  [Battery estimation and protection](Documentation/engineering/BATTERY_GAUGE.md)
  and genuinely unfinished checks remain in STAGED_CHANGES. Owner reviews the
  draft PR targeting master and integrates with a merge commit, retaining the
  persistent branch. GitHub CI is pending; no physical testing occurred.

## TH-016: Keep the LVGL development branch independently synchronized

- Date / owner: 2026-10-12 / Codex on `codex/master`; baseline
  `97d0106aa2723d6f4fb48577f6a7d3f4c98f0d9d`; continuing draft PR #90.
- Scope: owner requests keeping `codex/esp32-s3-lvgl` independent of automatic
  fast-forward CI and deferring selected upstream integrations until later.
  Clarify startup/maintenance guidance and pin this boundary with a regression.
- Evidence before edits: the maintenance workflow executes a fixed allowlist of
  `codex/master`, `claude/master`, `agent/experimental`; LVGL is already excluded.
  Its remote tip is `a6dfc59b8c00e2b69c7eccc8c0d40fb9ed2c3521`, with PR #89
  separate from this work. Master remains `01bfe9c`; working tree was clean.
- Status: verified and guarded. Maintenance already excludes the LVGL branch;
  keep its fixed three-branch allowlist and document the owner-directed manual
  integration exception in startup guidance. A new regression verifies exclusion
  even when LVGL would be an ancestor eligible for fast-forward, independent of
  its current divergence or PR status.
- Validation: all five branch-maintenance tests passed; diff whitespace passed.
  No firmware change in this follow-up, so earlier battery validation remains
  applicable. Script/test changes use normal CI, without a skip marker.
- Delivery: extend PR #90 from `codex/master`; LVGL remains at its existing tip.
  This records isolation, not a request to merge either PR or update LVGL.
  Owner selects the scope and timing of later upstream integrations.

## 2026-10-12 laptop return and Claude handoff

- Owner / scope: Codex on `codex/master`; baseline
  `2f8dad4de1563a01f130e2cc99f0581d2645c63f`. Documentation-only wrap-up;
  owner requests leaving build-directory behavior unchanged. Clean tree and no
  stashes at audit; fetched refs. Master, Codex, Claude and experimental remote
  branches all matched this baseline. PR #90 is integrated (merge commit above).
- Integrated battery and workflow work: TH-015 and TH-016 above retain their
  original validation evidence; their review/integration requests are now
  superseded by the merge. Battery physical acceptance and calibration remain
  open in `Documentation/engineering/STAGED_CHANGES.md` and `BATTERY_GAUGE.md`.
- Only open PR at audit: #89, ESP32-S3 hardware/LVGL foundation, on
  `codex/esp32-s3-lvgl` at `a6dfc59b8c00e2b69c7eccc8c0d40fb9ed2c3521`.
  Keep it isolated; do not automatically synchronize it with master. Its own
  `COLLABORATION.md`, `LVGL/README.md`, `LVGL_MIGRATION.md`, hardware/BOM records
  and committed preview evidence own continuation. Desktop milestone validation
  is recorded there; Windows/MSVC and physical board qualification are pending.
  That branch independently used TH-015 for LVGL; use topic names and branch
  names to distinguish it from master's TH-015 battery topic.
- Laptop environment repaired: installed Arduino ESP32 framework lacked its
  entire `variants` directory. Reinstalled the same
  `4.20017.260907+sha.dcc1105b` package; `pins_arduino.h` restored and
  `pio run -e sigil` passed (53,900 B RAM, 948,869 B flash). Installed missing
  `cryptography==46.0.7` into PlatformIO's Python; an ephemeral Ed25519 key
  successfully signed/verified a test message. No real private key was opened.
- Current checkout is `D:\TurnHub\Include`. Main launchers derive the private
  directory from the checkout parent: keys in `D:\TurnHub\Private\TurnHub-keys`,
  signed packages in `D:\TurnHub\Private\TurnHub-builds\local-<time>-<commit>`.
  One PEM exists. Latest observed folder `local-20261012-151655-2f8dad4de156`
  contains Atlas 0.7.14 and both Sigil 0.9.17 packages; existence is not a fresh
  verification of those packages. Intermediate compiler files stay in each
  firmware project's `.pio\build`. Owner declined a new directory setup option.
- Board inventory was saved by the owner's setup run in ignored
  `tools/boards.local.md`; keep hardware identities and private artifacts out of
  commits. This chat did not flash boards or confirm the owner's later flash
  outcome. Next hardware session should confirm installed versions first.
- Legacy `tools/flash-signed-here.*` wrappers still describe the temporary laptop
  and use its old path layout. Use `tools/flash-all.cmd` or `tools/sign-local.cmd`
  on this laptop. No wrapper changes were authorized in this wrap-up.
- Next owner / Claude: read this handoff and the owning engineering references,
  use `claude/master` with normal startup checks, and select the next scope with
  the owner. Preserve physical acceptance items; do not infer completion from
  host builds or package existence. No new firmware changes or repeated full
  regression runs were needed for these local environment repairs.

## 2026-10-12 — Claude: review of Codex work since 2026-10-08; monarch/initiative succession

- Owner Claude, branch `claude/master`, baseline `b9cd577`. Reviewed the 31
  commits since `ec98e03` (battery gauge/checkpoint, live loss rules and
  concessions, monarch/initiative/counters and checkpoint schema 4, SD artwork
  upload/review, shared-tablet sessions, hardware tests, Sigil themes and
  joystick). No defects found in traced paths; Linux host suites, adapter audit,
  client contract, tokens, portal and manual checks all passed on master.
- Owner decision (2026-10-12): follow Magic CR 724.4/725.4. A leaving holder's
  monarch or initiative now passes to the active player, or to the next living
  player in turn order when the active player leaves; it clears only if nobody
  remains. Atlas `GameEngine::eliminatePlayer` and Android
  `StandaloneGame.passStatuses` implement it; `claimMonarch` also rejects during
  a pending win claim, matching counter changes. Manual V0.18 (in place),
  `manual.md`, `GAMEPLAY.md` and `http-v1.md` updated.
- Not changed: recovery still decodes checkpoint schemas 1–3. Dropping them
  would make an old record `UnsupportedSchema`, which leaves recovery read-only
  rather than overwriting it; keep until a factory reset policy covers it.
- Validation: Atlas Linux host suites (ASan/UBSan) passed with updated monarch
  and counter scenarios. Android unit tests were updated but not run locally (no
  SDK in this container); CI runs them. Hardware acceptance pending.
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

## 2026-10-12 — Claude: integrate PR #89 (LVGL/S3 foundation) into master

- Owner direction (2026-10-12): merge #89 as long as current-gen Atlas and
  Sigils still work; current-gen development is parked and this week's focus is
  LVGL and the Android app. Merged `codex/esp32-s3-lvgl` (`a6dfc59`) into
  `claude/master` at master `4da1c1f`.
- Legacy safety: outside `LVGL/` the branch changes only documentation, legal
  notices and preview images; no Atlas, Sigil, shared, protocol, Android,
  PlatformIO or workflow file changes, so legacy builds are unaffected.
- Conflicts were documentation only (engineering index, size history, this
  file); both sides kept. The LVGL topic is renumbered TH-017 here because
  master already uses TH-015 for the battery gauge. `AGENTS.md` and
  `AGENT_WORKFLOW.md` now describe the old branch as historical; the
  maintenance allowlist still excludes it.

## 2026-10-12 — Claude: retire OTA on current-generation Atlas and Sigils

- Owner direction (2026-10-12): legacy builds need not stay intact; the owner
  flashes the current hardware one final time, then never again, and asked
  that it refuse OTA updates after that version.
- `firmware_package.h` gains `Error::Retired` and `otaRetired()`, driven by
  `TURNHUB_OTA_RETIRED`, which both PlatformIO projects set to 1. Atlas fails
  Atlas uploads and denies Sigil package uploads with the retired message;
  `checkUpdateOffer` refuses every offer; Atlas ignores app release reports so
  the update LED stays off. Host suites build with the flag at 0, so existing
  OTA coverage still runs.
- Validation: Atlas and Sigil Linux host suites (ASan/UBSan) and the adapter
  audit passed; a scratch build of `update_offer.h` with the flag on compiled
  and returned the retired message. Firmware builds run in CI. Hardware
  acceptance (an upload and a Sigil offer both refused after the final flash)
  is the owner's next flash.

## 2026-10-12 — Claude: app opens to the game screen while connecting

- Owner request (2026-10-12): after first-time setup, open to the main game
  screen while connecting, and fall back to the current opening screen if
  Atlas doesn't connect within a set time.
- `HomeViewModel.onAppStarted` sets a launch wait when a table is saved;
  `HomeUiState.awaitingTable` holds while no table has arrived, the table
  hasn't been reported not found and no error shows, for at most
  `LAUNCH_WAIT_MS` (15 s, Claude's default). `HomeScreen` then shows the
  bottom tabs, a "Your table" status line, player-card placeholders and "Play
  on this device instead". Device play cancels the wait.
- Validation: three `HomeViewModelTest` cases added (arrival, timeout, not
  found). No Android SDK in this container, so tests run in CI only. Device
  acceptance pending on the owner's phone.

## 2026-10-13 — Claude: LVGL simulator builds and runs on Windows

- Owner request (2026-10-13): get the LVGL simulator running on the Windows
  machine. Installed VS 2022 Build Tools (C++ workload with CMake/Ninja).
- The MSVC build compiled first time, but both self-tests failed injecting
  input: LVGL is a DLL on MSVC and SDL was linked statically into both
  `lvgl.dll` and the executable, so the executable's SDL was never
  initialized (its event queue, event watch and renderer calls went to the
  wrong copy; real keyboard input would also have been lost). MSVC now builds
  SDL as `SDL2.dll`; Linux keeps static SDL.
- Validation: Release build, CTest atlas_ui/sigil_ui passed, both
  `--self-test` runs passed with real windows, Atlas 480x320 and Sigil
  (zoom 2) captures reviewed. README Windows section updated as validated.

## 2026-10-13 — Claude: phone-first app, round 1

- Owner direction (2026-10-13): the app should be fully featured without a
  physical Atlas, with extras unlocked by one. Multi-phone play stays
  Atlas-only. Phone-only play leads with the full-screen (tablet) table. The
  app opens to the game either way and does not push Atlas on phone-only
  users; an in-app switch can hide Atlas altogether.
- Branch `claude/project-thread-orsvc7` from `origin/master` 55d7d43 (the
  session's assigned branch; `claude/master` had no unmerged work).
- Standalone turn timer (`StandaloneGame.turnTimerMs`, `turnTimer()`), saved
  with the game. The tablet turn clock shows `+m:ss` overtime when expired, for
  Atlas tablet mode too. Local per-player statistics (`LocalLibrary.stats`) in
  Device players and history. Launch opens device play when no Atlas Wi-Fi is
  saved; `atlas_enabled` switch in the lobby. Lobby renamed Full-screen table,
  with a larger Start full-screen game button.
- Validation: unit tests added in `StandaloneGameTest` (timer phases, pause,
  recovery, stats). Local Gradle could not resolve dependencies (Maven Central
  HTTP 429 through the container proxy), so tests run in CI. Device acceptance
  and the user manual's wording are pending.

## 2026-10-13 — Claude: app round 2 (owner device feedback)

- Owner feedback (2026-10-13, screenshots): no approve button for a pending
  image, uploads look stuck, the custom life change field rendered one letter
  per line, counters looked basic, and the Game tab was crowded.
- Settings > Uploaded images now lists every pending image with Approve and
  Reject (before, only the per-person sheet had them). Image uploads show a
  progress bar (`AtlasPlayerSession.uploadProgress`, start/chunks/submit).
  The custom change Apply button has a fixed width (`AccentButton` fills its
  constraints). Counters show as colored chips (`CounterChips`) on My seat and
  each life tile; the counter editor folds behind Change and uses round
  steppers. Game setup, elimination rules and table details moved into a Game
  settings screen opened beside a Full-screen table button.
- Validation: local Gradle still blocked by Maven Central HTTP 429; CI runs the
  build and unit tests. Device acceptance pending.
