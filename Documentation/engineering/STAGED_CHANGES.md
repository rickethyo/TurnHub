# Staged Changes and Open Checks

The one list of what is agreed but not done: open work, hardware checks still
to run, and product decisions still to make. When an item lands, move any
lasting fact into its topic document and delete the line here; git keeps the
old entries. Longer designs for planned features are in
[Planned Designs](PLANNED_DESIGNS.md).

Implementation and acceptance are tracked separately below. Builds and host/JVM
tests do not close physical-device, accessibility or lifecycle checks.

## Track and scope

Master is the S3/LVGL track; current-generation hardware fixes belong on
`legacy/gen1`. See [Agent workflow](AGENT_WORKFLOW.md) for branch rules and
[the S3 hardware plan](hardware/ESP32_S3_REFRESH.md) for target status.
Pre-release compatibility policy belongs in `CLAUDE.md` and
[Firmware Updates](FIRMWARE_UPDATES.md).

## Open work

### Android stabilization before the iOS port

- **Stable Android reference for iOS** (owner, 2026-10-16): Android has the
  core product experience implemented: shared navigation, standalone setup and
  gameplay, local accounts/customization, recovery/history/statistics,
  Planechase and Atlas-connected play. It is ready for a focused stabilization
  pass; the open device checks below mean it is not yet an accepted porting
  baseline. Prioritize reliability and settled flows before expanding the iOS
  implementation beyond its existing foundation.
- **Acceptance gate:** select and record an Android build/commit and intended
  initial iOS feature set. Play complete offline and Atlas-connected games on
  physical devices; verify game/account/history recovery after rotation,
  backgrounding, process death and relaunch. Exercise multi-phone play,
  disconnect/reconnect, verified account linking and history delivery, including
  uncertain/rejected results. Check phone/foldable/tablet layouts, large text,
  TalkBack, pending decisions and sound behavior. Use the detailed checks in
  this document, record device/build evidence and fix blocking defects before
  the owner accepts the reference baseline. These checks remain open.
- **Scope of the gate:** stabilize core gameplay, navigation, identity and
  recovery. Export/backup, richer statistics, future common-engine/phone
  takeover work and hardware administration expansion can remain later work;
  completing the entire Android backlog is not required. Once accepted, record
  the baseline and port that bounded experience into native Swift/SwiftUI,
  adapting platform interactions and layouts while preserving behavior.

### More game types

- **Pokémon damage counters and Atlas support** (owner, 2026-10-16;
  *Planned*): Pokémon and Lorcana are in the app (`GAMEPLAY.md`). Next are
  per-Pokémon damage counters (Active and Bench, HP in tens). Atlas, the schema
  and the Sigils learn the new formats and import their games later, per the
  dev sequence. One Piece can reuse the Prize-card count if the owner wants it.

### Stability and diagnostics

- **Android device acceptance:** complete the stabilization gate above. Include
  shared navigation and role-gated administration; signup with the keyboard on
  foldables/tablets; reachable Confirm/Deny during paused win claims; zero-life
  elimination versus concession; shared-tablet re-entry, simultaneous personal
  sign-in and signed-out exit/reconnect. Verify foreground sounds against
  silent/vibrate/media volume, mute, rapid actions and lifecycle transitions.
- **Accounts, discovery and delivery:** verify cold launch with saved Atlas
  preferences never prompts for permission, Wi-Fi or biometric sign-in from
  background discovery. Exercise local PIN changes/removal, account editing/
  deletion, artwork and retained history offline; verified Atlas links,
  new-account public-ID preservation, approval/cardless behavior, automatic
  imports after reconnect and retained partial/rejected results. See
  [Android behavior](../../Android/README.md).
- **Recent Android flows:** playtest the cancellable start countdown, practice
  table, Death May Die active-investigator focus, skill/sanity markers and
  reference editing/recovery, community card search/custom entry, reference-image
  zoom/download/offline recovery, staged Up next handoff and Pass to chooser,
  commander names/printing art, Star layout/Sean Mode and Planechase
  download/deck selection/offline recovery. Check art credits, legibility,
  missing assets and TalkBack. Verify bug-report prefilled values and editable
  form/attachment submission on phone and tablet without automatic submission.
- **Multi-phone stability:** run C06 below; measure CPU/heap/SD/radio behavior
  with current polling and visibility optimizations. S3/second-radio changes
  are not a demonstrated fix. Procedures: [Diagnostics](DIAGNOSTICS.md).
- **Pause-screen stall:** the 2026-09-28 soak report remains unreproduced.
  Carry the scenario into the current stability matrix; historical evidence is
  in [the play-note investigation](history/2026-10-09-play-note-investigation.md).
- **Manual acceptance:** visually verify the current exported manual and run
  the unfamiliar-user setup/play check F02 below.

### Statistics and history

- **Crash-safe statistics:** durable MatchId and completion receipts before
  replaying completion, so a cut during profile writes can't leave missing or
  partial results ([Storage and Recovery](STORAGE_AND_RECOVERY.md)).
