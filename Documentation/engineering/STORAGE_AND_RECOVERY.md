# Storage, Identity and Recovery

What Atlas saves, where, who owns it, and how an interrupted match comes
back. Byte layouts live in the headers named below; this file says what each
record is for.

## Rules

- **Every persistent fact has one owner** (Invariant 7). Sigils keep only
  their pairing; connected user and device settings live on Atlas. Standalone
  Android local identities/history are the explicit separate-game exception.
- **No migrations before release** (owner, 2026-09-30). Saved layouts can
  change freely; a changed layout means a factory reset. Do not add
  compatibility readers for old records. This ends when the owner says
  TurnHub is saving live statistics.
- **Fail closed.** `NotFound` is the only result that allows defaults. A
  corrupt, unreadable or future-schema record is reported and never silently
  overwritten.
- **The engine knows game facts, not keys or paths.** Backends know bytes,
  not rules or permissions. Callers authorize client access.
- **New formats** spell out field encoding, byte order, schema byte, length
  and validation. Do not persist raw C++ struct layouts.

## Identities

Atlas issues identities; names, list positions, MAC addresses and tokens are
not substitutes (`Atlas/include/identity.h`).

| Identity | Today |
|---|---|
| ProfileId | 8 hex characters, durable; part of NVS keys and the PIN hash input |
| Participant | Atlas-local numeric `participantId` for the current table (RAM) |
| Controller | Internal `controllerId`: physical Sigil handles, plus a separate range for browser registrations |
| Device | Factory MAC. Atlas shows its MAC as its `THA-` ID; Sigils appear as `THS-` IDs |
| MatchId, GameProfileId, durable ParticipantId | *Planned* 128-bit IDs (32 uppercase hex), needed for crash-safe statistics and game-scoped statistics |

IDs are not secrets or authorization.

## What is saved where

Atlas NVS namespace `turnhub` unless noted. Per-profile keys are a one-letter
prefix plus the profile ID.

| Data | Key | Owner (source) |
|---|---|---|
| Profile marker, name, PIN/password hash | `m`, `n`, `p` + id | `profile_store.cpp` |
| Physical-use and stats-privacy policy | `a` + id | `profile_store.cpp` |
| Account permissions, archive, nudge mute, reconnect-required | `u` + id; first Admin in `acctadmin` | `account_access.h` |
| Private moderation counts | `o` + id | `profile_stats_storage.h` |
| Core statistics (games played and won, last result and format) | `c` + id | `profile_stats_storage.h` |
| Per-player Sigil accessibility (sound, light style, hold times) | `x` + id | `accessibility_prefs.h` |
| Device names | `d` + MAC | `profile_store.cpp` |
| Next-game settings (format, starting life, turn timer) | `gamecfg` | `game_settings_store.h` |
| Pairing window | `pairwin` | `pairing_settings.h` |
| Speaker volume | `spkvol` | `speaker_settings.h` |
| First-run setup stage | `setup` | `setup_stage.h` |
| Atlas Wi-Fi password | `atlas-net/ap-pass` | `web_admin_api.cpp` |
| Sigil pairings and spare-only marks | `th_pair/s0`-`s7`, `th_pair/p<slot>` | `sigil_bus.cpp` |
| Touchscreen calibration | `atlas-touch/cal2` | `atlas_display.cpp` |
| Interrupted-match recovery record | `th_game_v1/checkpoint` | `game_recovery*.cpp` |
| Detailed statistics, Jewel color, avatar selection | card: `s`, `k`, `v` + id | `profile_store.cpp` via `sdBlobStore()` |

**RAM only:** browser sessions, table participation, seat bindings (released
when a seat leaves, at Reset, and on restart; kept through Game Over so
Sigils can name the winner; a seated Sigil that reboots keeps them), life
approvals, Commander undo receipts, presence verification, update jobs.

**On the Sigil:** `th_pair/atlas` (Atlas MAC, slot, pair key). Applied
settings (hold times, light style) are RAM only and resent by Atlas.

## Storage boundary

`storage.h` defines `BlobStore` with explicit results: `Ok`, `NotFound`,
`Unavailable`, `InvalidArgument`, `Corrupt`, `UnsupportedSchema`, `IoError`.
The NVS adapter reads whole blobs and checks set/commit results; it never
erases namespaces or repairs unknown records. Calls run on Atlas's
application task. There is no cross-record transaction: a commit error has an
uncertain outcome, so never blindly replay an increment.

**Capacity.** The NVS partition is 20 KB (about 500 entries), and each profile
uses about 20, so about 20 profiles fit rather than the 64 the code allows
(estimated from code, *Needs verification*). Making NVS bigger is parked in
[Staged Changes](STAGED_CHANGES.md), "Parked: storage batch". Storage stays
NVS plus checksummed SD blobs with no SQL database (owner, 2026-10-06).

## microSD card

Every Atlas ships with a card, but play never depends on it. The card holds
detailed statistics and
other luxury records, and the rotating diagnostics log
([Diagnostics](DIAGNOSTICS.md)). Without a card only the NVS core counts are
recorded and the stats API reports `"detailed": false`.

- All card access goes through `sd_card.cpp`. Application code uses only
  `sdBlobStore()` from the application task; its calls take the same card lock
  as the background log worker. Never call the Arduino `SD` library elsewhere.
- Mounted with `format_if_empty = false`; an unreadable card is reported as
  `no_card`, never wiped. A write/read-back self-test (`/turnhub/selftest`)
  gates every consumer.
