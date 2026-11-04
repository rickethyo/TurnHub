# Software Architecture and Intents

How Atlas is organized, and how every controller's input reaches the hub
that runs the game. The hard rules are in [Architectural Invariants](ARCHITECTURAL_INVARIANTS.md);
the module map is in `CLAUDE.md` and `Atlas/include/atlas_app.h`.

## Ownership

**The hub owns the game; Atlas owns hardware and records at rest** (owner,
2026-11-04; Invariant 1). The hub is the phone or tablet that runs the game
with the shared rules core (`core/`, through the NDK in the app). Guest
phones, Sigils, the Atlas touchscreen and simulators capture input, send it to
the hub and render the hub's state. If one disconnects, restarts or disagrees,
the hub wins.

The hub owns: players and seats, active player and turn number, lifecycle,
turn timing, life and Commander damage, elimination and concession, win
claims and the finished match record.

Atlas owns: its radio and Sigil pairings, its screen, the table lease
(`protocol/hosted-v1.md`), profiles and PINs, statistics, match history,
settings, first-run setup and resets. It keeps the hub's latest state in RAM
to show, queues its Sigils' and touchscreen's game inputs for the hub, and
records the finished game the hub imports.

```text
Profile (persistent person: ID, name, PIN/password hash, statistics)
  -> Participant (one per person in the hub's game)
    -> controllers: Sigil seat A/B, guest phones, the hub itself
```

A controller can change without replacing the participant or moving its
statistics. Hardware identity is never player identity.

## Game input

```text
Sigil key / Atlas touch / guest phone
  -> input to the hub (Atlas: /api/hosted/inputs queue; guests: the hub's LAN room)
  -> hub's shared core validates and applies it
  -> hub publishes state -> Atlas screen, Sigils and guests render it
```

Atlas never applies a game input itself. `hosted_game.cpp` holds the lease,
the hub's latest state, the input queue and the resume offer;
`hosted_sigils.cpp` turns Sigil menus and keys into inputs.

## The Intent pipeline (Atlas's own work)

```text
transport adapter (ESP-NOW packet / HTTP handler / touchscreen / BOOT button)
  -> TurnHub::Intent {type, origin, actor, payload}      Atlas/include/intent.h
  -> IntentDispatcher (fixed table, one handler per IntentType; a second bind is rejected)
  -> handle*Intent (Atlas/src/device_intents.cpp) -> device and account state
```

The adapter answers "what is this controller asking for?"; Atlas answers "is
that allowed, and what does it do?". The dispatcher is heap-free.

- **Adapters stay thin.** They decode their transport, authenticate it and
  build Intents or hosted inputs. `Atlas/tests/host/audit_adapters.py`
  enforces this for the adapters in `sigil_input.cpp`, `web_adapters.cpp`,
  `front_panel.cpp` and `touch_controls.cpp`; list any new adapter there.
- **Actors.** Browser identity comes from the authenticated session, never
  from a form field. Admin requests carry the signed-in account in
  `payload.moderatorId`; the handler re-checks its permission.
- **Results.** Handlers return `IntentResult` (`Accepted`, `Rejected`,
  `Unsupported`, `InvalidActor`, `InvalidState`, `Unauthorized`, `Conflict`);
  each adapter turns it into HTTP status, a Sigil tone or a screen message.
- **Not in a handler:** HTTP parsing, packet decoding, GPIO reads, debounce,
  drawing, HTML, pin numbers.
- **New device actions** need an `IntentType`, a handler bound in
  `main.cpp`'s `configureIntentHandlers()`, and host scenarios. New gameplay
  belongs in the shared core and the app. An enum value with no bound
  handler is not a feature; the game Intents left in `intent.h` are unbound
  since 2026-11-04.

The host suite compiles the real handlers, the hosted relay and adapters
against stubs, so a rule can be tested with no HTTP, radio or display.

## Intent catalog

Origins: `PhysicalSigil`, `Browser`, `AndroidApp`, `AtlasHardware` (the
touchscreen and BOOT button: physical presence), `Simulator`, `System`
(Atlas's own timers; never accepted from a caller).

| Intent | Who sends it | Notes |
|---|---|---|
| `PairRequest`, `PairConfirm`, `ForgetPairing`, `ConfigurePairing` | Touchscreen/BOOT; Admin | See [Pairing and Secure Link](PAIRING_AND_SECURE_LINK.md) |
| `FactoryReset`, `ResetTable` | Admin verified at the table; `FactoryReset` also touchscreen/BOOT | `ResetTable` is Return to lobby: clears the hosted table |
| `ConfigureSpeaker` | Admin, or the Atlas touchscreen | Atlas speaker volume 0-3 |
| `ConfigurePower` | Admin, or the Atlas touchscreen | Screen-off, screen in games, auto sleep |
| `ConfigureTableCode` | Admin; off needs a code | The optional table presence code (off by default); see [Players and Accounts](PLAYERS_AND_ACCOUNTS.md) |
| `ConfigureUploadApproval` | Admin | Whether new artwork needs review |
| `UpdateSigil` | Admin verified at the table | See [Firmware Updates](FIRMWARE_UPDATES.md) |
| `AdvanceSetup` | Admin (app), touchscreen | See [First-run Setup](FIRST_RUN_SETUP.md) |
| `Sleep` | Touchscreen, between games | Deep sleep; touch or BOOT wakes (a restart) |

The JSON Intent envelope (`protocol/intent-v0.1.schema.json`) and
`POST /api/v1/intent` are drafts, not implemented; never expose `System`
operations to callers.

## Design rules that follow

- **Timers are anchors, not ticks** (in the core): a turn stores when it
  started and the configured limit; remaining time is derived per call.
- **Settings are captured at the lifecycle boundary.** A match captures its
  settings at start; lobby edits apply to the next match.
- **Statistics come from finished records**, once per record ID, through the
  game import (see [Storage and Recovery](STORAGE_AND_RECOVERY.md)).
- **Events, not polling, on the radio.** Atlas sends state when it changes and
  resends on every Hello so a lost packet heals itself.
- **Capabilities, not version checks.** A Sigil announces what varies in its
  Hello capability byte ([Radio Protocol](RADIO_PROTOCOL.md)).
- **Presentation reacts to state.** LED and audio cues are picked from the
  state being shown.
- **One table per Atlas.** The two-game venue model (host-tested 2026-10-07)
  was removed with the engine on 2026-11-04.

## History

Generation 0 coupled hardware and rules; Generation 1 made a Raspberry Pi the
authority over wired modules; the ESP32 port (merged `6844f94`, 2026-09-19)
moved one operation at a time onto the dispatcher. Details:
[Generation History](history/GENERATION_HISTORY.md) and the
[2026-09-19 Intent migration record](history/2026-09-19-atlas-intent-verification.md).
