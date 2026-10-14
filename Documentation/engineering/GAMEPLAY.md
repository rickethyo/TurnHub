# Gameplay: Turns, Life, Commander and Cues

The rules Atlas runs during a match, and how they reach players as light,
sound and text. TurnHub tracks life and outcomes. Automatic elimination at zero
or negative life is on by default and may be disabled when cards change the
loss condition; Commander damage totals alone do not trigger elimination.

## Game profiles and settings

| Profile | Starting life |
|---|---|
| Generic (default) | 40 |
| Magic | 20 |
| Commander | 40, plus Commander damage |
| Star Commander | 40, Commander rules for exactly five players (below) |
| Yu-Gi-Oh! | 8000, counted in hundreds on every client |

- **Official rules** (2026-10-14, owner request; host tests only, *needs a
  playtest*): Magic (20 life, 10 poison), Commander (40, 10), Two-Headed Giant
  Magic (30, 15 shared, exactly two teams of two) and Yu-Gi-Oh! (8000, exactly
  two players) keep those values unless the table turns on **House rules**
  (`GameSettings::houseRules`, `houseRules` on `/api/game/settings`, bit 4 of
  the `gamecfg` and recovery flags byte). Atlas saves the official values
  whatever a request carries (`applyOfficialRules`), refuses a live poison-limit
  change, and refuses Start on the wrong table size (`validOfficialTable`).
  Custom modes (Generic, Star Commander, Two-Headed Giant Commander) stay
  editable. The standalone app follows the same table (`OfficialRules.kt`).
- Custom starting life 0 to 1,000,000 (custom modes or house rules). Any
  seated player may change the next game's settings in the lobby
  (`ConfigureGame`); a running match keeps what it captured at start, and a
  rematch starts fresh.
- **Back to setup and short games** (2026-10-14; host tests only, *needs a
  playtest*): within the first three minutes of game time (`UNDO_START_MS`)
  any seated player can undo the start (`BackToSetup`): the lobby comes back
  with the same players and settings and nothing is recorded. A match under a
  minute (`SHORT_GAME_MS`) commits its finished checkpoint but its statistics
  wait for `CountShortGame` (state `shortGameUncounted`); left uncounted, it
  never reaches the statistics. The standalone app does the same.
- **Who goes first:** any player can choose (`SelectStarter`); choosing the
  chosen player again clears it. With nobody chosen, Atlas picks a random
  player when the countdown ends, as the standalone app does (rematches too).
- Saved in `gamecfg` (format, starting life, turn timer). A corrupt record
  blocks setup rather than being overwritten.
- HTTP: `GET/POST /api/game/settings` (omitted fields keep their value);
  `/api/session/me` and `/api/seats` report `lifeAvailable` and `life`.
- Lifetime statistics are not yet split by game (staged), apart from the
  Star Commander counters below.

## Zero-life elimination and exit reasons

`autoEliminateAtZeroLife` is on by default. Any seated player can change it in
Android Game, the connected tablet lobby/table menu, or standalone play. During
a connected match ConfigureGame permits only this field to change: profile,
starting life, teams and turn timer stay fixed. The live change belongs to the
current match; lobby changes choose the next-match default. Changes wait while a
win claim or manual elimination decision is open.

Enabling it immediately checks all living players at zero or below, including
paused games. The engine resolves the full set before choosing a winner; if
no side survives the match is a draw. Life reductions from Commander hits and
accepted or expired life requests use the same check. Active-player elimination
advances the turn and cancels a queued pass; eliminated players cannot be healed
back into the match. Disable the setting before applying a card's alternate
loss condition. Shared team life eliminates both teammates.

Concede records a voluntary exit (`conceded=true`, `eliminated=true`); Eliminate
and automatic zero-life removal record elimination (`conceded=false`). Both
remove the player from turn order and can leave the last side as winner. The
Android controls confirm both actions; table panels and standalone history show
the recorded reason. Concessions increment `gamesConceded` and use the Conceded
last result; eliminations increment `gamesEliminated`. Team exits share a reason.
Saved match recovery preserves the policy and reason. Standalone results carry
the reason through optional Atlas import (`concededN`, 0 or 1).

The settings profile byte uses bit 0x40 for manual-only elimination (clear means
automatic). Checkpoint schema 2 uses the existing eliminated byte as flags:
bit 0 out, bit 1 conceded. Statistics schema 2 uses the former four trailing
padding bytes for `gamesConceded`, preserving the record size. Historical
concessions cannot be reconstructed from older eliminated results.

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
- **One life total.** Starting life is the team's total: 30 for Magic (fixed,
  with exactly four players, unless house rules are on) and 60 for Commander
  (editable). Either
  teammate changes it directly; a change to the other team's life is a
  request either opponent can answer, one pending per team.
