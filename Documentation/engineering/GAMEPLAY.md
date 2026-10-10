# Gameplay: Turns, Life, Commander and Cues

The rules Atlas runs during a match, and how they reach players as light,
sound and text. TurnHub is a bookkeeping tool, not a rules engine: no life or
damage total ever eliminates anyone automatically.

## Game profiles and settings

| Profile | Starting life |
|---|---|
| Generic (default) | 40 |
| Magic | 20 |
| Commander | 40, plus Commander damage |
| Yu-Gi-Oh! | 8000, counted in hundreds on every client |

- Custom starting life 0 to 1,000,000. Any seated player may change the
  next game's settings in the lobby (`ConfigureGame`); a running match
  keeps what it captured at start, and a rematch starts fresh.
- Saved in `gamecfg` (format, starting life, turn timer). A corrupt record
  blocks setup rather than being overwritten.
- HTTP: `GET/POST /api/game/settings` (omitted fields keep their value);
  `/api/session/me` and `/api/seats` report `lifeAvailable` and `life`.
- Lifetime statistics are not yet split by game (staged).

## Two-Headed Giant

*Planned → implemented 2026-10-06 (owner request); needs a playtest.* A team
option for the Magic and Commander profiles (`GameSettings::twoHeadedGiant`,
`twoHeadedGiant` on `/api/game/settings`, `settings.twoHeadedGiant` and
`players[].team` in the state).

- **Teams** are neighbours in turn order: players 1+2 are team 1, 3+4 team 2,
  and so on. Change teams by changing the lobby's turn order. Start needs an
  even table of at least 4 (`validTeamTable`). Teammates may share one Sigil
  (seats A and B) or use separate Sigils or phones.
- **One turn per team.** `activePlayer` is one teammate; both have the turn
  (`GameEngine::hasTurn`), either may pass, claim the win or cancel the
  team's pending pass, and the pass goes to the next team. A team turn counts
  for both teammates' turn statistics. Both teammates' Sigils show the turn
  and hear its cues.
- **One life total.** Starting life is the team's total: 30 for Magic and 60
  for Commander when the toggle is turned on in a client (editable). Either
  teammate changes it directly; a change to the other team's life is a
  request either opponent can answer, one pending per team.
- **Commander damage** stays per player (21 from one commander is the usual
  loss), and every hit comes off the team's life.
- **Leaving together.** Eliminating or conceding one teammate eliminates the
  team. A win claim waits only on the other teams; the last team standing
  wins, and both teammates get the win in their statistics.
- **First draw.** The starting team skips its first draw; the Atlas screen
  reminds the table during that first turn. TurnHub does not track draws.
- Storage keeps the flag in the profile byte's top bit (`PROFILE_BYTE_TEAMS`)
  of `gamecfg` and the recovery record, so neither layout changed. Sigils need
  no new firmware: Atlas sends the team's life and turn in the existing
  display packets.

## Turns

- **Pass** waits `PASS_GRACE_MS` (3 s) so it can be undone: the passer's
  Sigil shows PASSING with a green ring countdown and the click undoes it;
  every other Sigil shows "P<n> PASSING" with an amber countdown
  (`PassPending`); the Atlas screen says "Passing in Ns". Two falling ticks
  play when a pass is queued and two rising ticks when it is undone.
- **Master pass** (Atlas Table screen, hold 2 s) and a Game Master's pass
  skip the grace and are logged as master passes.
- **Rounds:** the Atlas header and the Sigils show the table round, counted
  from each living player's completed turns. The Atlas screen also shows
  each player's total time on their own turns and the match time.
- **Shared Sigils** carry two seats (A and B). Seat B's turn uses an azure
  double pulse on B's half of the ring. Down on the Sigil switches which
  seat it shows (and so whose life Left/Right change) until the turn comes
  round; an eliminated seat never leads over a living one.
- **Pause** stops the turn clock. **End match** (Atlas Table screen, hold 5 s)
  ends a running or paused match as a draw.
- **Win claims** need the other living players to confirm, in Atlas's player
  order; a denial resumes play. **Concede** and Game Master removal eliminate
  through the same flow.

## Turn timer

- One value, `turnTimerMs`: 0 = off, else 15 s to 60 min in whole seconds.
  Presets (Off, 1, 2, 3, 5 min, `TURN_TIMER_PRESETS_MS` in `game_profile.h`)
  are UI shortcuts; a custom value is just as valid.
- The countdown is derived from the existing turn anchor: pause freezes it,
  a new turn restarts it. Phases, derived per call:
  - `NORMAL`; `WARNING` at 10 s or less;
  - `EXPIRED`: **the turn continues**. Atlas never passes, pauses or
    penalizes on expiry;
  - `LONG_TURN`: timer off and the turn reached 5 minutes (a gentle green
    cue, no sound).
- `/api/v1/state` reports `settings.turnTimerMs` and
  `turnTimer {phase, remainingMs}` (clock samples: they change without a
  revision bump). The recovery checkpoint carries the timer.
- Shown as text in the app and as a countdown on the Atlas screen.
  Not on Sigil screens: e-ink refresh doesn't suit a live clock (owner
  decision); try it on a future LCD Sigil, which needs a radio field.

## Life

- Own life changes apply at once (`ChangeLife`). A Sigil batches presses and
  sends one total 2 s after the last; holding repeats, and the ring shows the
  pending amount (green clockwise for a gain, red counter-clockwise for a
  loss). A Sigil steps by 100 in games starting at 1000 or more.
