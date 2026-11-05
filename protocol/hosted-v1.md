# Phone-hosted games, v1

*Experimental.* Owner decision, 2026-11-04: the hub phone or tablet runs every
game with the shared rules core (`shared/core/`, the app's `NativeGameCore`), and
Atlas becomes the table accessory that shows it, drives the Sigils and keeps
records. Plan and later slices: `Documentation/engineering/STAGED_CHANGES.md`
("Phone-hosted games").

Atlas never decides anything about a hosted game. All routes need a paired
phone's key or a signed-in session in `X-TurnHub-Token` (see "Pairing" below).
Bodies are form fields, like the rest of `http-v1.md`.

The app shows its game automatically after connecting (pairing first if this
phone has no key for that Atlas), unless the player has stopped showing it. The
pairing key survives Atlas restarts. Temporary claim or state-push failures retry while the
connection is usable; another hub's live lease is respected. Disconnecting or
removing the pairing key stops publication until the phone pairs again.

## Automatic Atlas room

The hub phone also hosts the existing version-2 Play together room. Other
paired Android phones on the same Wi-Fi discover it through Atlas and choose
a profile from the Game tab, without entering a room code. Choosing an existing
seated profile attaches a controller; choosing an unseated profile adds it while
the hub is in the lobby. Multiple controllers may use the same profile. Atlas
does not run the room or its game rules. An independent running local game or
another shared room is preserved rather than replaced automatically.

- `POST /api/hosted/room`: the current hub publishes `invite`, a version-2
  invitation whose join-secret field is 64 zeroes. The ordinary invitation
  secret is never published. The descriptor expires after 30 seconds without
  renewal or when the hub loses its lease.
- `GET /api/hosted/room`: paired phones receive `room`, `host`, and a random
  per-phone `ticket`, or `room:null`. Tickets last 60 seconds and are delivered
  only over the paired HTTPS connection.
- `POST /api/hosted/controller`: only the descriptor's current lease holder
  can verify a `ticket` or recheck a joined `phone`. A valid check returns that
  phone's ID. A revoked pairing or expired ticket returns 403; another hub gets
  409. Controllers pause during unavailable checks rather than being removed.

The hub validates the ticket before admission and rechecks pairing during
controller requests (positive checks may be cached for five seconds). Atlas
membership authorizes gameplay only: no PIN prompt, account unlock, Device Admin
grant, private history exchange, or controller-invitation minting. Existing
ordinary room invitations retain their separate profile authorization rules.
The room's TLS certificate remains pinned by its invitation. Host loss preserves
the local game; Atlas never creates a second rules authority.

## Pairing: no account needed

*Experimental* (owner, 2026-11-05): Atlas is an accessory, so a phone pairs once
instead of signing in, the way a Sigil does.

- `POST /api/phones/pair` with `name` (the phone's name, printable ASCII, 32
  characters) answers `{"request": "<16 hex>", "code": "123 456", "waitMs": 60000}`.
  The Atlas screen shows "Allow this phone?" with the phone's name and the same
  code. `409` while another phone is asking, or when eight phones are paired.
  Home-network clients also send `home=1`: the code is the first 12 hexadecimal
  digits of Atlas's TLS key fingerprint, formatted in groups of four. The app
  derives its display from the endpoint's actual pin, never the returned code.
  Atlas automatically shows its own fingerprint in the same phone approval.
  A single Allow establishes TLS trust and the phone pairing; discovery alone
  never persists trust. Hotspot pairing retains its six-digit code.
- `GET /api/phones/pair?request=<id>` answers `{"state": "waiting"}`, then
  `"allowed"` with `"phone"`, `"key"` and `"owner"` exactly once, or
  `"refused"`, `"expired"` (60 s without an answer) or `"unknown"`.
- `POST /api/phones/pair/cancel` with `request` cancels that waiting request only;
  it cannot revoke an already approved phone. The app sends it when its
  full-screen approval is cancelled.
- The phone then sends `X-TurnHub-Token: phone:<phone>:<key>` on the hosted and
  plane, player-copy and finished-record routes. Atlas keeps only a hash of the key (NVS `phones`). The first
  phone paired is the owner. A `401` means Atlas forgot the phone: pair again.
- Only the Atlas screen allows a phone or forgets them all (Settings > Device >
  Forget paired phones).

A phone's lease is keyed on its phone ID and named after the phone.

## Player copies and records

These routes require a valid paired phone credential; a player sign-in is not
needed. `401` means pair again. Pairing grants access to the table's shared
profile copies, totals and archived match records, including other players in
those records. Profile copies include salted PIN verifiers, never plaintext
PINs or private moderation history. The receiving device keeps account settings
and detailed statistics locked until the profile is unlocked locally.

- `POST /api/player-copies`: form `profileId` (8 hexadecimal characters),
  `name` (1–32 UTF-8 bytes), `color` (`none` or `#RRGGBB`), and `avatar`
  (0 for none, or a preset ID from `/api/avatars`). Creates or updates the copy
  under that exact ID. Same-name players with different IDs remain separate.
  `copy` carries the schema-2 profile subset: `copySchema`, `profileId`, `name`,
  `lastActivityMs`, `createdAtMs`, `personalization` (color, preset avatar and
  reduced motion), and `credential` (null or base64 salt/hash, 16/32 bytes).
  Atlas stores this in NVS namespace `phone-copies`, keyed by the profile ID.
  Extra device-role/link/history/image fields are discarded. Newer updates win;
  at equal timestamps an existing verifier can only be filled, not replaced.
- `GET /api/player-copies` returns `{"copies":[...]}` only to paired phones.
  Phones check every 15 seconds and commit copies locally without unlocking
  them or changing their device Admin. Copies persist offline by default. A
  device-owner opt-out removes unused automatic copies after 24 hours offline,
  preserving current/saved setups and match records; reconnect resets the grace.
- Success: `{"ok":true,"appearanceStored":true}`. With no usable SD card,
  appearance can fail independently: `appearanceStored:false` still permits
  basic record credit using the identity in NVS. `400` rejects malformed
  metadata; `503` means the identity/name could not be saved. Clients retry on
  reconnect and keep their own copy throughout. Unchanged looks avoid writes.
- `GET /api/player-copies/stats?profileId=<id>` returns `ok`, `profileId`,
  `name`, `winRate`, `detailed`, `lastGameType`, `lifetime` and `lastGame`, with
  the same statistics fields as `/api/session/stats`, without `moderation`.
  `detailed:false` means only basic totals and the last result are available.
- `GET /api/player-copies/history?profileId=<id>&skip=0&limit=20` returns
  `{"ok":true,"records":[...],"more":false}`. `skip` is a nonnegative integer;
  `limit` is 1–20 (default 10). `404` means no such copy; `503` means archived
  history cannot be read without its SD card.

Hosted setup also carries `hostedProfiles`, the hub's public saved-profile
IDs and names, so a Sigil can choose a profile not yet seated or copied to Atlas.
No credentials travel in that list. Sigil `Join game` opens a paged picker:
existing lobby players, remaining saved profiles from hub and Atlas, then a
device-named true guest. Selection queues `sigil-join` with `mode` (`attach`,
`profile`, `guest`), `sigil`, `slot`, `name`, `profileId`, `target` (local ID)
and `playerIndex` (attachment only). The hub rechecks the identity snapshot,
roster, binding and lobby phase. `sigilSeat` holds `sigil:<id>:<slot>` separately
from `localId`; legacy snapshots using `localId` remain readable. Detaching
clears only the binding, preserving the participant and phone controllers.

The Android hub projects saved players' stable profile IDs into hosted state;
phone-local seat IDs and Sigil device IDs are never substituted for profile IDs.
On connection and library changes it sends metadata copies, then finished games
through `/api/standalone/import`, then reads totals and archived history. Only
acknowledged copy IDs are mapped automatically. Other seats remain guests;
matching names never create a mapping. Import receipts count each record/player
once, even after a lost response or later guest mapping. History already on the
phone is kept once by record ID, and returning players bind by stable ID.
The phone saves its pairing key before reporting success; Android cloud backup
and device transfer exclude it.

## Lease: one hub per table

`POST /api/hosted/claim` takes the table, or renews it for the same session.
Another session's live lease is never taken over: Atlas answers `409` with the
holder's name. A lease lapses 30 s (`leaseMs`) after the hub's last claim or
push. A new hub starts with no game and no inputs.

`POST /api/hosted/release` gives the table back (holder only, else `409`).

Both answer:

```json
{"ok": true, "host": "Ana", "revision": 12, "lastInput": 0, "leaseMs": 30000}
```

## State: the hub's game

`POST /api/hosted/state` with `state` = the core's game JSON (the app's
`StandaloneTable.gameToJson` plus `profile`, at most 64 KB). Atlas keeps the
latest one:

