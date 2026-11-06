# Shared devices

Play Together uses a persistent game-night room on the same Wi-Fi. A phone or
tablet opens it before or during game setup; a join code or QR authorizes entry
without host approval. The room survives rematches, different formats and app
reopening. The host is the game's hub and owns its game and result, with or without
Atlas; with Atlas, the hub also hosts the game on it ([hosted-v1.md](../../protocol/hosted-v1.md)).

Devices choose a public read-only display, account-free shared table controls,
or a personal profile. The shared tablet reuses facing-seat `TabletTable` panels.
Personal profiles enter the room immediately; the host need not preselect them.
A protected profile asks for its PIN on first linking, then its saved room token
restores access on reconnect. A member sitting out a game stays in the room.
Stable profile IDs bind members to current participants; equal names never
establish identity. Explicit account merging remains separate. A player's
finished games travel with them: on joining, reconnecting and each match end, the
guest and the hub swap that profile's games and keep the union by record ID
(`history` in [LAN rooms](../../protocol/lan-v1.md#player-history),
`HistorySync.kt`), so games played apart merge into the host without duplicates.

Table and Planes views are read-only by default. Shared table controllers
can act for all normal gameplay panels. The host can grant game-master access to
a device for editable public views. Room grants confer no Atlas administration
or private-statistics permissions. Joined players and shared-table controllers use the same Game-tab lobby as the
hub: seat drag/tap swaps, players, game/variant, Planechase, rules, timers, saved
setups and Start. Lobby edits are validated and saved by the hub; old revisions
are rejected. A setup change cancels either phone's start countdown. Read-only
displays retain their permissions. During play, personal phones can name their
own commander. After game over,
seated players (including eliminated seats) and shared controllers can request
Rematch or Reset from the hub. The profile picker keeps the table visible behind
it. Special decisions outside the LAN command
vocabulary remain on the host and show a refusal on a remote controller.

The hub owns membership writes. Its room and guests' reconnect credentials are
encrypted in device-private preferences, excluded from OS transfer/backup.
End room explicitly revokes membership; closing the app preserves it. Guests
wait for the hub app to return; Atlas keeps no copy of the room.

Screen-awake behavior is already enabled for hosts/guests. The existing
five-minute background hold remains a convenience; persistent membership does
not promise indefinite background execution. DNS-SD advertises no secret and
lets returning guests find a new address/port while retaining the saved room ID
and TLS pin. Each process has a fresh command epoch so a lost pre-restart action
cannot accidentally become a second delta.

The complete wire, profile and recovery contract is [LAN rooms](../../protocol/lan-v1.md).
Real-device acceptance remains open for process recreation, local-network
permission, discovery, multiple simultaneous panels and large text/TalkBack.
The Android implementation validates through the hub's shared core. iOS remains a
foundation and must implement this contract alongside its future game client.

## Physical touchscreen clients

The Atlas S3 and E32R28T touchscreen Sigil use the same Home, Game, Table and
Settings navigation. Both show the hub's game; Atlas Game queues host controls (start, pass,
pause, resume) for the hub. Both Table destinations are read-only.
Physical Sigils remain paired with Atlas over their encrypted radio session;
they do not join the Android LAN host or run a local game. The radio now carries
a paged public table roster and the focused player's counters/status. More
opens player actions and **Counters**, a **Counters & status** panel: poison, energy, experience,
treasure, rad, city’s blessing, monarch, initiative and, in Commander, casts,
next tax and partner commanders. Three rows fit per page; values change only
after the hub accepts a request. Device settings use the Settings tab. Artwork
and Planechase presentation remain open. The remaining convergence work belongs in
[Staged Changes](STAGED_CHANGES.md), without a second game authority.

## Hosting as a paid capability

Hosting is isolated behind `LanHostingAccess`; joining does not consult it.
The current implementation enables hosting without a purchase. Store billing,
product IDs, price, purchase verification/restoration and distribution policy
remain unimplemented. The proposed product model is one paid host with free
guests, keeping solo games and tracking accessible. Do not show a purchase flow
or describe this as a paid product until its entitlement implementation exists.

## Profile merges

An explicit Admin profile merge publishes old IDs as aliases of the survivor.
Room members follow those identities without name matching or new PIN entry;
linked devices follow the authenticated profile-sync redirect. This does not
merge device-local histories or grant account permissions.

## Atlas notifications

Atlas keeps the latest 12 sync, pairing, transfer and hosted-game status events
in a RAM-only journal. Swipe down from the top edge or tap **Notifications** on
Home to open the shelf. Swipe up or tap Back to return. Newer and Older page
through two events at a time; Clear empties the shelf. The header marks unread
activity; opening the shelf marks existing entries read. Confirmation and pairing
overlays take precedence, and opening the shelf cannot activate an underlying
control. All actions have full-size button alternatives; severity uses text as
well as color, with no required animation.

A paired phone's History shows **Atlas notifications** after syncing. The API
contract is in [hosted-v1.md](../../protocol/hosted-v1.md#atlas-activity-shelf).
The shelf is temporary status feedback; saved games and statistics remain in
their existing stores. Restarting Atlas clears notifications.

## Compact Game setup

The app header gives connection, Atlas battery and link icons the full row below
the brand and help buttons. Battery percentages stay on one line, with wrapping
between status items on narrow screens. Play together is a compact, full-row
link to its invitation and member controls.

The Game tab begins with **1. Game**. Its selected game and Magic variant each
expand through Change; Planechase appears only for Magic. **Rules** is always
available as a collapsed section, without an Advanced settings switch. Saved
setups follow the start/rules controls. Atlas sharing can be stopped or started
under More > Atlas connection.

## Retry presentation and Atlas authorization

One or two unavailable room polls retry silently with the last snapshot. Three
consecutive failures expose the outage and disable remote controls; a successful
poll restores the connection. Authorization refusal and ended rooms are handled
immediately. Network rediscovery starts after confirmed failure rather than on
every missed poll.

Shared-room errors and history feedback use a reserved status row above the app
tabs, outside game controls, including in setup and Planes. Tap Details to read
the full message. Routine artwork status stays in More > Atlas > Atlas connection.
An unconfirmed command keeps its exact receipt and offers
**Retry last action**; a successful background poll cannot clear that uncertainty.
Only the receipt response resolves it. Confirmed action outcomes remain separate
from background poll messages. A refusal cancels queued edits and says how many
were cancelled. After a host restart, an old receipt may be unknown: the app
requires review rather than claiming the original action did not happen.
Atlas discovery never replaces a manually joined or hosted room. Connection
labels explicitly distinguish Atlas from the game host. Feedback never shifts
the game layout or obscures player controls.

Atlas controller verification coalesces concurrent checks per field/value. A
pending check cannot delay another phone's valid cache hit. Successful results
remain cached for five seconds of monotonic time measured from completion, in
a bounded 32-entry cache. Errors and refusals create no grant; callers share the
same in-flight result, and cancelling one caller does not cancel other callers'
verification. Expired grants require a new successful check.
