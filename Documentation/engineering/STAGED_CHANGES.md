# Staged Changes and Open Checks

The one list of what is agreed but not done: open work, hardware checks still
to run, and product decisions still to make. When an item lands, move any
lasting fact into its topic document and delete the line here; git keeps the
old entries. Longer designs for planned features are in
[Planned Designs](PLANNED_DESIGNS.md).

Owner, 2026-10-02: playtesting and bench testing have verified nearly
everything implemented. Only the checks listed below are still open.

## Standing decisions

- **No compatibility before release** (owner, 2026-09-30): reflash every
  board together, no saved-data migrations, a changed layout means a factory
  reset (`CLAUDE.md`). For release, the version rules in
  [Firmware Updates](FIRMWARE_UPDATES.md) come back first.
- **The OLED Sigil leads** (owner, 2026-09-28): new Sigil UI lands on the OLED
  first; the e-ink follows and may drop extras that cost it full refreshes.
- **No turn timer on e-ink**; try it on a future LCD Sigil.
- **Test harness retired** (2026-10-05); moved off master with the rest of Gen 1.
- **Gen 1 split** (owner, 2026-10-13): the Gen 1 ESP32 Atlas/Sigil firmware,
  harness, Sigil v1 PCB, enclosures, flash/board-setup scripts and firmware
  release workflow live on `legacy/gen1`, a one-way branch for fixing Gen 1
  bugs in place. Master is the LVGL/S3 track with one `sigil` build
  (headless behind `sigil_display.h`, pins in `sigil_board.h`) and a
  board-less Atlas (`atlas_board_none.cpp`); the spare path is gone. Still on
  master until `atlas-s3`/`sigil-s3` land, because the Android update flow
  reads them: the OTA product names (`sigil-eink`, `sigil-oled`),
  `CAPABILITY_DISPLAY_OLED` and `TURNHUB_OTA_RETIRED`.

## Open work

### Planar deck selection

- **Owner request, 2026-10-15:** filter the digital planar library by release /
  collection, including Planechase Anthology, and use the chosen pool for play.
  Proposed extension: browse card previews, select individual cards and save a
  custom deck. Keep All planes as a simple default and selection before a game.
  Current download deduplicates cards (`unique=cards`); one `setName` per card
  cannot establish membership in every reprint collection. Obtain actual release
  membership before implementing collection filters. Selection must persist
  locally and the standalone engine must validate/deal the chosen pool, including
  phenomena handling; preserve active-game decks and offline play.


### Stability and diagnostics

- **Android shared navigation acceptance:** verify Game/Table/Players/History/
  Settings with a live Atlas, role-gated administration, disconnect/reconnect
  and TalkBack. Local build 7 navigation was checked on an API 37 emulator;
  the physical phone was unavailable. The updated V0.20 manual text/export
  passes validation; page rendering awaits a host with LibreOffice.

- **2026-10-09 play-note investigation:** source findings and proposed designs
  for all ten notes are in [the report](history/2026-10-09-play-note-investigation.md).
  Minor tablet overlay and signup scrolling fixes are staged on `codex/master`;
  the former browser verification passed; Android build and device acceptance are tracked in TH-013.
- **Multi-phone stability** (in progress): bench the Atlas 0.6.7+ build with
  unchanged polling, then the wider matrix. Android now reduces unseen-table
  polling and redundant metadata/settings reads (TH-018); CPU/heap/SD/radio
  effects remain unmeasured. Steps in [Diagnostics](DIAGNOSTICS.md).
- **Phone-owned accounts and background discovery acceptance (TH-018):** test
  cold launch with saved Atlas preferences, process recreation/rotation, no
  permission/Wi-Fi/biometric prompt from the probe, local PIN settings, images
  and verbose history without an Atlas. Test new-account ID preservation on
  Atlas 0.7.15+, verified merge into existing/pinless accounts, approval/cardless
  behavior, automatic imports after reconnect and retained partial/rejected
  results. Current Gen 1 firmware remains on `legacy/gen1`; master firmware
  builds are board-less. Manual V0.19 content/export passed; visual rendering
  awaits a host with LibreOffice.
- **Open bug, not reproduced:** Atlas stuck on the pause screen during a
  harness soak (2026-09-28).

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

### iOS app

