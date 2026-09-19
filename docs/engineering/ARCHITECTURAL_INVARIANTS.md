# TurnHub Architectural Invariants

These are hard design rules for TurnHub. They are not suggestions. A proposed implementation that violates an invariant should be treated as an architectural regression unless the invariant itself is deliberately revised.

## Invariant 1: Atlas is the sole authority for game and table state

Only Atlas may create, validate, mutate, persist, recover, or resolve canonical game/table state.

Canonical state includes, at minimum:

- Player list and seat assignments.
- Active player and turn order.
- Turn number.
- Game lifecycle state.
- Pause/resume state.
- Turn timing anchors and warning configuration.
- Life totals and game-specific counters.
- Elimination/concession state.
- Victory claims, confirmations, denials, and winner.
- Controller-to-player assignment.
- Session recovery state.
- Statistics derived from game events.

Physical Sigils, Virtual Sigils, browsers, future apps, and simulated controllers MUST NOT independently decide or mutate canonical game state.

They may:

- Capture input.
- Authenticate a user/controller where appropriate.
- Send an intent/request to Atlas.
- Cache presentation data.
- Render state supplied by Atlas.
- Perform local UI behavior such as debounce, animation, sound, vibration, display refresh, or connection indicators.

They may not treat cached presentation state as authoritative.

Conceptual rule:

```text
Controller -> request/intent -> Atlas -> validate/mutate -> canonical state -> render/update -> Controller
```

If a controller disconnects, restarts, or disagrees with Atlas, Atlas wins.

## Invariant 2: One semantic implementation per game action

A game action must have one authoritative semantic implementation.

For example, physical Pass and browser Pass may arrive through different transports, but both must converge on the same Atlas operation rather than maintaining separate pass logic.

This applies to:

- Pass.
- Pause/resume.
- Concede/eliminate.
- Claim/confirm/deny victory.
- Life/counter changes.
- Turn progression.
- Timer behavior.
- Game completion.

Transport adapters may validate transport-specific concerns, but must not reimplement game rules.

## Invariant 3: Transport does not own semantics

BLE, Wi-Fi, HTTP, USB/serial, or any future transport is responsible for moving messages, not deciding what they mean for the game.

Changing transport should not require rewriting the game engine.

## Invariant 4: Shared contracts have one source of truth

Definitions consumed by multiple firmware targets should originate from one canonical source wherever the toolchain permits it.

Examples:

- Protocol message types.
- Protocol version.
- Capability flags.
- Shared IDs and limits.
- Common semantic enums.

Do not maintain hand-copied Atlas and Sigil versions of the same contract long-term.

## Invariant 5: Hardware details stop at the hardware boundary

GPIO numbers, electrical polarity, board-specific quirks, and display-driver details must not leak into game-rule logic.

Hardware code should translate physical behavior into semantic events such as `PASS_REQUEST`, `ACTION_PRESSED`, or `PAIR_REQUEST`.

## Invariant 6: Presentation state is disposable

LED patterns, e-ink contents, browser rendering, animation state, and local caches may be reconstructed from Atlas state.

Loss of presentation state must not corrupt a game.

## Invariant 7: Persistence has an owner

Each persistent fact must have one defined owner and storage location. Multiple components should not independently persist competing copies of the same canonical fact.

Examples:

- Game/session recovery: Atlas.
- Pairing relationship: Atlas is authoritative; Sigil may retain the minimum identity needed to reconnect.
- Player profiles/statistics: Atlas.
- Device-local calibration or hardware configuration: the device that requires it, unless promoted to table-level configuration.

## Invariant 8: Derived values should stay derived

Do not persist or continuously synchronize values that can be safely calculated from authoritative anchors.

Examples include elapsed time, remaining time, warning phase, and many display-only labels.

## Invariant 9: New features declare ownership before implementation

Before adding a significant feature, answer:

1. Who owns its canonical state?
2. What is the request/intent API?
3. Which component validates it?
4. Which component persists it, if needed?
5. Which clients merely render it?
6. What shared contract is required?

If ownership is unclear, implementation should wait until the boundary is defined.

## Code review test

For every new feature, ask:

> If every Sigil and browser vanished and later reconnected, could Atlas reconstruct the correct game entirely from its own canonical state?

For game/table behavior, the expected answer is **yes**.

Last established: 2026-09-19
