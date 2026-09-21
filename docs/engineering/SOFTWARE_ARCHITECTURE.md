# TurnHub Software Architecture

This document records the architectural direction of TurnHub and the major transitions that led to it.

## Core rule

**Atlas owns canonical game and table state.**

Physical Sigils, Virtual Sigils, browser pages, and future applications should submit player/controller intents and render the resulting state. They should not independently implement the full game engine.

Examples of controller intent:

- Pass requested.
- Action pressed/released.
- Victory claim requested.
- Concession requested.
- Life adjustment requested.
- Nudge requested.
- Pairing requested.

Examples of authoritative Atlas state:

- Player list and seat assignments.
- Active player.
- Turn number.
- Running/paused/game-over state.
- Turn timing anchors.
- Life totals and game-specific counters.
- Elimination/concession state.
- Victory-claim state.
- Player profile/controller association.
- Statistics and recoverable session state.

---

## Evolution

### Generation 0

Hardware and game behavior were closely coupled because the goal was to prove the physical interaction model.

### Generation 1

The Raspberry Pi became authoritative. Arduino/ESP modules became I/O devices. This was the first major separation between game decisions and hardware behavior.

### Generation 1.5

The Python application was split into focused modules for game engine, lobby, players, serial transport, LEDs, audio, profiles, persistence, web authorization, and web UI. Physical and virtual controllers began to share one logical player model.

### Generation 2

The Pi-hosted architecture is being reimplemented on the ESP32 Atlas. The architectural goal is not to copy old code mechanically, but to preserve the clean boundaries learned during the Pi phase.

Current Atlas migration source already separates concerns into native modules including game engine, lobby, LED rendering, audio, Sigil transport, OTA, profile storage/statistics, and web API/pages.

---

## Logical object model

A useful high-level model is:

```text
Profile (optional persistent identity)
  -> Player (one participant in the current game)
    -> Controller assignment
       -> Physical Sigil
       -> Virtual Sigil/browser
       -> future app/controller
```

A controller can change without replacing the Player object or losing that player's game state.

---

## Single-source-of-truth rules

### Game rules

Implement once in the Atlas game engine.

Do not duplicate pass rules, elimination rules, victory resolution, timer rules, or game-state transitions in the browser and Sigil firmware.

### Player identity

Atlas owns player identity and controller assignment. A Sigil should know enough to render and submit input, but should not become the authoritative player database.

### Settings

Capture game-affecting settings at the appropriate lifecycle boundary rather than continuously reading hardware state. Example: when a game starts, the selected timer/game profile becomes part of that session's state.

### Statistics

Statistics should derive from authoritative game events/state transitions rather than independent counters on every controller.

### Hardware mapping

GPIO and electrical details belong in hardware configuration/abstraction layers, not in game-rule logic.

---

## Event-driven design

Controllers should report events or intents when something happens rather than continuously sending redundant state.

Preferred conceptual flow:

```text
Sigil or browser
    -> PASS_REQUEST(player/controller identity)
Atlas
    -> validates request
    -> updates canonical state
    -> emits resulting state/update
Controllers
    -> render the new state
```

This approach reduces duplicated computation, radio traffic, and synchronization bugs.

---

## Timer design

Turn timers should be represented primarily by timing anchors rather than a canonical state mutation every second.

Example:

```text
turn_started_at = monotonic timestamp
turn_limit_ms   = configured duration
paused_total_ms = accumulated pause time
```

Remaining time can be derived when needed. Display refresh cadence is a presentation concern rather than a requirement for the game engine to rewrite state every second.

---

## E-ink rendering rule

E-ink is best treated as a state-change display, not a high-frequency animation surface.

Prefer updates when meaningful data changes:

- Player/profile assignment.
- Active/waiting state.
- Life/game counter changes.
- Timer warning threshold.
- Pause/resume.
- Victory/game over.
- Pairing/connection state.

The display implementation may later use partial refresh or dirty-region strategies where the panel supports them reliably.

---

## Physical and virtual controller parity

Atlas `0.6.0-dev` implements hardware-independent profile login and browser
participation, including concurrent physical/phone control of one participant.
See [Profile login and virtual play](PROFILE_LOGIN_AND_VIRTUAL_PLAY.md) for the
implemented boundaries, compatibility coordinates and remaining work. Live
participation now resolves from authenticated profile identity; the engine and
statistics completion path do not require radio discovery for phone players.

A physical Sigil and Virtual Sigil should map to the same semantic operations wherever practical.

For example, both may produce a logical `PASS_REQUEST`, even though one comes from a GPIO button and the other from an authenticated HTTP/API action.

This enables:

- Mixed physical/virtual tables.
- Automated virtual testing.
- Future mobile applications.
- Replacement controller hardware.
- Easier simulation of large player counts.

---

## Simulator direction

Virtual Sigils should evolve into a first-class test harness, not just a convenience UI.

Desired capability:

- Create simulated controllers without physical boards.
- Mix physical and simulated Sigils in one game.
- Script button/action sequences.
- Verify resulting game state.
- Simulate disconnect/reconnect.
- Simulate old/new capability sets.
- Stress-test maximum supported player/controller counts.

Atlas should ideally care about controller semantics, not whether the controller is physical or simulated.

---

## Persistence and recovery

The identity/storage foundation is documented in
[Identity and storage contracts](IDENTITY_AND_STORAGE.md). Existing profile
statistics now use a repository over `BlobStore`, with an internal NVS backend
that preserves deployed keys and the v1 image. Unknown/corrupt records fail
without being replaced by empty totals. Other small Preferences owners remain
in place; new match/controller lifecycles are not yet implemented.

Persistent information and active-session recovery are separate concerns.

Persistent examples:

- Player profiles.
- Preferences.
- Device pairings.
- Hardware identity/configuration.
- Historical statistics.

Recoverable session examples:

- Current players.
- Controller assignments.
- Current game profile.
- Life/counters.
- Active player and turn number.
- Pause state.

On recovery after an unexpected restart, active timing should fail safe rather than silently charging downtime to a player.

---

## OTA/update responsibilities

Atlas and Sigil firmware should expose version information and update compatibility separately from ordinary game state.

The update architecture should eventually define:

- Hardware revision compatibility.
- Firmware version.
- Protocol version.
- Required/optional update policy.
- Recovery/fallback behavior.
- Update verification/integrity.
- Atlas-to-Sigil update transport if used.

An update mechanism should not require game-rule code to know how firmware bytes are transported.

---

## Computational efficiency priorities

Optimization effort should focus first on avoiding unnecessary work rather than micro-optimizing instructions:

1. One authoritative game engine.
2. Event/state-change messages rather than constant polling.
3. Derived timers rather than per-second canonical mutations.
4. E-ink updates only when presentation state changes.
5. Capability negotiation rather than board-specific branching scattered throughout the code.
6. Shared semantics for physical, browser, simulated, and future controllers.
7. Persistent configuration loaded once and updated only when changed.

Last reconstructed: 2026-09-19