- **Native iPhone/iPad app** (*Foundation implemented; gameplay planned*, owner,
  2026-10-13; TH-019): SwiftUI shell, a Foundation-only core package, metadata/
  endpoint contract tests and unsigned simulator CI. The
  [development framework](IOS_DEVELOPMENT.md) owns the architecture, milestone
  sequence and acceptance criteria. Next: standalone lobby, then full local
  gameplay and recovery/history; Atlas connection and shared-table controls
  follow. [Build and source layout](../../iOS/README.md). Physical iPhone/iPad,
  VoiceOver and lifecycle acceptance remain open. Signing and distribution
  are later owner decisions.

### Accounts, portal and app

- **Common engine and full phone takeover** (owner, 2026-10-13): shared lobby
  and live table presentation are unified. Extract one portable engine for
  Atlas/native apps, prefer Atlas after a validated handoff, and let the
  designated phone host all game actions after fenced disconnection.
  Checkpoint/command contracts, authority leases, reconnect reconciliation and
  durable completion receipts are designed in [Shared table authority](TABLE_AUTHORITY.md)
  and remain to be implemented. Current connected offline behavior still queues
  life/Commander edits only.

- **Router-style web administration** (*Implemented locally; device acceptance pending*, owner confirmed, 2026-10-09):
  web portal for Admin settings/maintenance and Android for personal/shared
  tablet play; retire browser gameplay, including the web tablet. Preserve the
  app's HTTP game APIs.
  Define app parity, initial-Admin bootstrap, diagnostics permissions,
  cardless administration/updates before removal. [Feature parity audit](history/2026-10-09-portal-app-parity.md)
  records existing app coverage and missing custom timer entry, multi-game
  selection, empty paired-Sigil attachment, invite QR generation and local
  event feedback settings. Hardware tests/local signed-package uploads remain
  web-admin features. Implemented in the current TH-013 slice: administration-only embedded pages in Atlas 0.7.7-dev,
  retired separate pack build/install/app/release flows, retired tablet/statistics routes, preserved bootstrap and
  signed maintenance, native game/session polling, custom timer entry, empty
  seat A attachment and app-install QR. Android unit tests/debug build, Atlas
  host/firmware checks and administration browser checks with/without a card pass; device
  acceptance remains pending; native vibration preferences/background decision alert remain open.
- **Android game sounds device acceptance:** confirm pass/start/pause/resume/
  win/draw cues in local and connected play, rapid actions, silent/vibrate/media
  volume, mute while a cue plays, lifecycle/reconnect suppression and accessible
  labeled sound/volume controls. JVM transition tests and a build do not verify
  speaker behavior.
- **Password hashing:** parked, see "Parked: storage batch" below.
- **High-resolution uploaded avatars and tablet card backgrounds** (*Planned*,
  owner, 2026-10-08; TH-003 in [COLLABORATION.md](../../COLLABORATION.md)):
  Connected avatar upload/crop/review is implemented in PR #86 (2026-10-10).
  Standalone account avatars/crop/preview/replace/remove are implemented in TH-018;
  physical acceptance remains above. Remaining: player-card background artwork, with crop,
  preview, replace and remove controls. Preserve enough image detail for large
  tablet cards; choose explicit source/display dimensions and file limits during
  design rather than reducing everything to tiny icons. Retain the existing
  Admin-approval requirement for avatars before public display.
  Connected assets need an Atlas-owned metadata/storage design; standalone
  players must be able to use local artwork without owning Atlas. Scope artwork
  to player identity, not the tablet-only account. Plan device-sized derivatives
  and caching so large originals do not burden Atlas RAM or game-state polling.
  Define SD storage/failure behavior and safe fallback artwork; play must remain
  available without an image or SD card. Exact storage/transfer contracts remain
  to be designed.
- **Fuller tablet-mode presentation** (*Planned*, owner, 2026-10-08):
  "de minimize the tablet mode screen." Working interpretation: richer player
  cards, prominent avatar/background artwork, and more useful game information
  and controls visible without opening secondary views. Prepare a visual design
  before implementation to settle density and which controls remain exposed;
  this is not yet a request to disable Android immersive/full-screen mode.
  Keep life totals, active-player state, names and touch controls readable over
  custom artwork, with contrast overlays and accessible labels. Apply the
  presentation direction to connected and standalone tablet play, including
  phone/foldable layouts. Verify crop fidelity, large text, orientation changes,
  missing/rejected images, and responsive gameplay while images load.
