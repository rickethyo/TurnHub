# Software Architecture and Intents

How Atlas is organized and how every controller's input becomes a game
change. The hard rules are in [Architectural Invariants](ARCHITECTURAL_INVARIANTS.md);
the module map is in `CLAUDE.md` and `Atlas/include/atlas_app.h`.

## Ownership

**Atlas owns canonical game and table state.** Sigils, phones (browser and
Android), the Atlas touchscreen and simulators capture input, send requests
and render what Atlas sends back. If a controller disconnects, restarts or
disagrees with Atlas, Atlas wins.

Atlas owns: players and seats, active player and turn number, lifecycle
(lobby, starting, running, paused, game over), turn timing anchors, life and
Commander damage, elimination and concession, win claims, controller
assignments, the recovery record and statistics derived from game events.

```text
Profile (persistent person: ID, name, PIN/password hash, statistics)
  -> Participant (one per person at the current table)
    -> controller assignments: Sigil seat A/B, browser sessions, Android app
```

A controller can change without replacing the participant or moving its
statistics. Several controllers (two phones and a Sigil, say) can drive one
participant. Hardware identity is never player identity.

## The Intent pipeline

```text
transport adapter (ESP-NOW packet / HTTP handler / touchscreen / BOOT button)
  -> TurnHub::Intent {type, origin, actor, payload}      Atlas/include/intent.h
  -> IntentDispatcher (fixed table, one handler per IntentType; a second bind is rejected)
  -> handle*Intent (Atlas/src/*_intents.cpp) -> GameEngine / Lobby mutation
  -> observer hook -> checkpointGame() (recovery record)
```

The adapter answers "what is this controller asking for?"; Atlas answers "is
that legal, and what does it do?". The dispatcher is heap-free.

- **Adapters stay thin.** They decode their transport, authenticate it and
  build Intents. They never call `game.*`/`lobby.*` mutators or transition
  helpers. `Atlas/tests/host/audit_adapters.py` enforces this for the adapters
  in `sigil_input.cpp`, `web_adapters.cpp`, `front_panel.cpp` and
  `touch_controls.cpp`; list any new adapter there.
- **Actors.** `IntentActor` carries controller-facing identity
  (`controllerId`, slot, player number). Atlas resolves it to the canonical
  participant and checks that they agree. Browser identity comes from the
  authenticated session, never from a form field, and `payload.profileId` is
  filled only by trusted Atlas adapters. Admin requests carry the signed-in
  account in `payload.moderatorId`; the handler re-checks its permission.
- **Payload.** Fixed-size: `targetPlayer`, `value`, `flags`, `requestId`,
  `counterSource`/`counterSlot`, `durationMs`, `moderatorId`, `profileId`. If unrelated meanings pile up in one field,
  give that Intent a typed payload instead.
- **Results.** Handlers return `IntentResult` (`Accepted`, `Rejected`,
  `Unsupported`, `InvalidActor`, `InvalidState`, `Unauthorized`, `Conflict`);
  each adapter turns it into HTTP status, a Sigil tone or a screen message.
- **Not in a handler:** HTTP parsing, packet decoding, GPIO reads, debounce,
  drawing, HTML, pin numbers. **In a handler:** may this actor ask, is the
  state right, which participant, which transition, which result.
- **New gameplay actions** need an `IntentType`, a handler bound in
  `main.cpp`'s `configureIntentHandlers()`, and host scenarios. An enum value
  with no bound handler is not a feature (`NudgeTable` is reserved, unbound).

The host suite compiles the real handlers, `GameEngine`, `Lobby` and adapters
against stubs, so a rule can be tested with no HTTP, radio or display.

## Intent catalog