- `SdBlobStore` files carry a 16-byte header (`THSD`, format, length, CRC-32);
  records are at most 1024 bytes. Writes go to `<key>.tmp`, are read back,
  then replace `<key>` via `<key>.bak`. This is not a power-loss transaction.
- **Hot-plug:** the log worker notices removal (failed write, or a raw sector
  read every 3 s), unmounts and logs `ATLAS|SD|REMOVED`; it retries a mount
  every 2 s with no card (every 30 s for a card that fails its self-test).
  The application loop follows `sdCardGeneration()` and moves detailed
  statistics to or from the card. The Atlas screen shows **NO SD CARD** while
  none is ready.
- **Factory reset** empties the card (`wipeSdCard()`); the portal stays in firmware,
  then erases NVS.

## Interrupted-match recovery

Atlas saves a compact, versioned checkpoint (`th_game_v1/checkpoint`) after
each accepted Intent that changes recoverable state, not on timer ticks. It
holds players and seats (including A/B order on a shared Sigil), turn,
life, Commander damage, settings and the turn timer.

- At boot a valid unfinished record restores the match **paused**, so
  power-off time is never charged to a player. Discard is the 5 s End match
  hold.
- Sessions, life approvals, menus and other adapter state are never restored;
  phones sign in again and controllers reattach to their participants.
- Partner commanders are not saved separately: recorded commander-2 damage
  turns them back on.
- Corrupt, unsupported or ambiguous records fail safe: profiles and
  statistics stay, and Atlas starts a fresh lobby.

## Completion ordering and the statistics limit

The engine's game-completed event calls `profile_stats_bridge.cpp` once per
match, whatever the ending path. The bridge must **commit the finished-match
checkpoint before** incrementing any profile statistics. A failed or uncertain
checkpoint write skips statistics and logs
`ATLAS|PROFILE_STATS|SKIPPED_CHECKPOINT|<status>`. Restoring a finished record
never replays completion.

This closed the power-cut double count (2026-09-26), but a cut **after** the
checkpoint and during profile writes can still leave missing or partial
results. Do not claim crash-safe, exactly-once statistics until durable
MatchIds, completion receipts and replay-safe writes exist (staged in
[Staged Changes](STAGED_CHANGES.md)). Host fault injection covers the
boundaries; it does not prove ESP32 flash or FAT behavior.

## Verification

`Atlas/tests/host/storage_scenarios.cpp` and `profile_store_scenarios.cpp`
run the real storage, repository and SD store code against fault-injecting
NVS and file-system fakes. Profiles, PINs and totals surviving updates and
reboots, booting without a card, failing cards and SD hot-plug are verified
in use (owner, 2026-10-02). Power-cut checks R05-R07 and card swaps D04-D05
are open ([Staged Changes](STAGED_CHANGES.md), "Open checks").

## Android local library

Android owns local player UUIDs, immutable finished-match UUIDs/snapshots and
separate delivery metadata in `turnhub_standalone/library` (JSON schema 1).
Active local game and library writes share one SharedPreferences transaction.
No import acknowledgement/rejection deletes history; no automatic retention
limit is applied. Existing queued records are retained for explicit review
without inferred local or Atlas identity, as required by TH-001. This bounded
queue intake is not a firmware storage migration. Already discarded records
cannot be recovered. See [Android](../../Android/README.md) for schema, validators,
retention, storage-failure and backup limits. Atlas's existing `sgimport` receipt
ring remains 64 entries; completion receipts/partial-write durability are not
expanded by this feature, and the parked storage batch remains parked.

## Bounded SD artwork records (2026-10-10)

`avatar_artwork.h` chunks each immutable JPEG into 768-byte records, below
SdBlobStore's unchanged 1024-byte ceiling. Keys `i<8-hex-revision><2-hex-index>`
hold image chunks; `r<revision>` holds the schema byte plus a 16×16 RGB332
thumbnail for today's Atlas. Per-profile `a`, `p`, `u` prefixes hold approved,
pending and unfinished manifests (schema byte, little-endian revision and length,
9 bytes). All pass through the existing locked/checksummed BlobStore and its
verified temp/backup replacement; no direct SD calls or image allocations on
Atlas. Metadata publication is the commit point, after complete image-envelope
validation. There is no cross-record transaction or FAT power-loss guarantee.

Each profile can hold one approved, one pending and one unfinished asset, bounded
by 48 KiB each plus headers/thumbnail. One transfer runs at a time with a two-minute
inactivity deadline. Ordered writes advance only on confirmed storage success;
a lost response requires restarting rather than replaying a chunk. Starting the
next upload/removing artwork cleans that profile's unfinished chunks, including
a transfer interrupted by restart. Old approved/pending chunks are reclaimed
after successful replacement. Failed cleanup may retain bounded orphan records;
no general garbage collector is claimed. Removal does not discard chunks still
referenced by another manifest after a partial approval/cleanup failure.

Readers validate metadata, lengths, checksums and a baseline JPEG envelope
(square 32–512 pixels, three 8-bit components, complete scan/EOI); firmware does
not fully decode the JPEG. Android bounds decoded dimensions and falls back on
decode failure. Pending GET requires the owning player or Admin, approved GET is
public. Public paths include revision; cache is disposable and scoped to the
Atlas connection. Thumbnail pixels come from the app's final compressed crop and
are reviewed as part of the same asset; the phone retains no original on Atlas.
