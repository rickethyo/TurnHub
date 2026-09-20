# TurnHub Local Storage Architecture

**Status:** Planned architecture. Current firmware already uses ESP32 Preferences/NVS for small durable records.

## Goal

TurnHub should remain local-first while allowing profiles, stats, game definitions, recovery state, logs, and future assets to grow without turning ESP32 NVS into a general-purpose database.

The architecture separates *what owns a persisted fact* from *which physical medium stores it*.

## Storage tiers

### Tier 0: Volatile runtime state

Examples:

- Current render caches.
- Debounce state.
- Temporary network/session data.
- Derived timer display values.

Loss is acceptable. Rebuild from authoritative state.

### Tier 1: Critical key/value state

Best suited to ESP32 NVS/Preferences.

Examples:

- Device identity.
- Small pairing records or pointers to them.
- Schema/migration markers.
- Boot/update state.
- Minimal recovery metadata.
- Small hardware configuration values.

Properties:

- Small.
- Infrequently written.
- Needed early in boot.
- Individually addressable.

Avoid high-frequency logging or unbounded records here.

### Tier 2: Structured local files

Use a filesystem/storage adapter for records that are naturally document-, table-, or log-shaped.

Examples:

- Player profile index and metadata.
- Game profiles.
- Privacy/sharing preferences.
- Bounded session history.
- Machine-readable stat exports.
- Diagnostic logs.
- Imported/exported configuration.
- Web assets when not compiled into firmware.

The concrete medium may initially be internal flash. The domain/API must not assume that forever.

### Tier 3: Optional expanded/removable local storage

Future Atlas hardware may expose a larger local storage device through a storage adapter.

Potential implementations include removable flash media or dedicated external nonvolatile storage. Selection belongs to hardware revision work, not the game engine.

Good candidates for Tier 3:

- Long session history.
- Detailed diagnostics.
- Backup/export bundles.
- Additional web/app assets.
- User-created game/profile libraries.
- Firmware packages staged for Sigil updates.

Core gameplay must remain functional if optional expanded storage is absent.

## Ownership matrix

| Data | Canonical owner | Recommended tier |
| --- | --- | --- |
| Live game state | Atlas GameEngine/domain | RAM + bounded recovery snapshot |
| Recovery snapshot | Atlas | Tier 1 or Tier 2 depending size/write pattern |
| Atlas device identity | Atlas device layer | Tier 1 |
| Paired-device registry | Atlas pairing domain | Tier 1 initially; Tier 2 if it grows |
| Minimal Sigil reconnect identity | Sigil | Tier 1 on Sigil |
| Player profile identity | Atlas profile domain | Tier 1/2 behind profile store |
| PIN/auth material | Atlas auth/profile domain | Tier 1/2 with restricted API exposure |
| Game profiles | Atlas game-profile domain | Tier 2 |
| Per-game statistics | Atlas statistics domain | Tier 2 |
| Bounded raw session history | Atlas statistics/session domain | Tier 2, Tier 3 when available |
| Debug logs | Atlas diagnostics domain | Tier 2/3, bounded |
| OTA staging packages | OTA/update domain | Tier 2/3 depending size |

## Storage interface boundary

Domain code should depend on semantic repositories/stores rather than NVS keys or filesystem paths.

Conceptually:

```text
GameEngine / ProfileDomain / StatsDomain / PairingDomain
                      |
              semantic store APIs
                      |
          StorageManager / adapters
             /                  \
      NVS adapter          File adapter
                                |
                    internal or expanded media
```

Examples of semantic APIs:

```text
ProfileStore::load(profileId)
ProfileStore::save(profile)
GameProfileStore::load(gameProfileId)
SessionStore::append(session)
SessionStore::recent(limit)
PairingStore::lookup(deviceId)
RecoveryStore::save(snapshot)
```

Do not expose raw filesystem paths to the web API or GameEngine.

## Write policy

Flash endurance and crash consistency matter more than micro-optimizing reads.

Rules:

- Do not persist countdown/elapsed timer values every loop.
- Persist stable anchors/state transitions instead of rapidly changing derived values.
- Batch low-value counters when safe.
- Use atomic-replacement or journal-style techniques for structured records that cannot tolerate partial writes.
- Keep logs bounded by count or bytes.
- Treat a failed optional stat/log write differently from a failed critical recovery write.
- Surface storage-health state to diagnostics without making ordinary controllers storage authorities.

## Schema and migration

Every durable record family needs:

- A schema version.
- A stable logical ID.
- Validation before use.
- A migration strategy.
- A defined response to unknown/newer versions.

Migration must be idempotent when practical. A failed migration must not silently destroy the only copy of user data.

## Capacity policy

Do not hard-code product behavior around the current development board's free bytes.

At boot or diagnostics time, Atlas should eventually be able to report:

- Medium type.
- Total capacity.
- Used/free capacity.
- Storage schema version.
- Health/mount state.
- Session/log retention limits.

Retention should degrade gracefully:

1. Preserve critical identity/configuration.
2. Preserve current/recovery state.
3. Preserve profile aggregates.
4. Trim oldest raw session history.
5. Trim diagnostics/logs.

Never discard current game state in order to preserve old diagnostics.

## Backup and portability

Local-first should not mean trapped data.

Planned capabilities:

- Export a profile and its authorized stats.
- Export game profiles.
- Export an Atlas backup bundle.
- Validate before import.
- Preserve stable IDs where safe or explicitly remap collisions.
- Keep authentication secrets out of ordinary public exports.

A future removable-storage feature should use the same export/import contracts rather than creating a second data model.

## Recovery behavior

On missing/corrupt optional storage:

- Atlas should boot when critical Tier 1 state remains valid.
- Gameplay should remain available when possible.
- Diagnostics should clearly report degraded persistence.
- Do not silently initialize over recoverable user data.

On corrupt critical recovery state:

- Prefer returning to a safe lobby/recovery decision over fabricating game state.

## Near-term implementation sequence

1. Keep existing Preferences-backed profile behavior working.
2. Define `gameProfileId` and v2 stat/session contracts before adding many new statistics.
3. Add a storage manager/interface without moving all records at once.
4. Introduce a structured file store for game profiles and bounded session history.
5. Add capacity/health diagnostics.
6. Add export/import using the same versioned record contracts.
7. Evaluate expanded local storage for the first reproducible Atlas hardware revision.

## Hardware decision still open

This document deliberately does not select a production storage device. The exact Atlas board, available buses/GPIOs, enclosure constraints, OTA needs, cost, and desired history depth should drive that decision.

The architectural commitment is the boundary: storage media may change without rewriting game semantics, player identity, statistics meaning, or web/app contracts.

Last updated: 2026-09-20
