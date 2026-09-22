# Identity and storage contracts

Status: storage foundation implemented. Subsequent profile login/browser
participation is tracked in [Profile login and virtual play](PROFILE_LOGIN_AND_VIRTUAL_PLAY.md).
Durable match/history identities and new storage migrations remain planned.

## Accepted baseline and scope

`master` contains the Atlas ESP32 migration (merge `6844f94`). On 2026-09-20,
the owner confirmed that repository firmware builds, installed hardware matches
the code, and no work was lost in the Windows reinstall. This accepts the prior
baseline, not hardware validation of subsequent changes.

This foundation preserves gameplay, controller addressing, HTTP/ESP-NOW payloads,
profile IDs, PIN hash inputs, NVS keys and the deployed statistics format. Existing
profile statistics are the first storage consumer. Other small settings retain
their existing Preferences owners. There is no SD dependency.

## Identity contracts

Atlas assigns canonical identities. Names, list positions, MAC addresses, radio
module IDs and authentication tokens are not substitutes. `Atlas/include/identity.h`
provides distinct fixed-size types and validation.

| Identity | Meaning and lifetime | Issuance/storage |
| --- | --- | --- |
| ProfileId | Optional persistent person, independent of controllers | Existing eight hex characters, preserved byte-for-byte; profile repository |
| GameProfileId | Game/format definition, independent of a match | Planned 128-bit random ID; game-definition repository |
| MatchId | One play instance; rematch creates a new ID | Planned 128-bit random ID; Atlas allocates at accepted game creation and retains during recovery |
| ParticipantId | One participant within a match, optionally linked to a ProfileId | Planned 128-bit random ID; match owns membership; stable through handoff and elimination |
| ControllerId | Registered input source authorized by Atlas for participation | Planned 128-bit random ID; reconnect reuses retained registration; replacement registration receives a new ID |
| DeviceId | Persistent physical device, distinct from its controller/seat channels | Planned 128-bit random ID; trusted-device registry, separate from player profiles |

New IDs use 32 uppercase hex characters without punctuation. Parsing accepts
either case and preserves spelling, including existing profile spelling because
it participates in NVS keys and PIN hashes. Producers issue canonical uppercase;
clients treat IDs as opaque, case-sensitive values. Empty typed IDs mean absent.
IDs are not secrets or authorization. Parsing does not create an entity.

New issuers must use device entropy, check repository uniqueness, and persist
identity before exposing a durable entity. New issuers, registries and device
provisioning are not implemented here. Existing profile generation/collision
checks remain unchanged. Future backup/import must include Atlas provenance and
collision handling; existing short profile IDs are not globally unique.

A game definition has a positive `rulesetVersion`, distinct from storage
`schemaVersion`. A match captures `(gameProfileId, rulesetVersion)` and effective
game settings. Editing definitions cannot reinterpret old matches. Guests have
participants without saved profiles. Multiple authorized controllers, including
an assistive companion, may target one participant.

The original foundation retained module/slot addressing. Atlas `0.6.0-dev` now
uses logical controller handles internally, runtime participant IDs and
profile-authenticated browser sessions. Existing browser coordinate fields remain
compatible. The typed durable IDs above are contracts for future persisted
match/controller records, not a claim that those repositories already exist.

## Persistence ownership

| Data | Owner | Current/planned location |
| --- | --- | --- |
| Profile identity, name, PIN hash, physical-seat binding, device label | Profile repository | Profile and device-label records use `turnhub` NVS; both physical-seat bindings are RAM-only and clear on restart/reconnect and game end |
| Lifetime/latest-game statistics | Profile statistics repository | `BlobStore` -> `NvsBlobStore`, existing `turnhub` namespace and `s<profileId>` keys |
| Physical-use and stats-privacy choices | Atlas profile repository | `a<profileId>` blob in `turnhub`: schema byte 1, allow-physical byte 0/1, hide-stats byte 0/1; implemented locally |
| Network credentials | Network settings owner in web API | Existing Preferences namespace |
| Game definitions/rulesets | Planned game-definition repository | Versioned records through storage boundary |
| Next-game profile and starting life | Atlas game-settings repository | Six-byte schema-1 `gamecfg` blob in `turnhub`; match captures settings at start, life totals stay in RAM |
| Match facts, membership, history and recovery | Planned Atlas match repository | Bounded versioned records; recovery separate from completed history |
| Controller assignment/device trust | Planned controller/trust registries | Small critical records independent of statistics |
| Sigil user/device settings and last-used preferences | Atlas device-settings owner | No remembered profile on either seat; legacy `b<mac>A/B` and `r<mac>A/B` keys are retired on reconnect; no Sigil-side profile persistence |
| Shared accessibility preferences | Atlas profile owner | Versioned profile preferences; per-Sigil user adjustments belong to Atlas device settings |
| Exports | Atlas authorized projection | Generated views, never a competing database |