- **Game-scoped statistics** (owner, 2026-09-24, the long-term goal): a
  stable `gameProfileId` partitions statistics by game; existing totals stay
  unclassified; a draw counter; raw session facts are canonical and rates are
  derived. Participation facts public by default, derived performance private
  by default with opt-in sharing, enforced on Atlas. Hiding never stops
  recording.
- **Life event log:** record each change (profile, actor, time, old, new);
  rapid same-actor reversals within about a second count as corrections
  (`40 -> 48 -> 47` is +7 gained), with raw events kept.
- **Session history:** bounded game records on the card, enough to rebuild
  aggregates, with a retention limit, separate from the recovery record.
- **Counter extensions:** per-counter profile statistics and arbitrary custom
  counters remain later work; built-in Magic counters/statuses are implemented.

### iOS app

- **Native iPhone/iPad port** (*Foundation implemented; port awaits Android
  stabilization*, owner, 2026-10-16; TH-019): SwiftUI shell, a Foundation-only
  core package, metadata/endpoint contract tests and unsigned simulator CI
  exist; gameplay, persistence and HTTP transport do not. The Android
  stabilization gate above comes first. Use the accepted Android experience as
  the reference for features, flows and behavior. The
  [development framework](IOS_DEVELOPMENT.md) owns the native architecture and
  implementation sequence within the accepted scope: standalone lobby, local
  gameplay and recovery/history, then Atlas connection/shared-table controls.
  [Build and source layout](../../iOS/README.md). Physical iPhone/iPad,
  VoiceOver and lifecycle acceptance remain open. Signing and distribution
  are later owner decisions.

### Accounts, portal and app

- **Common engine and full phone takeover:** implement the portable engine,
  validated handoff, fenced phone authority, reconnect reconciliation and
  durable completion receipts in [Shared table authority](TABLE_AUTHORITY.md).
  Unified lobby/table presentation is implemented; full offline hosting of an
  Atlas game is not.
- **Local library management:** export/backup, explicit history deletion/
  retention and large-history storage/performance remain open. Local player
  editing, PIN removal, account deletion and retained UUID-based history are
  implemented; device checks belong to stabilization above.
- **Connected artwork:** design Atlas-owned player-card background storage,
  transfer, caching, approval and no-card/failure fallback. Local tile artwork
  and connected avatar upload/crop/review are implemented. Preserve player
  identity and sufficient tablet resolution without burdening state polling.
  Device crop/approval/card-removal checks remain open.
- **Native feedback and visual regression:** vibration preferences and a
  background win-decision notification remain planned. Add representative
  theme/layout screenshot coverage; native accessibility and lifecycle checks
  remain part of stabilization, rather than separate cosmetic milestones.
- **Dedicated tablet-only account type:** a restricted RAM-only shared-tablet
  credential already exists ([Accounts](PLAYERS_AND_ACCOUNTS.md)). A distinct
  account type remains planned: no PIN, player seat, personal statistics or
  general Admin access; define its operation allowlist and enforce table scope
  on Atlas. Authorized shared-tablet life edits must preserve the personal
  controller's approval requirements. Existing credential/re-entry acceptance
  is tracked above.
- **Administration follow-ups:** revisit HTTPS and optional home-Wi-Fi update
  fetching after S3 maintenance contracts settle. The latter needs explicit
  Admin configuration, verified downloads, sufficient heap/CA trust and return
  to the Atlas AP/radio channel. Include the later Atlas-name setup step in
  that setup work. Browser gameplay retirement, native game selection, custom
  timers, empty seat-A attachment and app-install QR are implemented; see
  [Android](../../Android/README.md) and [First-run setup](FIRST_RUN_SETUP.md).

### Table, controllers and accessibility

- **Atlas gameplay parity:** add commander names/art and the Star Commander
  Sean Mode rematch through Atlas-owned state and semantic actions. Android
  standalone implementations exist. Two-Headed Giant and Star Commander
  physical playtests remain G13/G14 below.
