# Play Together LAN rooms, version 2

The Android hub serves a persistent game-night room independently of the current
match or game format. Guests send commands and render snapshots; the standalone
hub or connected Atlas remains the game authority. There is no room host election
or automatic change of game authority.

## Invitation and transport

All devices need the same reachable Wi-Fi; internet is unnecessary. The hub binds
a pinned TLS server to its current private IPv4 address and an ephemeral port.
DNS-SD `_turnhub._tcp` advertises `v=2`, `s=<room ID>` and `p=<certificate SHA256>`;
no invitation secret is advertised. This includes QR-only rooms so previously
joined devices can rediscover a changed address/port using their saved room ID
and certificate pin.

Standard entry uses a code such as `abc-123`. QR-only entry uses a random 256-bit
key. The invitation is
`turnhub://join/2/<IPv4>/<port>/<certificate SHA256>/<room ID>/<key>`.
For standard rooms the key may be the normalized join code. The room ID is
32 lowercase hexadecimal characters; the certificate pin and random key are 64.
Sharing the invitation authorizes entry without host approval. Ten incorrect
keys or profile PIN attempts within a minute lock new entry for that minute.
Never log invitations, reconnect tokens, PINs, link keys or room records.

Each connection carries one request/reply: a four-byte big-endian length followed
by strict UTF-8 JSON. Maximum frame size is 512 KiB and nesting is bounded at 32.
The existing transport bounds, TLS verification and explicit Wi-Fi routing apply.
Every envelope includes integer `v: 2` and string `session` (the room ID).

## Membership and identity

| Operation | Fields | Authorization |
| --- | --- | --- |
| `preview` | `key` | Invitation |
| `join` | `key`, UUID `joinId`, `role`, optional profile fields and `deviceName` | Invitation plus initial profile proof |
| `poll` | `token`, optional `looksId`, `deviceName` | Saved room membership |
| `presentation` | `token`, `displayTarget`, boolean `displayAllUpright` | Player/shared-table membership or game master |
| `command` | `token`, `epoch`, UUID `actionId`, `match`, `revision`, `kind`, `value`, `slot`, optional `source`, `target` | Membership and room capability |
| `history` | `token`, `step` (`index`, `pull` with `ids`, `push` with `history`) | Player membership; the member's own profile only |
| `leave` | `token` | Membership; explicitly removes this device |

Roles are `player`, `display`, `table` and `planes`.
Public displays are read-only. `table` grants ordinary shared-seat gameplay with
no personal account. Player membership binds to a stable profile
ID, independently of seat number/name. Removing a participant from a game leaves
the member connected with player 0, waiting for their next game. Multiple devices
may control the same participant. Up to 16 device memberships are retained;
idle/disconnected devices are not automatically removed.

The directory contains public `profileId`, `name`, and `hasPin`, with optional
`color` (`#RRGGBB`) and preset `avatar` key for profile picking. No private image,
PIN verifier, privilege or history travels in these appearance fields. A first
profile link supplies `profileId` and its optional `pin`. An injected local
`profile` carries its public ID, name, original creation time, activity timestamp,
look and salted credential verifier; no other Atlas links, private history,
permissions or device-admin designation travels with it (the player's own games
follow through `history`, below). Protected profiles require
verification on first link. Existing same-ID credentials are never replaced by
an offered verifier. Equal names do not establish identity or merge accounts.
A private join reply can contain the verified profile's `profileLink` and synced
`profile`; these never enter the public snapshot or another member's polls.

A repeated `joinId` returns the saved member rather than seating a duplicate.
Reopening/reconnecting uses its saved token, without PIN entry or host approval.
The member's selected view is device-local saved state. Hosts can explicitly
revoke devices or grant/revoke room game-master access. These grants do not
confer Atlas Admin permissions or access to another profile's private statistics.

## Player history

A player's finished games travel with them whoever hosts (owner, 2026-11-05).
A player member and the hub swap the games of the member's own profile and keep
the union, matched by `recordId`:

- `index` answers `records`, the record IDs of that profile's games on the hub
  (at most 5,000, newest last).
- `pull` takes `ids` (at most 32) and answers `history`, those games as records
  without local account IDs; every seat that is an account is named by its public
  `profileId`.
- `push` takes `history` (at most 32 records) and answers `added` and `conflicts`.
  Each pushed record must name the member's profile on exactly one seat.

A record already on a device is never replaced. A copy that differs from it in
outcome is skipped and counted as a conflict, so nothing counts twice and a bad
copy cannot overwrite a game. Only the syncing profile's own seat binds to a
local account; other seats keep their profile IDs and bind when those players
sync themselves. Statistics are always recomputed from the merged games.

The guest runs the exchange after joining, after every reconnect and when a
match ends or a new one starts, so games played apart merge on the first
connection. Display, table and planes members get `forbidden`. The hub answers
`unavailable` when it can't save, and the guest retries at the next trigger.

## Public display orientation

Room snapshots include `displayTargets` (public `id`, friendly `name`, device `detail`,
boolean `public`, boolean `upright`), the receiver's `displayId`, and its own
`displayAllUpright`. A target ID is `host` or a member's non-secret `joinId`, never
its reconnect token. Join/poll may supply a bounded 64-character `deviceName` for
labels; it confers no authority.

`presentation` sets an absolute per-target boolean. Any authenticated player
or shared-table member can change it, even when sitting out or
eliminated; public read-only members need a game-master grant to send edits.
The hub validates the target and boolean, saves before acknowledging, rolls back
failed writes, and returns current presentation state. Repeating the same value
is idempotent. Other targets retain their values. Room `displayOrientations`
persists across reconnects and restarts.
This does not alter game revisions, seat order, rules or gameplay permissions.

