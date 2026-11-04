# Game authority: the hub runs every game

Status: owner decision, 2026-11-04 (*Experimental*: host-tested, not yet
played on the boards). It supersedes the 2026-10-13 design for Atlas-hosted
games with a fenced handoff to a phone, and the 2026-11-03 offline play that
merged phone queues back into Atlas's game; both left with Atlas's engine.
Their text is in git history before this change.

## Who owns what

- **The hub** is the phone or tablet that runs the game with the shared rules
  core ([`shared/core/`](../../shared/core/README.md), through the NDK in the app). It owns
  the whole match: roster and seats, turns and timers, life and counters,
  eliminations, winners and the finished record. It saves the game on the
  device like any local game.
- **Atlas** is the table accessory. It holds the table lease, shows the hub's
  state on its screen and Sigils, queues their inputs for the hub, and keeps
  profiles, statistics, match history and settings. It decides nothing about
  a game. Contract: [hosted-v1.md](../../protocol/hosted-v1.md).
- **Guest phones** join the hub's room ([Shared devices](SHARED_DEVICES.md))
  and send inputs to it; they never become another authority.

There is no phone-less play with Atlas: without a hub, Atlas shows its idle
screen.

## When something drops

- **Atlas drops:** the hub's game keeps going on the hub. Sigils keep their
  last screen until Atlas is back.
- **The hub drops:** its lease lapses after 30 s and Atlas keeps the game in
  RAM as a resume offer. Another signed-in phone with no game under way may
  adopt it; the first hub coming back with the same `gameId` takes it off the
  offer. An Atlas restart drops the offer; the hub still has its own copy.

## Completion and statistics

The hub reports each finished game through the game import, once per record
ID (`POST /api/standalone/import`). Atlas saves hashed (record, profile)
receipts before any statistics ([Storage and Recovery](STORAGE_AND_RECOVERY.md)).
Durable completion receipts across power loss remain staged work.

## Remaining slices

Tracked in [Staged Changes](STAGED_CHANGES.md) ("Phone-hosted games").
