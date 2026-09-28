# Prototype v1 verification checklist

Working branch: `codex/prototype-v1-stabilization`.
Base reviewed: `959a09b416bdcd374d2aa30f30be96fc01dc970f`, 2026-09-26.
Status: stabilization in progress; **not a release-candidate acceptance record**.

This is the current field-test checklist. The broader
[verification backlog](VERIFICATION_BACKLOG.md) also contains historical research
and production decisions that are not v1 release gates. A checked host test does
not check off the corresponding hardware test. Re-run affected checks whenever
the candidate changes; a previous build's success is not evidence for new code.

## Candidate and evidence record

Fill these in before the physical run:

| Field | Value |
|---|---|
| Candidate commit / tag | Pending |
| Atlas build identity and board | Pending |
| Each Sigil ID, display/input variant and build identity | Pending |
| Harness build identity | Pending |
| Android build and phone / OS | Pending |
| Browser / OS | Pending |
| SD card model / capacity and power source | Pending |
| Tester and date | Pending |
| Logs, photographs and failure notes | Pending |

Record each executed line as `ID | PASS/FAIL | candidate | device(s) | evidence`.
Keep failures visible until a rerun passes. Use test profiles and expendable media
for interrupted-write checks. New features stay outside this stabilization slice;
defects, recovery, already-agreed update/setup work, accessibility, field hardware
and verification remain eligible.

## Remaining implementation and scope

- [x] **P01** Commit the finished-match checkpoint before statistics writes;
  fail closed on checkpoint errors. Host evidence: **A01**. Physical evidence:
  **R05-R07**. See [completion ordering](COMPLETION_RECOVERY.md).
- [ ] **P02** Complete durable match/completion receipts and replay-safe result
  persistence before claiming crash-safe, exactly-once statistics. P01 prevents
  the reproduced duplicate but can leave missing or partial results after a cut.
- [ ] **P03** Implement Sigil OTA: authenticated SD package upload/catalog,
  variant and integrity checks, named-device update/queue, retained compatible
  reinstall, boot confirmation and interrupted-update recovery. See **U01-U06**.
- [ ] **P04** Implement safe SD removal/reinsertion and remount, including resuming
  consumers without racing the diagnostics worker. Current firmware needs a restart.
- [ ] **P05** Set the field kit's required profile-selection behavior, including
  e-ink Seat B, duplicate-name labels and startup choice. Finish required paths;
  record any explicitly deferred option. Do not treat the Seat-A picker as all
  planned selection work being complete.
- [ ] **P06** Finish and validate the three-Sigil field kit. Carrier footprints and
  schematics exist; PCB layout and physical fit remain unfinished. See **H01-H04**.

## Automated checks for this branch

- [x] **A01** Atlas application scenarios pass, including draw/win/elimination
  completion interrupted after each profile write, failed/uncertain checkpoint
  commits, read failures, corrupt/future-record protection and recovery without replay.
- [x] **A02** Atlas storage scenarios pass, including legacy statistics, core
  counts, injected NVS failures and bounded SD record/log behavior.
- [x] **A03** Real profile-store scenarios pass, including guest/reconnect behavior,
  migration and the NVS/core versus SD/detail statistics split.
- [x] **A04** Sigil OLED, LED and menu host suites pass.
- [x] **A05** `audit_adapters.py` passes with no controller-side canonical mutation.
- [x] **A06** `check_client_contract.py` passes for generated responses and shared fixtures.
- [ ] **A07** Portal smoke and two-context counter smoke pass on the candidate.
- [ ] **A08** Android `testDebugUnitTest` passes on the candidate.

Atlas: `Atlas/tests/host/run-gcc.ps1` or `run.cmd`; Linux command equivalents are
in that directory's README. Sigil: `Sigil/tests/host/run-gcc.ps1`. Run the adapter
and contract Python checks from the repository root after Atlas scenarios.
No device is flashed by these host checks.

## Firmware and application build gate

- [x] **B01** Build Atlas `atlas`; record toolchain and RAM/flash usage in the size ledger.
- [ ] **B02** Build e-ink `sigil` and OLED `sigil-oled`; record sizes and variant identity.
- [ ] **B03** Build `sigil-wokwi` and TestHarness `harness` without breaking either target.
- [ ] **B04** Build Android `assembleDebug`; retain the APK identity with the candidate.
- [ ] **B05** Flash the matched candidate to the field kit and confirm each running
  build/variant. Identify boards before choosing ports; never assume a COM number.