- **App and portal screenshot tests:** golden images of the Android screens
  in every theme (Roborazzi or Paparazzi on the JVM) next to the portal
  renders.
- **Portal theme packs:** retired by owner, 2026-10-10; the portal keeps only
  System/Dark/Light/High contrast accessibility appearance.
- **HTTPS on Atlas:** revisit administration transport after the app-first cutover; native tablet wake/immersive behavior does not depend on portal HTTPS.

- **Zero-life elimination and separate concessions** (*Implemented locally*,
  owner, 2026-10-10): automatic elimination at zero/negative life defaults on,
  editable in lobby and during running/paused play; enabling checks everyone
  immediately. Concede and Eliminate have separate controls, state, saved exit
  reasons and statistics, in connected and standalone play. Atlas host/firmware
  checks and 222 Android unit tests pass; physical acceptance pending. See [GAMEPLAY.md](GAMEPLAY.md).

- **Tablet mode, later rounds** (round one, 2026-10-06: Atlas grant and
  seating, the portal's `/tablet` with life, Commander damage, pass, win and
  concede; round two, the same screen in the Android app; see
  PLAYERS_AND_ACCOUNTS.md). Magic counters and statuses now include poison,
  commander casts/tax, energy, experience, initiative and city’s blessing,
  with live poison/Commander elimination settings and shared team poison.
  Physical native acceptance remains pending. Per-counter profile statistics,
  arbitrary custom counters and tablet seating of Sigil seat B remain future
  work; uploaded player backgrounds are tracked above.

- **Seat B attachment:** the picker offers already-seated tablet players but
  the handler rejects them. Needs identity-preserving seat-level reassignment,
  including an explicit order policy for nonadjacent shared players; see the
  play-note report. Do not solve it by removing a rejection alone.
- **Tablet win-claim acceptance:** verify reachable player-facing Confirm/Deny
  without the central Resume overlay on Android and the portal, including
  sequential four-player claims and denial. The browser regression passes;
  Android compilation/physical testing remains outstanding. Later UX: claim
  progress, responder/waiting labels and decision-first controls.
- **Fold signup acceptance:** verify staged scrolling with keyboard open,
  folded/unfolded, rotation, large text and long account lists; retain the
  7- and 10-inch tablet checks.

- **Standalone tablet game, later rounds** (round one, 2026-10-07: the app
  plays a game alone; round two: Atlas imports each finished game once,
  `POST /api/standalone/import`, protocol/http-v1.md; user manual V0.12,
  section 18). Local UUID players and retained history/basic local totals now
  and explicit per-match Atlas mapping/import/rejection status exist on
  `codex/app-only-entry` (TH-001 B/C; Android/README.md). Remaining: export/backup,
  deliberate deletion/retention controls, local-player editing, storage/performance
  work for large histories, and physical device acceptance.

### Setup and updates

- **Atlas fetches updates over home Wi-Fi** (optional): an Admin stores a
  home SSID and password (presence-gated); **Check for updates** joins it in
  station mode between games, reads the feed over HTTPS, downloads and
  verifies the same packages, then returns its AP and ESP-NOW to channel 6.
  Needs a CA bundle and about 40 KB heap; can't pass a sign-in page.
- **Atlas name** as a later setup step.

### Table, controllers and accessibility

- **Commander names, card art and Sean Mode on Atlas** (owner, 2026-10-15):
  the Android phone-hosted table has optional commander names with Scryfall
  art and the Star Commander Sean Mode rematch. Atlas games need a profile or
  participant commander field in `/api/v1/state`, an Intent to set it and a
  Sean Mode rematch Intent before the app can offer them with Atlas. Device
  check of the art credit, scrim legibility and TalkBack still open.

