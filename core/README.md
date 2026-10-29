# turnhub-core

One set of game rules for every TurnHub device. *Experimental* (2026-10-29):
built and tested on the host only; Atlas and the app don't link it yet. The
plan and its phases are in [Shared table authority](../Documentation/engineering/TABLE_AUTHORITY.md)
and [Staged changes](../Documentation/engineering/STAGED_CHANGES.md).

The reference is the app's on-device game, `StandaloneGame.kt`. This is its
port to portable C++17 (no Arduino, no Android, no third-party library), so
Atlas can run it natively, the app through the NDK and iOS through Swift's
C++ interop.

## Shape

- `Game` is a value. Each rule returns the next game, or the same one with an
  unchanged `revision` when it doesn't apply, exactly like the Kotlin.
- Time is passed in (`nowMs`) and so are random draws (`turnhub::Random`), so
  a game is replayable from its actions.
- A player's own controls (a Sigil, a guest phone, the Atlas screen) go
  through `applySeatAction`, the port of the app's Play together door
  (`StandaloneTable.applyLanAction`): it checks match, revision and seat.
- Data in, data out: `toJson(game)`, `gameFromJson(json)` and
  `applyAction(game, action, random)`. JSON field names are the Kotlin
  property names; enums use the Kotlin names (`MTG_COMMANDER`, `RUNNING`).

Ported so far: the lobby (players, formats, house rules, Two-Headed Giant,
starting life, turn timer and its rules), start, rematch, Sean Mode, reset
and Back to setup; turns, timeout, extensions and pause; life (Pokémon
prizes, Lorcana lore), Commander damage, counters and rad; monarch,
initiative, day/night and reversed order; elimination rules, attribution,
concession and win claims; Star Commander, Emperor and Bag of Doom.
Not yet: Planechase, Pokémon/Lorcana turn markers and Special Conditions,
commander selection and the finished-game record.

Death May Die can't ship in a released version (licensing, owner
2026-10-29). Its rules will live in a separate local-only module,
`core/local/death-may-die/`, compiled only with `TURNHUB_LOCAL_DMD` for local
and preview builds, never in release firmware or Play builds. Keep its rules,
names and data out of the main core.

## Actions

Atlas links the core as a PlatformIO library (`lib_deps = symlink://../core`,
built with gnu++17).

Each action is a JSON object whose `do` is the Kotlin method name, with its
parameters by name, for example
`{"do": "changeLife", "index": 0, "delta": -3, "nowMs": 1000}`.
`{"do": "seat", "player": 1, "kind": "life", "value": -1, ...}` sends a seat
action; scenarios leave out `match` and `revision` (the current ones are
used, or the one before with `"stale": true`). The full list is in `applyAction` (`src/turnhub_core.cpp`); anything else is
ignored.

## Scenarios

`scenarios/*.json` are the shared spec. Each is a list of steps: an `action`,
an optional `random` list (draws used in order, and all of them must be
used), `expect` (dotted paths into the state, e.g. `players.0.life`),
`unchanged: true` (the action must not apply) and an optional `timer` check.
The same files run against:

- the core: `bash core/tests/run-linux.sh` (GCC, ASan and UBSan; CI's host
  tests job), and
- the Kotlin rules: `CoreScenarioTest` in the app's JVM unit tests.

When a rule changes, change the Kotlin and the core together and add a step
that shows it; both suites must pass.
