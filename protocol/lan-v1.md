# Phone-hosted shared game, version 1

Android's shared-device transport is independent of Atlas HTTP and ESP-NOW.
Magic variants, Yu-Gi-Oh!, Pokémon, Lorcana and Death May Die have one standalone
host, from the lobby on. TCG guests control one player and may inspect the full
table read-only without changing their session. Death May Die guests display
the Elder One board or control one investigator. Guests do not run a game engine,
save a match/history, award statistics, or take over when the host disappears.

## Invitation and transport

The host binds a TLS server to its current private Wi-Fi IPv4 address and an
ephemeral TCP port. All devices must be on the same reachable Wi-Fi network;
guest isolation can prevent connections. Internet is unnecessary. The host's
join security setting chooses one way in:

- **Standard:** the host registers DNS-SD service `_turnhub._tcp` on the Wi-Fi
  network with TXT `v=1`, `s=<session>` and `p=<SHA256-certificate-pin>`. The
  key is the join code: three letters from `abcdefghjkmnpqrstuvwxyz`, a hyphen
  and three digits (`abc-123`). Guests normalize typed codes, discover hosts
  for about three seconds and send `preview` with the code to each; TXT data is
  unauthenticated, so only the code grants access.
- **QR only:** no announcement. An invitation, shown as a QR or copied
  explicitly, has this grammar:

  `turnhub://join/1/<IPv4>/<port>/<SHA256-certificate-pin>/<session>/<key>`

The address is RFC 1918 or IPv4 link-local, port 1024–65535. Pin and key are
64 lowercase hex characters; session is 32. Pinning authenticates the self-signed
Android-keystore host certificate. TLS 1.2/1.3 encrypts invitations' subsequent
use, snapshots and controls; cleartext Android policy is unchanged. Never log
invitations or tokens. Treat copying/scanning an invitation as granting access
to this table. Stopping hosting invalidates its credentials; a new host session
creates a fresh code or invitation. Ten wrong keys within a minute return
`locked` to every `preview`/`join` until the minute passes. The app does not register an external deep link.

Each TLS connection carries one request and response. Frames are a four-byte
unsigned-compatible big-endian positive length, then UTF-8 JSON. Lengths outside
1–524288 are rejected before allocation/dispatch. UTF-8 is strict, nesting is
limited to 32 levels and lenient JSON token extensions are rejected before
the platform parser runs. Client connect timeout is
3 seconds; socket read/handshake timeout is 4 seconds. Server capacity is four
active workers plus four waiting connections; excess connections close.

Every envelope contains integer `v: 1` and the invitation's string `session`.
Requests contain `op`. Responses contain `code`. Integers reject strings and
fractions. A different session/version is invalid. This prototype's match
projection uses the host's current standalone game JSON, without `localId` or
`profileId`. The library, account credentials, links and history are never sent.
Each snapshot carries `looksId`; when it differs from the `looksId` a `poll` or
`command` supplied, it also carries `looks`: one entry per seat in player order,
`null` or that seat's public color, avatar and panel background (the
`LocalPersonalization` JSON without thumbnail or motion preference). Background
and avatar images are included while all looks fit in 320 KiB; otherwise image
backgrounds fall back to a gradient of the seat's color. Both apps must understand this protocol and its match projection;
incompatible data stops joining rather than starting a local game.

## Requests

| Operation | Additional request fields | Capability |
|---|---|---|
| `preview` | `key` | Invitation: view the match before choosing a role |
| `join` | `key`, `joinId`, `role`, `player` | Invitation: acquire a session |
| `poll` | `token`, optional `looksId` | Authenticated guest: get latest snapshot |
| `command` | `token`, `actionId`, `match`, `revision`, `kind`, `value`, `slot`, optional `source`, optional `looksId` | Player/investigator: request an own-seat edit |
| `leave` | `token` | Release this guest session |

`role` is `player` for a TCG seat; Death May Die accepts `elder` with `player: 0`,
or `investigator` with a current one-based player number. The host rejects roles
from another game family. It binds a player session to that player's name and
renumbers it as lobby changes move the player; if the player is removed, the
next authenticated request returns `released` once and the guest chooses again.
A session lasts through the lobby, matches and rematches; commands apply only
while a match is running or paused. Only one guest controls a given investigator; the host retains
its controls. Up to 16 guests can join. Sessions idle for five minutes are
released when processing a new join. A live guest polls once per second.
Repeated `joinId` UUIDs return the same token for the same role/seat. A successful
join returns `token`; authenticated snapshots return `game` and `claimed`
(one-based investigator seats). The token, rather than any submitted player
field, owns the investigator. Elder guests cannot submit commands.

