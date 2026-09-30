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
   - Reported with 4 phones connected. A serial log or the
     `/api/diagnostics/log` download from a crash would still help.

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
   - Decided: the card offers **Concede** for that player. Removing a player
     is a separate, later **Moderate** path behind admin sign-in (see
     section E).
   - Design questions: how it interacts
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

11. **Set turn order from the Atlas screen.**
    - Decided: lobby / setup only, Atlas touchscreen only, any player may set
      it (see section E).
    - Touches Lobby starter selection and seating.
    - Needs a new Intent and validator.

## D. Suggested order
1. Crash (1) and OTA soft lock (2), since both block play testing.
2. Shared-seat problems (3, 4) and name display (5).
3. Small items (6, 7, 8).
4. Features (9, 10, 11), each starting with a feature-gate outline.

## E. Owner answers (2026-09-29)
- **Crash (item 1):** it happened with 4 phones connected. No log captured
  yet.
- **OTA TEST 1 label and 0.8.1-dev bump:** keep them on the branch for now.
  The OTA path still needs testing, but that is not the current priority.
- **Remove vs concede (item 9):** they are two different paths. The player
  card offers **Concede** only for now. A separate **Moderate** button that
  requires some form of admin sign-in, and would carry the remove action, is
  deferred (*Planned*).
- **Turn order (item 11):** only in the lobby / game setup, and only from the
  Atlas touchscreen. Any player may set it. The app and web do not issue it.

## F. Status after the fix pass (2026-09-29, `turnhub-integration`)

All of this is host-tested only: *Needs verification* on hardware. Both
firmwares need reflashing (`protocol.h` changed: `SigilAction::SwitchSeat`).

| # | Commit | What changed | Confidence |
|---|---|---|---|
| 1 | Playtest #1 | Portal polls live data each second and the rest every fifth tick or on change; hidden tabs stop. `ATLAS|HEALTH` logs heap and Wi-Fi clients every minute and on each join/leave. | Suspected cause, *Needs verification* with 4 phones and a log |
| 2 | Playtest #2 | Sigil update page sends the sign-in token (it never did), uploads with progress and a timeout; upload callbacks no longer answer mid-upload; an orphaned upload is dropped after 30 s instead of blocking game starts; the Atlas screen and the Sigil show the outcome. | Root cause found in code |
| 3 | Playtest #3, #4 | Sigil menu **Switch seat** shows (and so adjusts life for) the other living seat until the turn comes round to that Sigil; an eliminated seat never leads over a living one. | Root cause found in code |
| 4 | Playtest #3, #4 | Sigils show the table round (as on the Atlas header), labelled `R` (OLED) and "Round" (e-ink). It was the shown player's own next-turn count. | Root cause found in code |
| 5 | Playtest #5 | `/api/v1/state` carries `displayName`; the Atlas screen names phone-joined players in the lobby. | Two causes found; *Needs verification* on Android |
| 6 | Playtest #6 | E-ink life hold: +1 from 0.7 s, then +1 every 0.3 s, never steps of 5 (owner: jumps of 5 cannot be counted on the LEDs). The OLED keeps its steps of 5. Both scale with the hold-timing preference. | Tuning, owner to judge |
| 7 | Playtest #7 | Seat B's turn: azure, double pulse, B's half of the ring (LedState Sigils). | New |
| 8 | Playtest #8 | Add seat B opens the profile picker for seat B. | New |
| 9 | Playtest #9 | Tap a chip in a match: Player screen with -5/-1/+1/+5 and Concede (asks again). | New; Moderate path *Planned* |
| 10 | - | Not implemented: feature-gate outline below. | *Planned* |
| 11 | Playtest #11 | Tap a chip in the lobby: Earlier / Later (MoveSeat, Atlas screen only). | New |

Not done in this pass: the user manual (V0.4) does not yet describe Switch
seat, the round label, seat B's colors, the seat B picker, the Player screen
or turn order.

### Item 10 feature-gate outline: create accounts from the Android app

1. **State owner:** Atlas (profile store, accounts); the app only asks.
2. **Intent / endpoint:** the existing `POST /api/profiles/register` (name,
   PIN) already creates a profile and returns a session; the app would call
   it rather than add a new Intent. First-Admin setup stays behind the table
   presence code (`/api/presence/*`), which the app would also need.
3. **Validator:** the existing registration checks (profile policy, name and
   PIN rules, the profile limit).
4. **Persistence:** NVS profile records, as today; the app stores only the
   session token (Android Keystore-backed storage).
5. **Rendering clients:** Android gains its first write screens (sign in,
   register), which ends its read-only milestone; the portal is unchanged.
6. **Protocol/contract:** document register/login in `http-v1.md` as client
   contract, add schemas and examples in `protocol/`, and follow the Android
   layering (`protocol/` DTOs, `data/` networking behind `AtlasRepository`).
7. **Third-party dependencies:** none expected.
8. **Accessibility:** TalkBack labels, no timing-only steps, PIN entry that
   works with a screen reader.

Open question for the owner: should the app also sign in (and then act as a
controller), or only create accounts?
