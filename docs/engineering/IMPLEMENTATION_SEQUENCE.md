# TurnHub Near-Term Implementation Sequence

**Purpose:** Keep parallel Work, local, and future mobile development from solving the same problem twice or widening unstable interfaces.

This sequence assumes `atlas-esp32-port` remains the active development line until it is deliberately merged.

## Phase 0: Preserve the verified baseline

Before broadening features:

- Keep Atlas authoritative for canonical game/table state.
- Route semantic controller actions through Intents.
- Keep current host scenarios passing.
- Do not merge controller-specific rule logic back into the web portal or Sigils.
- Treat the current `ProfileStats` record as v1 compatibility data, not the final statistics model.

## Phase 1: Introduce game profile identity

Add a stable `gameProfileId` concept before adding many more statistics.

Minimum scope:

- Stable ID independent of display name.
- Schema/version field.
- Human-readable name.
- Starting-state/rules capability metadata needed by Atlas.
- A designated legacy/default game profile for migration of existing v1 behavior.

Do not require a full game marketplace/editor before this boundary exists.

### Tests

- Game profile IDs survive rename.
- Missing/unknown game profile fails safely.
- Existing games continue through the legacy/default profile.
- Export includes game profile ID and schema version.

## Phase 2: Separate v2 statistics storage from v1 aggregates

Create a new store/API keyed by:

```text
(playerProfileId, gameProfileId)
```

Keep the current v1 `ProfileStats` API operational until callers move.

Minimum v2 data:

- games played
- games won/result counts when meaningful
- turns completed
- total turn time
- fastest/longest turn
- starting-player count

Prefer counts and integer duration anchors over persisted floating-point percentages.

### Tests

- Same player can accumulate independent stats for two game profiles.
- A rename does not split stats.
- A hardware/controller replacement does not split stats.
- Unknown future schema version is rejected without overwriting stored data.
- Re-running migration is idempotent.

## Phase 3: Put privacy filtering in the API boundary

Add explicit visibility classes and requester-aware serialization.

Initial behavior:

- Participation/basic fields can be public by default.
- Derived performance fields are private by default.
- Profile owner can opt into sharing.
- System/auth fields never enter ordinary profile payloads.

The server/Atlas filters fields before response serialization. Clients do not receive hidden values merely to conceal them visually.

### Tests

- Anonymous/other-player view cannot obtain private-derived fields.
- Authenticated owner view can obtain its own private fields.
- Sharing opt-in exposes only allowed categories.
- PIN hashes and internal auth material never appear in profile/stat API responses.

## Phase 4: Create a common controller facade for physical and virtual Sigils

Finish the existing verification-backlog item by defining one semantic controller surface.

Conceptually:

```text
ControllerAdapter
  -> authenticated actor/controller context
  -> Intent
  -> IntentDispatcher
  -> Atlas domain
```

Physical, browser, simulated, and Android adapters should differ in transport/input handling but converge before game semantics.

### Tests

Run the same semantic scenario against at least:

- direct test controller
- virtual/browser adapter
- physical-Sigil message adapter where host simulation permits

Expected canonical states must match.

## Phase 5: Add a storage-manager boundary

Do not migrate everything at once.

First introduce semantic storage interfaces, then move one record family at a time.

Suggested order:

1. Game profiles.
2. Bounded session history.
3. v2 per-game aggregates.
4. Export/import bundles.
5. Diagnostics/log retention.

Keep small boot-critical device identity and migration markers in NVS/Preferences unless there is a reason to move them.

### Tests

- Store unavailable at boot degrades according to data criticality.
- Partial/corrupt structured record does not silently replace valid data.
- Capacity/retention pruning removes oldest optional records first.
- Current-game recovery data is never pruned to preserve old logs.

## Phase 6: Record bounded GameSession facts

Create a versioned completed-session record sufficient to support recent history and future aggregate rebuilds.

Do not attempt unlimited event telemetry on Gen 1 hardware.

Initial session record should capture only facts already authoritative on Atlas, such as:

- session ID
- game profile ID
- participant profile IDs
- results
- start/finish metadata where available
- duration
- turn counts/timing facts
- completion reason

### Tests

- Session record is written once for a completed game.
- Reboot/retry cannot double-count a completed session.
- Aggregates can be updated from a session deterministically.
- Oldest session pruning does not alter already-persisted aggregates.

## Phase 7: Expand virtual Sigil capability on top of the common boundary

Once Phases 1-4 are stable, the web portal can safely become a full virtual Sigil without gaining its own game engine.

Expected capabilities include:

- authenticated player binding
- Pass
- Pause/Resume when authorized
- Action/claim-win semantics
- life/counter changes through Intents
- notifications
- reconnect from authoritative Atlas state

The browser remains a controller and view.

## Phase 8: Android client co-development

The Android app should consume the same contracts as the browser rather than introduce Android-only game endpoints.

Prioritize:

- discovery/join
- authentication/profile binding
- state subscription/polling strategy
- Intent submission
- reconnect
- virtual Sigil surface

Delay native-only features until shared API semantics are stable.

## Phase 9: OTA and hardware revision hardening

After controller/state/persistence boundaries are stable:

- re-test Atlas OTA
- add validation and failure reporting
- define rollback/recovery policy
- define Sigil update orchestration
- attach hardware revision compatibility to firmware packages
- choose expanded local storage for the first reproducible Atlas hardware revision if required

## Merge gate for `atlas-esp32-port`

Before merging the active ESP32 line to `master`, require at minimum:

- Atlas and Sigil firmware build cleanly.
- Host scenarios pass.
- Hardware regression checklist passes for core lobby/game/Pass/Pause/Concede/Win flows.
- Pairing behavior is known and documented for the tested transport.
- OTA status is explicitly documented as verified or known-limited.
- No controller has a second canonical game-state implementation.
- Documentation identifies experimental transport/hardware decisions accurately.
- Third-party notices/dependency tracker are current.

A merge does not require every future feature to be complete. It requires the architecture and current behavior to be coherent, testable, and recoverable.

Last updated: 2026-09-20
