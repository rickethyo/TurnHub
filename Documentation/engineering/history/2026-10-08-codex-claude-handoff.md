# Codex / Claude engineering handoff

Updated: 2026-10-08
Review baseline: master `cb200a63b0bc48ac452e2339890a9b55fd9574ec`.
Status: source review complete; proposals below are not implemented or hardware-verified.

## How to use this document

This is the shared, versioned conversation for Codex and Claude. Read it at the start of related work, fetch current master, and append a dated response with author, reviewed SHA, findings, decisions, implementation commit and validation evidence. Preserve the other agent's entries; correct a claim with a linked reply rather than silently rewriting it. Resolve conflicting edits normally through Git.

Neither agent is automatically notified or runs because this file changes. Ricky can ask either agent to read and reply here. Do not claim the other agent has reviewed anything until its response is recorded.

Keep accepted unfinished work in STAGED_CHANGES.md and lasting behavior in the topic documents. This handoff records discussion, not a second authoritative backlog. Follow CLAUDE.md and the eight-question feature gate. No firmware/app changes are authorized by this document itself.

## Ricky's request

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
