# Prototype v1 verification checklist

The field-test acceptance record for prototype v1. A host or compile result
never checks off a hardware line; re-run an affected check whenever the code
it covers changes.

## Status (owner, 2026-10-02)

Playtesting and bench testing have verified nearly everything implemented:
the host and firmware build gates (A01-A08, B01-B05), setup, pairing and
player identity (S01-S08), gameplay on physical devices (G01-G10), ordinary
recovery and persistence (R01-R04, R08), SD behavior without a card, with a
card and on errors (D01-D03), Atlas and Sigil firmware updates (U01-U05),
access and client behavior (C01-C05) and the e-ink and OLED Sigils in use
(H04). The detailed line items are in git history (this file before
2026-10-02).

Record new runs as `ID | PASS/FAIL | build | device(s) | evidence`, and keep
failures visible until a rerun passes.

## Still open

### Not implemented yet

- [ ] **P02** Durable match/completion receipts and replay-safe result
  persistence before claiming crash-safe, exactly-once statistics. **P01**
  (commit the finished-match checkpoint before statistics) prevents the
  duplicate count but can leave missing or partial results after a cut. See
  [completion ordering](COMPLETION_RECOVERY.md).
- [ ] **H01** Measure and test-fit the DevKit and Jewel footprints: pin
  orientation, display strap, connectors and antenna clearance, before
  ordering carrier boards.
- [ ] **H02** Complete the PCB layout and pass design-rule checks; inspect and
  test assembled boards (schematic ERC is not hardware acceptance).

### Hardware checks to confirm

These are the ones that need a deliberate bench setup rather than ordinary
play. Tick them, with evidence, once done.

- [ ] **R05** Cut power at completion before the terminal checkpoint commits.
  Saved counts don't advance while the unfinished match remains recoverable.
- [ ] **R06** Cut power after the terminal checkpoint and during profile
  writes. Restart as Game Over; ending it again can't increment counts. Missing
  or partial results are the open P02 limitation, not an exactly-once pass.
- [ ] **R07** Failed/uncertain NVS commits and corrupt/future recovery records:
  statistics fail closed, errors show in diagnostics, unreadable records are
  kept. Host injection covers what the bench can't reproduce precisely.
- [ ] **D04** Swap the card during and after a game: no double-counted
  statistics, a different card works, the NO SD CARD warning follows.
  Pull-and-reinsert without restart is *Verified* (2026-09-28,
  [SD diagnostics](SD_DIAGNOSTICS.md#hot-plug-2026-09-28)).
- [ ] **D05** Log rotation on a real card stays bounded, and slow writes don't
  disrupt input, radio or display.
- [ ] **U06** Interrupt an update (transfer and power) and a signed image that
  fails to reconnect: both variants recover, and the bootloader rolls back
  without USB. Don't claim automatic rollback before this passes.
- [ ] **H03** Measure idle, radio, display, buzzer and capped LED current; the
  chosen USB supplies and power banks run without brownouts or early cutoff.
- [ ] **F02** A tester unfamiliar with the build powers up, sets up, pairs,
  assigns players and plays a game using only the kit and manual.
- [ ] **F03** Name the accepted candidate with an immutable tag and matching
  firmware and APK packages.

## Host evidence (2026-09-26)

The P01 regression fails against the original completion bridge and passes
with the fix (GCC 13, C++14, ASan and UBSan). CI now runs the host suites,
contract checks, every firmware build and the Android tests on each push
([Continuous Integration](CONTINUOUS_INTEGRATION.md)).
