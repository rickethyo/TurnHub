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

### Permanent tester prestige

- **Closed/Beta Tester recognition** (owner, 2026-10-19; *Planned*): separate
  one-time, non-expiring unlocks for closed testers (gold) and beta testers
  (silver), shown as achievement badges and a matching app launcher icon.
  Keep both earned tiers; settle icon selection/precedence when both are owned.
  This is app-owned recognition; Google Play Games integration is not required.
- **Offline and minimal data:** use a small digitally signed tester credential
  containing an opaque credential ID, awarded tier and issuance metadata, with
  no expiry. Validate locally with the issuer's public key; keep signing keys
  outside the app. Do not collect names, email, contacts or device identifiers
  for this feature, require a new TurnHub account or periodically revalidate an
  earned badge online. Define tester eligibility, issuance and copied-pass policy
  before implementation; installing an arbitrary debug build is not proof.
- **Recovery:** evaluate [Google Block Store](https://developer.android.com/identity/block-store)
  for restoring the credential after uninstall/reinstall. This depends on
  supported Google Play services and enabled Android backup; it is not an
  unconditional permanence guarantee. Offer **Save tester pass** to export the
  signed credential outside app-private storage, and offline import to recover
  after reinstall, on another device or when moving from Preview to the public
  app. Automatic Block Store access is scoped to package name and signing
  certificate, so Preview-to-public migration needs an explicit transfer path.
  Without either a recoverable backup or saved pass, do not promise recovery;
  fully automatic recovery in every case would need an account-backed service.
- **Acceptance before shipping:** verify award eligibility, signature/tier
  validation and tampered-pass rejection; updates, offline launches, retention
  of both tiers, launcher changes, uninstall/reinstall with backup enabled and
  disabled, file export/import, new-device and Preview-to-public recovery, and
  clear recovery/data-use wording. Confirm Block Store suitability and restore
  behavior on the intended Play-distributed builds. No implementation yet.

### More game types

- **Pokémon damage counters and Atlas support** (owner, 2026-10-16;
  *Planned*): Pokémon and Lorcana are in the app (`GAMEPLAY.md`). Next are
  per-Pokémon damage counters (Active and Bench, HP in tens). Atlas, the schema
  and the Sigils learn the new formats and import their games later, per the
  dev sequence. One Piece can reuse the Prize-card count if the owner wants it.

- **Atlas Magic counters** (implemented 2026-10-28, `GAMEPLAY.md`): playtest
  treasure, rad and the rad prompt, day/night, Everyone's counters and
  reversed turns in Atlas-connected play from the tablet and personal phones,
  including Two-Headed Giant, recovery after a power cut and a new match.

### Elimination attribution and full-screen checks

- Verify full-screen due/result windows on phone, foldable and tablet, including
  large type/TalkBack, rotation, Back and Review life totals. Close a preview
  before the first check and a result before the next check; each newly due
  check must open automatically. Reopen it using the tablet center badge.
  Exercise manual and
  automatic “Eliminated by?” prompts, off-turn/already-out sources, explicit Self
  and Unknown, teams and simultaneous/all-out/final removals. Ensure nothing is
  preselected, source questions take priority, and final history/stats wait for
  the answer. Interrupt process/power with a pending source and retry from two
  clients; the question survives and stale/duplicate answers cannot award credit.

### Bag of Doom acceptance

- Play a ten-player phone-hosted and Atlas-connected Commander match. Check the
  fourth-cycle pause from a non-first starter, later one-cycle checks, life
  payments/Commander damage, delayed approvals and explicit corrections.
  Confirm credited/unknown removals, concessions, boundary removal and the
  four-survivor shutoff. Try two phones drawing simultaneously and stale
  requests after switching games. Interrupt power/process before and after a
  draw: a reserved name/result must survive, with no reroll or credited bag kill.
  Verify ten panels, the scrolling bag overlay, rotation, large text and TalkBack.
  Automated checks do not close this physical-device acceptance item.

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
  table, Pass/action-strip edge taps and holds (including drags out of life,
  interrupted holds, narrow/rotated panels, large text and compact Planechase),
  Death May Die active-investigator focus, the investigator/condition/relic Start
  gate, the Elder One tab (symbols, track, ritual, stages, armed losses/win),
  skill/sanity markers and
  reference editing/recovery, draw-two physical/app choices and stable pairs after
  cancel/recovery/rematch, added fourth/fifth skills and marker preservation,
  investigator random/filter/preview and turn-phase reminders, device-wide
  condition/relic rules review, independent face checks/edit invalidation and
  correction-prefill/legacy-note preservation, community card
  search/custom entry, reference-image
  zoom/download/offline recovery, staged Up next handoff and Pass to chooser,
  commander names/printing art, Star layout/Sean Mode and Planechase
  download/deck selection/offline recovery. Check art credits, legibility,
  missing assets and TalkBack. Verify bug-report prefilled values and editable
  form/attachment submission on phone and tablet without automatic submission.
- **TCG shared-device acceptance:** verify personal score/counters/Commander damage,
  team turn controls, lobby/rematch reseating and read-only table switching without
  reconnecting. Exercise stale/lost replies, host background/death, large text and
  TalkBack. Tablet-only verification does not establish two-physical-device Wi-Fi
  routing acceptance. See [shared devices](SHARED_DEVICES.md).
- **Multi-phone stability:** run C06 below; measure CPU/heap/SD/radio behavior
  with current polling and visibility optimizations. S3/second-radio changes
  are not a demonstrated fix. Procedures: [Diagnostics](DIAGNOSTICS.md).
- **Pause-screen stall:** the 2026-09-28 soak report remains unreproduced.
  Carry the scenario into the current stability matrix; historical evidence is
  in [the play-note investigation](history/2026-10-09-play-note-investigation.md).
- **Manual acceptance:** visually verify the current exported manual and run
  the unfamiliar-user setup/play check F02 below.

- **Physical card review:** owner will compare all conditions and both relic
  faces with their collection, correct drafts and supply missing/unclear scans.
  All checks remain pending in the [card review sheet](DEATH_MAY_DIE_CARD_REVIEW.md).
  Source photos/automated tests are not physical-card verification.

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
- **Atlas match history at app parity** (owner, 2026-10-29; Invariant 12):
  one match record format (`protocol/game-record-v1.schema.json`, core
  `gameRecord`), CSV export from the app and Atlas's card history for
  native and imported games (`games.jsonl`, `games.csv`, `/api/profiles/history`;
  `protocol/http-v1.md`, "Match records and CSV") are implemented. Open:
  pulling history for accounts linked before
  (restore already pulls it), a retention limit, the same statistics computed from records on both sides,
  and CSV download from the portal and app.
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

- **More and About** (owner, 2026-10-30): Android's rightmost tab opens
  About, Settings and Store as separate pages. Next parity slice: provide the
  same About/project information from Atlas's Info/Settings surfaces, sized
  for its display, while keeping physical game controls accessible. Owner's
  biography and motivation, donation destination and store offerings are still
  pending; the store has no checkout or purchase links. Validate Back,
  recreation, large text and TalkBack on a device.

- **Shared Death May Die devices:** physical two-/multi-device Wi-Fi, Android
  keystore TLS, QR/paste pairing, investigator ownership and Elder One display
  checks; reconnect, screen lock/background, rotation, process death, large
  text and TalkBack acceptance ([Shared devices](SHARED_DEVICES.md)). Hosting
  is a candidate upgrade; billing/entitlements/pricing remain separate work.

- **Common engine and full phone takeover:** the portable engine is started
  in [`core/`](../../core/README.md) (2026-10-29) and Atlas plays on it
  (2026-10-30, `GameEngine` is now a layer over it; hardware acceptance open).
  Next, Sigils and the Atlas screen render the core's state, the app links it
  through the NDK, the remaining
  modes (Death May Die, Planechase, Pokémon/Lorcana turn markers) are ported
  one per PR, then the old engines and Invariant 1's wording go. Then implement
  the validated handoff, fenced phone authority, reconnect reconciliation and
  durable completion receipts in [Shared table authority](TABLE_AUTHORITY.md).
  Unified lobby/table presentation is implemented; full offline hosting of an
  Atlas game is not.
  When Planechase reaches the core, the Atlas screen gets a Planes tab that,
  as in the app (2026-10-30), shows only while Planechase is on; the Atlas
  New game list then gains its Planechase toggle and the app's other modes.
  New game on Atlas offers what Atlas runs today: Commander (Free-for-all,
  Two-Headed Giant, Bag of Doom, Star Commander), Standard, Yu-Gi-Oh! and
  Other, with starting life, turn timer and house rules.
- **Profile transfer and merge** (owner, 2026-10-28; host-tested, device checks open): profiles live
  on the device that created them and carry all their statistics to an Atlas.
  Done: imported matches accept guest seats and per-(record, profile)
  receipts ([import contract](../../protocol/http-v1.md#standalone-tablet-game-import-2026-10-07)),
  and Android delivers every match with one linked player, re-sending it when
  another of its players is linked, and "Bring players to this table" uploads
  every local account (PIN-less ones together on the signed-in session, owner
  2026-10-28). Automatic sync is in (link keys,
  `/api/profiles/sync`, newest activity wins name/color/avatar/reduced motion,
  owner 2026-10-29; the app syncs whenever the Atlas accounts load and shows
  each linked account's Atlas totals). PINs don't sync. An Admin can merge two Atlas
  accounts (`/api/accounts/merge`: summed totals, merged ID archived and
  redirected for sync and imports). Open: device checks, and a durable merge
  record so a cut mid-merge can't leave totals on both accounts.
- **Device Admin account** (owner, 2026-10-29): implemented in the app (first
  launch prompt, Admin label, "Install your admin account as the device admin?"
  in Atlas setup; see [Accounts](PLAYERS_AND_ACCOUNTS.md#device-admin-account-owner-2026-10-29)).
  "Use on another device" copies one account between the owner's phones and
  tablets by QR over the same Wi-Fi. Open: device checks; syncing settings and
  games between those copies without an Atlas.
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
- **Saved decks per player** (owner, 2026-10-20; *Planned*, idea only): let a
  local account keep a few named decks (commander and tile art), pick one when
  seating the player, and favourite players so they stay at the top. Statistics
  could then split by deck. Plan: Statistics and Accounts docs first; it needs
  an Atlas counterpart per the local-account parity rule.
- **Administration follow-ups:** revisit HTTPS and optional home-Wi-Fi update
  fetching after S3 maintenance contracts settle. The latter needs explicit
  Admin configuration, verified downloads, sufficient heap/CA trust and return
  to the Atlas AP/radio channel. Include the later Atlas-name setup step in
  that setup work. Browser gameplay retirement, native game selection, custom
  timers, empty seat-A attachment and app-install QR are implemented; see
  [Android](../../Android/README.md) and [First-run setup](FIRST_RUN_SETUP.md).

### Table, controllers and accessibility

- **One app and Atlas player experience** (owner, 2026-10-28): Home/Game/Table/
  Settings and Share Play-style Sigil life controls are implemented on physical
  touchscreens, with a read-only Atlas roster. Validate navigation, held/dragged
  touches, modal prompts, shared seats, offline/reconnect, full rosters and themes
  on both boards. Remaining convergence: common player presentation contracts,
  artwork, Planechase, remaining table-wide counter tools and transport-independent actions, following the
  existing shared-authority plan. Each live game keeps one host; direct
  phone-hosted Sigils and live authority transfer are separate work.

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
  Game 2 pairing/order, Sigil startup profile choice, accessible pairing/error
  cues and guest preferences remain open. Sigil is touchscreen-only; LED and
  joystick work is removed from the firmware scope. Retain reduced-motion behavior.
- **S3 theme and battery acceptance:** verify supported themes, contrast,
  QR scanning and persistence on the new displays. Calibrate battery indication
  against real discharge; verify source/charge ambiguity, critical protection,
  interrupted-game recovery, sleep current and USB wake across charge levels.
  Procedures: [Battery estimation and protection](BATTERY_GAUGE.md).

### Hardware

- **S3 boards and production bindings:** resolve the hardware plan's flash,
  audio/UART, connector, power and mechanical holds before fabrication. Build
  board drivers, production Intent/snapshot bindings, SD JPEG loading and S3
  OTA. The Atlas is portrait (320 × 480) since 2026-10-28: re-lay the LVGL
  simulator's Atlas screens for it before binding them. [Hardware plan](hardware/ESP32_S3_REFRESH.md),
  [LVGL migration](LVGL_MIGRATION.md) and [desktop foundation](../../LVGL/README.md)
  own details; the simulator is not physical acceptance. *Experimental*
  (2026-10-28): `env:sigil` runs the Sigil firmware on the old
  E32R28T Atlas board with an LVGL Sigil screen and the five keys as
  on-screen buttons, the default touchscreen-only Sigil until S3 exists; its touch
  calibration is a default, not checked per panel.
- **Player controls and PCB:** Sigil firmware is touchscreen-only (owner,
  2026-10-28): no LED/ring, joystick or sleep. Validate touch hold/release,
  pairing, calibration and portrait layouts on the prototype, then bind the
  S3 touch board. Revisit navigation hardware and power management only with
  an explicit product decision. Verify footprints, power and connectors;
  complete layout/DRC and assembled-board inspection before freezing revisions.
- **Transport and power:** compare S3 memory relief/second-radio options using
  measured multi-phone evidence. Measure idle/radio/display/audio/LED/sleep
  current and power-bank behavior (H03). Product transport, battery/charging
  and manufacturing choices remain in Production decisions below.

- **Mana Tap champion on Atlas** (Planned, owner idea 2026-10-30): Atlas could
  show a "Mana Tap champ" line between games. Needs the Sigils to report best
  scores over the radio (a `protocol.h` change, so Atlas and Sigils reflash
  together) and an Atlas owner for the table-wide record; the app's
  counterpart would be the same game in tablet mode.

## Parked: storage batch

- **NVS capacity and secret hashing:** choose and measure S3 partition capacity
  against intended profile counts; replace Atlas's single SHA-256 secret hash
  with salted PBKDF2 via mbedTLS. Plan the storage reset together. Android local
  accounts already use PBKDF2; this item concerns Atlas. Flash encryption is a
  release decision. See [Accounts](PLAYERS_AND_ACCOUNTS.md) and
  [Storage and Recovery](STORAGE_AND_RECOVERY.md).

## Open checks

Hardware checks that need a deliberate bench setup. Firmware/display/OTA checks
apply to the S3 targets and the E32R28T touchscreen Sigil where supported.
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

## About and donation parity

Android More includes the owner-approved About introduction and Support TurnHub page with PayPal link and supplied QR code. Next slice: expose the same introduction and donation destination in the Atlas portal.
