# Phone-hosted games, v1 (slice 1)

*Experimental.* Owner decision, 2026-11-04: the hub phone or tablet runs every
game with the shared rules core (`core/`, the app's `NativeGameCore`), and
Atlas becomes the table accessory that shows it, drives the Sigils and keeps
records. Plan and later slices: `Documentation/engineering/STAGED_CHANGES.md`
("Phone-hosted games").

Atlas never decides anything about a hosted game. All routes need a signed-in
session (`X-TurnHub-Token`), including the shared table tablet's. Bodies are
form fields, like the rest of `http-v1.md`.

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
`{"live": true, "host": "Ana", "game": {...}}`, or `"game": null` with no
live hub.

## Inputs: Atlas's own controls

`GET /api/hosted/inputs?after=<seq>` (holder only) lists the inputs Atlas's
controls queued after `seq`, oldest first, at most 32:

```json
{"inputs": [{"seq": 3, "id": "uuid", "player": 2, "action": {"do": "pass"}}], "last": 3}
```

`player` is 1-based (0 for a table action). The hub applies each `id` once
through its seat-action door, which checks the seat and turn as it does for a
Play together guest. Slice 1 has no producers yet; Sigils (slice 2) and the
Atlas screen (slice 3) add them.
