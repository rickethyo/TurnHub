# TurnHub Intent Model

The Intent layer is the application boundary between controllers/transports and authoritative Atlas behavior.

## Purpose

TurnHub has multiple ways to request the same action:

- Physical Sigil buttons.
- Browser / Virtual Sigil controls.
- Atlas physical controls.
- Automated simulators and tests.
- Future mobile or venue applications.

These interfaces must not independently implement gameplay semantics.

Instead:

```text
Physical Sigil ----\
Browser ------------\
Atlas control --------> Intent -> Atlas Intent Dispatcher -> authoritative handler -> canonical Atlas state
Simulator -----------/
Future application --/
```

The adapter answers only: "What is this controller requesting?"

Atlas answers: "Is that request legal, and what does it do to the game?"

## Hard ownership rule

An Intent is a request, never a state mutation performed by the sender.

Examples:

- A Sigil sends `Pass`; it does not advance the turn itself.
- A browser sends `Concede`; it does not mark the player eliminated locally.
- A future app sends `ChangeLife`; it does not become the source of truth for life.
- A Pair button sends `PairRequest`; Atlas decides whether pairing is permitted and records the authoritative relationship.

## Current framework

The ESP32 Atlas now contains:

```text
Atlas/include/intent.h
Atlas/include/intent_dispatcher.h
Atlas/src/intent_dispatcher.cpp
```

`intent.h` defines:

- `IntentType`
- `IntentOrigin`
- `IntentActor`
- `IntentPayload`
- `IntentStatus`
- `IntentResult`

The dispatcher uses a fixed handler table. Each semantic Intent type may have one authoritative handler binding. An accidental second binding is rejected rather than silently replacing the first implementation.

This is intentionally heap-free and small enough for the ESP32 architecture.

## Initial Intent vocabulary

Core game:

- `Pass`
- `Pause`
- `Resume`
- `TogglePause`
- `Concede`
- `ClaimWin`
- `ConfirmWin`
- `DenyWin`

Lobby/lifecycle:

- `Join`
- `Leave`
- `SelectStarter`
- `StartGame`
- `Rematch`
- `ResetGame`

Game-specific data:

- `ChangeLife`
- `ChangeCounter`

Table interaction:

- `NudgePlayer`
- `NudgeTable`

Device management:

- `PairRequest`
- `PairConfirm`
- `ForgetPairing`

The vocabulary is expected to evolve. Adding an enum is not the same as implementing the feature. A feature becomes authoritative only when Atlas binds a handler for that Intent.

## Actor model

In Atlas `0.6.0-dev`, internal `IntentActor.controllerId` replaces the old
`moduleId` name. The physical radio prefix is retained for adapter compatibility;
browser registrations use a separate logical handle range. Authenticated browser
profiles resolve to current participation before constructing game Intents.
`JoinProfile`, `LeaveProfile` and physically confirmed `BindProfile` are Atlas
application requests carrying a trusted adapter-supplied `payload.profileId`.
Clients cannot choose another actor by supplying a player/profile field to a
game-control endpoint. Profile attachment is lobby-only and does not create a
second participant when the profile already joined by phone.

An adapter may know only a controller identity at first.

For example, a physical packet may identify a Sigil/controller and slot while an authenticated browser request may identify a browser session that Atlas maps to a player.

`IntentActor` therefore carries controller-facing identity fields, while Atlas remains responsible for resolving those into the canonical Player when required.

Do not trust a client-provided player number when Atlas can resolve the player from authenticated controller identity.

## Payload rule

The initial payload is deliberately fixed-size:

```text
targetPlayer
value
flags
profileId
```

This keeps the boundary heap-free and transport-neutral while the intent set stabilizes. `profileId` is populated only by trusted Atlas authentication/application adapters for profile-scoped requests.

As individual intents mature, payload semantics must be documented. If many unrelated meanings begin accumulating in generic fields, replace them with explicit typed payload structures rather than building an undocumented bit field.

## Migration strategy

Do not rewrite all current Atlas behavior at once.

Migrate one semantic operation at a time:

1. Identify all current entry points for the operation.
2. Define or confirm its `IntentType`.
3. Move semantic validation and mutation into one authoritative handler.
4. Bind that handler once in the Atlas dispatcher.
5. Convert physical/web/Atlas/simulator entry points into adapters that construct the same Intent.
6. Verify behavior and delete the old duplicated direct paths.

### Current implemented boundary

PASS, pause/resume/toggle, concession, victory claim/confirm/deny, join/leave,
starter selection, start arming/countdown/cancellation, rematch/reset, and
elimination selection/cycle/cancel/confirmation now enter the dispatcher.
Timers submit System intents for deferred PASS commitment and countdown completion.
The current bindings and validation evidence are recorded in
[Atlas intent verification](ATLAS_INTENT_VERIFICATION.md).

Application handlers and their private transition helpers live in the Atlas
application modules declared by `Atlas/include/atlas_app.h` (`gameplay_intents.cpp`,
`table_intents.cpp`, `moderation_intent.cpp`); `main.cpp` binds them. Adapters live in
`sigil_input.cpp`, `web_adapters.cpp` and `front_panel.cpp`.
The native harness compiles those actual handlers with the real GameEngine/Lobby.
Transport authentication, packet decoding, GPIO debounce, and held/chord/suppression
bookkeeping remain outside the game rules. Presentation side effects remain adjacent
to the authoritative transitions.

