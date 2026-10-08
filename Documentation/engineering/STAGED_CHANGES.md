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
- **Test harness retired** (2026-10-05): source kept, no builds, no upkeep.

## Open work

### Stability and diagnostics

- **Multi-phone stability** (in progress): bench the Atlas 0.6.7+ build with
  unchanged polling, then the wider matrix, then seat-metadata caching and
  lighter client polling. Steps in [Diagnostics](DIAGNOSTICS.md).
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

### Accounts, portal and app

- **Password hashing:** parked, see "Parked: storage batch" below.
- **High-resolution uploaded avatars and tablet card backgrounds** (*Planned*,
  owner, 2026-10-08; TH-003 in [COLLABORATION.md](../../COLLABORATION.md)):
  upload custom player avatars and player-card background artwork, with crop,
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
- **Portal theme packs** from the card; built-in themes stay in the pack.
- **HTTPS on Atlas** (unlocks the portal's full-screen launch and wake lock).

- **Tablet mode, later rounds** (round one, 2026-10-06: Atlas grant and
  seating, the portal's `/tablet` with life, Commander damage, pass, win and
  concede; round two, the same screen in the Android app; see
  PLAYERS_AND_ACCOUNTS.md). Next, as project owner asked: more counters
  per player (poison, commander tax, energy, experience and similar) in
  `GameEngine` and the state, each also kept in profile statistics as
  "counters received"; uploaded player backgrounds are tracked above;
  tablet seating of Sigil seat B.

- **Standalone tablet game, later rounds** (round one, 2026-10-07: the app
  plays a game alone; round two: Atlas imports each finished game once,
  `POST /api/standalone/import`, protocol/http-v1.md; user manual V0.12,
  section 18). Still to do: showing imported games in the app (today the
  lobby only counts the games still waiting).

### Setup and updates

- **Atlas fetches updates over home Wi-Fi** (optional): an Admin stores a
  home SSID and password (presence-gated); **Check for updates** joins it in
  station mode between games, reads the feed over HTTPS, downloads and
  verifies the same packages, then returns its AP and ESP-NOW to channel 6.
  Needs a CA bundle and about 40 KB heap; can't pass a sign-in page.
- **Atlas name** as a later setup step.

### Table, controllers and accessibility

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
  switch on the tablet page and in the Android app. User manual V0.12
  (section 19) describes round one; update it when these land.
- **Profile picker:** a per-Sigil startup choice (last profile or picker).
- **Accessibility:** Sigil-local pairing/error lights in the player's style,
  e-ink text scale, a monochrome-safe portal
  theme, guest accessibility preferences. LED intensity and buzzer volume wait
  for hardware that can vary them.
- **Atlas battery gauge, later rounds** (round one, 2026-10-08: the voltage
  estimate, see HARDWARE.md "Atlas battery"): calibrate the divider and curve
  on the board, the percent in the Android app, and a manual entry once the
  readings are trusted. Auto sleep (2026-10-08: Atlas idle or empty on the
  cell, Sigils idle; HARDWARE.md, PAIRING_AND_SECURE_LINK.md) also needs its
  manual entry, and its USB wake check a bench test.

### Hardware

- **Sigil carrier PCB:** caliper-check the DevKit rows (22.86 mm assumed) and
  test-fit DevKit and Jewel footprints, finish layout and DRC, then inspect
  assembled boards. The OLED PCB is still a placeholder
  ([KiCad](../../KiCad/PCB/Sigilv1/README.md)).
- **Power:** measure idle, radio, display, buzzer, capped LED and sleep
  current; confirm the USB supplies and power banks hold up.
- Freeze hardware revisions only after GPIO, power, display, transport,
  controls and accessibility decisions settle.

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
- [ ] **G12** Spare Sigil end to end: make spare, pair without a code, return
  to service over the air, flash-all records it.
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
