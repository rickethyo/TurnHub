# Players, Profiles and Accounts

Profiles, sign-in, joining a table from a phone or a Sigil, permissions and
moderation. Storage keys are in [Storage and Recovery](STORAGE_AND_RECOVERY.md);
HTTP routes are in `protocol/http-v1.md` and the route table in
`Atlas/src/web_api.cpp`.

## Profiles and sign-in

- A profile is a person: ID, name, a PIN (4-8 digits) or password (8-64
  UTF-8 bytes, no control characters), statistics and preferences. Both
  secrets travel in the same `pin` field, use the same hash
  (`profilePinHash`, a single SHA-256 over profile ID and secret) and the same
  login limiter. At most 64 profiles; new profiles need a name and secret.
- Anyone on Atlas's network can register. Signing in gives a browser or app
  session token (RAM only, eight-hour inactivity expiry, sent in the
  `X-TurnHub-Token` header, never in URLs). Changing the secret revokes the
  profile's other tokens. Logout removes authorization, not the participant.
- Signing in does not join the table. The authenticated profile, never a
  client-supplied player field, decides what a session may control.
- The Android app can keep one profile's secret in the Android Keystore,
  unlocked by fingerprint, face, screen lock or an app PIN, and signs in
  whenever it connects (Android 11+, `Android/README.md`). Real passkeys don't
  fit an offline Atlas (WebAuthn needs a domain name and a trusted
  certificate).

## Joining a table

- One profile has **at most one participant** at the table. Two phones and a
  Sigil may all control that participant; statistics count once.
- **Phones** join with `POST /api/session/join` and can form a whole table
  with no Sigils: pick a starter, start, play, finish, rematch.
- **Sigils** join through the **profile picker** (`profile_picker.cpp`, RAM
  browsing state only). The page lists Guest, then saved profiles, three to
  a page. E-ink: each name has a fixed key (Up/Right/Down), the click shows
  more names, Left goes back. OLED: the same page as a scrolling list. Words
  mark special rows: "no profile" (Guest), "phone sign-in" (the owner turned
  off physical use without a PIN and has no signed-in phone), "at table:
  attach" (already playing from a phone; choosing it attaches this Sigil to
  that participant). Archived or moderated profiles and profiles on another
  Sigil are not listed. Names that would look the same on a Sigil (the
  same name, or the same first 12 characters) are numbered in profile ID
  order, "Sam 1" and "Sam 2", cut to fit (2026-10-06). The picker closes after a minute idle, at game start,
  or when the Sigil joins another way.