Join, poll and command acknowledgements also return the session's current
one-based `player`, so guest controls follow lobby reseating. A hosted format
change ends the session; lobby/matches/rematches within the same format keep it.
TCG table/personal navigation is local presentation state: it neither joins a
second seat nor changes the token or poll loop. The table exposes no edit menus,
score gestures, Pass button or actionable turn chips.

### TCG commands

| `kind` | `value` | `slot` | `source` |
|---|---|---|---|
| `life` | Nonzero delta −10000…10000 (life, prizes or lore) | 0 | 0 |
| `commander` | −1 or +1, damage received by the owned player | Commander 1 or 2 | One-based source player |
| `poison`, `energy`, `experience`, `treasure`, `rad`, `commander-casts` | −1 or +1 | 0/1; commander casts may use 2 | 0 |
| `city-blessing`, `initiative` | 0 or 1 | 0/1 | 0 |
| `turn-action:<enum>`, `pokemon-condition:<enum>` | 0 or 1 | 0 | 0 |
| `pass`, `extend-turn`, `win`, `confirm`, `deny`, `concede`, `eliminate`, `monarch` | 0 | 0 | 0 |

A guest's `win` opens a claim (`winClaim`, `winConfirmed` in the game JSON) that
the other living players (other teams in Two-Headed Giant) answer one at a time
in turn order after the claimant with `confirm` or `deny`, as Atlas asks for a
Sigil's claim. A denial withdraws it; the last confirmation ends the match.
Cooperative games and a table with nobody left to ask win at once.

The existing standalone engine validates format, active turn/team, living seat,
counter bounds, Commander allies and all outcomes. The authenticated player is
always the recipient; `source` selects only whose Commander dealt damage. In
Two-Headed Giant, life/poison apply to the team and either teammate may pass;
other counters stay personal. Setup, pause/reset/rematch, day/night, Planechase
and Bag of Doom resolution remain host-only. Commander names and chosen art
printings (public card IDs) are included in public snapshots; account IDs and the
Planechase deck stay private.
`source` defaults to 0 for existing DMD clients and is part of immutable receipts.

### Death May Die commands

| Command `kind` | `value` | `slot` |
|---|---|---|
| `wounds`, `stress`, `fire`, `insanity` | −1 or +1 | 0 |
| `skill` | New level 1–4 | One-based skill slot |
| `take-turn` | 0 (engine stages the investigator) | 0 |
| `pass` | Explicit next player number | 0 |
| `eliminate` | 0 | 0 |

Only the active investigator may pass; the existing cooperative-turn validator
checks the recipient. Reference/card assignment edits remain on the host UI; a guest can explicitly
confirm their own investigator’s death. Clients can read assigned cards (both relic faces),
adjust markers/skill levels, stage their next turn and choose a pass recipient.
All game rules and bounds are enforced by the existing standalone authority.
Commands never pause/start/reset/rematch a game or change the Elder One board.

## Ordering, acknowledgement and failure

Host UI and network requests serialize on the Android main dispatcher. Commands
require the exact match ID and revision. The host runs the normal rule transition,
saves the game and any resulting history in one synchronous storage transaction,
then publishes and acknowledges it. Failure leaves the previous game published,
blocks further writes and cannot return success. Completion uses the existing
single host history/statistics pipeline.

An `actionId` UUID identifies one immutable request payload within its guest
session. Duplicate requests return the recorded result with a fresh snapshot;
reusing an ID for a different payload fails. Each guest retains at most 2048
receipts without eviction; at that limit it must leave and rejoin. Receipts,
tokens and invitations are memory-only. A host process restart invalidates all
sessions; clients cannot replay an uncertain action into a fresh session. The
host's saved match/history survive under the existing persistence rules. This
does not promise crash-safe distributed exactly-once statistics or host takeover.

Clients keep one in-flight command, with no optimistic mutation. A lost reply
keeps that exact request pending and disables other edits. Retry uses its same
ID and base revision. Stale edits are rejected with the latest snapshot; the
player reviews current values before submitting a new edit. Disconnection shows
the last snapshot with controls disabled. Polling reconnects to the same session;
revocation or process death requires joining again. No offline edits
are queued. Leaving with an uncertain reply explicitly warns the player to
check the host before entering a replacement edit.

Codes: `ok`, `invalid`, `unauthorized`, `forbidden`, `claimed`, `full`, `stale`,
`rejected`, `unavailable`, `renew`, `released`, `locked`, `ended` (the host left
the hosted format). Unauthenticated errors carry no match
data. When the host app is backgrounded or cannot save, it returns `unavailable`.
Hosting keeps the screen awake but does not run a foreground service; keep the
host app open. Switching back to it allows existing guests to reconnect. Changing
Wi-Fi or a host process restart can require stopping/restarting sharing.