- **Planechase expansion:** shared digital/physical play, collections, saved
  custom decks and active-team rolling are implemented on Android. Remaining:
  individual player/team decks, official deck-limit enforcement with house
  rules, card scripts and automatic effects with switches/Undo; then Atlas
  parity using the shared scenarios, the LVGL plane screen and a two-panel S3
  bench check. [Gameplay](GAMEPLAY.md#planechase) owns current behavior;
  [Planned Designs](PLANNED_DESIGNS.md#planechase-on-atlas-and-the-plane-screen-planned-owner-2026-10-15)
  owns the remaining design. Device acceptance is grouped above.
- **Controller reassignment:** implement seat-B attachment to tablet-seated
  players and release/reassign a Sigil once all its players are eliminated.
  Preserve match identities/results, define nonadjacent shared-seat ordering,
  and verify recovery, rematch and two-game isolation. Design evidence:
  [play-note investigation](history/2026-10-09-play-note-investigation.md).
- **Scheduled extra turns:** settle grant order, permissions and team behavior,
  then implement temporary turns separately from lobby seat ordering.
- **S3 display/controller behavior:** Atlas touchscreen game selection with
  Game 2 pairing/order, Sigil startup profile choice, player-facing LEDs,
  accessible pairing/error cues and guest preferences remain open. Resolve
  optional waiting-light breathing and variable LED/audio controls alongside
  the S3 input/output design; retain reduced-motion behavior.
- **S3 theme and battery acceptance:** verify supported themes, contrast,
  QR scanning and persistence on the new displays. Calibrate battery indication
  against real discharge; verify source/charge ambiguity, critical protection,
  interrupted-game recovery, sleep current and USB wake across charge levels.
  Procedures: [Battery estimation and protection](BATTERY_GAUGE.md).

### Hardware

- **S3 boards and production bindings:** resolve the hardware plan's flash,
  audio/UART, connector, power and mechanical holds before fabrication. Build
  board drivers, production Intent/snapshot bindings, SD JPEG loading and S3
  OTA. [Hardware plan](hardware/ESP32_S3_REFRESH.md),
  [LVGL migration](LVGL_MIGRATION.md) and [desktop foundation](../../LVGL/README.md)
  own details; the simulator is not physical acceptance.
- **Player controls and PCB:** carry the required player-facing Sigil LEDs
  into the S3 design; settle count, brightness, current and case fit. Evaluate
  the proposed pass/action button alongside the joystick, including semantic
  routing and wake. Verify footprints, GPIO, power and connectors; complete
  layout/DRC and assembled-board inspection before freezing revisions.
- **Transport and power:** compare S3 memory relief/second-radio options using
  measured multi-phone evidence. Measure idle/radio/display/audio/LED/sleep
  current and power-bank behavior (H03). Product transport, battery/charging
  and manufacturing choices remain in Production decisions below.

## Parked: storage batch

- **NVS capacity and secret hashing:** choose and measure S3 partition capacity
  against intended profile counts; replace Atlas's single SHA-256 secret hash
  with salted PBKDF2 via mbedTLS. Plan the storage reset together. Android local
  accounts already use PBKDF2; this item concerns Atlas. Flash encryption is a
  release decision. See [Accounts](PLAYERS_AND_ACCOUNTS.md) and
  [Storage and Recovery](STORAGE_AND_RECOVERY.md).

## Open checks

Hardware checks that need a deliberate bench setup. Firmware/display/OTA checks
apply to the S3 targets once available; master has no physical board drivers.
Gen 1 maintenance checks belong on `legacy/gen1`. Record runs as
`ID | PASS/FAIL | build | device(s) | evidence` here until they pass, then
delete the line. A host or compile result never checks off a hardware line.

- [ ] **C06** Multi-phone stability matrix ([Diagnostics](DIAGNOSTICS.md)).
- [ ] **R05** Cut power at completion before the finished-match checkpoint
  commits: counts don't advance and the match stays recoverable.
- [ ] **R06** Cut power after the checkpoint, during profile writes: restarts
  as Game Over and ending it again can't increment counts (missing or partial
  results are the known limit).
- [ ] **R07** Failed or uncertain NVS commits and corrupt or future recovery
  records: statistics fail closed, errors show in diagnostics, records kept.
- [ ] **D04** Swap the card during and after a game: no double counting, a
  different card works, the NO SD CARD warning follows.
- [ ] **D05** Log rotation on a real card stays bounded; slow writes don't
  disrupt input, radio or display.
- [ ] **U06** Interrupt an update (Wi-Fi drop, Sigil power cut, package
  upload, Atlas restart mid-job) and install a signed image that fails to
  reconnect: everything recovers and the bootloader rolls back without USB.
- [ ] **S09** Secure Link negatives: Reject, the 60 s timeout, portal Codes
  match, eight Sigils at once, forged/replayed/cleartext packets refused.
- [ ] **G11** Playtest S3 Sigil Commander entry/partners and shared-Sigil
  turn order once its presentation/input bindings exist.
- [ ] **G13** Playtest Two-Headed Giant (2026-10-06): four players on separate
  Sigils and on two shared Sigils, Magic and Commander, either teammate
  passing, team life on every client, team concession and the win.
- [ ] **G14** Playtest Star Commander (2026-10-13): five players on phones,
  the tablet and standalone; seat swaps, ally damage refused, a shared win,
  a concession win and the statistics.
- [ ] **H03** S3 power measurements and USB/power-bank behavior (above).
- [ ] **F02** Someone unfamiliar with the build sets up, pairs, assigns
  players and plays using only the kit and manual.
- [ ] **F03** Name the accepted candidate with a tag and matching firmware
  and APK packages.

## Production decisions

- Production Sigil transport: ESP-NOW with the Secure Link today; BLE and
  other options not compared on latency, power, count, interference and
  complexity.
- Maximum Sigil count for product hardware (firmware allows 8).
- Two players per Sigil: product feature, optional mode, or prototype only.
- Protocol version negotiation and compatibility messages for released
  hardware; which radio messages need acknowledgement or deduplication.
- Atlas: separate USB-C power and data, input voltage and regulator,
  battery/backup power, a secure element for authenticity.
- Sigil: battery chemistry and form factor; charging approach.
- First reproducible `ATLAS-REV-A` / `SIGIL-REV-A` with BOMs; enclosure
  mounting and docking after power and connectors settle; a
  design-for-assembly review with the PCBA maker.