- **Use this seat** (portal Players tab and the app's Players tab):
  `POST /api/session/request` with `module` and `slot`, then poll
  `GET /api/session/poll?id=` while the player chooses **Link phone** on that
  Sigil within 30 s. Signed out, approval signs the phone in to the seat's
  profile, and only for a profile without a PIN (a Sigil press proves
  possession, not the secret). Signed in, it attaches that Sigil seat to the
  signed-in profile. The portal also offers PIN sign-in by seat; the app
  signs in from its profile list instead.
- **Seat B** on a shared Sigil uses the same picker (**Add seat B**); Guest
  adds seat B directly. `PickProfile` with slot 2 adds the seat and binds the
  profile in one step.
- Attachment and profile choice happen in the lobby, with one exception
  (owner request 2026-10-07, *Experimental: host-tested*): during a running
  or paused game a Sigil outside it offers **Join** while some player in the
  game has no Sigil (seated from a phone or the tablet). Its picker then lists
  only those players (no Guest), and choosing one moves that player onto the
  Sigil with their place, life and clock (`GameEngine::replaceController`).
  Nobody new joins mid-game until venue lobbies, and a participant that
  already has a physical controller is never silently moved.
- Seat bindings are temporary (RAM): released when the seat leaves (the
  Sigil's Leave, Drop seat B, a phone leaving for a profile seated on a
  Sigil, a Game Master removal), at Reset and on restart. They survive Game
  Over so Sigils can name the winner, and Rematch restores them. A seated
  Sigil that reboots keeps its profiles.
- **Turn order** is set on the Atlas touchscreen in the lobby by tapping a
  player chip: Earlier/Later (`MoveSeat`) and, on a shared Sigil, B left or
  B right (`SetSeatSide`). Any player may do it, and so may a table tablet
  (Earlier/Later, 2026-10-07); a phone may not.
  Starter, participants and bindings stay with their physical seats, and
  recovery restores A/B and B/A pairs.

## Tablet mode (2026-10-06)

*Experimental: host-tested, not yet played on hardware.* One shared tablet or
phone lies in the middle of the table, split into a panel per player that
faces their seat (`Atlas/web/src/tablet.html`, served at `/tablet`; in the
Android app, My account → Open tablet mode, `ui/tablet/`). Both draw
`/api/v1/state` and send the same `/api/tablet/*` requests; the app also keeps
the screen on and hides the system bars during play.

- **Turning it on.** Any signed-in account asks for a table presence code
  with `purpose=tablet` (a player's code unlocks only this; every Admin action
  still checks Admin), enters the code the Atlas screen shows, then
  `POST /api/tablet/enable`. The grant lives on that browser session (RAM)
  until `POST /api/tablet/disable`, sign-out or the eight-hour idle expiry.
- **Seating.** In the lobby the tablet adds players: a new name creates a
  profile with no PIN (names are unique, ignoring case); an existing profile
  follows its owner's PIN choice below, else the tablet asks for its PIN
  (rate limited like a sign-in). Seated players join as browser controllers,
  so a Sigil can then attach to any of them through its picker ("at table:
  attach") and a phone can sign in to the same participant.
- **Turn order.** The tablet lobby's arrows move a seat earlier or later
  (`move-earlier`/`move-later`, `MoveSeat` with the `TableTablet` origin);
  panels sit clockwise in that order. The game's order is fixed once it
  starts. Claim the win and Concede sit at the top of each panel's More.
- **Acting for a seat.** Every panel action names the seat (`module`, `slot`
  from `/api/v1/state`) and goes through the same seat callbacks and Intents
  a phone uses (`web_tablet_api.cpp`). Atlas still decides: a seat changes
  only its own life, records only Commander damage it received, only the
  active player passes or claims a win, and win confirmations go to the
  confirming seat. Statistics are recorded per profile as for any game.

## Owner policy

Each profile owner chooses, in the portal or app (`POST /api/session/policy`):

- **Allow Sigil and tablet use without a PIN** (`allowPhysicalWithoutPin`).
  Off means a Sigil or a table tablet can seat that profile only while the
  same profile has a live phone session, or (tablet only) after its PIN is
  typed on the tablet. Turning it off needs a saved secret. Existing participation continues if the policy
  changes or the session expires.
- **Hide stats without authentication.** Controls visibility only; statistics
  always accumulate. Atlas enforces it before serializing anything.

Missing records keep physical play allowed and hide unauthenticated stats.
Anyone at the table can see the list of profile names, as in the portal's
sign-in list.

## Permissions

| Permission | Allows |
|---|---|
| Admin | Account permissions, device names, network settings, firmware and portal updates, pairing confirmation and forget, factory reset, Return table to lobby, speaker volume |
| Game Master | Force pass (logged as a master pass), nudge mute; optional sub-permissions **reset connections** and **remove from game** |
| Developer | Developer page, diagnostics and the RAM log download (`GET /api/diagnostics/log`) |

A normal account has none. Permissions combine freely and are checked on
Atlas for every protected request. Privileged accounts can't remove their
secret. **There is no table host** (2026-09-25): any seated player may start,
pick the starter, change next-game settings, rematch or reset.

**First Admin.** On a new Atlas the app's setup or the portal's banner lets a
signed-in account with a secret claim the first Admin, after proving
presence (below). Neither a name nor first registration grants privileges.
The first Admin can't be demoted.

**Presence at the table** (`front_panel.cpp`). Protected device actions need
proof that the account holder is at the table: the account asks for a code,
the Atlas screen shows a six-digit code and a QR code for that account only
for 90 s (`PRESENCE_CODE_MS`), and entering or scanning it verifies the
account for 10 minutes (`PRESENCE_GRANT_MS`). Five wrong codes cancel it;
**Cancel** on the screen removes a code nobody asked for; **Stop** in Device
Settings ends a grant early. Without it the request gets
`403 {"presenceRequired": true}` and the client starts the code flow. It is
needed for the first Admin, network settings, device names, firmware and
portal updates, pairing confirmation, Return table to lobby and factory reset.

## Moderation

Game Master actions go through the `Moderate` Intent, which re-checks
permission and target state:

- **Force pass** acts at once, within the normal pause and decision rules.
- **Reset connections** revokes the target's sessions and pending claims and
  suspends its Sigil controls until a PIN sign-in. Seat, life and game state
  stay. A shared Sigil is suspended as a unit.
- **Remove from game** also leaves the lobby or eliminates the participant
  through the concession flow (open decisions finish first; a primary seat
  with a seat B removes B first). The account and statistics stay.
- **Nudge mute** stops that account sending nudges (`NudgePlayer` is refused).

Both disconnecting actions need the target to have a secret. Connection
resets and removals are counted (`o<profileId>`), saved before acting
(a storage failure rejects the action), and shown only to the account's own
PIN-verified session, never to Admins, Game Masters, seats, Sigils or
exports.

Archived accounts stay visible to Admins at the bottom of the account list,
can be restored, and are hidden from Game Master tools.

## Not done yet

- A slower salted hash (PBKDF2 via mbedTLS) for PINs and passwords, and a
  larger NVS partition, since today's 20 KB likely fits only about 20 profiles
  rather than 64. Both are parked together in
  [Staged Changes](STAGED_CHANGES.md), "Parked: storage batch".
- Custom avatars with Admin approval (`AVATAR_CUSTOM` is reserved; presets
  work).
- Per-Sigil startup choice (last profile or picker).
- Moderation from the Atlas Player screen.

## Verification

Host scenarios run the real HTTP handlers, Intent handlers and engine:
registration and login (PIN and password), hardware-free games, duplicate-join
refusal, concurrent sessions, logout and re-login, PIN throttling and
revocation, mixed attachment, the picker for both seats and policies,
permissions, presence codes, moderation and counter privacy. Phone-only
games, phone plus Sigil on one player and persistence across reboots are
verified in use (owner, 2026-10-02).
