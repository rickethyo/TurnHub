# Completed-match recovery ordering

Status: implemented and host-tested on `codex/prototype-v1-stabilization`,
2026-09-26; firmware build and hardware acceptance pending. This is a conservative prototype fix, not transactional statistics.

## Problem and boundary

The engine's completion callback used to increment saved profile statistics
before the dispatcher's observer saved the finished-match checkpoint. A power
cut between those operations restored an unfinished match whose statistics had
already been counted. Ending it again counted it twice.

The completion callback must first commit a valid finished-match checkpoint
through the existing recovery owner. Only a successful commit permits statistics
writes. Unreadable or future-schema recovery records remain protected. If the
checkpoint cannot be committed, statistics are skipped and a diagnostic records
the failure. Normal checkpoint observation still runs after the Intent.

Restoring a completed snapshot never replays completion. A power cut after the
checkpoint but before all profile writes can therefore leave zero or partial
statistics. A write reporting an uncertain outcome also causes statistics to be
skipped. This deliberately prevents duplicate increments without guessing which
writes reached flash. Durable MatchId/completion receipts and replay-safe writes
are still required to recover missing results; that remains a separate v1 gate.

## Feature gate

1. **State owner:** Atlas GameEngine owns the result; GameRecovery owns its record.
2. **Intent:** existing win, elimination/concession and EndMatch Intents.
3. **Validator:** existing Intent/engine checks, then checkpoint validation and
   the NVS commit result before statistics persistence.
4. **Persistence owner:** existing `th_game_v1/checkpoint` record and profile
   statistics repositories. No key, record format or schema changes.
5. **Presentation:** existing game-over state; failures appear in the redacted
   diagnostic stream as `ATLAS|PROFILE_STATS|SKIPPED_CHECKPOINT|<status>`.
6. **Protocol:** no radio, HTTP or client-contract changes.
7. **Dependencies:** none added.
8. **Accessibility:** no gesture, cue or control changes; diagnostic status is text.

## Verification

The new host regression fails against the original completion bridge and passes
with the fix. Host regression and physical power-cut results are tracked separately in
[the v1 verification checklist](PROTOTYPE_V1_VERIFICATION.md). Host fault injection
models interruption at persistence boundaries; it does not prove ESP32 flash
behavior, SD filesystem crash consistency or all-or-nothing multi-profile writes.