- **Commander damage** stays per player (21 from one commander is the usual
  loss), and every hit comes off the team's life. A teammate is never a source
  (2026-10-14). Two-Headed Giant Commander is a custom mode: its life stays
  editable.
- **Leaving together.** Eliminating or conceding one teammate eliminates the
  team. A win claim waits only on the other teams; the last team standing
  wins, and both teammates get the win in their statistics.
- **First draw.** The starting team skips its first draw; the Atlas screen
  reminds the table during that first turn. TurnHub does not track draws.
- **On a phone without Atlas** (2026-10-13, owner request) the full-screen
  table offers the same mode under **Game mode** in its setup, with the same
  rules: team life 30/60, shared poison (15), one team turn credited to both,
  Commander damage per player off the team's life, and teams leaving together.
  The local history credits both winners (`GameRecord.won`); importing such a
  record into Atlas still credits only the recorded winner. *Needs a playtest.*
- Storage keeps the flag in the profile byte's top bit (`PROFILE_BYTE_TEAMS`)
  of `gamecfg` and the recovery record, so neither layout changed. Sigils need
  no new firmware: Atlas sends the team's life and turn in the existing
  display packets.

## Star Commander

*Implemented 2026-10-13 (owner request); host and JVM tests only, needs a
playtest.* A five-player Commander variant with its own profile,
`mtg_star_commander` (`GameProfile::StarCommander`, Android
`MTG_STAR_COMMANDER`). Every Commander rule, counter and automatic-elimination
switch applies (`commanderRules()` / `magicRules()` in `game_profile.h`).

- **Exactly five players** (`STAR_PLAYERS`, `validStarTable`); start is refused
  with "Star Commander needs exactly five players." Two-Headed Giant is not
  allowed with it. Life (40), turns, counters and statistics stay individual.
- **Relationships by seat.** For player number p, i = p − 1: allies are
  (i+1)%5 and (i+4)%5, enemies (i+2)%5 and (i+3)%5 (`starAllies`,
  `starEnemies`; Android `domain/StarCommander.kt`). They follow the lobby turn
  order, are fixed once the match starts, and eliminated players keep their
  seat. Names, profiles and Sigils never decide them. Allies are not a team:
  no shared life, turns or automatic win.
