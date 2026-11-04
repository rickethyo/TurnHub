# Staged Changes and Open Checks

The one list of what is agreed but not done: open work, hardware checks still
to run, and product decisions still to make. When an item lands, move any
lasting fact into its topic document and delete the line here; git keeps the
old entries. Longer designs for planned features are in
[Planned Designs](PLANNED_DESIGNS.md).

Implementation and acceptance are tracked separately below. Builds and host/JVM
tests do not close physical-device, accessibility or lifecycle checks.

## Track and scope

Master is the S3/LVGL track; current-generation hardware fixes belong on
`legacy/gen1`. See [Agent workflow](AGENT_WORKFLOW.md) for branch rules and
[the S3 hardware plan](hardware/ESP32_S3_REFRESH.md) for target status.
Pre-release compatibility policy belongs in `CLAUDE.md` and
[Firmware Updates](FIRMWARE_UPDATES.md).

## Open work

### Phone-hosted games (owner, 2026-11-04)

The hub phone or tablet runs every game with the shared core; Atlas shows it,
drives the Sigils and keeps profiles, statistics and match history. Phone-less
games go away; Atlas's screen keeps setup and table views, sending every tap to
the hub. Contract: `protocol/hosted-v1.md`. Slices, each a flash-and-test round:

1. **Hosted contract and hub push** (implemented, `claude/master`): lease,
   state and input routes; read-only hosted view on the Atlas screen; the
   app's "Show on Atlas". Open check: push a running game from a phone and
   watch the Atlas screen follow; a second phone sees "Atlas busy".
2. **Sigils** (implemented, `claude/master`): a Sigil joins the hub's setup
   from its menu (bound by `localId`), shows the hub's game, and its menu
   choices and life keys are queued for the hub, which polls every 300 ms.
   Open checks: join two Sigils from setup, pass and change life from them
   and see the phone follow; measure Sigil to hub to Sigil latency (target
   under 200 ms). Not yet: Commander damage and partner pickers, player
   controls, elimination, rematch and seat switching on hosted Sigils.
3. **Atlas screen controls** (implemented, `claude/master`): the hosted game
   screen offers Start game (setup, two or more players), Pass and Pause (a
   living player's turn) and Resume, each queued for the hub for the active
   player; the player tiles only show. Open check: start, pass, pause and
   resume from the Atlas screen and see the phone follow. Not yet: tile life
   taps, the player screen and game options on a hosted game; "Connect a
   phone to start" with no hub waits for slice 6, while Atlas still runs its
   own games.
4. **Records and resume** (implemented, `claude/master`): finished hosted
   games reach Atlas's history and stats through the local-game import, which
   already runs while the hub is connected and signed in. Atlas keeps an
   unfinished game when its hub releases the table or goes quiet. Another
   phone that taps Show on Atlas with no game under way is offered "Resume
   <host>'s game" or "Show mine instead". Open checks: stop hosting mid-game
   on one phone, resume it on another, finish it and see one record on Atlas.
   Not yet: keeping the resumable game across an Atlas restart (RAM only).
5. and 6. **One app path, Atlas's engine deleted** (implemented, PR #196,
   2026-11-04): Atlas's `GameEngine`, `Lobby`, game Intents' handlers, game
   HTTP routes, venue tables, offline merge and recovery record are gone, and
   so are the app's Atlas-run Game tab, seat claims, offline queues, venue
   selector and Atlas Planes tab. `/api/v1/state` is `atlas-state-v1` (hub,
   battery, links). Open check: flash Atlas and a Sigil, install a Preview
   build, host a game from a phone and play it from the Sigil and the Atlas
   screen.

Follow-ups left by slices 5 and 6:

- The plane catalog and card routes (`/api/planes`, `/api/planes/card`) and
  the Atlas screen's plane card view stay, but no app client sends cards now:
  wire `AtlasPlaneCards.kt` into the hosted game, or retire the view.
- `/api/accounts` still reports `nudgeMuted` and the Game Master moderation
  bits (Reset connections, Remove player), which only the app's People sheet
  shows now; decide whether Game Master still means anything on Atlas before
  changing the account layout.
- "Connect a phone to start" on the Atlas screen with no hub (slice 3).

## Saved game setups on Atlas (next parity slice, 2026-10-31)

Android saves named lobby configurations locally (seats/identities, format, rules, timers, Planechase choices). Since 2026-11-04 the hub runs every game, so this may need only Atlas-side storage of the app's setups; revisit before building. Never replay scores/clocks/history or remap identities by name. Existing app lobby simplification and Advanced settings already share presentation across hosts.

## Persistent rooms: device acceptance and iOS counterpart

Android implements the version-2 room contract in
[Shared devices](SHARED_DEVICES.md). Verify two real devices through process
recreation, Wi-Fi address changes, same-profile reconnect without PIN, game-type
changes and simultaneous facing-panel taps.
Verify the UI with large text and TalkBack. The hub remains the game's
authority; the room does not create an additional takeover path.

iOS remains a foundation. Its future gameplay/transport slice must implement
version-2 persistent membership, secure reconnect storage, profile-first joining,
PIN-once links, read-only public views and account-free shared table controls.
LAN special setup/decision commands beyond normal gameplay remain host-only;
extend the shared command vocabulary and server validators together when adding
remote support. Explicit cross-ID account merging keeps the existing Admin
merge workflow; entering a room never silently combines private histories.