Origins: `PhysicalSigil`, `Browser`, `AndroidApp`, `AtlasHardware` (the
touchscreen and BOOT button: physical presence), `Simulator`, `System`
(Atlas's own timers; never accepted from a caller).

| Intent | Who sends it | Notes |
|---|---|---|
| `Pass`, `CancelPass`, `CommitPass` | Sigil, phone; `CommitPass` System | PASS waits `PASS_GRACE_MS` (3 s, `protocol.h`) so it can be undone; the commit uses the original request time for turn statistics |
| `MasterPass` | Touchscreen (Table screen, hold 2 s); a Game Master's portal pass logs the same way | Passes a stuck turn at once, no grace; refused while paused or with a claim or elimination open. Logged `ATLAS|GAME|MASTER_PASS` |
| `Pause`, `Resume`, `TogglePause` | Sigil, phone, touchscreen | `Pause` flag `ARM_WIN_ON_PAUSE` arms a pause that may continue into a win claim |
| `ClaimWin`, `ConfirmWin`, `DenyWin` | Sigil, phone | Claimant must be the active living seat; confirmations follow Atlas's player order, shared seats included. Flag `CLAIM_FROM_ARMED_PAUSE` completes an armed pause |
| `Concede`, `BeginElimination`, `CycleElimination`, `CancelElimination`, `Eliminate` | Sigil, phone | A surviving elimination stays paused; a concession restores prior running play |
| `EndMatch` | Touchscreen (Table screen, hold 5 s) | Ends a running or paused match as a draw, statistics once |
| `Join`, `Leave`, `JoinProfile`, `LeaveProfile`, `BindProfile`, `PickProfile` | Sigil, phone | Lobby only. Slot 1 = seat A (whole controller), slot 2 = seat B. A profile has at most one participant. See [Players and Accounts](PLAYERS_AND_ACCOUNTS.md) |
| `SelectStarter` | Sigil, phone | `value`: exact seat (0), cycle (1), random (2) |
| `ArmStart`, `StartGame`, `CancelStart`, `CompleteStart` | Any seated player; touchscreen needs no seat or arming; `CompleteStart` System | Two or more players. There is no table host (2026-09-25) |
| `Rematch`, `ResetGame` | Any seated player, touchscreen (after a game) | Rematch keeps seats; reset empties the lobby |
| `RemoveSeat` | Touchscreen, lobby only (Player screen, hold Remove 2 s) | The player leaving, not moderation: removes that seat; seat A also takes its Sigil's seat B. Logged `ATLAS|LOBBY|REMOVE` |
| `MoveSeat`, `SetSeatSide` | Touchscreen (and a table tablet for `MoveSeat`), lobby only, any player | Turn order. On a shared Sigil, moving toward the other seat swaps A/B; otherwise the whole Sigil moves. `SetSeatSide` puts B before (left) or after (right) A |
| `ConfigureGame` | Any seated player, lobby | `flags` game profile, `value` starting life, `durationMs` turn timer (0 = off). See [Gameplay](GAMEPLAY.md) |
| `ChangeLife`, `RequestLifeChange`, `RespondLifeChange`, `ExpireLifeChanges` | Sigil, phone; expiry System | Own life directly; another player's through an approval window the recipient chooses (15, 30 or 60 s, default 15 s) |
| `NudgePlayer` | Phone (app and portal), running game | A living player who is not up prods the active player: their Sigil plays the Nudge cue and their phone shows who nudged them (state `nudge`). One per player per 30 s (`NUDGE_COOLDOWN_MS`); refused for a Game Master-muted account. Logged `ATLAS|GAME|NUDGE` |
| `ChangeCounter`, `RecordCommanderHit`, `UndoCommanderHit`, `SetPartner` | Phone (`ChangeCounter`), Sigil Commander flow | The receiver records damage; see [Gameplay](GAMEPLAY.md) |
| `Moderate` | Game Master | Force pass, reset connections, remove from game, nudge mute |
| `PairRequest`, `PairConfirm`, `ForgetPairing`, `ConfigurePairing` | Touchscreen/BOOT; Admin (portal) | See [Pairing and Secure Link](PAIRING_AND_SECURE_LINK.md) |
| `FactoryReset`, `ResetTable` | Admin verified at the table; `FactoryReset` also touchscreen/BOOT | `ResetTable` is the portal's Return table to lobby: ends a match as a draw, then empties the lobby |
| `ConfigureSpeaker` | Admin | Atlas speaker volume 0-3 |
| `UpdateSigil` | Admin verified at the table | See [Firmware Updates](FIRMWARE_UPDATES.md) |
| `AdvanceSetup` | Admin (portal/app), touchscreen | See [First-run Setup](FIRST_RUN_SETUP.md) |
| `Sleep` | Touchscreen, between games | Deep sleep; touch or BOOT wakes (a restart) |

Turn-timer expiry has no Intent: it is a derived cue and never changes state.
The JSON Intent envelope (`protocol/intent-v0.1.schema.json`) and
`POST /api/v1/intent` are drafts, not implemented; never expose `System`
operations to callers.

## Design rules that follow

- **Timers are anchors, not ticks.** The engine stores when a turn started
  and the configured limit; remaining time and warning phase are derived per
  call. Pause freezes the anchor. Nothing rewrites state every second.
- **Settings are captured at the lifecycle boundary.** A match captures its
  game profile, starting life and timer at start; lobby edits apply to the
  next match.
- **Statistics come from engine events**, once per match, through
  `profile_stats_bridge.cpp` (see [Storage and Recovery](STORAGE_AND_RECOVERY.md)).
- **Events, not polling, on the radio.** Atlas sends state when it changes and
  resends on every Hello so a lost packet heals itself. E-ink redraws only on
  meaningful change.
- **Capabilities, not version checks.** A Sigil announces what varies in its
  Hello capability byte ([Radio Protocol](RADIO_PROTOCOL.md)).
- **Presentation reacts to state.** LED and audio cues are picked from state
  by cue layers ([Gameplay](GAMEPLAY.md), "Cues"); some invalidation still
  sits next to the transitions that cause it.
- **Two games per Atlas** (*Experimental*, host-tested 2026-10-07): each
  game's state is a `GameTable` (`tables[MAX_GAME_TABLES]`, two), reached
  through `table()` under a `TableScope` that each entry point selects. The
  design and the remaining steps are in [Planned Designs](PLANNED_DESIGNS.md).

## History

Generation 0 coupled hardware and rules; Generation 1 made a Raspberry Pi the
authority over wired modules; the ESP32 port (merged `6844f94`, 2026-09-19)
moved one operation at a time onto the dispatcher. Details:
[Generation History](history/GENERATION_HISTORY.md) and the
[2026-09-19 Intent migration record](history/2026-09-19-atlas-intent-verification.md).
