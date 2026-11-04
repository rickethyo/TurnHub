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
establish identity. Explicit account merging remains separate.

Table and Planes views are read-only by default. Shared table controllers
can act for all normal gameplay panels. The host can grant game-master access to
a device for editable public views. Room grants confer no Atlas administration
or private-statistics permissions. Special decisions outside the LAN command
vocabulary remain on the host and show a refusal on a remote controller.

The hub owns membership writes. Its room and guests' reconnect credentials are
encrypted in device-private preferences, excluded from OS transfer/backup.
End room explicitly revokes membership; closing the app preserves it. Guests
wait for the hub app to return. Atlas's room backup route (`/api/room`) has
no app client since 2026-11-04.

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