- the same `gameId` must not go back in `revision`, and the same revision is
  accepted only unchanged (a retry);
- a new `gameId` replaces the game and clears the input queue;
- setup between games has an empty `gameId` and is always taken.

`409` means the session does not hold the table (claim again); `400` means the
game is not valid. The hub re-sends its latest state every 10 s while nothing
changes, which keeps the lease.

`GET /api/hosted/state` (any session) answers
`{"live": true, "host": "Ana", "game": {...}, "resume": null}`, or
`"game": null` with no live hub.

## Resume and records

When a hub releases the table or goes quiet in the middle of a game (started,
not over), Atlas keeps that game in RAM. While no hub holds the table, `resume`
carries it: `{"host": "Ana", "game": {...}}`. Another paired phone with no
game under way may adopt it and host it. Any hub that pushes the same
`gameId` (this phone, or the first one coming back) takes it off the offer. A
finished game, setup or an Atlas restart leaves nothing to resume; the first
hub still has its own copy.

A hosted game is the hub's own local game, so its finished record reaches
Atlas's history and statistics the way every local game does, through
`POST /api/standalone/import` (`http-v1.md`), once per `recordId` (the
`gameId`). That happens automatically while the hub is connected and paired.
Sigil device seats are unlinked players; they do not create saved accounts.