### Implemented payload meanings

- Game-seat intents use `actor.controllerId`, `slot`, and `playerNumber`; Atlas validates
  their agreement with the current canonical seat. Browser identities come from
  authenticated sessions, not arbitrary form fields.
- `Join`/`Leave`: slot 1 requests whole-controller membership; slot 2 requests secondary
  seat membership. Membership changes require Lobby state. Whole-controller Leave is
  bound but has no new physical gesture or HTTP endpoint in this patch.
- `SelectStarter`: `payload.value` is `StarterSelection::ExactSeat` (0),
  `CycleModule` (1), or `Random` (2). Random selection requires a seated actor and two
  players. Browser requests select the exact authenticated seat. `CycleModule`
  retains its compatibility name while the internal actor field is controller-based.
- `Pause`: `payload.flags & ARM_WIN_ON_PAUSE` identifies a pause gesture that may
  continue into a win claim. Atlas arms it only for the active player's controller.
- `ClaimWin`: `payload.flags & CLAIM_FROM_ARMED_PAUSE` requests completion of that
  armed gesture. Atlas validates the arm and resumes play on denial. Without this
  flag, denial restores the state before the claim. Neither flag is inferred from
  transport/origin inside the semantic handler.
- `ArmStart`, `StartGame`, `Rematch`, and `ResetGame` identify the requesting controller;
  Atlas validates that the controller is seated (there is no table host since
  2026-09-25), state, player count, and start-arm constraints.
- `BeginElimination`, `CycleElimination`, `CancelElimination`, and `Eliminate` refer
  to Atlas's selected target. Eliminate is intentionally distinct from Concede:
  a surviving elimination stays paused; concession restores prior running play.
- `CancelPass` identifies the controller owning the pending pass. `CommitPass` and
  `CompleteStart` are internal System-only intents; Atlas rechecks elapsed time and
  current state and supplies the authoritative timestamps itself.

These additions are internal C++ application requests, not new ESP-NOW packet IDs or
HTTP endpoints. The future JSON envelope is not currently decoded by this runtime;
do not expose System origin or deferred-commit operations as caller-selected ingress.
`PairRequest` (Atlas Pair button only) opens Atlas's pairing window (15 s by
default; see [Manual Pairing](MANUAL_PAIRING.md)). `ForgetPairing` and
`ConfigurePairing` come from the admin portal: `payload.moderatorId` carries the
signed-in account, and the handler re-checks its Admin permission. `ForgetPairing`
takes a Sigil ID or `FORGET_ALL_SIGILS` (-1) in `value`, works only in the lobby
and refuses Sigils with seated players. `ConfigurePairing` takes Atlas's window in
milliseconds (15,000, 30,000 or 60,000). `ConfigureSpeaker` (Admin) takes the
Atlas speaker volume, 0 (off) to 3 (high), and saves it before applying it.
`ResetTable` (Admin, verified at the table with the presence code)
returns the table to an empty lobby from any state: it cancels a countdown, ends
a running or paused match as a draw first (statistics once, like `EndMatch`),
then clears every participant. It is the portal's **Return table to lobby**,
for when nobody at the table can finish or reach their Sigil. `EndMatch` is Atlas-hardware only (a
5-second End match hold on the Atlas touchscreen): it ends a running or paused match as a draw,
overriding a queued PASS, a win claim or an elimination selection, and commits
statistics once through the normal game-completed callback. `PairConfirm` remains
unsupported. General counters and nudges
remain unsupported. Local life-counter work binds `ChangeLife`: `targetPlayer`
must match the validated actor, and `value` is the signed delta. `ConfigureGame`
requires a seated actor in the lobby (any seat; no table host); `flags` is the game-profile enum,
`value` is starting life and `durationMs` is the turn timer (0 = off). See
[Game profiles and life](GAME_PROFILES_AND_LIFE.md) and
[Turn timer and cues](TURN_TIMER_AND_CUES.md). Turn-timer expiry has no Intent:
it is a derived presentation cue and never changes game state.

## Result model

Intent handlers return an `IntentResult` rather than forcing transport-specific output.

For example:

```text
Accepted
Rejected
Unsupported
InvalidActor
InvalidState
Unauthorized
Conflict
```

A browser adapter can translate that into HTTP/JSON text. A physical Sigil adapter can translate it into a tone, LED response, display message, or no response. The semantic handler does not need to know which presentation is being used.

## What does NOT belong in an Intent handler

- HTTP parsing.
- BLE packet parsing.
- GPIO reads.
- Button debounce.
- E-ink drawing.
- Browser HTML.
- Device-specific LED pin numbers.

Those belong in adapters or hardware/presentation layers.

## What DOES belong in an authoritative handler

- Is this actor allowed to request the action?
- Is the table/game in a state where the action is legal?
- Which canonical player does the actor represent?
- What canonical state transition occurs?
- Which domain operation is called?
- What semantic result is returned?

Presentation side effects such as audio/LED invalidation may initially remain adjacent during migration, but the long-term goal is for them to react to resulting state/events rather than becoming part of rule ownership.

## Migration completion test

An operation is fully migrated when:

- Every controller path constructs the same semantic Intent.
- Exactly one Atlas handler owns its rules.
- The handler is the only path that performs that canonical state transition.
- Transport-specific code cannot bypass the handler.
- Domain tests can exercise the behavior without HTTP, BLE, GPIO, or e-ink hardware.

Last updated: 2026-09-21
