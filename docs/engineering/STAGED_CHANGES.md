# TurnHub Staged Changes

This is the durable staging document for agreed work that has not yet been implemented or fully verified.

Use this file instead of chat history for near-term changes. Keep it concise. Once an item is implemented and verified, move any lasting architectural facts into the appropriate reference document and remove it from here.

## Current baseline

Active development branch: `atlas-esp32-port`

Current product rule: Atlas is authoritative for canonical game/table state. Physical Sigils, virtual Sigils, browsers, simulators, and future apps request semantic actions through the shared Intent boundary and render Atlas state.

## Staged implementation order

### 1. Game profiles and statistics separation

- Introduce a stable `gameProfileId` so statistics are partitioned by game/format instead of one global lifetime bucket.
- Keep player identity independent from physical or virtual controllers.
- Preserve the current v1 aggregate statistics during migration.
- Separate universal facts from game-specific statistics.
- Treat raw session facts as canonical inputs and derived values such as win percentage as computed data.

Privacy direction:

- Public by default: basic participation facts such as games played.
- Private by default: derived performance statistics such as win rate, average turn time, and similar comparative metrics.
- Allow explicit opt-in sharing of private/derived statistics.
- Enforce visibility on Atlas before serialization, not only in the browser UI.

### 2. Common controller layer

- Make physical and virtual Sigils use the same semantic action layer wherever practical.
- Define a controller interface usable by browser, simulator, Android, and physical Sigil adapters.
- Controllers may cache presentation state but never own canonical game state.

### 3. Local storage boundary

- Keep small critical configuration in NVS/preferences.
- Introduce a storage-manager boundary before adding large structured history.
- Store player profiles, game profiles, session history, and exports independently from game-engine logic.
- Design for optional expanded local storage later without changing game semantics.
- Version stored schemas and define migrations before changing persistent formats.

### 4. Session history

- Add bounded local game/session records after the storage boundary exists.
- Session records should contain enough raw facts to rebuild aggregates where practical.
- Define retention limits so Atlas storage cannot grow without bound.

### 5. Virtual Sigils and web portal

- Finish fully virtual Sigils on top of the same Intent/state contracts as physical Sigils.
- Role/profile authorization determines what each browser session can see or control.
- Web presentation should not duplicate game rules.

### 6. Android app

- Build against the same versioned Intent, state, profile, and statistics contracts used by the browser.
- Do not introduce Android-specific game semantics.

### 7. OTA and hardware hardening

- Re-test Atlas OTA application and reboot behavior on physical hardware.
- Define validation and rollback/recovery behavior.
- Choose the production Sigil transport and OTA strategy.
- Freeze hardware revisions only after GPIO, power, display, pairing, and transport decisions are verified.

## Pending physical verification

- Pairing-window behavior and pairing LED mode.
- Current Atlas/Sigil hardware pin mappings.
- E-ink orientation, refresh behavior, and power measurements.
- Atlas OTA after ESP32 migration.
- Production wireless transport decision.

## Working rules

1. Do not create a new long-lived branch for planning/documentation alone.
2. Keep near-term unimplemented work in this document.
3. Use short-lived branches only when code changes need isolation, review, or experimental protection.
4. Keep `atlas-esp32-port` as the active product-development branch until its merge gate is satisfied.
5. Do not merge to `master` until the ESP32 line is build-clean, host tests pass, required hardware regression checks pass, and the architecture references match implemented behavior.
6. Important decisions should be reflected in repo documentation, not preserved only in chat.

Last updated: 2026-09-20
