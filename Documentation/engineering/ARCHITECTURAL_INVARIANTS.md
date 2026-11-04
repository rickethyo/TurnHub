# TurnHub Architectural Invariants

These are hard design rules for TurnHub. They are not suggestions. A proposed implementation that violates an invariant should be treated as an architectural regression unless the invariant itself is deliberately revised.

## Invariant 1: The hub that runs the core owns the game; Atlas owns hardware and records at rest

*Revised by the project owner on 2026-11-04 (phone-hosted games); was "Atlas is the
sole authority for game and table state".*

One device runs each game: the **hub**, a phone or tablet that runs the shared
rules core (`core/`, through the NDK on Android). Only the hub may create,
validate, mutate, persist or resolve that game's state:

- Player list, seats and turn order.
- Game lifecycle, pause/resume and turn timing.
- Life totals and game-specific counters.
- Elimination, concession, victory claims and winners.
- The in-progress match and its result.

Atlas runs no game. It owns its hardware and what it keeps at rest: the radio and
Sigil pairings, its screen, profiles and PINs, statistics, match history,
settings and the table lease (`protocol/hosted-v1.md`). It shows the hub's game,
relays Sigil and touchscreen input to the hub as inputs, and records the finished
game the hub reports.

Sigils, the Atlas touchscreen, guest phones and simulators MUST NOT decide game
outcomes. They may capture input, authenticate a user where appropriate, send an
input or request to the hub, cache presentation data, render the hub's state and
perform local UI behavior (debounce, animation, sound, vibration, connection
indicators). They may not treat cached presentation state as authoritative.

```text
Controller -> input -> hub (shared core) -> validate/mutate -> state -> Atlas and guests render
```

If a controller disconnects, restarts, or disagrees with the hub, the hub wins.
If the hub drops, its game pauses on Atlas until the hub (or, from Atlas's
checkpoint, another phone) takes the lease again.

A [shared-devices](SHARED_DEVICES.md) room host is that game's hub: guest apps
render its snapshots and send authenticated own-player/shared-table requests; they
never become another authority, save remote results or take over on disconnect.
Room grants never confer account administration.

## Invariant 2: One semantic implementation per game action

A game action must have one authoritative semantic implementation.

For example, a Sigil's Pass and a phone's Pass may arrive through different transports, but both must converge on the same core operation on the hub rather than maintaining separate pass logic.

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
- Connected player profiles/statistics: Atlas.
- Standalone player UUIDs, match history and delivery metadata: the native
  app's local domain (the separate-game exception above); no name-derived or
  unscoped Atlas identity. Delivery acknowledgement cannot delete local history.
- Sigil user/device settings, including startup preference: Atlas, keyed to the
  device. Sigils apply disposable runtime values and do not persist user settings.
- Minimum device identity/pairing bootstrap material is distinct from user
  settings and does not confer configuration or game-state authority.

## Invariant 8: Derived values should stay derived

Do not persist or continuously synchronize values that can be safely calculated from authoritative anchors.

Examples include elapsed time, remaining time, warning phase, and many display-only labels.

## Invariant 9: The feature gate is mandatory

Before implementation begins on any significant feature, the design MUST answer:

1. **Who owns its canonical state?**
2. **What intent changes or requests it?**
3. **Which component validates that intent?**
4. **Does it need persistence, and who owns that persistence?**
5. **Which clients only render or present it?**
6. **Does it require a shared protocol/contract change?**
7. **What third-party dependencies does it add?** (update `Documentation/legal/`)
8. **What is its accessibility impact?** (see `ACCESSIBILITY.md`)
9. **What is its counterpart on the other side?** (Invariant 12: the app's
   change on Atlas, or Atlas's change in the app)

These questions are a feature gate, not optional design guidance.

If any answer is unclear, the feature boundary must be defined before implementation continues. Temporary prototypes may deliberately bypass the production boundary only when clearly marked experimental and prevented from becoming the canonical implementation by accident.

A feature should not create a second state owner, a second semantic implementation, or a transport-specific version of a game rule merely because doing so is locally convenient.

## Invariant 10: Controller inputs converge on semantic Intents

Physical Sigils, browsers, Atlas controls, simulators, and future applications should converge on the same semantic vocabulary before canonical behavior is executed: on Atlas, its Intents for device and account work; for the game, the hub's inputs.

Adapters may decode and authenticate their own transport, but canonical validation and mutation belong to the authoritative handler: an Atlas Intent handler for what Atlas owns, the hub's core for the game.

For example:

```text
Sigil PASS ---------\
Atlas screen Pass ---+--> hosted input --> the hub's core
Guest phone Pass ----/
```

The Intent layer is an application boundary on Atlas. It does not run games and it does not create a second source of truth.

## Invariant 11: Accessibility is a first-class system requirement

No essential TurnHub state, warning, instruction, or action may depend on a single sensory characteristic or a single input method when a practical alternative exists.

At minimum:

- Color MUST NOT be the sole carrier of essential meaning.
- Sound MUST NOT be the sole carrier of essential meaning.
- Motion, flashing, or LED cadence MUST NOT be the sole carrier of essential meaning.
- Essential digital controls MUST provide accessible input paths appropriate to the platform.
- Physical actions that depend on timing, fine dexterity, or a specific gesture SHOULD have an authorized assistive path when practical.
- Accessibility alternatives MUST converge on the same semantic action and authorization as the default controller path. They do not create a second game engine or state owner.

Web and application surfaces SHOULD target WCAG 2.2 Level AA. Physical Atlas/Sigil design MUST follow the separate requirements in `ACCESSIBILITY.md`, including redundant cues and assistive-input considerations.

A feature is not complete merely because its default interaction works for a user who can perceive every cue and perform every default gesture.

## Invariant 12: The app and Atlas stay at parity (owner, 2026-10-29)

Anything built for the app is built for Atlas, and anything built for Atlas is
built for the app, so TurnHub never again splits into two ecosystems.
Accounts, statistics, game rules, records, settings and their behavior work
the same whether a game is shown on a phone, a tablet or an Atlas; since
2026-11-04 every game runs on one shared core on its hub, so the rules cannot
drift.
The product model is **one game, many controllers**: host/transport selection
must not select a different game layout or interaction flow. Shared presentation
consumes the same state/action vocabulary; adapters handle credentials,
requests and persistence. Player and shared-table roles may expose different
permissions, consistently across devices.

- A feature lands on both sides in the same PR, or the PR records the
  counterpart in `STAGED_CHANGES.md` as the next slice. Neither side ships a
  user-visible feature the other can't reach without that entry.
- Shared formats (game records, statistics, account copies) have one schema
  that both sides read and write; neither keeps a richer private version.
- The only exceptions are features that need hardware one side doesn't have
  (Sigils, the Atlas radio, a phone camera); the PR names them as such.

## Code review tests

For every new feature, ask:

> If every Sigil, guest phone and the Atlas screen vanished and later reconnected, could the hub reconstruct the correct game entirely from its own state?

For game/table behavior, the expected answer is **yes**.

Also ask:

> If the same action can be requested through two different controllers, do both paths converge on the same semantic Intent and authoritative handler?

The expected answer is **yes**.

For every user-facing feature, also ask:

> If a user cannot perceive one of our cues or cannot perform the default input gesture, is there another practical way to obtain the same information or request the same authorized action?

The expected answer is **yes** whenever a practical alternative exists.

Last established: 2026-09-20; feature gate extended to eight questions (as in `CLAUDE.md`) 2026-10-06; Invariant 1 revised for phone-hosted games 2026-11-04