- **Another player's life** goes through an approval (`RequestLifeChange`):
  one pending request per recipient; only the recipient answers (Right
  approves, Left denies on a Sigil; buttons on a phone); an unanswered request
  is **accepted after the recipient's approval window**, 15 s by default
  (`LIFE_APPROVAL_MS`) or 30 or 60 s by the recipient's accessibility choice
  (the window is fixed when the request is made), and pause does not stop
  that timer. The delta applies to the current total, rechecked at
  acceptance. Request IDs stop a stale answer approving a newer request. Win
  claims, eliminations, game end, reset and rematch cancel pending requests.
- **Atlas touchscreen Player screen:** tap a chip in a match for -5/-1/+1/+5
  and Concede (asks again). In the lobby the Player screen has Earlier, Later
  and Remove (hold 2 s): Remove takes that seat out (seat A also takes its
  Sigil's seat B).
- **Nudge** (app): while a game runs, a living player who is not
  up can nudge the active player. Their Sigil plays the Nudge cue (if its
  sound is on) and their phone shows "<name> nudged you" and vibrates. One
  nudge per player every 30 s; a Game Master can mute an account's nudges.
- Life is bounded to ±1,000,000 and changes are accepted while running or
  paused, except during a table decision or after elimination. Zero or less
  never eliminates. Life changes never write flash (only the checkpoint).
- HTTP: `POST /api/control/life` (own life), `/life/request`,
  `/life/respond`, `GET /api/game/counters`.

## Commander damage

- The **receiver** records damage taken from a source player's commander.
  Each source has commander 1 and, with partners, commander 2. Positive
  damage lowers life and negative corrects it; both totals change together.
  Counters are 0 to 1,000,000 and reset each match. 21 damage does not
  eliminate automatically.
- **Partner commanders** (2026-10-06) are off for every player by default.
  A player turns them on for their own shown seat from the Sigil Menu
  (`SetPartner`); damage entered for commander 2 from a phone turns them on
  too, and once commander 2 has dealt damage they stay on. Rematch and reset
  clear them.
- **From a phone:** `POST /api/control/commander` (`ChangeCounter`).
- **From a Sigil** (Menu > Cmd damage, both displays): Atlas runs the entry
  pages (`commander_picker.cpp`, `CommanderFlow`/`CommanderKey`). Choose the
  attacker with Left/Right (the list skips the receiver and eliminated
  players), the commander (only with partners), the amount (hold to repeat),
  then a preview; the click applies. Up goes back; Down cancels. The
  receiver's name and A/B seat stay on every page even if turns move on.
  **Undo hit** reverses the last hit recorded this way for that receiver,
  once; a correction to the same cell invalidates it, and it is gone after a
  reboot. Entry closes after 60 s idle, on disconnect, elimination, a table
  decision or match end. Stale or duplicate keys never reapply a hit.

## Cues: light, sound and text

Every cue is presentation: it never changes game state, and nothing relies on
light or sound alone (see [Accessibility](ACCESSIBILITY.md)).

**Lights.** Atlas picks a semantic state per Sigil (`selectSigilLedState`):
a primary `LedCue` (Unassigned, Joined, Starting, TurnStarted, YourTurn,
Waiting, Paused, ConfirmationNeeded, EliminationSelect, GameOver) plus
`LedOverlay` facets (Starter, Winner, TurnWarning, TimerExpired, LongTurn) and
the seated players' light style. The Sigil draws it on the Jewel ring
(`Sigil/src/sigil_led.cpp`), in step with Atlas's table clock. Timer cues:
warning is a slow red pulse, expired steady red, long turn steady green, over
the active player's blue breathe. Every cadence is at most 2.5 Hz. Reduced
motion and Monochrome-safe styles are in [Accessibility](ACCESSIBILITY.md).

**Sound.** `AudioCue` names events; `AudioCueProfile` maps each to notes.
Sigil buzzers get per-player cues (turn start, joining, decisions waiting on
that Sigil: `ActionRequired`, two short notes, for a pending win confirmation
or life request), muted per player by their accessibility setting. The
**Atlas speaker** plays table-wide cues (countdown, start, turn change, timer
warning and expiry, pause, eliminations, win claim results, game over) so
phone players hear them: a small chime synthesizer (`atlas_speaker.cpp`,
pentatonic chimes on the DAC), volume Off/Low/Medium/High (Admin,
`ConfigureSpeaker`). Timer cues play once per turn.

**Text.** The app and Atlas screen show every state in words (time
left, warning, time over, long turn, PASSING, pending requests).

## Verification

Two-Headed Giant: host scenarios cover team turns, shared life, requests,
win claims, elimination, Commander, recovery, the settings API, the state JSON
and both teammates' Sigils (`twoHeadedGiant*` in `scenarios.cpp`). Not yet
played at the table.

Host scenarios cover settings and storage, life bounds and approvals
(authorization, deadline races, rollover, cancellation), Commander entry from
both Sigils (shared seats, partners, preview, undo, stale keys, closure), the
timer phases and cue selection, turn order with shared seats and recovery.
Browser approval smoke was retired with the administration-only portal; native
HTTP/controller tests and host scenarios retain approval coverage. Life, approvals,
the timer and Commander from phones are in use at the table (owner,
2026-10-02). Sigil Commander entry and partners (Sigil 0.9.10, Atlas 0.6.8)
and the shared-seat turn order (2026-10-05) still need a playtest.
