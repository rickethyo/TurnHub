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
- **Custom avatars** with Admin approval before they go public.
- **App and portal screenshot tests:** golden images of the Android screens
  in every theme (Roborazzi or Paparazzi on the JVM) next to the portal
  renders.
- **Portal theme packs** from the card; built-in themes stay in the pack.
- **HTTPS on Atlas** (unlocks the portal's full-screen launch and wake lock).

- **Tablet mode, later rounds** (round one, 2026-10-06: Atlas grant and
  seating, the portal's `/tablet` with life, Commander damage, pass, win and
  concede; round two, the same screen in the Android app; see
  PLAYERS_AND_ACCOUNTS.md). Next, as Ricky asked: more counters
  per player (poison, commander tax, energy, experience and similar) in
  `GameEngine` and the state, each also kept in profile statistics as
  "counters received"; player tile backgrounds as a profile personalization;
  tablet seating of Sigil seat B.

### Setup and updates

- **Atlas fetches updates over home Wi-Fi** (optional): an Admin stores a
  home SSID and password (presence-gated); **Check for updates** joins it in
  station mode between games, reads the feed over HTTPS, downloads and
  verifies the same packages, then returns its AP and ESP-NOW to channel 6.
  Needs a CA bundle and about 40 KB heap; can't pass a sign-in page.
- **Atlas name** as a later setup step.

### Table, controllers and accessibility

- **Profile picker:** a per-Sigil startup choice (last profile or picker).
- **Accessibility:** Sigil-local pairing/error lights in the player's style,
  e-ink text scale, a monochrome-safe portal
  theme, guest accessibility preferences. LED intensity and buzzer volume wait
  for hardware that can vary them.
- **Atlas battery gauge.**

### User manual (`Documentation/User Manual/TurnHub Manual V0.11.docx`)

Make a V0.12 (then run `python3 Android/tools/export_manual.py`) that covers:

- The single Sigil menu layout (Sigil 0.9.10): Up always opens Menu on both
  displays, e-ink Menu as compass pages with More, Device as a sub-menu. The
  Commander section still says "E-ink: press Up for Game menu".
- **Partner** commanders (off by default, turned on from Menu).
- Passwords as well as PINs.
- Factory reset of Atlas now empties the microSD card (except the portal
  pack); the manual says the card is not erased.
- A "Setting up a new table" section (first-run setup).
- Shared-Sigil turn order (B left/right) on the Atlas screen.
- Remove (hold 2 s) on the Atlas lobby Player screen.
- The life-approval window choice (15, 30 or 60 s) under Sigil accessibility;
  the manual says requests are accepted after 15 s.
- Nudge from the app and the portal, and what Mute nudges now does.
- The footer still says Manual 0.9, Atlas 0.6.4-dev, Sigil 0.9.6-dev.
- Tablet mode (`/tablet`): turning it on with a table code, adding players
  (PIN-less accounts), panels, and the renamed "Allow Sigil and tablet use
  without a PIN" choice.

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