## Setup, pairing and player identity

- [ ] **S01** From a factory-reset Atlas, complete setup by QR/code and create the
  first Admin. A phone without the displayed code cannot claim Admin access.
- [ ] **S02** Join the Atlas network and portal without an internet connection.
  Android connects to the same physical Atlas and displays its actual identity.
- [ ] **S03** Pair all three field Sigils in the chosen pairing window. A request
  outside the window is refused; Pair is usable without entering the bootloader.
- [ ] **S04** Cold-boot Atlas/Sigils repeatedly; saved devices reconnect to their
  own Atlas and do not adopt a neighboring Atlas.
- [ ] **S05** Forget one Sigil, re-pair it, then exercise forget-all and Sigil-local
  unpair. Device lists and both ends' retained pairing agree after reboot.
- [ ] **S06** Select/change a saved profile and choose Guest on both display variants.
  Authorization and duplicate-participant protection survive controller handoff.
- [ ] **S07** Exercise the supported shared e-ink Seat A/B assignment. OLED allows
  only one player. Leaving/rejoining does not reuse another player's identity.
- [ ] **S08** Control one participant from a phone and physical Sigil together.
  The participant and statistics attribution remain the same.

## Gameplay on physical devices

- [ ] **G01** Start/cancel the countdown from each supported controller; any seated
  player may start/cancel according to current rules. No duplicate seats appear.
- [ ] **G02** Pass and undo from Sigils, portal and Android. Player passes have the
  shared three-second grace; Atlas's deliberate Master pass remains immediate.
- [ ] **G03** Pause/resume and verify all clients agree and paused time is excluded.
- [ ] **G04** Adjust life with taps/holds/batching on both Sigils and phones. The
  final Atlas total matches the requested delta, including rapid mixed input.
- [ ] **G05** Approve, deny and time out cross-player life requests. Expired/stale
  responses cannot apply a different request; timeout follows the current 15 s rule.
- [ ] **G06** Change Commander damage and verify linked life changes and bounds.
- [ ] **G07** Claim, deny and confirm a win, including shared seats; concede/eliminate
  and check the last-player outcome. Every client reaches the same final state.
- [ ] **G08** End by the five-second End match hold; release early to cancel. A
  completed draw and rematch/reset behave consistently on every controller.
- [ ] **G09** Verify timer-off, warning and expiry. Expiry never passes a turn.
  Screens retain essential information when sound or animated lights are disabled.
- [ ] **G10** Run harness 2p, 4p, Rematch and Soak x5, then a full physical game.
  Save summaries. Harness success does not substitute for buttons, displays or power tests.

## Recovery and persistence

- [ ] **R01** Update/reboot with existing profiles: IDs, names, PIN access, core
  totals, permissions, settings and pairing remain intact.
- [ ] **R02** Cut power in an empty lobby and during start countdown; restart
  without a phantom running game or accidental result.
- [ ] **R03** Cut power while running, paused, just after a committed pass and after
  a nonterminal concession. Recover the last valid checkpoint paused, excluding downtime.
- [ ] **R04** After recovery, reauthenticate phones and reconnect Sigils; Resume
  continues the same participants, life, damage and turn. End match records a draw.
- [ ] **R05** Cut power at completion before the terminal checkpoint commits.
  Saved counts do not advance while an unfinished match remains recoverable.
- [ ] **R06** Cut power after the terminal checkpoint and during profile writes.
  Restart as Game Over; ending it again cannot increment counts. Record any missing
  or partial results as the open P02 limitation, not an exactly-once pass.
- [ ] **R07** Inject failed/uncertain NVS commits and corrupt/future recovery records.
  Statistics fail closed, errors are visible in diagnostics, and unreadable records
  are preserved. Use host injection for cases the bench cannot reproduce precisely.
- [ ] **R08** Complete ordinary win/draw/concession games and reboot: saved profiles
  receive one result each; guests receive no saved profiles or statistics.

## Optional SD behavior

- [ ] **D01** Boot without a card; play a full game and retain core counts in NVS.
- [ ] **D02** Boot with a working card; validate detailed-stat migration and log
  restart markers without deleting unreadable or conflicting records.
- [ ] **D03** Exercise full/unwritable/removed-card errors. Gameplay remains usable,
  core counts remain available, and the card is never automatically formatted.
- [ ] **D04** After P04, reinsert the card without restarting. Consumers recover
  safely and do not apply a completed game's statistics twice.