## Inputs: Atlas's own controls

`GET /api/hosted/inputs?after=<seq>` (holder only) lists the inputs Atlas's
controls queued after `seq`, oldest first, at most 32:

```json
{"inputs": [{"seq": 3, "id": "uuid", "player": 2, "action": {"do": "pass"}}], "last": 3}
```

`player` is 1-based (0 for a table action). The hub applies each `id` once
through its seat-action door, which checks the seat and turn as it does for a
Play together guest. The hub polls about every 300 ms while it hosts.

Seat actions use the Play together vocabulary (`lan-v1.md`):
`{"kind": "pass" | "win" | "confirm" | "deny" | "life" | "pause", "value": n, "slot": 0, "source": 0}`
(`pause` value 1 pauses, 0 resumes; `life` value is the signed change).
Table actions (`player` 0) are applied in setup only:

- `sigil-join` uses the picker selection fields described under Player copies
  and records above; it attaches a lobby player or adds a profile/true guest;
- `{"kind": "sigil-leave", "sigil": id, "slot": 0|1|2}` releases that controller
  (0: both of the Sigil's seats), preserving the player and other controllers;
- `{"kind": "start"}`: starts the game.

## Sigils

A hosted player is bound to a Sigil seat by its `sigilSeat`,
`sigil:<sigilId>:<slot>` (slot 1 = seat A, 2 = seat B). The hub sets it; Atlas
never edits the game. While a hub hosts Game 1, Atlas draws Game 1's Sigils
from the hub's game (display, table roster and light cues), offers their menu
from it (Join in setup; Pass and Claim win on their turn; Pause, Resume and
life keys while playing; Confirm or Deny a win claim), and queues every choice
and key as an input.

## Atlas screen

While a hub hosts Game 1, the Atlas game screen shows the hub's game and offers
Start game (setup with two or more players, a `start` table action), Pass and
Pause (a living player's turn) and Resume (paused with no open win claim). Each
is queued for the active player, as the Atlas screen's own controls act for the
active seat; its input ids start with `atlas-` (Sigils' with `sigil-`). The
player tiles show only.

## Planechase on the Atlas screen

While the hub's game plays Planechase, the Atlas screen adds a Planes tab: the
face-up plane, its counters, the die's next cost and the latest roll, and a
full-screen landscape card view. Its Roll, Planeswalk and Resolve phenomenon
buttons queue the Play together planar commands (`planar-roll`, `planar-walk`,
`planar-resolve`) for the active player; the hub rolls the die.

The state names digital planes by ID only, so the hub sends what Atlas needs to
show them while it hosts: the plane names (`POST /api/planes`, when Atlas's
catalog version differs) and each face-up plane's card text and art
(`POST /api/planes/card`, `http-v1.md`), plus the next three deck cards so a
planeswalk shows at once. Atlas keeps them on its microSD card; without one, the
Planes tab still shows the counts and rolls.

The hub rechecks the saved card/art index while the desired planes stay the
same. Network/storage failures and a late phone catalog or art download retry
without another roll. A 202 response means queued, not saved: only a later
index response confirms artwork. Text-only cards keep retrying missing art;
a restarted Atlas is checked again. The phone shows transfer/wait/error text
near its On Atlas control. Atlas retries a temporarily missing JPEG in an open
card view instead of caching the missing image for the life of that plane.

### Direct Scryfall fallback on Atlas

While a digital hosted game is running or paused, Atlas may fill missing card
text/art from Scryfall over its configured home Wi-Fi. Only the hub's face-up
card IDs and next three deck IDs are eligible; physical cards cause no requests.
Atlas gives the phone 15 seconds to fill the cache first, fetches one card at a
time, and waits at least 60 seconds before retrying that missing ID. No card
storage or home connection means no fetch. These are card-cache operations,
never game actions, deck changes or die rolls.

The separate network task checks TLS chains/hostnames using GTS/ISRG roots,
permits only the fixed Scryfall API and its HTTPS art-crop host, sends User-Agent
and Accept headers, spaces requests by 150 ms, and bounds handshake/read/body
times and allocations. Metadata is limited to 16 KiB, source JPEG to 256 KiB
and dimensions to 2048. The JPEG-only stb adapter converts progressive images
in PSRAM to 480x320 baseline JPEG, at most 96 KiB, before the normal SD write
queue accepts them. The existing HTTP upload limit stays 96 KiB. Text can save
before art; missing art retries. The public card ID is the only lookup input;
Atlas sends no player profiles, PINs, game history or private game state.

Direct metadata adds that plane to the existing bounded catalog without
replacing the phone's other entries (at capacity, the last entry is replaced;
cached files remain). A changed/ended game discards an obsolete in-flight
result. Network and conversion run off loop() and off the SD worker, so the
screen, input and radio services continue. Stored cards still work offline.
