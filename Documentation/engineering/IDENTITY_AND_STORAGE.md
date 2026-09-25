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
their existing Preferences owners. There is no SD dependency; the optional
microSD store described below holds no authoritative records yet.

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
| Lifetime/latest-game statistics | Profile statistics repository | `BlobStore` -> `NvsBlobStore`, existing `turnhub` namespace and `s<profileId>` keys. Last-game result byte 4 is `Draw` (2026-09-24; it was the never-written `Completed`), so the v1 image and older firmware still accept draws |
| Private moderation history (connection resets, game removals) | Profile statistics repository | `o<profileId>` blob in `turnhub`: schema byte 1, then two little-endian uint32 counts. Served only to the owner's PIN-verified session; never exported. Counts older firmware kept in `u<profileId>` migrate on first account load (2026-09-24) |
| Account access control (permissions, archived, nudge mute, reconnect-required) | Atlas account repository | `u<profileId>` blob in `turnhub` (12 bytes, schema 2); its count bytes are legacy and read only for migration |
| Physical-use and stats-privacy choices | Atlas profile repository | `a<profileId>` blob in `turnhub`: schema byte 1, allow-physical byte 0/1, hide-stats byte 0/1; implemented locally |
| Network credentials | Network settings owner in web API | Existing Preferences namespace |
| Game definitions/rulesets | Planned game-definition repository | Versioned records through storage boundary |
| Next-game profile, starting life and turn timer | Atlas game-settings repository | `gamecfg` blob in `turnhub` (schema 2, ten bytes; schema 1 still read); match captures settings at start, life totals stay in RAM |
| Match facts, membership, history and recovery | Planned Atlas match repository | Bounded versioned records; recovery separate from completed history |
| Controller assignment/device trust | Sigil bus (pairing) | `th_pair_v1/s0`-`s7` MACs on Atlas, `th_pair_v1/atlas` on each Sigil; admins forget Atlas records, a 10 s Pair hold forgets the Sigil's (see [Manual Pairing](MANUAL_PAIRING.md)) |
| Atlas touchscreen calibration | Atlas display (`atlas_display.cpp`) | `atlas-touch/cal` blob (2026-09-24): raw `TouchCalibration` struct (swap flag, raw range per screen axis). Missing or invalid falls back to `config.h` and triggers on-device calibration at boot. Device presentation, not game state |
| Atlas pairing window | Atlas table settings | `pairwin` blob in `turnhub` (2026-09-24): schema byte 1, seconds (15/30/60); missing or unreadable reads as 15 s |
| Atlas speaker volume | Atlas table settings (`ConfigureSpeaker`, Admin) | `spkvol` blob in `turnhub` (2026-09-24): schema byte 1, volume 0 (off) to 3 (high); missing or unreadable reads as 2 (medium) |
| Sigil user/device settings and last-used preferences | Atlas device-settings owner | No remembered profile on either seat; legacy `b<mac>A/B` and `r<mac>A/B` keys are retired on reconnect; no Sigil-side profile persistence |
| Per-player Sigil accessibility (sound, light style, hold times) | Atlas profile repository | `x<profileId>` blob in `turnhub` (2026-09-24): schema byte 1, sound 0/1, light style 0-2, long press and win hold as little-endian uint16 ms (7 bytes). Missing reads as defaults; Corrupt/unsupported records are never overwritten silently. Sigils hold the applied values in RAM only |
| Other shared accessibility preferences | Atlas profile owner | Versioned profile preferences; per-Sigil user adjustments belong to Atlas device settings |
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

## Optional microSD storage

Status (2026-09-24): *Implemented* in firmware and host tests. *Verified* on
the board by the owner: the card mounts and registers. Booting without a card
still needs a bench check. Optional rotating diagnostics were added on
2026-09-25 (*Experimental*, hardware acceptance pending); authoritative records
remain in NVS and gameplay never depends on the card. See
[SD diagnostics](SD_DIAGNOSTICS.md) for retention, failure behavior and checks.

- `sd_card.cpp` (firmware-only, Arduino `SD` library on VSPI) mounts the card
  once at boot with `format_if_empty = false`: an unreadable or unformatted
  card is reported as `no_card`, never wiped. It then writes and reads back
  `/turnhub/selftest` through the same store the repositories will use.
  Serial log lines are `ATLAS|SD|NO_CARD`, `ATLAS|SD|MOUNTED|<type>|<size>MB`
  and `ATLAS|SD|SELF_TEST|<status>`; `GET /api/diagnostics` (Developer) adds an
  `sdCard` object with state, type, capacity, startup usage, the self-test result
  and logger state/lost-byte count. Usage is explicitly marked `usageSample: boot`.
- `SdBlobStore` implements `BlobStore` over a small `FileSystem` interface, so
  host tests run it against an in-memory, fault-injecting fake. Keys follow the
  NVS rules (1-15 characters) restricted to letters, digits, `_` and `-`.
  Records are at most 4096 bytes. Each file holds a 16-byte header (`THSD`
  magic, format 1, little-endian payload length, CRC-32 of the payload) and
  then the payload. A bad magic, length or checksum reads as `Corrupt`; another
  format number reads as `UnsupportedSchema`.
- Writes go to `<key>.tmp`, are read back and compared, then replace `<key>`
  via `<key>.bak` (FAT cannot rename over a file). A read that finds no `<key>`
  but a `<key>.bak` returns the backup when the filesystem is intact. This does
  not make FAT metadata or the card power-loss transactional. A damaged `<key>`
  is reported, not silently rolled back to the backup. A leftover `.tmp` is never read.
- `sdBlobStore()` returns `nullptr` unless the card mounted and the directory
  exists, the write/read-back self-test passed, and the diagnostic worker has
  not reported an I/O error; callers treat that and every error as "card unavailable".
  A card removed while running makes operations fail with `IoError`; remount
  needs a restart. The logger is currently the only post-boot SD consumer;
  future blob/package access must share a serialized SD owner with it.

Not decided yet (see [Staged changes](STAGED_CHANGES.md)): which records move,
PIN hashes on removable media, migration and rollback from NVS, and how a card
moved to another Atlas is treated.

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