The engine knows game facts, not NVS keys or SD paths. Backends know bytes, not
rules or permissions. Repository/service callers authorize client access. A new
backend must not widen access or create a competing state owner.

Owner decision, 2026-09-21: the planned Device Settings area is per Sigil, but
Atlas owns persistence and validation. Sigils consume disposable runtime values
from Atlas. Minimum device identity/pairing bootstrap material is separate from
user settings; retaining it does not grant configuration or gameplay authority.
See [Physical Profile Selection](PHYSICAL_PROFILE_SELECTION.md).

## Implemented storage boundary

`storage.h` defines `BlobStore` and explicit results:

- `Ok`: complete read or successful write/commit.
- `NotFound`: absent record; only this outcome permits fresh defaults.
- `Unavailable`: backend not open; no automatic reset or formatting.
- `InvalidArgument`: invalid key/buffer request.
- `Corrupt`: wrong NVS type, unexpected length or invalid contents.
- `UnsupportedSchema`: version this firmware cannot interpret.
- `IoError`: read, write or commit failure.

The NVS adapter opens the existing namespace, reads whole blobs and checks
set/commit results. It never erases namespaces or repairs unknown records.
Calls are serialized on Atlas's application task; there is no cross-record
transaction guarantee. Commit errors have uncertain outcomes: do not assume
rollback or blindly replay an aggregate increment. Durable match IDs/completion
receipts are prerequisites for automatic exactly-once retries.

The statistics repository checks length, schema and result enum before publishing
data. Saves inspect the existing record and refuse to overwrite unreadable or
unsupported data. Existing bool helpers retain their signatures: absent stats
load as zero defaults; other read failures return false. The completion callback
then skips that profile instead of replacing totals. Failed completions are not
yet queued for replay.

## Schema and migration rules

Deployed statistics v1 are a 72-byte ESP32 struct image, with schema at offset 64
and result at offset 66. Compile-time guards freeze the ABI. These bytes are not
a portable interchange format. This foundation does not rewrite them on read or
upgrade their schema.

Future formats must specify field encoding, byte order, record kind, schema,
identity, length/integrity validation and allocation limits. Do not persist new
raw C++ struct layouts. Storage versions are distinct from firmware, protocol
and ruleset versions.

Before enabling a migration:

1. Recognize and validate the complete source record.
2. Produce a destination without deleting/overwriting the source.
3. Write/read back and validate the destination before switching the authoritative
   reference; define crash-safe switching for that backend.
4. Make retry/restart idempotent and test interruption, full storage, source
   corruption and unsupported future versions.
5. Define rollback/downgrade behavior before retiring source data.

The next statistics milestone must preserve v1 totals as legacy/unclassified.
Their game types cannot be inferred; do not assign them to a new game or invent
match history. Keep new match facts separate from those preserved totals. History
retention must specify what remains rebuildable after old records are pruned.

Optional expanded storage is future work. Critical operation must survive an
absent SD card. Media failure must be visible and must not silently create
competing authoritative histories on multiple backends.

## Feature gate

1. State owner: existing Atlas profile/statistics repository and future owners above.
2. Request: existing profile/statistics operations and completion callback; no new Intent.
3. Validator: existing authorization plus ID and repository record validation.
4. Persistence: existing NVS layout through the statistics blob boundary.
5. Presentation: existing portal/export; gameplay rendering unchanged.
6. Shared contract: no wire changes; identity types are internal groundwork.
7. Dependencies: existing ESP-IDF NVS from Arduino ESP32; no new firmware dependency.
8. Accessibility: no new interaction; identity model permits authorized companion
   controllers without adding participants or bypassing authorization.

## Verification

`Atlas/tests/host/storage_scenarios.cpp` exercises actual storage/repository code
with fault-injected NVS: a deployed-layout byte fixture, fresh records, unknown
versions, invalid lengths/types/results, unavailable storage and read/set/commit
errors, plus ID validation/type separation. These tests do not simulate physical
flash power loss or prove future migrations.

Local validation for this foundation:

- PlatformIO Atlas build passed with Espressif32 7.1.3 / Arduino ESP32
  `4.20017.260907+sha.dcc1105b`; 47,940 bytes RAM, 925,265 bytes flash.
- Windows GCC runner passed all six existing gameplay scenario groups and the
  identity/storage scenarios; compilation used `-Wall -Wextra` without warnings.
- Adapter audit passed for all 12 checked adapters; `git diff --check` passed.
- No hardware was flashed by this change. Physical acceptance remains pending.

Hardware acceptance for this change: preserve an existing profile's name, PIN and
totals across update/reboot, finish one game, verify one statistics increment and
reboot/recheck. Do not erase NVS during that check.
