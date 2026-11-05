# Players, Profiles and Accounts

Profiles, sign-in, joining a table from a phone or a Sigil, permissions and
moderation. Storage keys are in [Storage and Recovery](STORAGE_AND_RECOVERY.md);
HTTP routes are in `protocol/http-v1.md` and the route table in
`Atlas/src/web_api.cpp`.

## Phone-owned profile copies

A paired phone hosts and exchanges player copies and records without a
player sign-in. Copies contain stable ID, name, colour, preset avatar,
creation/update times and the salted PIN/password verifier. No PIN entry is
required to copy or play; settings and detailed statistics retain their local
unlock gate. Copies never unlock accounts or grant device Admin. Plaintext
secrets, private link keys, pictures and tile backgrounds are excluded. Basic identities/totals
use NVS, while appearance, detailed statistics and archived history use SD.
The phone sends records after copies are acknowledged and reads back totals and
same-ID history. Names never establish identity. Pairing grants access to the
table's player totals and archived game records; it does not reveal private
moderation data. The wire contract is in [Hosted games](../../protocol/hosted-v1.md#player-copies-and-records).

Paired devices check the directory every 15 seconds and keep copies in their
local library, including offline. Newer profile updates win; names never merge
different IDs, and stale copies cannot clear an existing PIN verifier. Device
owners can turn off **Keep profiles copied from Atlas** in More > Settings.
Only automatic copies become eligible for removal after 24 hours disconnected;
reconnection resets the grace period. Current rosters, saved setups, original
device profiles, device Admin and all retained match records are preserved.

## Profiles and sign-in

- A profile is a person: ID, name, a PIN (4-8 digits) or password (8-64
  UTF-8 bytes, no control characters), statistics and preferences. Both
  secrets travel in the same `pin` field, use the same hash
  (`profilePinHash`, a single SHA-256 over profile ID and secret) and the same
  login limiter. At most 64 profiles; new profiles need a name and secret.
- Anyone on Atlas's network can register. Signing in gives a native client
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

## Profile identity

Stable profile IDs establish identity across devices. Same-ID copies can sync
after verification; matching names do not automatically merge accounts. Explicit
Admin account merges retain the existing verified merge workflow. Play Together
links a protected profile once and then restores its room-scoped credential;
shared table controllers require no personal account. See [Shared devices](SHARED_DEVICES.md).

## Joining a game

Since 2026-11-04 the hub phone or tablet runs every game
([hosted-v1.md](../../protocol/hosted-v1.md)), so the hub seats its players:
its own accounts, accounts copied from Atlas, or guests. Atlas's lobby, its
profile picker, seat claims ("Use this seat" and the Sigil's Link phone),
mid-game Sigil joins, the Atlas screen's Add player and tablet mode's
`/api/tablet/*` routes were removed with Atlas's engine.

- One profile has **at most one participant** in a game; statistics count once.
- **Sigils** choose a lobby player to attach to, an unseated saved profile
  from Atlas or the hub, or a true guest named after the Sigil. The hub applies
  the selection. `sigilSeat` binds the controller independently of the player's
  identity and phone memberships; detaching leaves the player seated. Guests
  create no account or personal statistics, only their match participation.
    Lobby and starting screens display names from the hub's player snapshot,
    independently of Atlas's stored seat-profile bindings.
- **Additional phones** join through a player-scoped invitation created on
  an already connected player phone. It uses the persistent room address and
  a separate bearer key restricted to that player. Phone-claimed profiles
  disappear from the general room directory; a Sigil alone does not claim one.
  Several phones and a Sigil can control the same player, with one stats credit.
  The room keeps its existing 16-device capacity; the Wi-Fi access point's
  capacity still applies. Revoking the issuing phone invalidates its invitations.
- **Phone profile selection:** an empty PIN creates a gameplay-only session.
  Entering a valid PIN creates a PIN-verified session for private stats and
  administration. A profile with no saved PIN can choose one for those uses.

## Owner policy

Each profile owner chooses, in the portal or app (`POST /api/session/policy`):

Gameplay profile selection never requires a PIN. The legacy physical-use
policy field remains readable for older clients but no longer blocks play.
PIN-free profile sessions cannot read private statistics, mint private-history
link keys or use privileged account permissions. PIN-verified sessions may do so.

- **Hide stats without authentication.** Controls visibility only; statistics
  always accumulate. Atlas enforces it before serializing anything.

Missing records keep physical play allowed and hide unauthenticated stats.
Anyone at the table can see the list of profile names, as in the portal's
sign-in list.

## Permissions

| Permission | Allows |
|---|---|
| Admin | Account permissions, device names, network settings, firmware and portal updates, pairing confirmation and forget, factory reset, Return table to lobby, speaker volume from the portal (anyone at Atlas may set it on its screen, Settings > Device) |
| Game Master | Sees the people list and reviews player images (its in-game moderation left with Atlas's engine, 2026-11-04) |
| Developer | Developer page, diagnostics and the RAM log download (`GET /api/diagnostics/log`) |

A normal account has none. Permissions combine freely and are checked on
Atlas for every protected request. Privileged accounts can't remove their
secret. In a game the hub decides who may start, change settings, rematch or reset.

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

**The table code is optional** (2026-10-08, after testers found the code on
every update a reason not to use TurnHub). It is **off by default**: every
signed-in account counts as at the table, so the account permission alone
decides (an Admin for everything above), and firmware still installs only
when signed and the same or newer. An Admin turns it on in Device Settings
(portal, basic portal or app), saved in NVS as `tcode`
(`table_code_setting.h`, Intent `ConfigureTableCode`). Turning it off again
needs a code that Admin actually entered, so an Admin elsewhere on the Wi-Fi
cannot quietly drop the extra step. While it is off the Atlas screen's Wi-Fi
QR code also shows a private password without a code.

## Moderation

Atlas's in-game moderation (force pass, reset connections, remove from
game, nudge mute; the `Moderate` Intent) was removed on 2026-11-04 with its
engine. Earlier resets and removals stay counted (`o<profileId>`) and are
shown only to the account's own PIN-verified session.

Archived accounts stay visible to Admins at the bottom of the account list,
can be restored, and are hidden from Game Master tools.

## Not done yet

- A slower salted hash (PBKDF2 via mbedTLS) for PINs and passwords, and a
  larger NVS partition, since today's 20 KB likely fits only about 20 profiles
  rather than 64. Both are parked together in
  [Staged Changes](STAGED_CHANGES.md), "Parked: storage batch".
- Future LCD artwork transfer/cache and final ESP32-S3 screen layouts.
- Per-Sigil startup choice (last profile or picker).

## Verification

Host scenarios run the real HTTP and Intent handlers: registration and login
(PIN and password), concurrent sessions, logout and re-login, PIN throttling
and revocation, policies, permissions and presence codes. Persistence across
reboots is verified in use (owner, 2026-10-02).

## Standalone device identity and optional import

Android local players have their own UUIDs, independent of Atlas profiles.
Adding a name creates a player; selecting a saved player reuses the local ID.
Equal names remain separate. Completed matches snapshot identity/name and keep
local statistics independent of Atlas totals. History/import state belongs to
Android's local library; see [Android](../../Android/README.md) for the schema.

Optional import requires explicit per-match choices on a specific
`(atlasId, profileId)` destination; a player left unmapped is a guest and is
not credited (2026-10-28), and linking them later credits just their part. Linking never renames/replaces a
local player or rewrites historical match facts. No typed-name, cached-name or
cross-Atlas matching exists. Pending mappings are frozen for retry, imported
matches cannot be submitted again by the app, and rejected records/reasons stay
visible for explicit review. Atlas validates destination hardware identity and
all exact profile identities before crediting a new record. Existing session
sign-in is still required; profile linking does not itself grant permissions.
See [import contract](../../protocol/http-v1.md#game-import-2026-10-07).

**Getting an account back from Atlas** (owner, 2026-10-29; *implemented, JVM
tests only*): after a reinstall or on a new phone, Players lists the loaded
Atlas accounts that aren't on the device ("Get my account from this Atlas").
Atlas checks the account's PIN; the app recreates it under the same profile
ID with Atlas's name and look, a verifier for that PIN and a link key, so
profile sync and the Atlas totals take over. The games Atlas kept for that
account (its match history, `/api/profiles/history`, needs Atlas's microSD
card) come back into the device's history as already delivered there; games
that never reached that Atlas do not. PIN-less Atlas accounts need a PIN set
on Atlas first.

## Device Admin account (owner, 2026-10-29)

*Implemented in the app; host/JVM tests only, device checks open.* Every
device has one Admin account: its owner's account type. On Atlas it is the
first-run Admin with Admin permissions. On a phone or tablet,
`LocalLibrary.deviceAdmin` names it; it needs a PIN or password and gives no
extra powers, so phone players stay equal. First launch asks the owner to
create it or choose an account already on the device (its PIN unlocks it, or
one without a PIN chooses one first). "Not now" closes the prompt; any account
with a PIN can be made Admin from its settings, and handing it on needs the
current Admin unlocked.
Deleting it leaves the device without one, and the prompt returns.

The type stays on its home device: uploads, merges and syncs carry no
permissions. Atlas first-run setup offers "Install your admin account as the
device admin?", which uploads the phone's Admin under its own ID (or signs in
to it if already there); the table-code step then makes it Atlas's Admin.
One person's account can live on several of their phones and tablets ("Use
on another device", `AccountCopy`): the device holding it shows a QR code with
a one-time invitation in Play together's format (`turnhub://account/...`), and
the other device, on the same Wi-Fi, fetches the account over the pinned TLS
link. It carries the profile ID, name, look, Atlas links and PIN verifier,
never the PIN; the receiving device asks for the PIN before adding it, under a
new local ID, and can make it its Admin. Each device keeps its own games;
same-ID copies meet on an Atlas, which adds them up.

## Player artwork (2026-10-10)

*Implemented; host/build validation recorded in COLLABORATION.md; physical
acceptance pending.* Preset keys and IDs stay stable. Android uses scalable
vector masters in `design/avatars`; Atlas uses generated 64-pixel alpha masks
area-filtered into its current slot. Frozen Sigils retain their existing assets.

My account → Personalization accepts a phone-local JPEG/PNG/WebP (up to 20 MiB
and 32 megapixels). Bounded decode honors EXIF orientation, crop sliders choose
zoom and position, and Preview displays the final compressed image plus
provisional 64/128-pixel LCD previews. The phone flattens transparency on white
and produces a baseline RGB JPEG, 512 square pixels and at most 48 KiB. The
original is never uploaded. Upload/replace stores pending art on Atlas's SD;
Admin review in Players → People → person approves or rejects the specific
revision. Pending art is private to that player and Admins; approval selects it
for public app/tablet/Atlas display. Pending replacement retains approved art.
None/presets may be selected without deleting approved artwork; Remove uploaded
image removes pending/approved artwork and selects None. Refresh image approval
reloads the profile after review. Current Sigils render their ordinary fallback.

SD unavailability fails saves visibly and falls back safely for play. A checksum
or decode failure displays a neutral fallback. Artwork is outside game-state
polling; immutable image paths are cached per connection/game. Names, life totals
and controls remain separate from decorative artwork. The master is suitable for
future LCD layouts; those firmware targets and their radio transfer are pending.

### One profile directory and two game views

Players is one identity-based list with location labels: This device, Atlas,
or both. Names alone never merge identities. Paired automatic copies retain
the profile's salted verifier without unlocking it. Public gameplay references
remain distinct from private account-copy links and device roles. Game shows the hub's own game, hosted on Atlas or not.
Private statistics and administration remain under PIN-verified profile access.
