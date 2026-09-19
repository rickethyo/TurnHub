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

An adapter may know only a controller identity at first.

For example, a physical packet may identify a Sigil/module and slot while an authenticated browser request may identify a browser session that Atlas maps to a player.

`IntentActor` therefore carries controller-facing identity fields, while Atlas remains responsible for resolving those into the canonical Player when required.

Do not trust a client-provided player number when Atlas can resolve the player from authenticated controller identity.

## Payload rule

The initial payload is deliberately fixed-size:

```text
targetPlayer
value
flags
```

This keeps the boundary heap-free and transport-neutral while the intent set stabilizes.

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

### First migration target: Pass

Pass is a good first target because the current firmware already has a mostly centralized `requestPass()` implementation, but it is invoked directly from multiple interfaces.

Target architecture:

```text
Physical Sigil PASS packet ----\
Browser Pass -------------------+--> IntentType::Pass --> handlePassIntent()
Atlas master control ----------/
```

`handlePassIntent()` should resolve the actor, enforce turn/state rules once, and then call the existing pending-pass/game-engine mechanisms.

After that migration is stable, use the same pattern for Pause/Resume, victory claims, concessions, and starter selection.

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

Last established: 2026-09-19