- [ ] **D05** Trigger log rotation on a real card; retention stays bounded and
  slow writes do not disrupt input, radio service or display updates.

## Firmware maintenance (blocked until P03)

- [ ] **U01** Re-test Atlas OTA through authenticated presence verification;
  confirm the running build after reboot and preserved profile/settings data.
- [ ] **U02** Upload each Sigil variant once to Atlas SD storage; validate package
  metadata/integrity and reject wrong-variant, incompatible and corrupt images.
- [ ] **U03** Update one named Sigil of each variant without COM-port selection;
  verify transfer, reboot, reported build and retained pairing.
- [ ] **U04** Queue several Sigils; offline devices/timeouts produce clear results
  and cannot silently redirect an update to a different device.
- [ ] **U05** Reinstall a retained compatible release. Recheck storage compatibility
  and distinguish deliberate reinstall from automatic failed-boot rollback.
- [ ] **U06** Interrupt transfer/power and fail new-firmware reconnection. Confirm
  recoverability on both variants before claiming automatic rollback.

## Access, accessibility and client behavior

- [ ] **C01** Admin/GM/Developer actions enforce their separate permissions; wrong,
  expired and another account's presence codes fail without granting access.
- [ ] **C02** Atlas and Sigil factory reset work only through the permitted flow;
  other devices' profiles/pairings and the retained SD card are handled as documented.
- [ ] **C03** Android and browser reconnect after Atlas restart. Stale/ambiguous
  requests cannot replay an action; Android app switching preserves intended connection behavior.
- [ ] **C04** Settings require explicit Save, retain unsaved edits during refresh,
  and confirm success. QR/camera denial leaves a usable code-entry path.
- [ ] **C05** Exercise high contrast, reduced motion, muted sound, longer hold
  timing and screen-reader navigation. Essential status/actions remain accessible.

## Hardware and independent handoff

- [ ] **H01** Measure/test-fit DevKit and Jewel footprints; check pin orientation,
  display strap, connectors and antenna clearance before ordering carrier boards.
- [ ] **H02** Complete PCB layout and pass electrical/design-rule checks; inspect
  and test the assembled boards before treating schematic ERC as hardware acceptance.
- [ ] **H03** Measure idle, radio, display, buzzer and maximum allowed LED current;
  chosen USB supplies/power banks sustain operation without brownouts or early cutoff.
- [ ] **H04** Protect wiring and expose all needed controls/connectors on the three
  field Sigils. Test e-ink readability/full refresh and OLED orientation/input response.
- [ ] **F01** Update the quick-start/manual to the tested candidate and document
  remaining prototype limitations, including any explicitly deferred P items.
- [ ] **F02** A tester unfamiliar with the build powers up, sets up, pairs, assigns
  players and plays a game using only the kit/manual, without a development computer.
- [ ] **F03** Record failures/fixes, repeat affected checks, then name the accepted
  candidate with an immutable tag and matching firmware/APK package.

## Evidence from this implementation pass

Verified on the host, 2026-09-26:

- **A01-A03:** GCC 13, C++14, AddressSanitizer and UBSan. Application scenarios
  report 47 passing groups, including the new completion regression; storage
  and real profile-store suites pass. LeakSanitizer was disabled because this
  execution runtime cannot inspect the process through `/proc`.
- The new A01 interruption regression was also run with the original completion
  bridge restored at link time: it fails because the recovered match is not
  Game Over. The same regression passes with the fix.
- **A04:** all three Sigil host suites pass. Sigil sources are unchanged from
  the reviewed base; their existing sanitizer builds were re-run.
- **A05:** 32 adapters pass the canonical-state mutation audit.
- **A06:** 10 generated responses and 9 shared fixtures pass contract validation.

**B01:** Atlas `pio run -e atlas` passes for source commit `56c058f` (published as `199c467` with the identical source tree), with
PlatformIO 6.2.0, Espressif32 7.1.3, Arduino framework
`4.20017.260907+sha.dcc1105b`, Xtensa GCC `8.4.0+2021r2-patch5` and
LovyanGFX 1.2.30. RAM: **105,156 / 327,680 bytes (32.1%)**. Flash:
**1,366,405 / 1,966,080 bytes (69.5%)**. Telemetry was disabled for the
successful local build. No firmware was flashed.

Sigil/harness firmware builds, Android checks, browser smoke and all physical
acceptance remain unverified on this branch. No hardware boxes may be inferred
from host or compile results.
