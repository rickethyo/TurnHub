# Playtest notes and Opus handoff packet, 2026-09-29

Confidence: *Reported* by the owner during play testing. Nothing here is
hardware-verified, and no root causes have been established. Causes named
below are suspicions to check, not findings.

## State when written
- Branch `turnhub-integration`: `codex/oled-two-players` (which includes
  `sigil-ota` and `finish-sigil-ota`), plus `codex/sigil-ota-test-1` and the
  GameTable groundwork commit `444f445`. `master` is an ancestor.
- Atlas and Sigil Linux host suites pass on the merged branch.
- Test-build leftovers from `sigil-ota-test-1`: the "OTA TEST 1" idle label
  and the 0.8.1-dev bump. Decide whether they stay.
- Atlas plus Sigils work well with hardware only. Every problem below was
  found in play testing.
- Process: the feature gate in CLAUDE.md applies to features 9, 10 and 11.
  Update the engineering docs and the manual wherever user-visible behavior
  changes.

## A. Bugs and stability (highest priority)

1. **Crash with several phones connected.**
   - Atlas becomes unstable and crashes when multiple phones connect at once.
   - Hardware-only use is fine.
   - Check: web server and connection limits, heap use, `/api/v1/state`
     polling load, session handling.
   - Unknown: how many phones trigger it. A serial log or the
     `/api/diagnostics/log` download from a crash would help.

2. **Sigil OTA soft lock.**
   - The upload never gets past "uploading", and no failure is ever shown.
   - Needed: find where the failure is detected, add a timeout, and show the
     error on the portal, the Atlas screen and the Sigil.
   - Relevant code: the OTA service, package store and update jobs, and the
     portal upload path. See `SIGIL_OTA.md`.

3. **Two-player Sigil after elimination.**
   - The second seat only gets life controls during its own active turn, so
     the inactive player must track life by hand until their turn. Suspect a
     turn-ownership gate on life input.
   - An eliminated player can still show as the active player when another
     Sigil has priority.
   - Both may share a cause in the shared-seat work from
     `codex/oled-two-players`.

4. **OLED turn counter runs ahead** of the real turn count. Possibly related
   to item 3. Unverified.

5. **Player names unreliable on the Android app and the Atlas screen.** They
   don't always show. No cause known. Check that names propagate from Lobby
   and Participant through state to the clients.

## B. Small changes

6. **E-ink hold-to-change-life scales too quickly.** Slow the ramp. Check it
   against the hold-timing accessibility settings.

7. **Different LED colors for player B's active turn**, so the two seats on
   one Sigil can be told apart. Color can't be the only indicator, so pair it
   with another cue (see `ACCESSIBILITY.md`).

8. **Pick a profile for Sigil seat B.** Seat B currently can't select a
   profile the way seat A can. Hardware is never player identity, and stats
   belong to the profile (see `PHYSICAL_PROFILE_SELECTION.md`).

## C. Features (feature gate first)

9. **Player-card menu on the Atlas touchscreen.**
   - Tap a player card to open a menu for that player.
   - The menu adjusts that player's life total.
   - A hold action removes the player. It could double as an easy concede, so
     it shouldn't be framed as a moderation action.
   - Design questions: are "removed" and "conceded" the same outcome, given
     they affect statistics and the win flow differently; how it interacts
     with the 15 s life approval (`LIFE_APPROVAL_MS`); no action may rely on a
     hold alone.
   - It must go through existing Intents, with `touch_controls.cpp` staying a
     thin adapter.

10. **Create accounts from the Android app.**
    - The app is currently a live read-only Atlas view, so this is a real
      scope change.
    - Needs new contract endpoints and probably the physical-presence proof
      (the table code flow).
    - Follow the Android layering rules in CLAUDE.md.

11. **Set turn order from the Atlas screen, the app or the web.**
    - Touches Lobby starter selection and seating.
    - Needs a new Intent and validator.
    - Three clients could issue it, so define who may and when (lobby only?).

## D. Suggested order
1. Crash (1) and OTA soft lock (2), since both block play testing.
2. Shared-seat problems (3, 4) and name display (5).
3. Small items (6, 7, 8).
4. Features (9, 10, 11), each starting with a feature-gate outline.

## E. Open questions for the owner
- How many phones does it take to crash Atlas, and can you capture a log?
- Should the OTA TEST 1 label and version bump stay in the branch?
- Are "remove" and "concede" the same outcome (item 9)?
- Who may set turn order, and only in the lobby (item 11)?