- **Planechase** (owner, 2026-10-15): the standalone round is built
  ([Gameplay](GAMEPLAY.md#planechase)). Still open:
  - The device check of the Planes tab, the download, art credit and
    TalkBack.
  - Round 2: own decks and Two-Headed Giant, the deck builder, official deck
    limits and house rules, card scripts, and automatic effects with
    switches and Undo.
  - Round 3: Atlas parity, with the host suite running the shared scenarios.
  - Round 4: the plane screen on the LVGL simulator, then a two-panel S3
    bench check.

  Design: [Planned Designs](PLANNED_DESIGNS.md#planechase-on-atlas-and-the-plane-screen-planned-owner-2026-10-15).

- **Eliminated-controller release** (owner, 2026-10-09): permit release once
  all players on a Sigil are eliminated, preserving their match identities and
  results; then attach the hardware to another seated player. Needs seat-level
  reassignment, recovery, rematch and two-game isolation; report has the design.
- **Temporary turns** (owner, 2026-10-09): support gifted extra turns using
  a separate scheduled-turn model; grant order, permissions and team behavior
  require decisions before implementation.
- **Waiting LED breathing** (decision open): waiting is intentionally steady
  in each seat's custom color today. Evaluate optional gentle breathing while
  retaining steady reduced motion and distinct turn/decision cues.
- **Tablet re-entry and dedicated tablet-only accounts** (*Planned*, owner,
  2026-10-08; discussion TH-002 in [COLLABORATION.md](../../COLLABORATION.md)):
  - A seated player can reopen tablet mode during an active game after leaving
    that screen, without giving up their seat, resetting the game or creating
    another participant.
  - Add a distinct **Tablet-only account type**, not a player role. It has no
    PIN, earns no statistics of its own, and cannot take a seat or become a
    player participant. It represents a shared table screen.
  - Restrict it to capabilities needed to operate tablet mode, including game
    setup settings. It cannot change unrelated account, administration,
    network, device, firmware or system settings. Define the exact tablet
    operation allowlist before implementation; do not grant general Admin
    access to make tablet mode work.
  - Shared tablet mode can change player life directly, without the ordinary
    approval request. Apply that capability to authorized tablet operations,
    including a seated player's shared-tablet session, without granting the
    same bypass to ordinary personal-controller requests.
  - Atlas must validate account type, table scope and permitted operations on
    the server through the existing semantic action path. Hiding buttons or
    accepting a client-supplied tablet flag is not authorization. Player
    results/statistics remain attributed to the actual players.
  - Acceptance: seated-player re-entry mid-game preserves game/seat state;
    tablet-only access needs no PIN; seat-taking and own-stat creation are
    rejected; game setup and direct life edits work; unrelated settings remain
    rejected even through direct API calls; personal-controller life approval
    remains intact. Verify isolation between the two Atlas games.


- **Two games per Atlas, later rounds** (round one, 2026-10-07: two tables,
  Sigil "Switch game", the portal switch; see PLANNED_DESIGNS.md): the Atlas
  touchscreen game selector (with pairing and turn order for Game 2), a
  native personal/shared-tablet selector is implemented in TH-013, awaiting device acceptance. User manual V0.12
  (section 19) describes round one; update it when these land.
- **Profile picker:** a per-Sigil startup choice (last profile or picker).
- **Accessibility:** Sigil-local pairing/error lights in the player's style,
  e-ink text scale, a monochrome-safe portal
  theme, guest accessibility preferences. LED intensity and buzzer volume wait
  for hardware that can vary them.
- **Device theme acceptance (2026-10-09, PR #82):** owner to check all four
  normal/inverted looks on Atlas and both Sigils, header emblems, e-paper full
  refresh/ghosting, inversion retained through reboot/unpair and cleared by
  factory reset, scanning Atlas QR codes, and calm LED colors with independent
  shared-seat preferences. Host previews/tests and firmware builds passed;
  physical appearance/persistence acceptance is still open.
- **Atlas battery physical acceptance (TH-015, 2026-10-12):** persistent stable
  indication, debounced source inference and independent voltage protection are
  implemented. Calibrate the ADC divider and generic curve against real timed
  discharge; confirm source/absence ambiguity, charge/taper rate, sustained load
  changes, critical/emergency latency before regulator brownout, checkpoint
  restart/deep-sleep continuity, interrupted-game recovery, sleep current and
  USB wake at full/middle/low charge. Procedures and limitations are in
  [Battery estimation and protection](BATTERY_GAUGE.md). Android/display device
  acceptance remains pending; manual now describes the implemented behavior.

### Hardware

- **S3 generation / LVGL** (2026-10-11, TH-015): source-reviewed ES3C40P/ES3C28P
  direction and partial prototype BOM are in [the hardware plan](hardware/ESP32_S3_REFRESH.md).
  Resolve listed flash/audio/UART conflicts, supplier prices/MOQ, connector families,
  battery/boost power and physical measurements before hardware fabrication.
  [LVGL desktop foundation](../../LVGL/README.md) now builds and exercises both
  profiles, shared themes and interactive fixtures; [migration architecture](LVGL_MIGRATION.md)
  preserves the production boundaries.
  Per-board firmware, production Intent/snapshot bindings, SD JPEG loading and S3
  OTA remain pending. Legacy e-ink/OLED features stay frozen.

- **Player-facing Sigil LEDs are required** (owner, 2026-10-09): implement
  the existing GPIO13/J6 front-strip plan on both display variants; choose
  count/location/brightness and verify current and case fit.
- **Dedicated pass/action button alongside joystick** (proposal): settle
  Pass versus contextual Action behavior, then GPIO/carrier/wake design and
  shared semantic input routing. Keep the joystick.
- **Atlas S3/second-radio evaluation:** capture current multi-Android failure
  evidence first. A PSRAM S3 board addresses movable memory pressure; a wired
  radio bridge may isolate transport contention. Neither is a confirmed fix;
  the report records port requirements and a common acceptance matrix.
- **Sigil carrier PCB:** caliper-check the DevKit rows (22.86 mm assumed) and
  test-fit DevKit and Jewel footprints, finish layout and DRC, then inspect
  assembled boards. The OLED PCB is still a placeholder (`KiCad/PCB/Sigilv1/`
  on `legacy/gen1`).
- **Power:** measure idle, radio, display, buzzer, capped LED and sleep
  current; confirm the USB supplies and power banks hold up.
- Freeze hardware revisions only after GPIO, power, display, transport,
  controls and accessibility decisions settle.

## Frozen legacy Sigils (owner, 2026-10-10)

Owner, 2026-10-12: the current-generation Atlas is retired too. After one final
USB flash, both refuse every OTA update ([Firmware Updates](FIRMWARE_UPDATES.md#retired-hardware-no-more-updates));
open checks below that need further legacy flashing (U06, G12) apply to the S3
hardware instead.

Current e-paper/OLED Sigil feature development is frozen. Future display work
moves to ESP32-S3 ES3C28P/ES3C40P candidates; source specifications and verification
holds are documented above. Hardware firmware targets and artwork transfer/cache
remain to be built. Player artwork's
512px SD master and provisional 64/128px app previews are implemented in PR #86;
physical acceptance of crop fidelity, card removal/reinsert, approval and Atlas
thumbnail display remains outstanding. Background artwork and standalone local
player images from TH-003 remain planned separately.

Shared tablet credential handoff is implemented in PR #86: Game-tab entry,
automatic replacement of this device's personal token with the restricted
Shared tablet account, and signed-out exit. Physical simultaneous tablet/phone
sign-in and exit/reconnect acceptance remains pending.

## Parked: storage batch

*Planned.* Proposed and parked by the owner (2026-10-06): do these together, since
both need a USB flash and a factory reset.

- **Larger NVS partition.** `min_spiffs.csv` gives NVS 20 KB (about 500
  entries) and each profile uses about 20, so the real ceiling is about 20
  profiles, not the 64 the code allows (*Needs verification*: estimated
  from code, not measured). Fix: a custom partition table with a bigger NVS.
- **Slow PIN and password hash.** Today it is one SHA-256 over the profile ID
  and secret (`web_session.cpp`), which a flash dump can brute-force. Fix:
  PBKDF2 via mbedTLS. Full protection also needs flash encryption (eFuse), a
  release-time decision.

Decided at the same time: storage stays NVS plus checksummed SD blobs, with
no SQL database. SQLite would add RAM pressure and power-loss risk on FAT,
and would make the card required. Revisit only for full per-match history, which would use
append-only files on the card.

## Open checks

Hardware checks that need a deliberate bench setup. Record runs as
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
- [ ] **G11** Playtest Sigil Commander entry and partners (Sigil 0.9.10) and
  shared-Sigil turn order (2026-10-05).
- [ ] **G13** Playtest Two-Headed Giant (2026-10-06): four players on separate
  Sigils and on two shared Sigils, Magic and Commander, either teammate
  passing, team life on every client, team concession and the win.
- [ ] **G14** Playtest Star Commander (2026-10-13): five players on phones,
  the tablet and standalone; seat swaps, ally damage refused, a shared win,
  a concession win and the statistics.
- [ ] **G12** Spare Sigil end to end (`legacy/gen1`): make spare, pair without
  a code, return to service over the air, flash-all records it.
- [ ] **H03** Power measurements (above).
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
- A decision log for major product decisions and reversals.
