# TurnHub Engineering Decision Log

This log records major product and architecture decisions so they do not live only in chat history. It complements `ARCHITECTURAL_INVARIANTS.md`: invariants are hard rules; this document records the decisions and rationale that produced or refine those rules.

Status values:

- **Accepted**: current direction. New work should follow it unless deliberately superseded.
- **Planned**: agreed direction, but implementation is incomplete.
- **Experimental**: useful prototype behavior that is not yet a product commitment.
- **Superseded**: retained for history but no longer the current direction.

## ADR-001: Atlas owns canonical table and game state

**Status:** Accepted  
**Date:** 2026-09-19

Atlas is the sole authority for game and table state. Sigils, browser clients, mobile clients, and simulators submit requests and render Atlas state rather than independently mutating canonical state.

**Why:** Prevents split-brain behavior, makes reconnect/recovery deterministic, and allows new controller types without duplicating rules.

**Consequence:** Any feature that directly changes game state outside Atlas is an architectural regression unless this decision is explicitly superseded.

## ADR-002: Semantic Intents are the application boundary

**Status:** Accepted  
**Date:** 2026-09-19

Controller-specific inputs converge on semantic Intents before authoritative game behavior runs.

**Why:** Physical Pass, browser Pass, Atlas controls, Android controls, and future controllers should all exercise the same rule path.

**Consequence:** Transport adapters may authenticate, decode, debounce, and reject malformed messages, but they do not own gameplay semantics.

## ADR-003: Transport is replaceable

**Status:** Accepted  
**Date:** 2026-09-19

BLE, Wi-Fi, HTTP, USB/serial, ESP-NOW experiments, and future transports move requests and state. They do not define game behavior.

**Why:** The product transport has changed during development and may change again. Rules should survive that change.

## ADR-004: TurnHub is local-first and subscription-free

**Status:** Accepted  
**Date:** 2026-09-20

Core game operation must not depend on a paid subscription or a continuously available cloud service. Local physical controls remain useful without Internet access. Browser/app access may use local networking or an optional relay, but loss of the relay must not destroy the table's canonical game state.

**Why:** Offline longevity, venue reliability, ownership, repairability, and predictable product cost are core product goals.

## ADR-005: Player identity is independent of controllers

**Status:** Accepted  
**Date:** 2026-09-19

A player profile is a durable local identity. A physical Sigil, virtual Sigil, browser session, or app session binds to a profile rather than becoming the profile.

**Why:** Controllers are replaceable presentation/input devices. Statistics, names, authentication data, and player preferences should survive controller replacement.

## ADR-006: Statistics are partitioned by game profile

**Status:** Accepted  
**Date:** 2026-09-20

Statistics that depend on game rules or format are recorded against a `gameProfileId` rather than being merged into one undifferentiated player total.

Examples include win rate, starting-player rate, turn timing, eliminations, score/life-specific metrics, commander-style damage, round counts, and format-specific counters.

A small set of truly universal totals may also be maintained across all game profiles, such as total completed sessions.

**Why:** A player may use TurnHub for games with incompatible rules and radically different timing or scoring. Combining those values makes derived statistics misleading and makes future migrations harder.

## ADR-007: Stats have explicit visibility classes

**Status:** Accepted  
**Date:** 2026-09-20

Statistics are not implicitly public merely because Atlas stores them.

Initial policy:

- **Public/basic by default:** display name when chosen for the current table, games played, and non-sensitive participation facts needed for table UX.
- **Private/derived by default:** win percentage, loss rate, elimination rate, average turn time, comparative rankings, streaks, and similar derived performance measures.
- **Shareable:** private/derived values may be exposed when the player explicitly opts in.
- **System-only:** authentication material, PIN hashes, internal identifiers not intended for display, migration metadata, and integrity fields.

**Why:** Participation stats can be useful socially, while derived performance data may feel personal or competitive. Privacy should be a model property, not an afterthought in the web UI.

## ADR-008: Raw session facts and derived aggregates are separate

**Status:** Planned  
**Date:** 2026-09-20

The long-term statistics model separates immutable or append-only session facts from derived profile aggregates.

**Why:** Aggregates can be rebuilt after a schema change, bug fix, new statistic, or game-profile migration if the underlying session record still exists.

**Consequence:** Resource-constrained Atlas hardware may retain only a bounded local history, but aggregate fields should not be treated as the only possible source of truth forever.

## ADR-009: Storage is tiered behind a storage boundary

**Status:** Planned  
**Date:** 2026-09-20

TurnHub should distinguish:

1. Small critical key/value state suitable for ESP32 NVS/Preferences.
2. Structured local files for profiles, game definitions, exports, logs, and bounded session history.
3. Optional removable/expanded local storage when hardware supports it.

Game logic must not depend directly on a specific storage medium.

**Why:** The current ESP32 development target has limited flash. Profiles, statistics, logs, firmware assets, and future game metadata will grow at different rates and should not compete in one ad-hoc namespace.

## ADR-010: Shared contracts get stable IDs and schema versions

**Status:** Accepted  
**Date:** 2026-09-20

Durable entities and exported records use explicit identifiers and schema versions. At minimum this applies to player profiles, game profiles, game/session records, protocol envelopes, and statistics exports.

**Why:** Stable IDs make migrations, Android/web clients, imports/exports, and local storage expansion possible without keying product behavior to MAC addresses, display names, or a particular transport.

## Maintenance

When a decision changes:

1. Add a new ADR or mark the prior entry **Superseded**.
2. Update affected architecture/reference documents.
3. Update protocol/schema files when shared contracts changed.
4. Add migration notes when persisted data is affected.

Last updated: 2026-09-20