Android displays render only their own setting. All panels upright ignores local
seat-facing/half-turn rotations; Face each player restores the existing layout.
The picker identifies the screen separately from the direction choice. Without a
room, Android stores this device's orientation in its local UI preferences.
Atlas-relayed Android displays use the same room presentation contract; the
physical Atlas overview already renders its player text upright.

## State, controls and receipts

Standalone snapshots carry the existing public `game` JSON and bounded public
`looks`/`looksId`. Hidden planar deck order, Bag of Doom selection, local account
handles and credentials are omitted. Atlas relay snapshots carry Atlas's public
`/api/v1/state` body in `atlas`, with its names and age. The room also returns
`epoch`, member `player`, `role`, `master` and the public directory.

Public Table/Planes views are read-only unless granted game master access;
a shared table controller can change ordinary seat gameplay. The server derives
a personal command's seat from the saved profile ID and ignores a forged target.
A shared controller/game master may name a current target. Planar actions allow living seated players and shared controllers while Planechase
is enabled, acting for the active planar controller. Public views require a
game-master grant for these gameplay changes. The host rechecks the originating
seat and rejects eliminated players. Die rolls (including card-effect rolls)
require the originating player to be on the active turn/team, even with a
game-master grant. Shared host/table controls act for the active controller. Format changes select the appropriate existing engine
validator rather than carrying old game-specific seat capabilities forward.

Existing local own-seat commands include life, counters, received Commander
damage, pass, win/confirm/deny/concede/eliminate, and pause
commands. Planar commands retain the `planar-` vocabulary; Atlas plane controls
use bounded `planar-atlas:` actions, and `atlas-screen:<game|table|planes|card>`
sets the Atlas screen's view for the guest's own seat (Atlas accepts signed-in
players only, as from their own phone). Setup and special table decisions unsupported
by the LAN vocabulary stay on the host and are reported visibly.

The personal and shared-panel counter menus
also send `everyone:<kind>:<delta>` for `poison`, `energy`, `experience`,
`treasure` and `rad`, with delta exactly `-1` or `1`; the outer `value`, `slot`
and `source` must all be zero. An authenticated living Magic player can apply
these through the normal host-engine or Atlas counter validator. Only living
players change, and shared team poison changes once per team. Read-only views
cannot send these commands; normal match/revision and receipt checks apply.

Local commands require the exact match/revision. Atlas commands require its
boot/game context. A per-member immutable action-UUID receipt prevents duplicate
deltas and rejects changed payloads; receipts are not evicted. After 2048 actions
the member's command generation renews without PIN entry. The boundary edit is
not applied and must be reviewed; old-generation commands cannot be replayed.
A lost reply is retried only with its
original UUID and payload.

Each hub process creates a fresh command `epoch`. Room membership survives a
restart, but old-epoch commands receive `interrupted` without applying. This
prevents replay when a game write succeeded just before the process lost its
receipt. Players review current values before making a new edit. Reconnect does
not replay commands automatically.

### Public display content

`presentation` accepts exactly one of `displayAllUpright` (boolean) or
`displayView` (`table` or `planes`), with the same stable `displayTarget`.
Content selection applies to member roles `display`, `table` and `planes`;
`planes` requires Planechase on the authoritative game. Host and personal
screens keep their normal navigation. Target rows include `view` for supported
public screens, and each public member reply includes `displayView`.
The room persists choices in `displayViews`, independently of orientation,
and rolls back failed writes. An unchanged request is idempotent.
Authenticated players/shared controllers and game masters may select public
content; read-only Table/Planes members may select only their own content.
This never changes membership role, seat, master grants or game state.

## Persistence and Atlas backup

The hub saves the room ID, invitation secret, owner capability and memberships
in device-private AES-GCM ciphertext with an Android Keystore key. Guest devices
save their token, invitation endpoint/pin and selected view similarly. Room
preferences are excluded from OS backup/transfer. Explicit End room deletes the
host record; Leave room deletes a guest's reconnect record. Closing the app does
neither. Display-awake behavior and the existing five-minute background link hold
remain; a closed standalone hub cannot serve until reopened.

The room lives only on the hub; Atlas keeps no copy. LAN guests wait for the
same hub to return.

## Verification

JVM room tests cover PIN-once membership, same-name identity separation, sitting
out, cross-game continuity, shared controllers, public-view authorization,
explicit grants, history exchange authorization (`LanRoomTest`) and the
two-way history merge with conflicts (`HistorySyncTest`), receipt deduplication, old-epoch rejection, write failure,
revocation and repeated join IDs. Existing engine/transport suites still validate
semantic actions and TLS. Atlas host scenarios cover capability checks, durable
backup reads, failed writes and room deletion. Real device checks remain necessary
for discovery, Wi-Fi isolation, process death, shared-panel layout and reconnect.

## Additional phones for one player

An authenticated player member requests `invite-controller`. The host persists a
separate random `controllerKey` under that membership and returns it with
`profileOnly`. The existing room URI gains `/PROFILEID` after its bearer key;
that key is the scoped controller key, never the general room key. Preview and
join must present both matching values. The host forces the `player` role and
the issuing member's identity, ignoring requested player, profile or table role.
It never grants master controls. General joins cannot select phone-claimed
identities, and the public directory excludes them; Sigil bindings do not affect
this filter. A member's existing reconnect token continues to work. Invitations
survive host restarts, and revoking the issuer invalidates its unused invites.
The room retains its 16-member capacity; several devices sharing a player count
as several devices, while game identity and stats still count once.
