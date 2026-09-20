# TurnHub Statistics and Profile Architecture

**Status:** Planned architecture with an existing v1 player-stat implementation.  
**Applies to:** Atlas firmware, web portal, future Android client, exports, and local storage.

## Goal

TurnHub needs statistics that remain meaningful across multiple tabletop games without turning a physical Sigil, browser session, or one firmware struct into the identity or source of truth.

The model therefore separates:

- Player identity.
- Controller/session authentication.
- Game/ruleset identity.
- Individual game sessions.
- Per-player/per-game aggregates.
- Cross-game universal aggregates.
- Visibility/privacy policy.

## Entity model

### PlayerProfile

A durable local identity representing one person or named local profile.

Minimum conceptual fields:

```text
playerProfileId
schemaVersion
displayName
authentication metadata
privacy preferences
createdAt / updatedAt (when clock quality permits)
```

The profile ID is stable. Display name is editable. Hardware MAC addresses, browser sessions, and virtual seats are bindings, not identity.

### GameProfile

A durable definition of the game or format whose rules affect statistics.

Examples:

- Generic 20-life game.
- Generic 40-life multiplayer game.
- A specific user-defined tabletop game profile.
- A game plus format/rules variant when those variants materially change stat interpretation.

Minimum conceptual fields:

```text
gameProfileId
schemaVersion
name
rules/feature capability identifiers
starting-state defaults
stat schema/capabilities
```

`gameProfileId` must be stable even if the display name changes.

### GameSession

One authoritative completed or abandoned table session.

Conceptual fields:

```text
sessionId
schemaVersion
gameProfileId
startedAt / endedAt when available
durationMs
completionReason
participant records
winner/result data
session-level counters/events retained by policy
```

Atlas creates and owns the session record. Controllers do not create competing game records.

### ParticipantResult

The per-player result within one GameSession.

Conceptual fields:

```text
playerProfileId
seatId
result
startedFirst
turnsCompleted
totalTurnMs
fastestTurnMs
longestTurnMs
game-specific counters
```

Game-specific fields should be capability/schema-driven rather than continually widening a universal fixed struct.

### PlayerGameAggregate

Derived totals for the tuple:

```text
(playerProfileId, gameProfileId)
```

Examples:

- Games played for that game profile.
- Games won for that game profile.
- Turns completed.
- Total turn time.
- Average/fastest/longest turn.
- Starting-player count.
- Eliminations or game-specific outcomes when meaningful.

These values are derived summaries, not player identity.

### PlayerUniversalAggregate

Only values that remain meaningful across all games belong here.

Initial examples:

- Total TurnHub sessions participated in.
- Total completed sessions.
- First/last activity metadata when useful.

Avoid placing win rate, average turn time, scoring, or game-rule-specific metrics in universal aggregates.

## Privacy model

Privacy is part of the statistics contract, not just presentation logic.

### Visibility classes

#### PublicBasic

Safe for ordinary table/profile discovery by default.

Examples:

- Display name when the profile is participating or intentionally discoverable.
- Games played / sessions participated in.
- Non-sensitive presence or participation counts.

#### PrivateDerived

Stored locally but hidden from other players unless sharing is explicitly enabled.

Examples:

- Win percentage.
- Loss/elimination percentages.
- Average turn time.
- Streaks.
- Comparative ranks.
- Performance trends.
- Any composite metric that can be interpreted as skill or performance.

#### SharedOptIn

A `PrivateDerived` field or category explicitly authorized by the profile owner for selected audiences.

The first implementation may use a simple profile-level share toggle, but the data model should allow future category-level controls without changing stat ownership.

#### SystemOnly

Never exposed as ordinary profile statistics.

Examples:

- PIN hashes.
- Authentication salts/tokens.
- Storage keys.
- Internal migration state.
- Integrity/checksum metadata.

## Access rule

A role does not gain access merely because a value exists on Atlas.

Conceptually:

```text
requester role
+ relationship to profile
+ stat visibility class
+ profile sharing preference
= fields returned
```

The API should filter before serialization. Do not send private fields to a client and rely on CSS/JavaScript to hide them.

## Current v1 migration

The current `TurnHubProfiles::ProfileStats` struct is useful as a compact v1 aggregate, but it mixes universal and game-dependent values in one record.

Current fields include:

- games played/won/started/eliminated
- turns completed
- total game/turn time
- fastest/longest turn
- last-game values

Migration direction:

1. Preserve the v1 struct while existing firmware/UI depends on it.
2. Introduce a game profile ID before new game-specific statistics proliferate.
3. Create a v2 aggregate representation keyed by both player and game profile.
4. Migrate existing v1 data into a designated legacy/default game profile when sufficient context exists.
5. Keep the migration idempotent and retain a schema version marker.
6. Stop adding unrelated new statistics to the v1 struct once the v2 boundary exists.

Do not silently reinterpret an old aggregate as belonging to a new game profile.

## Session retention strategy

TurnHub does not need unlimited telemetry to gain most benefits of session records.

A bounded local ring/history can provide:

- Last N completed sessions.
- Rebuild/recovery of recent aggregates.
- Debugging and support evidence.
- Player export.
- Future statistics added after the session was played, when enough raw facts were retained.

Retention count should be driven by storage capacity rather than embedded directly in game semantics.

## Export contract

Human-readable text reports remain useful, but machine-readable export should be the long-term interchange format.

Recommended envelope:

```json
{
  "format": "turnhub-stats",
  "schemaVersion": 1,
  "exportedAt": null,
  "player": {},
  "universal": {},
  "byGame": [],
  "recentSessions": []
}
```

Rules:

- IDs and schema versions are explicit.
- Private fields are included only in an authenticated owner export or explicit sharing export.
- Derived rates may be omitted and recalculated from counts when practical.
- Unknown future fields must be ignorable by older clients.
- Imports must validate schema/version before changing persisted data.

There is no need to force TurnHub into a sport-specific or one-game statistics convention. Compatibility is better achieved through stable IDs, versioned JSON, explicit units, and documented semantics.

## Units and naming

- Store durations in integer milliseconds unless there is a strong reason otherwise.
- Store counts as integers.
- Store rates as derived values where practical instead of persisting floating-point percentages.
- Include the denominator in API/export semantics for any rate.
- Avoid ambiguous fields such as `time` or `score` without game/profile context.

## Feature gate example: new statistic

Before adding `averageTurnMs` to a screen, answer:

1. **Owner:** Atlas statistics domain.
2. **Intent:** none for the derived value; underlying game actions create facts.
3. **Validator:** GameEngine/Intent handlers validate the source actions.
4. **Persistence:** Atlas stores counters/anchors needed to derive it.
5. **Presentation:** web/app/Sigil render only authorized views.
6. **Contract:** API/export field plus visibility class and game-profile scope.
7. **Privacy:** `PrivateDerived` by default.

## Immediate implementation guardrails

Until the v2 storage work begins:

- Do not key profile stats to physical controller identity.
- Do not add game-specific metrics without a game-profile scope plan.
- Do not expose `ProfileStats` wholesale from an API endpoint.
- Do not make derived performance fields public by default.
- Keep games-played style participation totals separable from performance metrics.
- Preserve schema versioning on persisted structures and exports.

Last updated: 2026-09-20
