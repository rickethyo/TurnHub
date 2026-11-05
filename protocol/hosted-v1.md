# Phone-hosted games, v1

*Experimental.* Owner decision, 2026-11-04: the hub phone or tablet runs every
game with the shared rules core (`shared/core/`, the app's `NativeGameCore`), and
Atlas becomes the table accessory that shows it, drives the Sigils and keeps
records. Plan and later slices: `Documentation/engineering/STAGED_CHANGES.md`
("Phone-hosted games").

Atlas never decides anything about a hosted game. All routes need a signed-in
session (`X-TurnHub-Token`), including the shared table tablet's. Bodies are
form fields, like the rest of `http-v1.md`.

The app shows its game automatically after connecting and signing in, unless
the player has stopped showing it. Completing Atlas setup restores sign-in
after Atlas restarts. Temporary claim or state-push failures retry while the
connection is usable; another hub's live lease is respected. Disconnecting or
signing out stops publication until the connection and session are ready again.

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
carries it: `{"host": "Ana", "game": {...}}`. Another signed-in phone with no
game under way may adopt it and host it. Any hub that pushes the same
`gameId` (this phone, or the first one coming back) takes it off the offer. A
finished game, setup or an Atlas restart leaves nothing to resume; the first
hub still has its own copy.

A hosted game is the hub's own local game, so its finished record reaches
Atlas's history and statistics the way every local game does, through
`POST /api/standalone/import` (`http-v1.md`), once per `recordId` (the
`gameId`). That happens automatically while the hub is connected and signed in.
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

- `{"kind": "sigil-join", "sigil": id, "slot": 1|2}`: the hub adds a device-bound local seat
  (`guest=false`, `localId="sigil:<id>:<slot>"`), without a saved account;
- `{"kind": "sigil-leave", "sigil": id, "slot": 0|1|2}`: removes that seat
  (0: both of the Sigil's seats);
- `{"kind": "start"}`: starts the game.

## Sigils

A hosted player is bound to a Sigil seat by its `localId`,
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