- **Ally protection.** Not attacking or targeting allies is a table rule that
  TurnHub only explains. Commander damage between allies is refused by the
  engine (`changeCommanderDamage`, HTTP 409 "Allies can't deal Commander damage
  to each other in Star Commander."), by the standalone engine, and allies are
  left out of every source list (app, tablet, Sigil picker). Life changes are
  never restricted, so blanket effects still hit allies.
- **Victory** (`evaluateStarVictory`, standalone `StandaloneGame`): after every
  elimination batch on any path (manual, concession, zero life, poison,
  Commander damage, a rule switch), each living player whose two enemies are
  out wins; all who qualify after the same batch share the win (at most two,
  always neighbours). An eliminated player never wins, even if their enemies
  are later out (owner review). Last-player-standing does not apply; a batch
  that leaves nobody alive is a draw. Win claims for alternate win conditions
  are unchanged and name one winner.
- **Shared winners** are a winner set (`winnerMask_`), sent as `winnerPlayers`
  next to `winnerPlayer` (the lowest winner) in the state, kept in recovery
  schema 5, and credited to each winner's statistics. A shared win is never a
  draw. Standalone records carry `winners`, and the Atlas import accepts the
  `winners` field. In Two-Headed Giant `winnerPlayers` lists both teammates.
- **Statistics** count Star games separately (`starGamesPlayed`,
  `starGamesWon`, also counted in the totals), in the SD record (schema 3) and
  the NVS core record (schema 2). Older stats records read as corrupt and need
  a factory reset (no migrations before release). Local app statistics show the
  same pair.
- **Clients.** Android setup offers Star Commander under Game mode with a star
  map (pentagon of allies, dashed pentagram of enemies, tap a player to
  highlight their relationships, tap two to swap seats) and a text list of
  allies and enemies. Panels show "Enemies out n/2", the table menu shows the
  live star, and game over names every winner as a Star Commander victory.
  Sigils get no new display text: the display packet has no free field, and
  master's Sigil is headless until LVGL.

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
- The Android standalone game (no Atlas) applies the same rules locally
  (`StandaloneGame.turnTimer`, lobby presets only). *Implemented* 2026-10-13;
  device acceptance pending.

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
  eliminates when `autoEliminateAtZeroLife` is enabled (default true). Life changes never write flash (only the checkpoint).
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
phone players hear them, through `ATLAS_SPEAKER_MASK` to the board's speaker
(none on master's board-less build; the Gen 1 chime synthesizer is on
`legacy/gen1`), volume Off/Low/Medium/High (Admin, `ConfigureSpeaker`). Timer cues play once per turn.

**Android game sounds.** Local and connected play use the same observer for
confirmed start, pass/turn change, pause, resume, victory and draw, with one
short cue per observed transition. Sound is presentation only: button presses,
pending/cancelled/refused passes, repeated polls, first load, foreground return,
rotation and reconnect catch-up do not produce game-action sounds. Native
SoundPool plays original generated WAVs. Game sounds defaults on at 50%; its
phone-only toggle and volume are available in table setup, the table menu and
My account. Silent/vibrate mode and media volume still apply. Leaving the app
stops a cue; the background turn notification remains a separate OS channel.

**Text.** The app and Atlas screen show every state in words (time
left, warning, time over, long turn, PASSING, pending requests).

## Verification

Star Commander: `starCommander*` and `starStandaloneImport` host scenarios
(relationships, five-player start, ally Commander damage, victory paths, shared
winners, claims, recovery, rematch, two tables, statistics, import) and the
Star cases in Android `StandaloneGameTest`. Not yet played at the table.


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

## Magic monarch

Magic and Commander games have one optional monarch, identified by individual
player number even in Two-Headed Giant. A living player can choose **I’m the
monarch** from their player controls or table tile drawer, including while
paused. Claiming immediately transfers the crown from the previous holder;
there is no turn restriction. Table tiles show the crown and a Monarch label.
If the holder concedes or is eliminated (including through zero-life resolution
or team removal), the crown passes to the active player, or to the next living
player in turn order when the active player is leaving (Magic CR 724.4; owner
decision 2026-10-12, replacing the earlier clear-on-exit behavior). It clears
only if nobody is left. A pending win claim blocks new claims. Pause and recovery preserve it; a new
match or return to lobby clears it. TurnHub tracks the declaration; players
resolve the card’s combat and draw rules themselves.

Atlas owns this singleton in GameEngine. Authenticated seat controls converge
on ClaimMonarch; the domain rejects other formats, players out of the game,
and inactive matches. The state projection exposes nullable `monarchPlayer`,
and schema 3 checkpoints preserve it. Android standalone maintains and recovers
the equivalent singleton for its separate local game. No radio change.

## Magic counters, commander tax and statuses

Magic/Commander native player controls and table drawers now expose poison,
energy and experience, initiative, city’s blessing, and (Commander) command-zone
casts. Counters range from 0 to 1,000,000; negative corrections cannot cross zero.
Counters and city’s blessing remain associated with their individual owner.
Initiative has one living holder per game, independent of monarch; claiming it
transfers it, and the holder may release it to correct an entry. A leaving
holder's initiative passes like the monarch (CR 725.4). City’s blessing
is independently declared by each player; clearing it is an explicit correction.
TurnHub does not count permanents, resolve the Undercity or enforce card effects.

Each commander’s casts from the command zone are tracked separately. Its next
cast tax is derived as twice the count; first cast has no tax. Counting a second
commander’s casts enables partners in Atlas. A minus corrects an accidental cast.

Automatic elimination has independent live toggles for zero life, poison and
21 damage received from one commander (not pooled across sources or partners).
New-game defaults are on. Poison defaults to 10, or 15 shared for a Two-Headed
Giant team; its limit is editable. Switching team mode in the lobby selects the
standard poison limit unless the request supplies a custom one. Changing a limit
or enabling a rule immediately checks all living players, as one batch; this
avoids a false winner if multiple players meet loss conditions simultaneously.
Poison/Commander losses use the elimination reason, not concession. The relevant
rule should be disabled before a card changes that loss condition. All these
settings remain reachable during pause.

Connected counters use the existing ChangeCounter intent with CounterKind,
validated by GameEngine and the living-seat/table-decision handler. Settings use
ConfigureGame; the common post-intent rule finalizer resolves automatic losses
before projecting/checkpointing. Settings schema 3 and checkpoint schema 4
persist rules, counters and initiative. The HTTP state is a read model; no
radio change. Native connected clients require Atlas for the new actions while
disconnected (they do not replay status/counter changes). Standalone games own
and persist their equivalent local state. New matches reset counters/status;
match settings carry forward. Elimination clears initiative; city’s blessing
and numerical counters remain as the last values on an eliminated player.
